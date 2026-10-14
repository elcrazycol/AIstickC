// Распознавание речи через свой прокси на VPS.
//
// Стик -> прокси (HTTPS, сырой PCM) -> Groq Whisper.
// Прокси нужен потому, что api.groq.com недоступен из России (403).
// Multipart и WAV-заголовок собираются на сервере: в прошивке их нет.
//
// HTTPClient не используется: его потоковый путь сдаётся после одной
// короткой записи в сокет, а нам нужна терпеливая отправка.

#include "stt.h"

#include "pure/byte_ring.h"
#include "pure/ulaw.h"

#include <WiFiClientSecure.h>
#include <cstring>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "pure/http_response.h"
#include "pure/json_extract.h"

namespace stt {

namespace {

// Разбор ответа прокси: статус и текст распознавания.
void parseResponse(const String& raw, Result& r) {
  std::string rawStr(raw.c_str(), raw.length());
  std::string headers, payload;
  httpr::splitHeadBody(rawStr, headers, payload);
  r.httpCode = httpr::parseStatus(headers);
  if (r.httpCode < 0) {
    r.error = String("не разобран статус: ") +
              String(raw.substring(0, 40).c_str());
    return;
  }
  if (httpr::isChunked(headers)) payload = httpr::dechunk(payload);

  if (r.httpCode != 200) {
    // Тело печатаем целиком в Serial: на экране 135 px, а причина — здесь.
    Serial.printf("[stt] тело ошибки: %s\n", payload.c_str());
    r.error = String("HTTP ") + r.httpCode + ": " +
              String(payload.substr(0, 200).c_str());
    return;
  }

  std::string text;
  if (!jsonx::extractPath(payload.c_str(), "text", text)) {
    r.error = "в ответе нет поля text";
    Serial.printf("[stt] ответ без text: %s\n", payload.substr(0, 200).c_str());
    return;
  }
  r.text = String(text.c_str());
  r.ok = true;
}

// --- потоковая отправка ---
//
// Сеть прокачивает около 92 КБ/с (окно TCP 5760 байт на RTT 62 мс), а речь
// даёт 16 КБ/с в µ-law. Значит, отправлять можно прямо во время говорения:
// сеть втрое быстрее речи и успевает с запасом. К отпусканию кнопки почти
// весь звук уже на прокси, и остаётся дослать хвост.
constexpr size_t kRingBytes = 64 * 1024;  // ~4 секунды речи в µ-law
constexpr size_t kChunkBytes = 4096;

struct Stream {
  uint8_t* mem = nullptr;
  ring::ByteRing ring{nullptr, 0};
  TaskHandle_t task = nullptr;
  volatile bool ended = false;
  volatile bool done = false;
  volatile bool failed = false;
  Result result;
};

Stream g_stream;

void streamTask(void*) {
  Stream& st = g_stream;
  WiFiClientSecure& c = net::proxyClient();
  const uint32_t t0 = millis();

  if (!c.connected()) {
    c.setTimeout(kHttpConnectMs);
    Serial.printf("[stt] поток: подключаюсь к %s:%d\n", PROXY_HOST, PROXY_PORT);
    if (!c.connect(PROXY_HOST, PROXY_PORT)) {
      st.result.error = String("не подключиться к ") + PROXY_HOST;
      st.failed = true;
      st.done = true;
      vTaskDelete(nullptr);
      return;
    }
  }
  c.setTimeout(kHttpReadMs);

  // Длина тела заранее неизвестна — говорим серверу, что шлём кусками.
  char head[512];
  snprintf(head, sizeof(head),
           "POST %s HTTP/1.1\r\n"
           "Host: %s:%d\r\n"
           "X-Auth: %s\r\n"
           "Content-Type: %s\r\n"
           "Transfer-Encoding: chunked\r\n"
           "Connection: keep-alive\r\n"
           "\r\n",
           PROXY_PATH, PROXY_HOST, PROXY_PORT, PROXY_TOKEN, kSttContentTypeUlaw);

  if (!net::writeAll(c, (const uint8_t*)head, strlen(head), t0)) {
    st.result.error = "не отправить заголовки";
    st.failed = true;
    c.stop();
    st.done = true;
    vTaskDelete(nullptr);
    return;
  }

  uint8_t buf[kChunkBytes];
  size_t total = 0;
  bool stalled = false;
  while (!stalled) {
    const size_t n = st.ring.read(buf, sizeof(buf));
    if (n > 0) {
      char frame[16];
      const int fl = snprintf(frame, sizeof(frame), "%X\r\n", (unsigned)n);
      if (!net::writeAll(c, (const uint8_t*)frame, (size_t)fl, t0) ||
          !net::writeAll(c, buf, n, t0) ||
          !net::writeAll(c, (const uint8_t*)"\r\n", 2, t0)) {
        st.result.error = "обрыв отправки";
        st.failed = true;
        break;
      }
      total += n;
      continue;
    }
    if (st.ended) break;
    delay(5);
  }

  if (!st.failed) {
    net::writeAll(c, (const uint8_t*)"0\r\n\r\n", 5, t0);
    Serial.printf("[stt] поток: %u байт ушло за %u мс\n", (unsigned)total,
                  (unsigned)(millis() - t0));
    const String raw = net::readAll(c, t0, kMaxSttResponse);
    parseResponse(raw, st.result);
  } else {
    c.stop();
  }
  st.done = true;
  vTaskDelete(nullptr);
}

}  // namespace

namespace {
// Сжатый звук: µ-law вдвое короче исходного PCM. Буфер в PSRAM и живёт
// между вопросами — перевыделять его каждый раз незачем.
uint8_t* g_ulaw = nullptr;
}  // namespace

Result transcribe(const uint8_t* pcm, size_t pcmLen, const Hooks& hooks) {
  Result r;

  if (pcm == nullptr || pcmLen == 0) {
    r.error = "пустой буфер";
    return r;
  }
  if (pcmLen < kSttMinBytes) {
    r.error = "слишком коротко для распознавания";
    return r;
  }

  WiFiClientSecure& c = net::proxyClient();
  const uint32_t t0 = millis();

  // Соединение держим открытым между вопросами: рукопожатие TLS на ESP32
  // стоит около полутора секунд, а вопросов много.
  if (!c.connected()) {
    c.setTimeout(kHttpConnectMs);
    Serial.printf("[stt] подключаюсь к %s:%d\n", PROXY_HOST, PROXY_PORT);
    if (!c.connect(PROXY_HOST, PROXY_PORT)) {
      r.error = String("не подключиться к ") + PROXY_HOST;
      return r;
    }
  } else {
    Serial.println("[stt] соединение уже открыто — рукопожатия не будет");
  }
  c.setTimeout(kHttpReadMs);

  // Сжимаем в µ-law: 8 бит вместо 16. Для речи это штатный формат
  // телефонии — качество почти то же, а байт вдвое меньше, значит и
  // отправка вдвое быстрее.
  const size_t samples = pcmLen / 2;
  if (g_ulaw == nullptr) {
    g_ulaw = (uint8_t*)ps_malloc(kAudioBytes / 2);
    if (g_ulaw == nullptr) {
      r.error = "нет памяти под сжатие звука";
      return r;
    }
  }
  ulaw::encodeBuffer(pcm, samples, g_ulaw);
  const size_t bodyLen = samples;

  char head[512];
  snprintf(head, sizeof(head),
           "POST %s HTTP/1.1\r\n"
           "Host: %s:%d\r\n"
           "X-Auth: %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %u\r\n"
           "Connection: keep-alive\r\n"
           "\r\n",
           PROXY_PATH, PROXY_HOST, PROXY_PORT, PROXY_TOKEN,
           kSttContentTypeUlaw, (unsigned)bodyLen);

  if (!net::writeAll(c, (const uint8_t*)head, strlen(head), t0)) {
    r.error = "не отправить заголовки";
    c.stop();
    return r;
  }

  Serial.printf("[stt] отправляю %u байт µ-law (было %u PCM)\n",
                (unsigned)bodyLen, (unsigned)pcmLen);
  // Замер только тела: t0 включает TLS-рукопожатие, а оно на ESP32
  // занимает секунды, и портит оценку скорости.
  const uint32_t bodyStart = millis();
  if (!net::writeAll(c, g_ulaw, bodyLen, t0, hooks.onUpload, hooks.ctx)) {
    r.error = "обрыв отправки";
    c.stop();
    return r;
  }
  const uint32_t sentMs = millis() - bodyStart;
  const uint32_t kbps = sentMs ? (uint32_t)((uint64_t)bodyLen * 1000 /
                                            sentMs / 1024) : 0;
  Serial.printf("[stt] тело %u байт за %u мс (%u КБ/с), всего с TLS %u мс\n",
                (unsigned)bodyLen, sentMs, kbps, millis() - t0);

  const String raw = net::readAll(c, t0, kMaxSttResponse, hooks.onTick, hooks.ctx);
  // Соединение не закрываем — пригодится следующему вопросу.
  Serial.printf("[stt] ответ %u байт за %u мс\n",
                (unsigned)raw.length(), millis() - t0);

  parseResponse(raw, r);
  return r;
}

bool streamBegin() {
  Stream& st = g_stream;
  if (st.mem == nullptr) {
    st.mem = (uint8_t*)ps_malloc(kRingBytes);
    if (st.mem == nullptr) {
      Serial.println("[stt] нет памяти под поток");
      return false;
    }
    st.ring = ring::ByteRing(st.mem, kRingBytes);
  }
  st.ring.reset();
  st.ended = false;
  st.done = false;
  st.failed = false;
  st.result = Result();
  st.task = nullptr;
  if (xTaskCreatePinnedToCore(streamTask, "stt_up", 16384, nullptr, 1, &st.task,
                              0) != pdPASS) {
    Serial.println("[stt] не запустить задачу отправки");
    return false;
  }
  return true;
}

bool streamPush(const uint8_t* ulaw, size_t n) {
  Stream& st = g_stream;
  const uint32_t t0 = millis();
  size_t done = 0;
  while (done < n) {
    if (st.failed) return false;
    done += st.ring.write(ulaw + done, n - done);
    if (done < n) {
      // Кольцо полно: сеть не успевает. Ждём, но не бесконечно — запись
      // важнее, чем одна порция звука.
      if (millis() - t0 > 5000) return false;
      delay(2);
    }
  }
  return true;
}

Result streamEnd() {
  Stream& st = g_stream;
  st.ended = true;
  const uint32_t t0 = millis();
  while (!st.done && (millis() - t0) < 30000) delay(10);
  if (!st.done) {
    Serial.println("[stt] поток не завершился — снимаю задачу");
    if (st.task != nullptr) vTaskDelete(st.task);
    net::proxyClient().stop();
    st.task = nullptr;
    st.result.ok = false;
    st.result.error = "таймаут потока";
  }
  return st.result;
}

void streamAbort() {
  Stream& st = g_stream;
  st.ended = true;
  const uint32_t t0 = millis();
  while (!st.done && (millis() - t0) < 2000) delay(10);
  if (!st.done && st.task != nullptr) {
    vTaskDelete(st.task);
    net::proxyClient().stop();
  }
  st.task = nullptr;
}

}  // namespace stt
