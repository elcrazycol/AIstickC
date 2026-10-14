// Карманный голосовой ассистент: запись → Whisper → DeepSeek → экран.
//
// Управление:
//   BtnA (зажать)  — запись, отпусти — отправить
//   BtnB (тап)     — предыдущая страница
//   BtnPWR (тап)   — следующая страница (верхняя кнопка)
//   BtnB (3 с)     — сброс сессии (память + главный экран) с панелью
//
// Состояния: Idle → Recording → Sending → Recognizing → Streaming → Showing.
// Экран один: полоса состояния и тело. Пока идёт работа — три перетекающие
// точки (анимирует отдельная задача), затем по мере прихода печатается текст.
// Всё рисование — в ui.cpp.

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_sleep.h>

#include <cmath>
#include <string>
#include <vector>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "stt.h"
#include "llm.h"
#include "plugins.h"
#include "chat_settings.h"
#include "hold.h"
#include "pure/sentence_trim.h"
#include "pure/ulaw.h"
#include "repo.h"
#include "ui.h"
#include "font_jbmono.h"
#include "pure/wav.h"
#include "pure/text_wrap.h"
#include "pure/chat_history.h"

namespace {

constexpr int kWhite = lgfx::colors::TFT_WHITE;
constexpr int kRed   = lgfx::colors::TFT_RED;

constexpr int kBodyW = kScreenW - 2 * ui::kMargin;

uint8_t* g_audio = nullptr;   // PSRAM: заголовок + аудио
int16_t  g_chunk[kDmaChunkBytes / 2];

enum class State {
  Idle,
  Recording,
  Sending,
  Recognizing,
  Streaming,
  Showing,
  Settings,   // меню настроек (открывается тряской)
  Dialog,     // подменю «Диалог и ответ»
  Plugins,    // панель плагинов
};
State g_state = State::Idle;

std::vector<std::string> g_lines;
size_t g_page = 0;
int    g_color = kWhite;

chat::History g_history(kChatMaxTurns);

// Счётчик отправленных сообщений: живёт в NVS, переживает выключение.
Preferences g_prefs;
uint32_t g_msgs = 0;

// Волны: амплитуды последних кусков записи, свежие справа.
uint8_t g_wave[ui::kWaveBars] = {0};
size_t  g_waveN = 0;

uint32_t g_lastDraw = 0;     // троттлинг перерисовок текста
String   g_stream;           // накопленный текст ответа
// Листание. Экран сам не перелистывается: пока ответ пишется, страница
// остаётся там, где её оставил пользователь, а растёт только число страниц.
constexpr size_t kNoPage = (size_t)-1;  // на экране ещё нет текста
size_t   g_drawnPage = kNoPage;  // что сейчас на экране
size_t   g_drawnTotal = 0;       // с каким счётчиком страниц
size_t   g_drawnTo = 0;          // до какой строки страница отрисована

// Удержание боковой кнопки: отсчёт и полоска — в модуле hold.
hold::Watcher g_holdReset(M5.BtnB, kClearHoldMs);   // сброс истории
hold::Watcher g_holdExit(M5.BtnB, kBackHoldMs);      // «назад»: меню и ответ
hold::Watcher g_holdRecord(M5.BtnA, kRecordStartMs, false);  // запись — по зажатию
uint32_t g_panelShownAt = 0;  // панель сброса висит, пока держат кнопку
uint32_t g_panelUntil = 0;    // до какого момента держать панель результата
String   g_pendingPlugin;     // плагин, ждущий ответа пользователя (город)

// --- меню настроек и панель плагинов ---
size_t g_menuIndex = 0;
size_t g_dialogIndex = 0;
size_t g_pluginIndex = 0;
std::vector<ui::ListItem> g_pluginRows;  // как показать
std::vector<std::string> g_pluginNames;  // что ставить/удалять
bool     g_imuOk = false;
uint32_t g_lastShake = 0;

// --- Энергосбережение ---
uint32_t g_lastActivity = 0;
bool     g_screenOff = false;
bool     g_wifiOff = false;

// Любое действие пользователя: сбрасывает таймеры простоя и будит экран.
void markActivity() {
  g_lastActivity = millis();
  if (g_screenOff) {
    g_screenOff = false;
    ui::screenOn();
  }
}

// Простой: сначала гаснет экран, потом Wi-Fi, потом глубокий сон.
// Меню тоже уходит по этому таймеру, поэтому showIdle нужен заранее.
void showIdle();

void pollIdlePower() {
  const bool inMenu = (g_state == State::Settings || g_state == State::Dialog ||
                       g_state == State::Plugins);
  if (g_state != State::Idle && g_state != State::Showing && !inMenu) return;
  const uint32_t idle = millis() - g_lastActivity;

  // Меню не висит вечно: по тому же таймеру уходим на главный экран, а следом
  // гаснет и экран — как в обычном простое. Кнопки в меню обновляют отсчёт,
  // поэтому навигация меню не закрывает.
  if (inMenu && idle >= kScreenOffMs) {
    Serial.println("[main] простой: выхожу из меню на главный экран");
    showIdle();
  }

  if (!g_screenOff && idle >= kScreenOffMs) {
    g_screenOff = true;
    ui::dotsStop();
    ui::screenOff();
    Serial.println("[main] простой: экран погашен");
  }

  if (!g_wifiOff && idle >= kWifiOffMs) {
    g_wifiOff = true;
    Serial.println("[main] простой: Wi-Fi выключен");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  if (idle >= kDeepSleepMs) {
    Serial.println("[main] простой: глубокий сон");
    ui::screenOff();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    // Будит большая кнопка (BtnA, GPIO37) — по низкому уровню.
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_37, 0);
    esp_deep_sleep_start();
  }
}

// Ширина любого символа в моноширинном шрифте — константа.
int measureMono(const char*, size_t, void*) { return kFont_jbmono_XAdvance; }

void wrapInto(const char* text, std::vector<std::string>& out) {
  textwrap::wrap(text, kBodyW, measureMono, nullptr, out);
}

void pushWave(uint8_t amp) {
  if (g_waveN < ui::kWaveBars) {
    g_wave[g_waveN++] = amp;
    return;
  }
  memmove(g_wave, g_wave + 1, ui::kWaveBars - 1);
  g_wave[ui::kWaveBars - 1] = amp;
}

// Пиковая амплитуда куска -> 0..255. Речь идёт на уровне ~−18 dBFS, поэтому
// делим с запасом усиления, чтобы столбики были заметными.
uint8_t peakOf(const int16_t* s, size_t n) {
  int32_t peak = 0;
  for (size_t i = 0; i < n; ++i) {
    const int32_t v = s[i] < 0 ? -s[i] : s[i];
    if (v > peak) peak = v;
  }
  int32_t a = peak / 32;
  if (a > 255) a = 255;
  return (uint8_t)a;
}

// Экран ответа: текст + индикатор страниц.
void drawAnswer(size_t page) {
  g_page = page;
  g_drawnPage = page;
  g_drawnTotal = textwrap::pageCount(g_lines.size(), ui::kBodyLines);
  g_drawnTo = (page + 1) * ui::kBodyLines;
  ui::dotsStop();
  ui::answerReset();
  ui::answerText(g_lines, page * ui::kBodyLines, g_color);
  ui::pageIndicator(page, g_drawnTotal);
}

// Листание. Работает и во время стрима: готовые страницы можно читать,
// пока пишется остальное. Готовый ответ листается по кругу, а недописанный —
// без прыжков с начала в конец, и на последней странице снова идём за текстом.
void pageBy(int delta) {
  const size_t total = textwrap::pageCount(g_lines.size(), ui::kBodyLines);
  if (total <= 1) return;

  const int last = (int)total - 1;
  int next = (int)g_page + delta;
  if (next < 0) next = (g_state == State::Showing) ? last : 0;
  if (next > last) next = (g_state == State::Showing) ? 0 : last;

  Serial.printf("[main] страница %d/%d\n", next + 1, last + 1);
  drawAnswer((size_t)next);
}

void showError(const char* text) {
  g_state = State::Showing;
  g_color = kRed;
  g_page = 0;
  wrapInto(text, g_lines);
  drawAnswer(g_page);
}

// Показ результата плагина: вопрос панелью, обычный результат — панелью на
// пару секунд, длинный (погода) — как обычный ответ с пагинацией.
void showPluginResult(const plugins::Result& pr) {
  g_state = State::Showing;

  if (!pr.ask.empty()) {          // нужно спросить пользователя
    ui::notice(pr.ask.c_str());
    g_pendingPlugin = pr.plugin.c_str();
    return;
  }
  if (pr.asAnswer) {                // длинный результат — как ответ
    g_color = pr.ok ? kWhite : kRed;
    g_page = 0;
    wrapInto(pr.text.c_str(), g_lines);
    drawAnswer(g_page);
    return;
  }
  char panel[96];
  snprintf(panel, sizeof(panel), "%s: %s", pr.plugin.c_str(), pr.text.c_str());
  ui::notice(panel);
  g_panelUntil = millis() + kPluginPanelMs;
}

void showIdle() {
  ui::dotsStop();
  g_state = State::Idle;
  ui::idle(g_history.turnCount(), g_msgs);
}

// Начало фазы ожидания: чистый экран и запущенная анимация точек.
void startWaiting() {
  g_drawnPage = kNoPage;  // на экране ещё не текст: первый кадр всё сотрёт
  g_drawnTotal = 0;
  g_drawnTo = 0;
  g_lastDraw = 0;
  ui::answerReset();
  ui::dotsStart();
}

// Кусок ответа пришёл: копим текст и печатаем его без мерцания.
void onDelta(const char* piece, size_t len, size_t, void*) {
  if (len == 0) return;  // только reasoning — точки продолжают идти

  g_stream.concat(piece);
  const uint32_t now = millis();
  if (now - g_lastDraw < 50) return;
  g_lastDraw = now;

  wrapInto(g_stream.c_str(), g_lines);
  const size_t total = textwrap::pageCount(g_lines.size(), ui::kBodyLines);
  if (total == 0) return;

  if (g_page >= total) g_page = total - 1;  // страховка на всякий случай

  // Экран не перелистываем: пользователь остаётся на своей странице.
  // Растёт только последняя строка последней страницы, поэтому и рисуем
  // только её (и те строки, что появились с прошлого кадра) — иначе на
  // каждые 50 мс уходила бы вся страница, и вывод заметно тормозил.
  const size_t first = g_page * ui::kBodyLines;
  const size_t last = min(g_lines.size(), first + ui::kBodyLines);

  if (g_drawnPage == kNoPage) {
    ui::dotsStop();
    ui::answerReset();  // стереть точки перед первым текстом
    g_drawnPage = g_page;
    g_drawnTotal = total;
    g_drawnTo = first;
    ui::answerLines(g_lines, first, first, last, kWhite);
    ui::pageIndicator(g_page, total);
    g_drawnTo = last;
    return;
  }

  if (g_page == total - 1 && last > first) {
    size_t start = (g_drawnTo > first) ? g_drawnTo - 1 : first;
    if (start >= last) start = last - 1;
    ui::answerLines(g_lines, first, start, last, kWhite);
    g_drawnTo = last;
  }
  if (total != g_drawnTotal) {
    g_drawnTotal = total;
    Serial.printf("[main] страниц стало %u, смотрю %u\n", (unsigned)total,
                  (unsigned)(g_page + 1));
    ui::pageIndicator(g_page, total);
  }
}

// Захват audioBytes байт в PSRAM. Возвращает число записанных байт.
// Пишем, пока держат BtnA. Однократное нажатие BtnB — отмена: запись
// прекращается, и ничего никуда не уходит.
size_t record(size_t audioBytes, bool* cancelled,
              void (*onAudio)(const uint8_t*, size_t) = nullptr) {
  if (cancelled != nullptr) *cancelled = false;
  if (!M5.Mic.isRunning()) {
    if (!M5.Mic.begin()) {
      Serial.println("[main] микрофон не стартовал");
      return 0;
    }
  }

  const size_t totalSamples = audioBytes / 2;
  const size_t chunkSamples = sizeof(g_chunk) / 2;

  // Прогрев: первые 200 мс — переходный процесс PDM, не звук.
  {
    const size_t warm = (size_t)kSampleRate * 200 / 1000;
    size_t got = 0;
    while (got < warm) {
      const size_t want = min(warm - got, chunkSamples);
      if (!M5.Mic.record(g_chunk, want, kSampleRate)) { delay(1); continue; }
      while (M5.Mic.isRecording()) delay(1);
      got += want;
    }
  }

  // Писк — уже после прогрева: это сигнал «говори», и всё сказанное после
  // него записывается. Раньше он звучал до прогрева, и первые 200 мс речи
  // пропадали.
  M5.Speaker.tone(2000, 100);

  size_t done = 0;
  uint32_t lastSec = 0xFFFFFFFF;
  const uint32_t started = millis();
  while (done < totalSamples && (millis() - started) < (kMaxRecordMs + 2000)) {
    const size_t want = min(totalSamples - done, chunkSamples);
    if (!M5.Mic.record(g_chunk, want, kSampleRate)) { delay(1); continue; }

    // Ждём кусок, но кнопки читаем чаще куска: он идёт 256 мс, и быстрый тап
    // по BtnB иначе проскочил бы между двумя опросами и потерялся. Опрос
    // раз в 20 мс — как в основном цикле: чаще незачем, M5.update() читает I2C.
    bool cancel = false;
    uint32_t lastPoll = 0;
    while (M5.Mic.isRecording()) {
      if (millis() - lastPoll >= 20) {
        lastPoll = millis();
        M5.update();
        if (M5.BtnB.wasPressed()) cancel = true;
      }
      delay(1);
    }

    memcpy(g_audio + wav::headerSize() + done * 2, g_chunk, want * 2);
    done += want;

    pushWave(peakOf(g_chunk, want));

    // Отдаём сжатый кусок на отправку: пока пишем, звук уже уезжает.
    if (onAudio != nullptr) {
      static uint8_t ulawBuf[kDmaChunkBytes / 2];
      ulaw::encodeBuffer((const uint8_t*)g_chunk, want, ulawBuf);
      onAudio(ulawBuf, want);
    }

    if (cancel) {
      if (cancelled != nullptr) *cancelled = true;
      break;
    }

    M5.update();
    const uint32_t elapsed = millis() - started;
    if (millis() - g_lastDraw > 40) {
      g_lastDraw = millis();
      const uint32_t remain =
          elapsed >= kMaxRecordMs ? 0 : kMaxRecordMs - elapsed;
      const uint32_t sec = (remain + 999) / 1000;
      if (sec != lastSec) {
        lastSec = sec;
        ui::recording(remain, g_wave, g_waveN);  // полный кадр раз в секунду
      } else {
        ui::recordingWaves(g_wave, g_waveN);     // дешёвая перерисовка волн
      }
    }
    if (!M5.BtnA.isPressed()) break;
  }

  M5.Mic.end();
  return done * 2;
}

// Кусок записанного звука — сразу на отправку (потоковая выгрузка).
void pushAudio(const uint8_t* ulaw, size_t n) { stt::streamPush(ulaw, n); }

// Полный цикл: запись → STT → LLM → вывод.
// Ответ оборван потолком токенов — отрезаем недописанный хвост. Модель
// пишет до потолка и обрывается на полуслове («…популярен для сервер»),
// а показать обрубок стыдно. Режем до последнего конца предложения.
void finishAnswer(llm::Reply& r) {
  if (!r.truncated) return;
  std::string t(r.text.c_str());
  if (trim::trimToSentence(t)) {
    Serial.printf("[main] хвост подрезан: %u -> %u знаков\n",
                  (unsigned)r.text.length(), (unsigned)t.size());
    r.text = String(t.c_str());
  }
}

// Обрезка истории по текущей настройке памяти: конкретное число обменов
// или подобранное «авто» — под средний размер обмена в этом диалоге.
void trimHistory() {
  g_history.setMaxTurns(chatcfg::effectiveTurns(g_history));
  g_history.trim(kChatMaxChars);
}

void runQuestion() {
  if (WiFi.status() != WL_CONNECTED) {
    startWaiting();
    if (!net::connectWifi(kWifiTimeoutMs)) {
      showError(net::lastError());
      delay(2000);
      showIdle();
      return;
    }
    g_wifiOff = false;  // Wi-Fi снова поднят
  }

  g_state = State::Recording;
  g_waveN = 0;
  g_lastDraw = 0;
  ui::recording(kMaxRecordMs, g_wave, g_waveN);

  // Пока идём запись, звук уже уезжает на прокси: сеть втрое быстрее речи.
  const bool streaming = stt::streamBegin();

  bool cancelled = false;
  const size_t got = record(kAudioBytes, &cancelled,
                            streaming ? pushAudio : nullptr);
  const uint32_t ms = (uint32_t)(got / 2) * 1000 / kSampleRate;
  Serial.printf("[main] записано %u байт (%u мс)\n", (unsigned)got, ms);

  if (cancelled) {
    stt::streamAbort();
    Serial.println("[main] запись отменена");
    ui::notice("Запись отменена");
    delay(700);
    showIdle();
    return;
  }

  if (got < (size_t)kSampleRate * 2 * kMinRecordMs / 1000) {
    stt::streamAbort();
    showError("Слишком коротко");
    delay(1200);
    showIdle();
    return;
  }

  // Прокси на VPS принимает сырой LPCM без WAV-заголовка.
  const uint8_t* pcm = g_audio + wav::headerSize();

  g_state = State::Sending;
  startWaiting();

  g_state = State::Recognizing;
  stt::Result s;
  if (streaming) {
    // Хвост и ответ: почти всё уже ушло во время записи.
    s = stt::streamEnd();
    if (!s.ok) {
      // Поток не сложился — не теряем сказанное, отправляем целиком.
      Serial.printf("[stt] поток не сложился (%s) — отправляю целиком\n",
                    s.error.c_str());
      s = stt::transcribe(pcm, got);
    }
  } else {
    s = stt::transcribe(pcm, got);
  }
  if (!s.ok) {
    Serial.printf("[main] ошибка STT: %s\n", s.error.c_str());
    showError(s.error.c_str());
    return;
  }
  Serial.printf("[main] текст: %s\n", s.text.c_str());

  // Плагин ждёт ответа (например, город для погоды): это ответ ему, а не
  // вопрос модели.
  if (!g_pendingPlugin.isEmpty()) {
    const String pending = g_pendingPlugin;
    g_pendingPlugin = "";
    const plugins::Result pr =
        plugins::runInput(pending.c_str(), s.text.c_str());
    showPluginResult(pr);
    return;
  }

  // Сообщение отправляем — увеличиваем счётчик и сохраняем в NVS.
  ++g_msgs;
  g_prefs.putUInt("msgs", g_msgs);

  // Вопрос распознан — спрашиваем модель потоком.
  g_state = State::Streaming;
  g_stream = "";
  startWaiting();

  llm::Stream st;
  st.onDelta = onDelta;
  llm::Options opt;
  opt.toolsJson = plugins::toolsJson();
  opt.maxTokens = chatcfg::effectiveTokens();
  const std::string sys = chatcfg::systemPrompt();
  opt.systemPrompt = sys.c_str();
  Serial.printf("[cfg] в запрос: %s, память %u обменов, лимит %d токенов\n",
                chatcfg::lengthLabel().c_str(),
                (unsigned)chatcfg::effectiveTurns(g_history), opt.maxTokens);
  llm::Reply a = llm::ask(g_history.messages(), s.text, st, opt);
  finishAnswer(a);

  // Модель вызвала плагин — выполняем действие.
  if (a.ok && a.toolCall) {
    const plugins::Result pr =
        plugins::run(a.toolName.c_str(), a.toolArgs.c_str());
    showPluginResult(pr);

    // Плагин может попросить модель прокомментировать результат.
    if (pr.answerAfter && pr.ask.empty()) {
      g_stream = "";
      startWaiting();
      llm::Options fu;
      fu.maxTokens = chatcfg::effectiveTokens();
      fu.systemPrompt = sys.c_str();
      fu.toolCallId = a.toolId.c_str();
      fu.toolName = a.toolName.c_str();
      fu.toolArgs = a.toolArgs.c_str();
      fu.toolResult = pr.text.c_str();
      llm::Reply c = llm::ask(g_history.messages(), s.text, st, fu);
      finishAnswer(c);
      if (c.ok && !c.toolCall) {
        g_history.addUser(s.text.c_str());
        g_history.addAssistant(c.text.c_str());
        trimHistory();
        g_color = kWhite;
        g_page = 0;
        wrapInto(c.text.c_str(), g_lines);
        drawAnswer(g_page);
      }
    }
    return;
  }

  if (a.ok) {
    Serial.printf("[main] ответ: %s\n", a.text.c_str());
    g_history.addUser(s.text.c_str());
    g_history.addAssistant(a.text.c_str());
    trimHistory();
    Serial.printf("[main] в памяти %u обменов\n",
                  (unsigned)g_history.turnCount());

    g_state = State::Showing;
    g_color = kWhite;
    g_page = 0;
    wrapInto(a.text.c_str(), g_lines);
    drawAnswer(g_page);
  } else {
    Serial.printf("[main] ошибка LLM: %s\n", a.error.c_str());
    showError(a.error.c_str());
  }
}

// Боковая кнопка: тап — листание вперёд, удержание 3 с — сброс сессии.
// Панель сброса висит, пока кнопка удерживается, и уходит по отпусканию —
// так её невозможно пропустить.
void pollSideButton() {
  if (g_panelShownAt != 0) {
    if (!M5.BtnB.isPressed()) {
      const uint32_t shown = millis() - g_panelShownAt;
      if (shown < 700) delay(700 - shown);  // не мелькаем, если отпустил сразу
      g_panelShownAt = 0;
      showIdle();
    }
    return;
  }

  // Экран ответа: тап листает страницы, зажатие уводит на главный. Диалог
  // при этом сохраняется — стереть его можно только с главного экрана.
  if (g_state == State::Showing || g_state == State::Streaming) {
    switch (g_holdExit.poll()) {
      case hold::Event::Tap:
        pageBy(-1);
        return;
      case hold::Event::Fired:
        Serial.println("[main] назад на главный экран");
        showIdle();
        return;
      default:
        return;
    }
  }

  // Главный экран: зажатие — сброс диалога.
  if (g_state == State::Idle) {
    switch (g_holdReset.poll()) {
      case hold::Event::Fired:
        g_history.clear();
        ui::notice("История разговора очищена");
        g_panelShownAt = millis();
        return;
      default:
        return;
    }
  }
}

// Верхняя кнопка: тап — листание вперёд.
void pollPowerTap() {
  if (g_panelShownAt != 0) return;  // панель сброса важнее
  if (!M5.BtnPWR.wasClicked()) return;
  if (g_state == State::Showing || g_state == State::Streaming) pageBy(1);
}

// --- меню настроек и панель плагинов ---

void drawPlugins() {
  ui::list("Плагины", g_pluginRows, g_pluginIndex);
}

// Собирает список: доступные из репозитория + установленные, которых там нет.
void buildPluginRows() {
  g_pluginRows.clear();
  g_pluginNames.clear();

  for (const repo::Item& it : repo::available()) {
    const bool inst = repo::installed(it.name.c_str());
    const int have = repo::installedVersion(it.name.c_str());

    // Метка вместо слова: точка понятнее текста и не мешает читать имя.
    ui::Mark mark = ui::Mark::None;
    if (inst && it.version > have && have > 0) mark = ui::Mark::Update;
    else if (inst) mark = ui::Mark::Installed;

    g_pluginRows.push_back({it.name, mark});
    g_pluginNames.push_back(it.name);
  }

  // Установленные, которых в репозитории уже нет: их можно только удалить.
  std::vector<std::string> local;
  plugins::names(local);
  for (const std::string& n : local) {
    if (repo::find(n.c_str()) != nullptr) continue;  // уже в списке
    g_pluginRows.push_back({n, ui::Mark::Installed});
    g_pluginNames.push_back(n);
  }
}

void togglePlugin(size_t i) {
  if (i >= g_pluginNames.size()) return;
  const std::string name = g_pluginNames[i];

  if (repo::installed(name.c_str())) {
    repo::remove(name.c_str());
  } else {
    std::string err;
    if (!repo::install(name.c_str(), err)) {
      ui::notice(("не поставить: " + err).c_str());
      delay(1400);
    }
  }
  plugins::begin();  // перечитать скрипты
  buildPluginRows();
  if (g_pluginIndex >= g_pluginRows.size()) {
    g_pluginIndex = g_pluginRows.empty() ? 0 : g_pluginRows.size() - 1;
  }
  drawPlugins();
}

void enterPlugins() {
  g_holdExit.clear();
  markActivity();  // открыли панель — отсчёт простоя начинается заново
  g_state = State::Plugins;
  ui::list("Плагины", {{"обновляю список..."}}, 0);
  repo::refresh();
  buildPluginRows();
  g_pluginIndex = 0;
  drawPlugins();
}

// Пункты меню настроек. Порядок должен совпадать с settingsRows().
enum class SettingRow { Plugins = 0, Dialog, Reset, Count };

// Пункты подменю «Диалог и ответ». Длина ответа идёт первой: она определяет
// лимит токенов, который стоит ниже.
enum class DialogRow { Length = 0, Turns, Tokens, Count };

std::vector<ui::ListItem> settingsRows() {
  std::vector<ui::ListItem> rows;
  rows.push_back(ui::ListItem("Плагины"));
  rows.push_back(ui::ListItem("Диалог и ответ"));
  rows.push_back(ui::ListItem("Сбросить настройки"));
  return rows;
}

std::vector<ui::ListItem> dialogRows() {
  std::vector<ui::ListItem> rows;
  rows.push_back(ui::ListItem("Длина ответа", chatcfg::lengthLabel()));
  rows.push_back(ui::ListItem("Память диалога", chatcfg::turnsLabel()));
  rows.push_back(ui::ListItem("Лимит ответа", chatcfg::tokensLabel()));
  return rows;
}

void drawSettings() { ui::list("Настройки", settingsRows(), g_menuIndex); }
void drawDialog() { ui::list("Диалог", dialogRows(), g_dialogIndex); }

void enterDialog() {
  g_holdExit.clear();
  markActivity();  // открыли меню — отсчёт простоя начинается заново
  g_state = State::Dialog;
  g_dialogIndex = 0;
  drawDialog();
}

void enterSettings() {
  g_holdExit.clear();
  markActivity();
  g_state = State::Settings;
  g_menuIndex = 0;
  drawSettings();
}

// BtnA: переключить значение по кругу. Списки короткие, поэтому перебор
// понятнее отдельного экрана выбора.
void activateDialog(size_t row) {
  switch (static_cast<DialogRow>(row)) {
    case DialogRow::Length:
      chatcfg::nextLength();
      break;
    case DialogRow::Turns:
      chatcfg::nextTurns();
      break;
    case DialogRow::Tokens:
      chatcfg::nextTokens();
      break;
    default:
      return;
  }
  drawDialog();  // значение видно прямо в строке — это и есть отклик
}

void pollDialog() {
  const size_t n = (size_t)DialogRow::Count;

  // BtnB отдан модулю удержания: тап — назад по списку, зажатие — на уровень выше.
  switch (g_holdExit.poll()) {
    case hold::Event::Fired:
      enterSettings();
      return;
    case hold::Event::Tap:
      g_dialogIndex = (g_dialogIndex + n - 1) % n;
      drawDialog();
      return;
    default:
      break;
  }

  if (M5.BtnPWR.wasClicked()) {
    g_dialogIndex = (g_dialogIndex + 1) % n;
    drawDialog();
    return;
  }
  if (M5.BtnA.wasClicked()) { activateDialog(g_dialogIndex); return; }
}

void activateSetting(size_t row) {
  switch (static_cast<SettingRow>(row)) {
    case SettingRow::Plugins:
      enterPlugins();
      return;
    case SettingRow::Dialog:
      enterDialog();
      return;
    case SettingRow::Reset:
      chatcfg::reset();
      break;
    default:
      return;
  }
  drawSettings();
}

void pollSettings() {
  const size_t n = (size_t)SettingRow::Count;

  // BtnB отдан модулю удержания: тап — назад по списку, зажатие — выход.
  switch (g_holdExit.poll()) {
    case hold::Event::Fired:
      showIdle();
      return;
    case hold::Event::Tap:
      g_menuIndex = (g_menuIndex + n - 1) % n;
      drawSettings();
      return;
    default:
      break;
  }

  if (M5.BtnPWR.wasClicked()) {
    g_menuIndex = (g_menuIndex + 1) % n;
    drawSettings();
    return;
  }
  if (M5.BtnA.wasClicked()) { activateSetting(g_menuIndex); return; }
}

void pollPlugins() {
  const size_t n = g_pluginRows.size();

  // BtnB отдан модулю удержания: тап — назад по списку, зажатие — выход.
  switch (g_holdExit.poll()) {
    case hold::Event::Fired:
      enterSettings();
      return;
    case hold::Event::Tap:
      if (n > 0) {
        g_pluginIndex = (g_pluginIndex + n - 1) % n;
        drawPlugins();
      }
      return;
    default:
      break;
  }

  // Как на главном экране: PWR — вперёд.
  if (M5.BtnPWR.wasClicked() && n > 0) {
    g_pluginIndex = (g_pluginIndex + 1) % n;
    drawPlugins();
    return;
  }
  if (M5.BtnA.wasClicked() && n > 0) { togglePlugin(g_pluginIndex); return; }
}

// Тряска открывает настройки. Порог — заметно больше или меньше 1g,
// с паузой, чтобы одно движение не открывало меню несколько раз.
void checkShake() {
  if (!g_imuOk) return;
  if (g_state != State::Idle && g_state != State::Showing) return;
  if (millis() - g_lastShake < 1500) return;

  float ax = 0, ay = 0, az = 0;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
  const float mag = sqrtf(ax * ax + ay * ay + az * az);

  // Настоящая тряска — это несколько подряд сильных отсчётов (~60 мс),
  // а случайный толчок даёт один. Поэтому считаем попадания подряд.
  static uint8_t hits = 0;
  if (mag > 2.2f || mag < 0.3f) {
    if (++hits < 3) return;
    hits = 0;
    g_lastShake = millis();
    Serial.printf("[main] тряска (%.2f g) — настройки\n", mag);
    enterSettings();
  } else {
    hits = 0;
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(300);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(kScreenRotation);
  ui::begin();
  g_lastActivity = millis();

  // Причина пробуждения (если вышли из глубокого сна).
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
    Serial.printf("[main] пробуждение, причина: %d\n", (int)cause);
  }

  // Счётчик сообщений переживает выключение.
  g_prefs.begin("aistick", false);
  g_msgs = g_prefs.getUInt("msgs", 0);
  Serial.printf("[main] всего сообщений: %u\n", (unsigned)g_msgs);

  // Акселерометр: тряска открывает настройки.
  {
    float ax = 0, ay = 0, az = 0;
    for (int i = 0; i < 5 && !g_imuOk; ++i) {
      g_imuOk = M5.Imu.getAccel(&ax, &ay, &az);
      if (!g_imuOk) delay(50);
    }
    Serial.printf("[main] акселерометр: %s, тип %d, %.2f %.2f %.2f\n",
                  g_imuOk ? "есть" : "нет", (int)M5.Imu.getType(), ax, ay, az);
  }

  // Настройки диалога: сколько помнить и сколько просить у модели.
  chatcfg::begin();

  // Плагины: скрипты из LittleFS.
  plugins::begin();

  const size_t total = wav::totalSize(kAudioBytes);
  g_audio = (uint8_t*)ps_malloc(total);
  if (g_audio == nullptr) {
    M5.Display.fillScreen(lgfx::colors::TFT_BLACK);
    M5.Display.setTextColor(lgfx::colors::TFT_RED, lgfx::colors::TFT_BLACK);
    M5.Display.setCursor(ui::kMargin, 30);
    M5.Display.print("Нет PSRAM");
    Serial.println("[main] ОШИБКА: PSRAM недоступна");
    while (true) delay(1000);
  }

  // Микрофон включается только на время записи; здесь проверяем, что он есть.
  if (!M5.Mic.begin()) {
    M5.Display.fillScreen(lgfx::colors::TFT_BLACK);
    M5.Display.setTextColor(lgfx::colors::TFT_RED, lgfx::colors::TFT_BLACK);
    M5.Display.setCursor(ui::kMargin, 30);
    M5.Display.print("Микрофон не стартовал");
    while (true) delay(1000);
  }
  M5.Mic.end();

  startWaiting();
  if (!net::connectWifi(kWifiTimeoutMs)) {
    Serial.printf("[main] %s\n", net::lastError());
    showError(net::lastError());
    return;  // не блокируемся: BtnA попробует снова
  }
  showIdle();

}

void loop() {
  M5.update();

  // Любое нажатие — активность: будит экран и сбрасывает таймеры простоя.
  if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() ||
      M5.BtnPWR.wasPressed()) {
    markActivity();
  }

