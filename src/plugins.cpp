#include "plugins.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>

#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "ui.h"
#include "pure/http_response.h"
#include "pure/plugin_file.h"
#include "pure/weather_text.h"

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
  if (k == "weather") return toolx::Kind::Weather;
  if (k == "tuya") return toolx::Kind::Tuya;
  return toolx::Kind::Gpio;
}

// Встроенный набор: используется, если файла нет (первый запуск или сбой ФС).
void addBuiltins(std::vector<plugfile::Def>& d) {
  auto add = [&](const char* name, const char* desc, const char* kind,
                 int pin, const char* url) {
    plugfile::Def x;
    x.name = name;
    x.description = desc;
    x.kind = kind;
    x.pin = pin;
    if (url) x.url = url;
    d.push_back(x);
  };
  add("set_lamp",
      "Включить, выключить или переключить лампу на GPIO26. Вызывай всегда, "
      "когда просят включить, выключить или переключить лампу.",
      "gpio", 26, nullptr);
  add("show_text",
      "Показать короткий текст на экране стика. Вызывай, когда просят "
      "показать что-то на экране.", "screen", -1, nullptr);
  add("weather",
      "Узнать погоду на сегодня. Вызывай, когда спрашивают про погоду.",
      "weather", -1, nullptr);
  add("switch",
      "Включить, выключить или переключить канал выключателя (1, 2 или 3). "
      "Вызывай всегда, когда просят включить, выключить или переключить "
      "свет или выключатель.",
      "tuya", -1, nullptr);
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

// --- сеть ---

// HTTPS GET. Возвращает код ответа (-1 — не дошло), тело кладёт в body.
int httpsGet(const char* host, const char* path, String& body, size_t maxLen) {
  body = "";
  WiFiClientSecure& c = net::tlsClient();
  c.setTimeout(kHttpConnectMs);
  if (!c.connect(host, 443)) return -1;
  c.setTimeout(kHttpReadMs);

  char req[512];
  snprintf(req, sizeof(req),
           "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: aistick/0.1\r\n"
           "Connection: close\r\n\r\n",
           path, host);
  const uint32_t t0 = millis();
  if (!net::writeAll(c, (const uint8_t*)req, strlen(req), t0)) {
    c.stop();
    return -1;
  }
  const String raw = net::readAll(c, t0, maxLen);
  c.stop();

  const int code = httpr::parseStatus(raw.c_str());
  const int sep = raw.indexOf("\r\n\r\n");
  if (sep >= 0) body = raw.substring(sep + 4);
  return code;
}

// --- действия ---

// POST на свой прокси с общим секретом. Возвращает код ответа, тело в resp.
int proxyPost(const char* path, const char* body, String& resp, size_t maxLen) {
  resp = "";
  WiFiClientSecure& c = net::tlsClient();
  c.setTimeout(kHttpConnectMs);
  if (!c.connect(PROXY_HOST, PROXY_PORT)) return -1;
  c.setTimeout(kHttpReadMs);

  char head[400];
  snprintf(head, sizeof(head),
           "POST %s HTTP/1.1\r\nHost: %s\r\nX-Auth: %s\r\n"
           "Content-Type: application/json\r\nContent-Length: %u\r\n"
           "Connection: close\r\n\r\n",
           path, PROXY_HOST, PROXY_TOKEN, (unsigned)strlen(body));
  const uint32_t t0 = millis();
  if (!net::writeAll(c, (const uint8_t*)head, strlen(head), t0) ||
      !net::writeAll(c, (const uint8_t*)body, strlen(body), t0)) {
    c.stop();
    return -1;
  }
  const String raw = net::readAll(c, t0, maxLen);
  c.stop();

  const int code = httpr::parseStatus(raw.c_str());
  const int sep = raw.indexOf("\r\n\r\n");
  if (sep >= 0) resp = raw.substring(sep + 4);
  return code;
}

// Tuya-выключатель: команда уходит на прокси, он ходит в облако Tuya.
void runTuya(const std::string& args, Result& r) {
  std::string state;
  if (!toolx::argString(args.c_str(), "state", state)) {
    r.text = "нужен state";
    return;
  }
  int channel = 1;
  std::string ch;
  if (toolx::argString(args.c_str(), "channel", ch) && !ch.empty()) {
    channel = atoi(ch.c_str());
  }
  if (channel < 1 || channel > 8) {
    r.text = "канал 1..8";
    return;
  }

  char body[96];
  snprintf(body, sizeof(body), "{\"channel\":%d,\"state\":\"%s\"}", channel,
           state.c_str());

  String resp;
  const int code = proxyPost("/tuya", body, resp, 512);
  if (code < 0) {
    r.text = "нет связи с прокси";
    return;
  }
  if (code != 200) {
    r.text = "прокси: HTTP " + std::to_string(code);
    return;
  }
  std::string st;
  if (toolx::argString(resp.c_str(), "state", st)) {
    char buf[40];
    snprintf(buf, sizeof(buf), "канал %d %s", channel,
             st == "on" ? "вкл" : "выкл");
    r.text = buf;
  } else {
    r.text = "выполнено";
  }
  r.ok = true;
}

void runGpio(const toolx::Plugin& p, const std::string& args, Result& r) {
  std::string state;
  if (!toolx::argString(args.c_str(), "state", state)) {
    r.text = "нужен state";
    return;
  }
  bool on = g_lampOn;
  if (state == "on") on = true;
  else if (state == "off") on = false;
  else if (state == "toggle") on = !g_lampOn;
  else { r.text = "state=" + state + "?"; return; }

  pinMode(p.pin, OUTPUT);
  digitalWrite(p.pin, on ? HIGH : LOW);
  g_lampOn = on;
  char buf[32];
  snprintf(buf, sizeof(buf), "G%d %s", p.pin, on ? "вкл" : "выкл");
  r.text = buf;
  r.ok = true;
}

void runScreen(const std::string& args, Result& r) {
  std::string text;
  if (!toolx::argString(args.c_str(), "text", text)) {
    r.text = "нужен text";
    return;
  }
  ui::notice(text.c_str());
  r.text = "показано";
  r.ok = true;
}

// Процентное кодирование города (кириллица) для пути URL.
std::string urlEncodeCity(const char* city) {
  std::string enc;
  for (const unsigned char* p = (const unsigned char*)city; *p != 0; ++p) {
    const unsigned char c = *p;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.') {
      enc.push_back((char)c);
    } else if (c == ' ') {
      enc += "%20";
    } else {
      char b[8];
      snprintf(b, sizeof(b), "%%%02X", c);
      enc += b;
    }
  }
  return enc;
}

