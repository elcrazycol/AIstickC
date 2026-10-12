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

namespace {

// Терпеливая запись: ждём, пока сокет примет данные.
bool writeAll(WiFiClient& c, const uint8_t* data, size_t len, uint32_t t0) {
  size_t sent = 0;
  while (sent < len) {
    if (!c.connected() && c.available() == 0) return false;
    if (millis() - t0 > kHttpReadMs) return false;

    const size_t n = c.write(data + sent, len - sent);
    if (n > 0) {
      sent += n;
      continue;
    }
    delay(2);  // сокет занят — даём ему время
  }
  return true;
}

bool writeStr(WiFiClient& c, const char* s, uint32_t t0) {
  return writeAll(c, (const uint8_t*)s, strlen(s), t0);
}

// Читает ответ целиком, пока сервер не закроет соединение.
String readAll(WiFiClient& c, uint32_t t0) {
  String out;
  uint32_t idleSince = millis();
  while (true) {
    if (millis() - t0 > kHttpReadMs) break;
    while (c.available() > 0) {
      const int ch = c.read();
      if (ch < 0) break;
      out += (char)ch;
      idleSince = millis();
      if (out.length() > kMaxSttResponse) break;
    }
    if (out.length() > kMaxSttResponse) break;
    if (!c.connected() && c.available() == 0) break;
    // Сервер ответил и молчит — не ждём таймаут целиком.
    if (out.length() > 0 && (millis() - idleSince) > 1500) break;
    delay(5);
  }
  return out;
}

}  // namespace

Result transcribe(const uint8_t* pcm, size_t pcmLen) {
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

  if (!writeStr(c, head, t0)) {
    r.error = "не отправить заголовки";
    c.stop();
    return r;
  }

  Serial.printf("[stt] отправляю %u байт PCM\n", (unsigned)pcmLen);
  // Замер только тела: t0 включает TLS-рукопожатие, а оно на ESP32
  // занимает секунды, и портит оценку скорости.
  const uint32_t bodyStart = millis();
  if (!writeAll(c, pcm, pcmLen, t0)) {
    r.error = "обрыв отправки";
    c.stop();
    return r;
  }
  const uint32_t sentMs = millis() - bodyStart;
  const uint32_t kbps = sentMs ? (uint32_t)((uint64_t)pcmLen * 1000 /
                                            sentMs / 1024) : 0;
  Serial.printf("[stt] тело %u байт за %u мс (%u КБ/с), всего с TLS %u мс\n",
                (unsigned)pcmLen, sentMs, kbps, millis() - t0);

  const String raw = readAll(c, t0);
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
