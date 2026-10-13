#include "plugins.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>

#include <cstring>
#include <string>
#include <vector>

#include "settings.h"
#include "net.h"
#include "ui.h"
#include "pure/http_response.h"
#include "pure/plugin_file.h"

namespace plugins {

namespace {

constexpr const char* kPluginsPath = "/plugins.txt";

// Определения (владеют строками) и таблица для модели (указывает на них).
std::vector<plugfile::Def> g_defs;
std::vector<toolx::Plugin> g_table;
std::string g_tools;
bool g_lampOn = false;

toolx::Kind kindOf(const std::string& k) {
  if (k == "screen") return toolx::Kind::Screen;
  if (k == "http") return toolx::Kind::Http;
  return toolx::Kind::Gpio;
}

// Встроенный набор: используется, если файла нет (первый запуск или сбой ФС).
void addBuiltins(std::vector<plugfile::Def>& d) {
  plugfile::Def a;
  a.name = "set_lamp";
  a.description =
      "Включить, выключить или переключить лампу на GPIO26. Вызывай всегда, "
      "когда просят включить, выключить или переключить лампу.";
  a.kind = "gpio";
  a.pin = 26;
  d.push_back(a);

  plugfile::Def b;
  b.name = "show_text";
  b.description =
      "Показать короткий текст на экране стика. Вызывай, когда просят "
      "показать что-то на экране.";
  b.kind = "screen";
  d.push_back(b);

  plugfile::Def c;
  c.name = "http_get";
  c.description =
      "Отправить HTTP GET на домашний адрес (умный дом, вебхук). query — "
      "необязательный параметр строки запроса";
  c.kind = "http";
  c.url = "https://example.com/hook";
  d.push_back(c);
}

// Пересобирает таблицу и JSON инструментов из определений.
void rebuild() {
  g_table.clear();
  g_table.reserve(g_defs.size());
  for (const plugfile::Def& d : g_defs) {
    toolx::Plugin p{};
    p.name = d.name.c_str();
    p.description = d.description.c_str();
    p.parameters = plugfile::schemaFor(d.kind.c_str());
    p.kind = kindOf(d.kind);
    p.pin = d.pin;
    p.url = d.url.empty() ? nullptr : d.url.c_str();
    p.answerAfter = d.answerAfter;
    g_table.push_back(p);
  }
  g_tools = toolx::buildTools(g_table.data(), g_table.size());
}

// Читает плагины из LittleFS; при неудаче берёт встроенный набор.
void load() {
  g_defs.clear();

  if (LittleFS.begin(false)) {
    File f = LittleFS.open(kPluginsPath, "r");
    if (f) {
      const String text = f.readString();
      f.close();
      plugfile::parse(text.c_str(), g_defs);
      Serial.printf("[plug] %s: принято %u\n", kPluginsPath,
                    (unsigned)g_defs.size());
    } else {
      Serial.printf("[plug] файла %s нет\n", kPluginsPath);
    }
  } else {
    Serial.println("[plug] LittleFS не смонтировалась");
  }

  if (g_defs.empty()) {
    Serial.println("[plug] беру встроенный набор");
    addBuiltins(g_defs);
  }
  rebuild();
  Serial.printf("[plug] плагинов в таблице: %u\n", (unsigned)g_table.size());
}

// --- действия ---

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

void begin() { load(); }

const toolx::Plugin* table() { return g_table.data(); }
size_t count() { return g_table.size(); }
const char* toolsJson() { return g_tools.c_str(); }

Result run(const char* name, const char* argsJson) {
  Result r;
  if (name == nullptr || argsJson == nullptr) {
    snprintf(r.detail, sizeof(r.detail), "пустой вызов");
    return r;
  }
  for (const toolx::Plugin& p : g_table) {
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
