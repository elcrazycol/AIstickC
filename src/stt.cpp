#include "stt.h"

#include <HTTPClient.h>

#include "config.h"
#include "settings.h"
#include "multipart_stream.h"
#include "net.h"
#include "pure/json_extract.h"

namespace stt {

Result transcribe(const uint8_t* wav, size_t wavLen) {
  Result r;

  if (wav == nullptr || wavLen == 0) {
    r.error = "пустой буфер";
    return r;
  }

  MultipartStream body(kSttBoundary, wav, wavLen);
  if (!body.valid()) {
    r.error = "нет памяти под multipart";
    return r;
  }
  const size_t bodyLen = body.totalSize();

  HTTPClient http;
  http.setConnectTimeout(kHttpConnectMs);
  http.setTimeout(kHttpReadMs);
  http.setReuse(false);

  if (!http.begin(net::tlsClient(), kGroqHost, kGroqPort, kGroqPath, true)) {
    r.error = "не открыть соединение";
    return r;
  }

  http.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);
  http.addHeader("Content-Type",
                 String("multipart/form-data; boundary=") + kSttBoundary);

  Serial.printf("[stt] отправляю %u байт тела\n", (unsigned)bodyLen);
  const uint32_t t0 = millis();
  r.httpCode = http.sendRequest("POST", &body, bodyLen);
  Serial.printf("[stt] ответ %d за %u мс\n", r.httpCode, millis() - t0);

  if (r.httpCode <= 0) {
    r.error = String("сеть: ") + http.errorToString(r.httpCode);
    http.end();
    return r;
  }

  String payload = http.getString();
  http.end();

  if (r.httpCode != 200) {
    // Показываем начало тела: там причина отказа.
    r.error = String("HTTP ") + r.httpCode + ": " +
              payload.substring(0, 60);
    return r;
  }

  std::string out;
  if (!jsonx::extractString(payload.c_str(), "text", out)) {
    r.error = String("не разобран ответ: ") + payload.substring(0, 60);
    return r;
  }

  r.text = String(out.c_str());
  r.ok = true;
  return r;
}

}  // namespace stt
