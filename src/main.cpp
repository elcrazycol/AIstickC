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
#include "driver/rtc_io.h"

#include <cmath>
#include <string>
#include <vector>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "stt.h"
#include "llm.h"
#include "plugins.h"
#include "timeouts.h"
#include "chat_settings.h"
#include "hold.h"
#include "panel.h"
#include "pure/json_write.h"
#include "pure/markdown.h"
#include "pure/motion_wake.h"
#include "pure/sentence_trim.h"
#include "webfetch.h"
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
  Menu,       // меню настроек (открывается тряской) и его подменю
  Plugins,    // панель плагинов
  Panel,      // экран веб-панели: QR и код доступа
};
State g_state = State::Idle;

std::vector<ui::Line> g_lines;
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
String   g_note;                 // пометка над ответом (что сделал инструмент)
bool     g_justSlept = false;    // только что вышли из лёгкого сна

// Удержание боковой кнопки: отсчёт и полоска — в модуле hold.
hold::Watcher g_holdReset(M5.BtnB, kClearHoldMs);   // сброс истории
hold::Watcher g_holdExit(M5.BtnB, kBackHoldMs);      // «назад»: меню и ответ
hold::Watcher g_holdRecord(M5.BtnA, kRecordStartMs, false);  // запись — по зажатию
uint32_t g_panelShownAt = 0;  // панель сброса висит, пока держат кнопку
uint32_t g_panelUntil = 0;    // до какого момента держать панель результата
String   g_pendingPlugin;     // плагин, ждущий ответа пользователя (город)

// --- меню настроек и панель плагинов ---
size_t g_menuIndex = 0;
size_t g_menuCount = 0;  // сколько пунктов в открытом меню
size_t g_pluginIndex = 0;
std::vector<ui::ListItem> g_pluginRows;  // как показать
std::vector<std::string> g_pluginNames;  // что ставить/удалять
bool     g_imuOk = false;
uint32_t g_lastShake = 0;
motion::Wake g_wake(kPickupG);  // решение «движение будит экран»
bool g_panelOn = false;        // что показывает экран панели
bool g_panelUsed = false;      // заходил ли кто-то на страницу
bool     g_lightSleepLogged = false;  // про лёгкий сон пишем один раз за заход

// --- Энергосбережение ---
uint32_t g_lastActivity = 0;
bool     g_screenOff = false;
bool     g_wifiOff = false;

// Любое действие пользователя: сбрасывает таймеры простоя и будит экран.
void markActivity() {
  g_lastActivity = millis();

  // Проснулись — сразу поднимаем Wi-Fi, не дожидаясь вопроса. Пока человек
  // тянется к кнопке и зажимает её, модуль успевает ассоциироваться, и вопрос
  // начинается без паузы на подключение. Неблокирующе: ждать здесь нельзя.
  if (g_wifiOff) {
    g_wifiOff = false;
    net::wakeWifi();
  }

  if (g_screenOff) {
    g_screenOff = false;
    ui::screenOn();
  }
  g_lightSleepLogged = false;  // проснулись — про следующий сон скажем снова
}

// Движение будит погашенный экран.
//
// Проверка живёт рядом с энергосбережением и зовётся из pollIdlePower, а не из
// цикла состояний: так она работает одинаково со всех экранов — с главного, с
// ответа и из меню. Раньше её вызывал только основной путь, поэтому казалось,
// что сон работает не везде.
//
// Одного отсчёта мало. Акселерометр шумит, а сразу после лёгкого сна первый
// замер вообще мусорный: в логе ловилось «движение (0.12 g)», и такой мусор
// будил экран бесконечно. Поэтому отбрасываем заведомо неверные значения и
// требуем несколько попаданий подряд.
bool wakeByMotion() {
  if (!g_imuOk) return false;
  float ax = 0, ay = 0, az = 0;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return false;
  const float mag = sqrtf(ax * ax + ay * ay + az * az);

  // Решение принимает модуль: он отбрасывает мусорные отсчёты и требует
  // несколько попаданий подряд. Проверяется тестами — руками это не потрясти.
  if (!g_wake.feed(mag)) return false;

  Serial.printf("[main] движение (%.2f g) — будим экран\n", mag);
  markActivity();
  return true;
}