  // Меню и панель — отдельные режимы: кнопки в них значат другое.
  // Простой проверяем и здесь: иначе меню висело бы с горящим экраном вечно.
  if (g_state == State::Settings) {
    pollSettings();
    pollIdlePower();
    delay(20);
    return;
  }
  if (g_state == State::Dialog) {
    pollDialog();
    pollIdlePower();
    delay(20);
    return;
  }
  if (g_state == State::Plugins) {
    pollPlugins();
    pollIdlePower();
    delay(20);
    return;
  }

  // Боковая кнопка: тап — назад, удержание — сброс сессии.
  pollSideButton();
  // Верхняя кнопка: тап — вперёд.
  pollPowerTap();

  // BtnA: короткий тап только будит (это уже сделано выше), запись — на
  // уверенном зажатии. Так обычный клик не поднимает экран записи и не
  // запускает весь цикл распознавания.
  if (g_state == State::Idle || g_state == State::Showing) {
    if (g_holdRecord.poll() == hold::Event::Fired) {
      runQuestion();
      markActivity();  // вопрос занял время — таймер простоя с нуля
    }
  }

  // Панель результата плагина уходит сама через пару секунд.
  if (g_panelUntil != 0 && (int32_t)(millis() - g_panelUntil) >= 0) {
    g_panelUntil = 0;
    showIdle();
  }

  pollIdlePower();
  checkShake();

  delay(20);
}
