#include "plugins.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <cstring>
#include <string>

#include "settings.h"
#include "net.h"
#include "ui.h"
#include "pure/http_response.h"

namespace plugins {

namespace {

// Таблица плагинов. Добавил запись — она сразу появляется у модели как
// инструмент. Пин и URL — под свою периферию.
const toolx::Plugin kPlugins[] = {
    {"set_lamp",
     "Включить, выключить или переключить лампу на GPIO26. Вызывай всегда, "
     "когда просят включить, выключить или переключить лампу.",
     "{\"type\":\"object\",\"properties\":{\"state\":{\"type\":\"string\","
     "\"enum\":[\"on\",\"off\",\"toggle\"]}},\"required\":[\"state\"]}",
     toolx::Kind::Gpio, 26, nullptr, false},

    {"show_text",
     "Показать короткий текст на экране стика. Вызывай, когда просят показать "
     "что-то на экране.",
     "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},"
     "\"required\":[\"text\"]}",
     toolx::Kind::Screen, -1, nullptr, false},

    {"http_get",
     "Отправить HTTP GET на домашний адрес (умный дом, вебхук). "
     "query — необязательный параметр строки запроса",
     "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},"
     "\"required\":[]}",
     toolx::Kind::Http, -1, "https://example.com/hook", false},
};

bool g_lampOn = false;

// --- GPIO ---
void runGpio(const toolx::Plugin& p, const std::string& args, Result& r) {
  std::string state;
  if (!toolx::argString(args.c_str(), "state", state)) {
    snprintf(r.detail, sizeof(r.detail), "нужен state");
    return;
  }
  bool on = g_lampOn;
  if (state == "on") on = true;
  else if (state == "off") on = false;
  else if (state == "toggle") on = !g_lampOn;
  else { snprintf(r.detail, sizeof(r.detail), "state=%s?", state.c_str()); return; }

  pinMode(p.pin, OUTPUT);
  digitalWrite(p.pin, on ? HIGH : LOW);
  g_lampOn = on;
  snprintf(r.detail, sizeof(r.detail), "G%d %s", p.pin, on ? "вкл" : "выкл");
  r.ok = true;
}

// --- Экран ---
void runScreen(const std::string& args, Result& r) {
  std::string text;
  if (!toolx::argString(args.c_str(), "text", text)) {
    snprintf(r.detail, sizeof(r.detail), "нужен text");
    return;
  }
  ui::notice(text.c_str());
  snprintf(r.detail, sizeof(r.detail), "показано");
  r.ok = true;
}

// --- HTTP GET ---
bool httpGet(const char* url, char* detail, size_t n) {
  if (strncmp(url, "https://", 8) != 0) {
    snprintf(detail, n, "нужен https");
    return false;
  }
  const char* hostStart = url + 8;
  const char* slash = strchr(hostStart, '/');
  const std::string host(hostStart, slash ? (size_t)(slash - hostStart)
                                          : strlen(hostStart));
  const char* path = slash ? slash : "/";

  WiFiClientSecure& c = net::tlsClient();
  c.setTimeout(kHttpConnectMs);
  if (!c.connect(host.c_str(), 443)) {
    snprintf(detail, n, "нет связи");
    return false;
  }
  c.setTimeout(kHttpReadMs);

  char req[320];
  snprintf(req, sizeof(req),
           "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: aistick/0.1\r\n"
           "Connection: close\r\n\r\n",
           path, host.c_str());
  const uint32_t t0 = millis();
  if (!net::writeAll(c, (const uint8_t*)req, strlen(req), t0)) {
    c.stop();
    snprintf(detail, n, "обрыв отправки");
    return false;
  }
  const String raw = net::readAll(c, t0, 2048);
  c.stop();

  const int code = httpr::parseStatus(raw.c_str());
  if (code < 0) {
    snprintf(detail, n, "нет ответа");
    return false;
  }
  snprintf(detail, n, "HTTP %d", code);
  return code == 200;
}

void runHttp(const toolx::Plugin& p, const std::string& args, Result& r) {
  std::string url = p.url ? p.url : "";
  std::string query;
  if (toolx::argString(args.c_str(), "query", query) && !query.empty()) {
    url += (url.find('?') == std::string::npos) ? "?" : "&";
    url += query;
  }
  r.ok = httpGet(url.c_str(), r.detail, sizeof(r.detail));
}

}  // namespace

const toolx::Plugin* table() { return kPlugins; }
size_t count() { return sizeof(kPlugins) / sizeof(kPlugins[0]); }

const char* toolsJson() {
  static std::string cached;
  if (cached.empty()) cached = toolx::buildTools(kPlugins, count());
  return cached.c_str();
}

Result run(const char* name, const char* argsJson) {
  Result r;
  if (name == nullptr || argsJson == nullptr) {
    snprintf(r.detail, sizeof(r.detail), "пустой вызов");
    return r;
  }
  for (size_t i = 0; i < count(); ++i) {
    const toolx::Plugin& p = kPlugins[i];
    if (strcmp(p.name, name) != 0) continue;

    const std::string args(argsJson);
    r.plugin = p.name;
    r.answerAfter = p.answerAfter;
    switch (p.kind) {
      case toolx::Kind::Gpio: runGpio(p, args, r); break;
      case toolx::Kind::Screen: runScreen(args, r); break;
      case toolx::Kind::Http: runHttp(p, args, r); break;
    }
    Serial.printf("[plug] %s -> %s (%s)\n", name, r.detail,
                  r.ok ? "ок" : "ошибка");
    return r;
  }
  snprintf(r.detail, sizeof(r.detail), "нет плагина");
  Serial.printf("[plug] неизвестный плагин: %s\n", name);
  return r;
}

}  // namespace plugins