// Простой: сначала гаснет экран, потом Wi-Fi, потом глубокий сон.
// Меню тоже уходит по этому таймеру, поэтому showIdle нужен заранее.
void showIdle();

void pollIdlePower() {
  const bool inMenu = (g_state == State::Menu ||
                       g_state == State::Plugins ||
                       g_state == State::Panel);
  if (g_state != State::Idle && g_state != State::Showing && !inMenu) return;
  const uint32_t idle = millis() - g_lastActivity;
  const uint32_t screenAfter = cfg::screenMs();
  const uint32_t wifiAfter = cfg::wifiMs();

  // Меню не висит вечно: по тому же таймеру уходим на главный экран, а следом
  // гаснет и экран — как в обычном простое. Кнопки в меню обновляют отсчёт,
  // поэтому навигация меню не закрывает.
  if (inMenu && idle >= screenAfter) {
    Serial.println("[main] простой: выхожу из меню на главный экран");
    showIdle();
  }

  if (!g_screenOff && idle >= screenAfter) {
    g_screenOff = true;
    ui::dotsStop();
    ui::screenOff();
    // Экран погас — по панели больше не ходят (иначе запросы продлили бы
    // простой), значит сервер не нужен, а память нужна.
    if (panel::active()) panel::end();
    Serial.println("[main] простой: экран погашен");
  }

  if (!g_wifiOff && wifiAfter != 0 && idle >= wifiAfter) {
    g_wifiOff = true;
    Serial.println("[main] простой: Wi-Fi выключен");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  // Лёгкий сон: экран погашен и Wi-Fi выключен — значит делать нечего, и
  // можно усыпить процессор по-настоящему. Просыпается раз в 200 мс по
  // таймеру, проверяет датчик и кнопки, и засыпает снова. Память при этом
  // сохраняется: диалог не теряется, в отличие от глубокого сна.
  //
  // Потребление падает в десятки раз (единицы миллиампер против десятков),
  // а движение по-прежнему будит экран — просто с задержкой до 200 мс.
  // Лёгкий сон невозможен с включённым Wi-Fi: в нём модем засыпает вместе с
  // процессором, и соединение порвалось бы. Поэтому если время сна пришло
  // раньше, чем время Wi-Fi, гасим его сами — иначе сон просто не наступит,
  // и настройка будет обманывать.
  const uint32_t lightAfter = cfg::lightMs();
  if (lightAfter != 0 && g_screenOff && idle >= lightAfter && !g_wifiOff) {
    g_wifiOff = true;
    Serial.println("[main] простой: Wi-Fi выключен (перед лёгким сном)");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  if (lightAfter != 0 && g_screenOff && g_wifiOff && idle >= lightAfter) {
    // В лёгком сне цифровые подтяжки на выводах гаснут. Кнопки у нас на
    // внутренних подтяжках, поэтому их выводы повисли бы в воздухе: после
    // пробуждения они читались бы как нажатые, и первый же опрос «нажал»
    // бы что-нибудь — например, в панели плагинов удалил бы плагин. Плюс
    // повисший в низком уровне вывод кнопки будил бы стик сразу же.
    rtc_gpio_pullup_en((gpio_num_t)37);
    rtc_gpio_pullup_en((gpio_num_t)39);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_37, 0);  // большая кнопка
    esp_sleep_enable_timer_wakeup(kLightSleepUs);
    if (!g_lightSleepLogged) {
      g_lightSleepLogged = true;
      Serial.println("[main] простой: лёгкий сон");
    }
    const uint32_t t0 = millis();
    esp_light_sleep_start();
    g_justSlept = true;

    // После сна состояние кнопок недостоверно. Читаем их уровень напрямую
    // и выставляем состояние по факту — тогда ни ложных нажатий, ни
    // потерянных настоящих.
    const uint32_t now = millis();
    M5.BtnA.setRawState(now, digitalRead(37) == LOW);
    M5.BtnB.setRawState(now, digitalRead(39) == LOW);
  }

  const uint32_t sleepAfter = cfg::deepMs();
  if (sleepAfter != 0 && idle >= sleepAfter) {
    Serial.println("[main] простой: глубокий сон");
    ui::screenOff();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    // Будит большая кнопка (BtnA, GPIO37) — по низкому уровню.
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_37, 0);
    esp_deep_sleep_start();
  }

  // Движение будит погашенный экран — с любого экрана, потому что проверка
  // стоит здесь, в общей части энергосбережения. Сразу после лёгкого сна
  // замер пропускаем: первый отсчёт акселерометра бывает мусорным.
  if (g_screenOff && !g_justSlept) wakeByMotion();
}

