// Ответ LLM через OpenCode Go, потоком (SSE).
//
// Стик -> opencode.ai (HTTPS) -> провайдер модели. Прокси не нужен:
// OpenCode доступен из России напрямую, а ключ подписки лежит в config.h.
//
// Запрос идёт со stream:true, ответ приходит как text/event-stream. Кусок
// текста отдаётся наружу через Stream::onDelta, поэтому ответ появляется на
// экране по мере генерации.
//
// Chunked-тело декодируется инкрементально (chunked::Decoder): сырьё
// выбрасывается по мере разбора, память не растёт с длиной потока. Раньше
// тело копилось целиком и декодировалось заново — поток обрезался.

#include "llm.h"

#include <WiFiClientSecure.h>
#include <esp_system.h>

#include <cstring>
#include <string>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "pure/chunked.h"
#include "pure/http_response.h"
#include "pure/json_extract.h"
#include "pure/llm_request.h"
#include "pure/sse.h"
#include "pure/tool_call.h"

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
          const Stream& stream, const Options& opts) {
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

  // Обычный запрос или продолжение после вызова инструмента.
  const bool followUp =
      opts.toolResult != nullptr && opts.toolCallId != nullptr;
  const std::string body =
      followUp ? llmreq::buildToolFollowUp(
                     kLlmModel, msgs, opts.toolCallId,
                     opts.toolName ? opts.toolName : "",
                     opts.toolArgs ? opts.toolArgs : "", opts.toolResult,
                     kLlmMaxTokens, true, kLlmReasoningEffort)
               : llmreq::buildChat(kLlmModel, msgs, kLlmMaxTokens, true,
                                   kLlmReasoningEffort, opts.toolsJson);
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
  std::string headBuf;       // копится, пока не найден конец заголовков
  bool headerDone = false;
  bool chunked = false;
  int httpCode = 0;
  chunked::Decoder dec;
  sse::Parser parser;
  std::string content;       // накопленный ответ
  std::string toolId;        // id вызова инструмента (из первой дельты)
  std::string toolName;      // имя плагина
  std::string toolArgs;      // аргументы вызова (склеиваются посимвольно)
  std::string plainBody;     // тело ошибки или не-chunked фолбэк
  size_t activity = 0;
  bool done = false;
  uint32_t idleSince = millis();

  constexpr uint32_t kIdleMs = 6000;      // первый токен модель думает долго
  constexpr size_t kMaxPlain = 4096;      // тело ошибки/фолбэка

  // Разбирает уже накопленные SSE-события.
  auto drain = [&]() {
    std::string ev;
    while (parser.next(ev)) {
      if (ev == "[DONE]") {
        done = true;
        return;
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
      } else {
        // Модель вызвала инструмент: id и имя приходят в первом куске,
        // аргументы — посимвольно, их надо склеить.
        std::string t;
        if (toolId.empty() && toolx::deltaId(ev.c_str(), t)) toolId = t;
        if (toolx::deltaName(ev.c_str(), t)) toolName = t;
        if (toolx::deltaArgs(ev.c_str(), t)) toolArgs += t;
      }
    }
  };

  // Тело: декодируем chunked по мере прихода и отдаём парсеру.
  auto feedBody = [&](const char* p, size_t n) {
    if (chunked) {
      dec.feed(p, n);
      std::string piece;
      while (dec.next(piece)) {
        parser.feed(piece);
        if (httpCode != 200 && plainBody.size() < kMaxPlain) {
          plainBody += piece;
        }
      }
    } else {
      parser.feed(p, n);
      if (plainBody.size() < kMaxPlain) plainBody.append(p, n);
    }
    drain();
  };

  auto process = [&](const char* p, size_t n) {
    if (headerDone) {
      feedBody(p, n);
      return;
    }
    headBuf.append(p, n);
    const size_t sep = headBuf.find("\r\n\r\n");
    if (sep == std::string::npos) return;
    const std::string headers = headBuf.substr(0, sep);
    chunked = httpr::isChunked(headers);
    httpCode = httpr::parseStatus(headers);
    headerDone = true;
    const std::string rest = headBuf.substr(sep + 4);
    headBuf.clear();
    if (!rest.empty()) feedBody(rest.data(), rest.size());
  };

  while (!done) {
    if (millis() - t0 > kHttpReadMs) break;

    const int avail = c.available();
    if (avail > 0) {
      uint8_t tmp[256];
      const int want = avail < (int)sizeof(tmp) ? avail : (int)sizeof(tmp);
      const int n = c.read(tmp, want);
      if (n > 0) {
        idleSince = millis();
        process((const char*)tmp, (size_t)n);
        if (content.size() > kMaxAnswerChars) break;
        continue;  // возможно, в сокете есть ещё
      }
    }

    if (!c.connected() && c.available() == 0) break;
    if (headerDone && (millis() - idleSince) > kIdleMs) break;
    delay(5);
  }
  c.stop();

  if (!headerDone) {
    r.error = String("не разобран статус: ") +
              String(headBuf.substr(0, 40).c_str());
    return r;
  }
  r.httpCode = httpCode;
  Serial.printf("[llm] ответ %u симв. за %u мс\n", (unsigned)content.size(),
                millis() - t0);

  if (r.httpCode != 200) {
    Serial.printf("[llm] тело ошибки: %s\n", plainBody.c_str());
    r.error = String("HTTP ") + r.httpCode + ": " +
              String(plainBody.substr(0, 200).c_str());
    return r;
  }

  // Модель вызвала инструмент вместо обычного ответа.
  if (!toolName.empty()) {
    r.toolCall = true;
    r.toolId = String(toolId.c_str());
    r.toolName = String(toolName.c_str());
    r.toolArgs = String(toolArgs.c_str());
    r.ok = true;
    Serial.printf("[llm] вызов плагина: %s(%s)\n", toolName.c_str(),
                  toolArgs.c_str());
    return r;
  }

  if (content.empty()) {
    // Сервер мог ответить не потоком — пробуем разобрать как обычный JSON.
    std::string out;
    if (jsonx::extractPath(plainBody.c_str(), "choices/[0]/message/content",
                           out) &&
        !out.empty()) {
      content = out;
    } else {
      std::string err;
      if (jsonx::extractPath(plainBody.c_str(), "error/message", err)) {
        r.error = String("модель: ") + String(err.substr(0, 200).c_str());
        return r;
      }
      std::string fin;
      jsonx::extractPath(plainBody.c_str(), "choices/[0]/finish_reason", fin);
      Serial.printf("[llm] пустой content, finish_reason=%s\n",
                    fin.empty() ? "(нет)" : fin.c_str());
      r.error = "модель вернула пустой ответ";
      return r;
    }
  }

  r.text = String(content.c_str());
  r.ok = true;
  return r;
}

}  // namespace llm
