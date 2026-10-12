// Распознавание речи через свой прокси на VPS.
//
// Стик -> прокси (HTTPS, сырой PCM) -> Groq Whisper.
// Прокси нужен потому, что api.groq.com недоступен из России (403).
// Multipart и WAV-заголовок собираются на сервере: в прошивке их нет.
//
// HTTPClient не используется: его потоковый путь сдаётся после одной
// короткой записи в сокет, а нам нужна терпеливая отправка.

#include "stt.h"

#include <WiFiClientSecure.h>
#include <cstring>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "pure/http_response.h"
#include "pure/json_extract.h"

namespace stt {

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

  WiFiClientSecure& c = net::tlsClient();
  c.setTimeout(kHttpConnectMs);

  Serial.printf("[stt] подключаюсь к %s:%d\n", PROXY_HOST, PROXY_PORT);
  const uint32_t t0 = millis();
  if (!c.connect(PROXY_HOST, PROXY_PORT)) {
    r.error = String("не подключиться к ") + PROXY_HOST;
    return r;
  }
  c.setTimeout(kHttpReadMs);

  char head[512];
  snprintf(head, sizeof(head),
           "POST %s HTTP/1.1\r\n"
           "Host: %s:%d\r\n"
           "X-Auth: %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %u\r\n"
           "Connection: close\r\n"
           "\r\n",
           PROXY_PATH, PROXY_HOST, PROXY_PORT, PROXY_TOKEN,
           kSttContentType, (unsigned)pcmLen);

  if (!net::writeAll(c, (const uint8_t*)head, strlen(head), t0)) {
    r.error = "не отправить заголовки";
    c.stop();
    return r;
  }

  Serial.printf("[stt] отправляю %u байт PCM\n", (unsigned)pcmLen);
  // Замер только тела: t0 включает TLS-рукопожатие, а оно на ESP32
  // занимает секунды, и портит оценку скорости.
  const uint32_t bodyStart = millis();
  if (!net::writeAll(c, pcm, pcmLen, t0, hooks.onUpload, hooks.ctx)) {
    r.error = "обрыв отправки";
    c.stop();
    return r;
  }
  const uint32_t sentMs = millis() - bodyStart;
  const uint32_t kbps = sentMs ? (uint32_t)((uint64_t)pcmLen * 1000 /
                                            sentMs / 1024) : 0;
  Serial.printf("[stt] тело %u байт за %u мс (%u КБ/с), всего с TLS %u мс\n",
                (unsigned)pcmLen, sentMs, kbps, millis() - t0);

  const String raw = net::readAll(c, t0, kMaxSttResponse, hooks.onTick, hooks.ctx);
  c.stop();
  Serial.printf("[stt] ответ %u байт за %u мс\n",
                (unsigned)raw.length(), millis() - t0);

  std::string rawStr(raw.c_str(), raw.length());
  std::string headers, payload;
  httpr::splitHeadBody(rawStr, headers, payload);
  r.httpCode = httpr::parseStatus(headers);
  if (r.httpCode < 0) {
    r.error = String("не разобран статус: ") +
              String(raw.substring(0, 40).c_str());
    return r;
  }
  if (httpr::isChunked(headers)) payload = httpr::dechunk(payload);

  if (r.httpCode != 200) {
    // Тело печатаем целиком в Serial: на экране 135 px, а причина — здесь.
    Serial.printf("[stt] тело ошибки: %s\n", payload.c_str());
    r.error = String("HTTP ") + r.httpCode + ": " +
              String(payload.substr(0, 200).c_str());
    return r;
  }

  std::string out;
  if (jsonx::extractString(payload.c_str(), "text", out)) {
    r.text = String(out.c_str());
    r.ok = true;
    return r;
  }

  Serial.printf("[stt] тело ответа: %s\n", payload.c_str());
  r.error = String("не разобран ответ: ") +
            String(payload.substr(0, 200).c_str());
  return r;
}

}  // namespace stt