// Ширина любого символа в моноширинном шрифте — константа.
int measureMono(const char*, size_t, void*) { return kFont_jbmono_XAdvance; }

void wrapInto(const char* text, std::vector<ui::Line>& out) {
  out.clear();
  std::vector<std::string> wrapped;
  textwrap::wrap(text, kBodyW, measureMono, nullptr, wrapped);
  for (const std::string& s : wrapped) out.push_back({s, false});
}

// Разбор ответа: markdown превращается в строки, строки кода помечаются.
// Переносим каждую строку отдельно — так пометка «код» переезжает на все
// куски, и код остаётся кодом даже если не влез в ширину экрана.
void wrapMarkdown(const char* text, std::vector<ui::Line>& out) {
  std::vector<md::Line> src;
  md::parse(text, src);

  out.clear();
  std::vector<std::string> wrapped;
  for (const md::Line& l : src) {
    if (l.text.empty()) {
      out.push_back({"", l.code});
      continue;
    }
    wrapped.clear();
    textwrap::wrap(l.text.c_str(), kBodyW, measureMono, nullptr, wrapped);
    for (const std::string& w : wrapped) out.push_back({w, l.code});
  }
}

// Разметка ответа: если инструмент оставил пометку о том, что сделал, она
// идёт первой строкой. В память диалога пометка не попадает — она нужна
// только на экране.
void wrapAnswer(const char* text) {
  if (g_note.isEmpty()) {
    wrapMarkdown(text, g_lines);
    return;
  }
  // Пометка инструмента идёт первой строкой и разметке не подчиняется.
  const String all = g_note + "\n" + text;
  wrapMarkdown(all.c_str(), g_lines);
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
// Строка-пометка инструмента всегда первая; (size_t)-1 — пометки нет.
size_t noteLine() { return g_note.isEmpty() ? (size_t)-1 : 0; }

void drawAnswer(size_t page) {
  g_page = page;
  g_drawnPage = page;
  g_drawnTotal = textwrap::pageCount(g_lines.size(), ui::kBodyLines);
  g_drawnTo = (page + 1) * ui::kBodyLines;
  ui::dotsStop();
  ui::answerReset();
  ui::answerLines(g_lines, page * ui::kBodyLines, page * ui::kBodyLines,
                  (page + 1) * ui::kBodyLines, g_color, noteLine());
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

// Снимок истории для веб-панели. Панель отдаёт его как есть, поэтому JSON
// собираем здесь и только когда история меняется.
void updatePanelHistory() {
  if (!panel::active()) return;
  std::string out = "[";
  const std::vector<chat::Msg>& msgs = g_history.messages();
  for (size_t i = 0; i < msgs.size(); ++i) {
    if (i) out += ",";
    jsonw::message(msgs[i].fromUser, msgs[i].text, out);
  }
  out += "]";
  panel::setHistory(out.c_str());
}

void showIdle() {
  ui::dotsStop();
  g_state = State::Idle;
  ui::idle(g_history.turnCount(), g_msgs);
}

// Начало фазы ожидания: чистый экран и запущенная анимация точек.
// Сброс накопленного стрима перед новым запросом. Экран не трогаем: его
// перерисует первый же кадр нового ответа. Без этого текст второго запроса
// дописывался бы к первому, а экран считал бы часть строк уже нарисованной.
void resetStream() {
  g_stream = "";
  g_note = "";
  g_page = 0;
  g_drawnPage = kNoPage;
  g_drawnTotal = 0;
  g_drawnTo = 0;
  g_lastDraw = 0;
}

void startWaiting() {
  resetStream();
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

  wrapAnswer(g_stream.c_str());
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
    ui::answerLines(g_lines, first, first, last, kWhite, noteLine());
    ui::pageIndicator(g_page, total);
    g_drawnTo = last;
    return;
  }

  if (g_page == total - 1 && last > first) {
    size_t start = (g_drawnTo > first) ? g_drawnTo - 1 : first;
    if (start >= last) start = last - 1;
    ui::answerLines(g_lines, first, start, last, kWhite, noteLine());
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

void runQuestionInner();

// Вопрос — это два TLS-рукопожатия, а им нужна непрерывная память. Пока он
// идёт, панель отпускает снимок истории: страница всё равно не опрашивается,
// пока цикл занят ответом. Панель при этом остаётся поднятой и после ответа
// снова отдаёт историю — уже вместе с новым обменом.
void runQuestion() {
  const bool panelWas = panel::active();
  if (panelWas) panel::releaseHistory();
  runQuestionInner();
  if (panelWas) updatePanelHistory();
}

void runQuestionInner() {
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
    // Плагин может работать секунды (поиск в интернете ходит по страницам),
    // поэтому экран ожидания показываем до него, а не после: иначе экран
    // замирает на старом тексте и кажется, что стик завис. Метка говорит,
    // что именно происходит: «ищу в интернете», «переключаю свет».
    ui::toolWait(plugins::labelOf(a.toolName.c_str()));

    // Плагину нужна непрерывная память: своя задача со стеком в двадцать
    // килобайт, а если он ходит в сеть — ещё и TLS-рукопожатие. Сессия модели
    // для этого не нужна: ответ уже получен целиком, а следующий запрос
    // поднимет соединение заново. Без этого падало «не запустить плагин».
    net::llmClient().stop();
    Serial.printf("[main] перед плагином: куча %u, блок %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

    const plugins::Result pr =
        plugins::run(a.toolName.c_str(), a.toolArgs.c_str());

    // Панель результата — только если модель отвечать не будет.
    if (!pr.answerAfter) showPluginResult(pr);

    // Плагин просит модель ответить по материалу (поиск): простыню со
    // страниц на экран не выводим, ответит модель.
    if (pr.answerAfter && pr.ask.empty()) {
      // Новый ответ начинается с чистого листа: иначе он допишется к тому,
      // что модель успела сказать до вызова инструмента.
      resetStream();
      g_note = String(pr.note.c_str());

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
        updatePanelHistory();
        g_color = kWhite;
        g_page = 0;
        wrapAnswer(c.text.c_str());
        g_state = State::Showing;
        drawAnswer(g_page);
      } else {
        // Модель не ответила — не оставляем стик в состоянии «в процессе»:
        // иначе перестают работать и кнопка, и тряска, и он выглядит мёртвым.
        showError(c.ok ? "Модель не ответила" : c.error.c_str());
      }
    }
    return;
  }

  if (a.ok) {
    Serial.printf("[main] ответ: %s\n", a.text.c_str());
    g_history.addUser(s.text.c_str());
    g_history.addAssistant(a.text.c_str());
    trimHistory();
    updatePanelHistory();
    Serial.printf("[main] в памяти %u обменов\n",
                  (unsigned)g_history.turnCount());

    g_state = State::Showing;
    g_color = kWhite;
    g_page = 0;
    wrapAnswer(a.text.c_str());
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

// Сеть нужна не только вопросу: панель плагинов тоже ходит в интернет. В
// простое Wi-Fi гасится, а панель его не поднимала — список приезжал из кэша
// (и выглядел как «сеть есть»), а скачивание падало с «нет связи с
// raw.githubusercontent.com». Поэтому перед любым походом в сеть из панели
// поднимаем радио. true — сеть есть.
bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  startWaiting();
  if (!net::connectWifi(kWifiTimeoutMs)) return false;
  g_wifiOff = false;  // Wi-Fi снова поднят
  return true;
}

void drawPlugins() {
  ui::list("Плагины", g_pluginRows, g_pluginIndex);
}

// Экран веб-панели: выключатель. Сам сервер не поднимаем — панель включается
// кнопкой A, чтобы она жила ровно тогда, когда её включили.
void drawPanel() {
  g_panelOn = panel::active();
  g_panelUsed = panel::used();
  if (!g_panelOn) {
    ui::panel(false, false, "", "", "");
    return;
  }
  const std::string u = panel::url();
  const String ip = WiFi.localIP().toString();
  ui::panel(true, g_panelUsed, u.c_str(), ip.c_str(), panel::code());
}

void showPanel() {
  g_holdExit.clear();
  markActivity();
  g_state = State::Panel;
  drawPanel();
}

// Включить или выключить панель кнопкой A.
void togglePanel() {
  if (panel::active()) {
    panel::end();
    Serial.println("[main] панель выключена кнопкой");
    drawPanel();
    return;
  }
  if (!ensureWifi()) {
    ui::notice("нет сети — панель не поднять");
    delay(1500);
    drawPanel();
    return;
  }
  if (!panel::begin()) {
    ui::notice("панель не поднялась");
    delay(1500);
    drawPanel();
    return;
  }
  updatePanelHistory();  // страница должна показать то, что уже наговорено
  drawPanel();
  Serial.printf("[main] панель включена: %s\n", panel::url().c_str());
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
    bool ok = ensureWifi();
    if (!ok) {
      err = "нет сети";
    } else {
      ok = repo::install(name.c_str(), err);
    }
    if (!ok) {
      ui::notice(("не поставить: " + err).c_str());
      delay(1600);
    } else {
      ui::notice(("поставлен " + name).c_str());
      delay(900);
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
  ensureWifi();  // в простое Wi-Fi погашен — поднимаем до похода в сеть
  repo::refresh();
  // Список берётся из кэша, когда сети нет. Об этом надо сказать вслух:
  // иначе кажется, что сеть есть, и непонятно, почему плагин не ставится.
  if (!repo::online()) {
    ui::notice(repo::available().empty() ? "нет сети и список пуст"
                                         : "нет сети — список из кэша");
    delay(1400);
  }
  buildPluginRows();
  g_pluginIndex = 0;
  drawPlugins();
}

// Пункты меню настроек. Порядок должен совпадать с menuRows().
// Пункты главного меню. Порядок должен совпадать с menuRows().
enum class SettingRow { Plugins = 0, Web, Dialog, Power, Buttons, Reset };

enum class DialogRow { Length = 0, Turns, Tokens };

// Этапы сна по возрастанию простоя: экран, Wi-Fi, лёгкий сон, глубокий.
enum class PowerRow { Screen = 0, Wifi, Light, Deep };

enum class ButtonRow { Back = 0, ResetHold };

// Какое меню открыто. Устроены они одинаково: список, навигация и возврат
// на уровень выше, поэтому код общий, а различаются только пункты.
enum class Menu { Settings, Dialog, Power, Buttons };
Menu g_menu = Menu::Settings;

const char* menuTitle(Menu m) {
  switch (m) {
    case Menu::Dialog: return "Диалог";
    case Menu::Power: return "Питание";
    case Menu::Buttons: return "Кнопки";
    default: return "Настройки";
  }
}

std::vector<ui::ListItem> menuRows(Menu m) {
  std::vector<ui::ListItem> rows;
  switch (m) {
    case Menu::Dialog:
      rows.push_back(ui::ListItem("Длина ответа", chatcfg::lengthLabel()));
      rows.push_back(ui::ListItem("Память диалога", chatcfg::turnsLabel()));
      rows.push_back(ui::ListItem("Лимит ответа", chatcfg::tokensLabel()));
      break;

    case Menu::Power:
      // Порядок — как в жизни: сначала гаснет экран, потом уходит Wi-Fi,
      // потом сон, и только потом глубокий.
      rows.push_back(ui::ListItem("Экран гаснет", cfg::screenLabel()));
      rows.push_back(ui::ListItem("Wi-Fi выкл.", cfg::wifiLabel()));
      rows.push_back(ui::ListItem("Лёгкий сон", cfg::lightLabel()));
      rows.push_back(ui::ListItem("Глубокий сон", cfg::deepLabel()));
      break;

    case Menu::Buttons:
      rows.push_back(ui::ListItem("Назад (удержание)", cfg::backLabel()));
      rows.push_back(ui::ListItem("Сброс сессии", cfg::resetLabel()));
      break;

    default:
      rows.push_back(ui::ListItem("Плагины"));
      rows.push_back(ui::ListItem("Веб-панель"));
      rows.push_back(ui::ListItem("Диалог и ответ"));
      rows.push_back(ui::ListItem("Питание"));
      rows.push_back(ui::ListItem("Кнопки"));
      rows.push_back(ui::ListItem("Сбросить настройки"));
      break;
  }
  return rows;
}

void drawMenu() { ui::list(menuTitle(g_menu), menuRows(g_menu), g_menuIndex); }

void openMenu(Menu m) {
  g_holdExit.clear();
  markActivity();  // открыли меню — отсчёт простоя начинается заново
  g_menu = m;
  g_menuIndex = 0;
  g_menuCount = menuRows(m).size();
  g_state = State::Menu;
  drawMenu();
}

// Времена удержаний выбираются в настройках, поэтому применяем их к
// наблюдателям сразу после смены значения.
void applyHoldTimes() {
  g_holdExit.setThreshold(cfg::backMs());
  g_holdReset.setThreshold(cfg::resetMs());
}

void pollPanel() {
  // Панель могла закрыться сама — по тишине, потолку или вместе с экраном.
  // Тогда экран обязан сказать «выключена», а не врать про QR.
  if (panel::active() != g_panelOn || panel::used() != g_panelUsed) drawPanel();

  // Кнопки как в меню: боковая удержанием — назад.
  switch (g_holdExit.poll()) {
    case hold::Event::Fired:
      showIdle();
      return;
    case hold::Event::Tap:
      return;
    default:
      break;
  }
  if (M5.BtnA.wasClicked()) togglePanel();
}

// BtnA: войти в пункт или переключить значение. Списки короткие, поэтому
// перебор по кругу понятнее отдельного экрана выбора.
void activateMenu(size_t row) {
  if (g_menu == Menu::Dialog) {
    switch (static_cast<DialogRow>(row)) {
      case DialogRow::Length: chatcfg::nextLength(); break;
      case DialogRow::Turns: chatcfg::nextTurns(); break;
      case DialogRow::Tokens: chatcfg::nextTokens(); break;
      default: return;
    }
  } else if (g_menu == Menu::Power) {
    switch (static_cast<PowerRow>(row)) {
      case PowerRow::Screen: cfg::nextScreen(); break;
      case PowerRow::Wifi: cfg::nextWifi(); break;
      case PowerRow::Light: cfg::nextLight(); break;
      case PowerRow::Deep: cfg::nextDeep(); break;
      default: return;
    }
  } else if (g_menu == Menu::Buttons) {
    switch (static_cast<ButtonRow>(row)) {
      case ButtonRow::Back: cfg::nextBack(); applyHoldTimes(); break;
      case ButtonRow::ResetHold: cfg::nextReset(); applyHoldTimes(); break;
      default: return;
    }
  } else {
    switch (static_cast<SettingRow>(row)) {
      case SettingRow::Plugins: enterPlugins(); return;
      case SettingRow::Web: showPanel(); return;
      case SettingRow::Dialog: openMenu(Menu::Dialog); return;
      case SettingRow::Power: openMenu(Menu::Power); return;
      case SettingRow::Buttons: openMenu(Menu::Buttons); return;
      case SettingRow::Reset: chatcfg::reset(); cfg::reset(); applyHoldTimes(); break;
      default: return;
    }
  }
  drawMenu();  // значение видно прямо в строке — это и есть отклик
}

void pollMenu() {
  const size_t n = g_menuCount;

  // BtnB отдан модулю удержания: тап — назад по списку, зажатие — на уровень
  // выше (из главного — на главный экран).
  switch (g_holdExit.poll()) {
    case hold::Event::Fired:
      if (g_menu == Menu::Settings) {
        showIdle();
      } else {
        openMenu(Menu::Settings);
      }
      return;
    case hold::Event::Tap:
      if (n > 0) {
        g_menuIndex = (g_menuIndex + n - 1) % n;
        drawMenu();
      }
      return;
    default:
      break;
  }

  if (M5.BtnPWR.wasClicked() && n > 0) {
    g_menuIndex = (g_menuIndex + 1) % n;
    drawMenu();
    return;
  }
  if (M5.BtnA.wasClicked() && n > 0) { activateMenu(g_menuIndex); return; }
}

void pollPlugins() {
  const size_t n = g_pluginRows.size();

  // BtnB отдан модулю удержания: тап — назад по списку, зажатие — выход.
  switch (g_holdExit.poll()) {
    case hold::Event::Fired:
      openMenu(Menu::Settings);
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

// Тряска открывает настройки. Пробуждение погашенного экрана живёт отдельно,
// в wakeByMotion(): это разные вещи — будить экран надо со всех экранов, а
// открывать меню только с главного и с ответа.
void checkShake() {
  if (!g_imuOk) return;
  // Экран погашен — движением занимается wakeByMotion(). Меню при этом не
  // открываем: поднять стик со стола не значит «покажи настройки».
  if (g_screenOff) return;
  if (g_state != State::Idle && g_state != State::Showing) return;

  float ax = 0, ay = 0, az = 0;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
  const float mag = sqrtf(ax * ax + ay * ay + az * az);

  if (millis() - g_lastShake < 1500) return;

  // Настоящая тряска — это несколько подряд сильных отсчётов (~60 мс),
  // а случайный толчок даёт один. Поэтому считаем попадания подряд.
  static uint8_t hits = 0;
  if (mag > 2.2f || mag < 0.3f) {
    if (++hits < 3) return;
    hits = 0;
    g_lastShake = millis();
    Serial.printf("[main] тряска (%.2f g) — настройки\n", mag);
    openMenu(Menu::Settings);
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

  // Таймауты сна и времена удержаний кнопок.
  cfg::begin();
  applyHoldTimes();

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

  // Панель обслуживаем из общего цикла, а не отдельной задачей: её стек в
  // шесть килобайт — это ровно тот запас непрерывной памяти, которого не
  // хватало TLS-рукопожатию при открытых сессиях модели и распознавания.
  // Заодно панель сама уступает вопросу: пока цикл занят ответом, она молчит.
  panel::poll();
  // Запросы к панели — это активность: пока по ней ходят, стик не спит.
  if (panel::touched()) markActivity();

  // Меню и панель — отдельные режимы: кнопки в них значат другое.
  // Простой проверяем и здесь: иначе меню висело бы с горящим экраном вечно.
  if (g_state == State::Menu) {
    pollMenu();
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
  if (g_state == State::Panel) {
    pollPanel();
    // С экрана панели тоже можно спросить: ответ придёт и на страницу.
    if (g_holdRecord.poll() == hold::Event::Fired) {
      runQuestion();
      markActivity();
    }
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

  // Только что спали — сразу обратно, без паузы. Эта пауза добавляет
  // активного времени больше, чем вся работа цикла: в простое она чистый
  // расход, потому что между засыпаниями мы всё равно ничего не делаем.
  if (!g_justSlept) delay(20);
  g_justSlept = false;
}