// Погода на сегодня. Город пустой — просим его назвать.
void runWeather(const std::string& rawCity, Result& r) {
  const std::string city = weather::cleanCity(rawCity.c_str());
  if (city.empty()) {
    r.ask = "Какой ваш город?";
    r.ok = true;
    return;
  }
  const std::string path =
      "/" + urlEncodeCity(city.c_str()) + "?format=%l|%C|%t|%f|%w|%h";

  String body;
  const int code = httpsGet("wttr.in", path.c_str(), body, 1024);
  if (code != 200) {
    r.text = (code < 0) ? "нет связи с погодой" : "погода: HTTP " + std::to_string(code);
    return;
  }
  if (!weather::compose(body.c_str(), r.text)) {
    r.text = "не разобрал погоду";
    return;
  }
  r.ok = true;
  r.asAnswer = true;
}

void runHttp(const toolx::Plugin& p, const std::string& args, Result& r) {
  std::string url = p.url ? p.url : "";
  std::string query;
  if (toolx::argString(args.c_str(), "query", query) && !query.empty()) {
    url += (url.find('?') == std::string::npos) ? "?" : "&";
    url += query;
  }
  if (strncmp(url.c_str(), "https://", 8) != 0) {
    r.text = "нужен https";
    return;
  }
  const char* hostStart = url.c_str() + 8;
  const char* slash = strchr(hostStart, '/');
  const std::string host(hostStart, slash ? (size_t)(slash - hostStart)
                                          : strlen(hostStart));
  const char* path = slash ? slash : "/";

  String body;
  const int code = httpsGet(host.c_str(), path, body, 2048);
  if (code < 0) {
    r.text = "нет связи";
    return;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "HTTP %d", code);
  r.text = buf;
  r.ok = (code == 200);
}

}  // namespace

void begin() { load(); }

const toolx::Plugin* table() { return g_table.data(); }
size_t count() { return g_table.size(); }
const char* toolsJson() { return g_tools.c_str(); }

namespace {

// Ищет плагин по имени.
const toolx::Plugin* find(const char* name) {
  if (name == nullptr) return nullptr;
  for (const toolx::Plugin& p : g_table) {
    if (strcmp(p.name, name) == 0) return &p;
  }
  return nullptr;
}

}  // namespace

Result run(const char* name, const char* argsJson) {
  Result r;
  const toolx::Plugin* p = find(name);
  if (p == nullptr) {
    r.text = "нет плагина";
    Serial.printf("[plug] неизвестный плагин: %s\n", name ? name : "(null)");
    return r;
  }
  r.plugin = p->name;
  r.answerAfter = p->answerAfter;

  const std::string args(argsJson ? argsJson : "{}");
  switch (p->kind) {
    case toolx::Kind::Gpio: runGpio(*p, args, r); break;
    case toolx::Kind::Screen: runScreen(args, r); break;
    case toolx::Kind::Http: runHttp(*p, args, r); break;
    case toolx::Kind::Weather: {
      std::string city;
      toolx::argString(args.c_str(), "city", city);
      runWeather(city, r);
      break;
    }
    case toolx::Kind::Tuya: runTuya(args, r); break;
  }
  Serial.printf("[plug] %s -> %s\n", p->name,
                r.ask ? r.ask : r.text.c_str());
  return r;
}

Result runInput(const char* name, const char* userText) {
  Result r;
  const toolx::Plugin* p = find(name);
  if (p == nullptr) {
    r.text = "нет плагина";
    return r;
  }
  r.plugin = p->name;
  r.answerAfter = p->answerAfter;
  const std::string text(userText ? userText : "");

  switch (p->kind) {
    case toolx::Kind::Weather: runWeather(text, r); break;
    default: r.text = "плагин не ждёт ответа"; break;
  }
  Serial.printf("[plug] %s <- \"%s\" -> %s\n", p->name, text.c_str(),
                r.text.c_str());
  return r;
}

}  // namespace plugins
