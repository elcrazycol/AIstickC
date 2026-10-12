// Ответ LLM через OpenCode Go.
//
// Стик -> opencode.ai (HTTPS) -> провайдер модели. Прокси не нужен:
// OpenCode доступен из России напрямую, а ключ подписки лежит в config.h.
//
// Сетевой ввод-вывод — общий с STT (net::writeAll/readAll). Тело запроса
// маленькое и собирается целиком в памяти чистым модулем llm_request.

#include "llm.h"

#include <WiFiClientSecure.h>
#include <esp_system.h>

#include <cstring>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "pure/http_response.h"
#include "pure/json_extract.h"
#include "pure/llm_request.h"

namespace llm {

namespace {

char g_session[40] = {0};

// x-opencode-session: значение произвольное, но должно быть стабильным —
// по нему Go держит один и тот же провайдер и греет кэш промпта. Хватает
// стабильности на время работы устройства.
const char* sessionId() {
  if (g_session[0] == 0) {
    const uint32_t a = esp_random();
    const uint32_t b = esp_random();
    const uint32_t c = esp_random();
    const uint32_t d = esp_random();
    snprintf(g_session, sizeof(g_session), "ses_%08x%08x%08x%08x", a, b, c, d);
  }
  return g_session;
}

}  // namespace

Reply ask(const String& userText) {
  Reply r;

  if (userText.length() == 0) {
    r.error = "пустой текст";
    return r;
  }

  const std::string body =
      llmreq::build(kLlmModel, kLlmSystem, userText.c_str(), kLlmMaxTokens);
  if (body.empty()) {
    r.error = "не собрать запрос";
    return r;
  }

  WiFiClientSecure& c = net::tlsClient();
  c.setTimeout(kHttpConnectMs);

  Serial.printf("[llm] подключаюсь к %s\n", kLlmHost);
  const uint32_t t0 = millis();
  if (!c.connect(kLlmHost, kLlmPort)) {
    r.error = String("не подключиться к ") + kLlmHost;
    return r;
  }
  c.setTimeout(kHttpReadMs);

  char head[512];
  snprintf(head, sizeof(head),
           "POST %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "Authorization: Bearer %s\r\n"
           "Content-Type: application/json\r\n"
           "Accept: application/json\r\n"
           "User-Agent: %s\r\n"
           "x-opencode-session: %s\r\n"
           "Content-Length: %u\r\n"
           "Connection: close\r\n"
           "\r\n",
           kLlmPath, kLlmHost, OPENCODE_API_KEY, kLlmUserAgent, sessionId(),
           (unsigned)body.size());

  if (!net::writeAll(c, (const uint8_t*)head, std::strlen(head), t0) ||
      !net::writeAll(c, (const uint8_t*)body.data(), body.size(), t0)) {
    r.error = "обрыв отправки";
    c.stop();
    return r;
  }

  const String raw = net::readAll(c, t0, kMaxLlmResponse);
  c.stop();
  Serial.printf("[llm] ответ %u байт за %u мс\n", (unsigned)raw.length(),
                millis() - t0);

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
    Serial.printf("[llm] тело ошибки: %s\n", payload.c_str());
    r.error = String("HTTP ") + r.httpCode + ": " +
              String(payload.substr(0, 200).c_str());
    return r;
  }

  std::string out;
  if (jsonx::extractPath(payload.c_str(), "choices/[0]/message/content", out)) {
    r.text = String(out.c_str());
    if (r.text.length() == 0) {
      // Бывает у reasoning-моделей: бюджет ушёл в reasoning_content.
      r.error = "модель вернула пустой ответ";
      return r;
    }
    r.ok = true;
    return r;
  }

  // Ответ 200, но без content — возможно, вернули error-объект.
  std::string err;
  if (jsonx::extractPath(payload.c_str(), "error/message", err)) {
    r.error = String("модель: ") + String(err.substr(0, 200).c_str());
    return r;
  }

  Serial.printf("[llm] тело ответа: %s\n", payload.c_str());
  r.error = String("не разобран ответ: ") +
            String(payload.substr(0, 200).c_str());
  return r;
}

}  // namespace llm
