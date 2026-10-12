// Ответ LLM через OpenCode Go, потоком (SSE).
//
// Стик -> opencode.ai (HTTPS) -> провайдер модели. Прокси не нужен:
// OpenCode доступен из России напрямую, а ключ подписки лежит в config.h.
//
// Запрос идёт со stream:true, ответ приходит как text/event-stream. Кусок
// текста отдаётся наружу через Stream::onDelta, поэтому ответ появляется на
// экране по мере генерации, а счётчик принятых символов двигает индикатор.

#include "llm.h"

#include <WiFiClientSecure.h>
#include <esp_system.h>

#include <cstring>
#include <string>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "pure/http_response.h"
#include "pure/json_extract.h"
#include "pure/llm_request.h"
#include "pure/sse.h"

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

// Разбирает один SSE-кусок. 0 — дельты нет, 1 — текст ответа,
// 2 — reasoning (для индикатора, на экран не идёт).
int deltaOf(const std::string& json, std::string& out) {
  if (jsonx::extractPath(json.c_str(), "choices/[0]/delta/content", out)) {
    return 1;
  }
  if (jsonx::extractPath(json.c_str(), "choices/[0]/delta/reasoning_content",
                         out)) {
    return 2;
  }
  return 0;
}

}  // namespace

Reply ask(const std::vector<chat::Msg>& history, const String& userText,
          const Stream& stream) {
  Reply r;

  if (userText.length() == 0) {
    r.error = "пустой текст";
    return r;
  }

  // system + история + текущий вопрос.
  std::vector<llmreq::Msg> msgs;
  msgs.reserve(history.size() + 2);
  msgs.push_back({"system", kLlmSystem});
  for (const chat::Msg& m : history) {
    msgs.push_back({m.fromUser ? "user" : "assistant", m.text});
  }
  msgs.push_back({"user", userText.c_str()});

  const std::string body = llmreq::buildChat(
      kLlmModel, msgs, kLlmMaxTokens, true, kLlmReasoningEffort);
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
           "Accept: text/event-stream\r\n"
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

  // --- чтение потока ---
  // Декодируем chunked целиком и разбираем только новый хвост: ответ
  // небольшой, поэтому O(n^2) на десятках килобайт не имеет значения.
  std::string raw;
  std::string headers;
  size_t bodyOffset = 0;
  bool headerDone = false;
  bool chunked = false;
  int httpCode = 0;
  sse::Parser parser;
  size_t decodedDone = 0;
  std::string decodedBody;
  std::string content;
  size_t activity = 0;
  bool done = false;
  uint32_t idleSince = millis();

  // Первый токен модель может думать несколько секунд — не обрываем раньше.
  constexpr uint32_t kIdleMs = 6000;

  while (!done) {
    if (millis() - t0 > kHttpReadMs) break;

    bool got = false;
    while (c.available() > 0) {
      const int ch = c.read();
      if (ch < 0) break;
      raw += (char)ch;
      got = true;
      idleSince = millis();
      if (raw.size() > kMaxLlmResponse + 4096) break;
    }
    if (raw.size() > kMaxLlmResponse + 4096) break;

    if (!headerDone) {
      const size_t sep = raw.find("\r\n\r\n");
      if (sep != std::string::npos) {
        headers = raw.substr(0, sep);
        bodyOffset = sep + 4;
        chunked = httpr::isChunked(headers);
        httpCode = httpr::parseStatus(headers);
        headerDone = true;
      }
    }

    if (headerDone && raw.size() > bodyOffset) {
      const std::string bodyRaw = raw.substr(bodyOffset);
      decodedBody = chunked ? httpr::dechunk(bodyRaw) : bodyRaw;
      if (decodedBody.size() > decodedDone) {
        parser.feed(decodedBody.data() + decodedDone,
                    decodedBody.size() - decodedDone);
        decodedDone = decodedBody.size();
      }
      std::string ev;
      while (parser.next(ev)) {
        if (ev == "[DONE]") {
          done = true;
          break;
        }
        std::string piece;
        const int kind = deltaOf(ev, piece);
        if (kind == 1 && !piece.empty()) {
          content += piece;
          activity += piece.size();
          if (stream.onDelta) {
            stream.onDelta(piece.c_str(), piece.size(), activity, stream.ctx);
          }
        } else if (kind == 2 && !piece.empty()) {
          activity += piece.size();
          if (stream.onDelta) stream.onDelta("", 0, activity, stream.ctx);
        }
      }
    }

    if (done) break;
    if (!c.connected() && c.available() == 0) break;
    if (!got && raw.size() > 0 && (millis() - idleSince) > kIdleMs) break;
    delay(5);
  }
  c.stop();

  if (!headerDone) {
    r.error = String("не разобран статус: ") +
              String(raw.substr(0, 40).c_str());
    return r;
  }
  r.httpCode = httpCode;
  Serial.printf("[llm] поток %u байт, ответ %u симв., за %u мс\n",
                (unsigned)raw.size(), (unsigned)content.size(), millis() - t0);

  if (r.httpCode != 200) {
    // Тело ошибки — обычный JSON, не SSE.
    Serial.printf("[llm] тело ошибки: %s\n", decodedBody.c_str());
    r.error = String("HTTP ") + r.httpCode + ": " +
              String(decodedBody.substr(0, 200).c_str());
    return r;
  }

  if (content.empty()) {
    // Сервер мог ответить не потоком — пробуем разобрать как обычный JSON.
    std::string out;
    if (jsonx::extractPath(decodedBody.c_str(), "choices/[0]/message/content",
                           out) &&
        !out.empty()) {
      content = out;
    } else {
      std::string err;
      if (jsonx::extractPath(decodedBody.c_str(), "error/message", err)) {
        r.error = String("модель: ") + String(err.substr(0, 200).c_str());
        return r;
      }
      std::string fin;
      jsonx::extractPath(decodedBody.c_str(), "choices/[0]/finish_reason", fin);
      Serial.printf("[llm] пустой content, finish_reason=%s\n",
                    fin.empty() ? "(нет)" : fin.c_str());
      Serial.printf("[llm] тело ответа: %s\n", decodedBody.c_str());
      r.error = "модель вернула пустой ответ";
      return r;
    }
  }

  r.text = String(content.c_str());
  r.ok = true;
  return r;
}

}  // namespace llm
