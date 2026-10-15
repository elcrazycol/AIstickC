#include "plugins.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <map>
#include <string>
#include <vector>

#include "settings.h"
#include "net.h"
#include "script_host.h"
#include "pure/json_extract.h"
#include "pure/plugin_meta.h"
#include "pure/script.h"

namespace plugins {

namespace {

constexpr const char* kDir = "/plugins";
constexpr size_t kMaxSteps = 20000;  // защита от вечного цикла в скрипте

struct Script {
  std::string name;
  std::string description;
  std::string label;  // что писать на экране, пока плагин работает
  std::string schema;
  std::string source;
  std::vector<std::string> params;
};

std::vector<Script> g_scripts;
std::vector<toolx::Plugin> g_table;
std::string g_tools;

// Имена параметров из строки шапки "channel=1|2, state*=on|off".
std::vector<std::string> paramNames(const std::string& params) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    std::string s = cur;
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
    s.erase(0, b);
    const size_t eq = s.find('=');
    if (eq != std::string::npos) s = s.substr(0, eq);
    if (!s.empty() && s.back() == '*') s.pop_back();
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (!s.empty()) out.push_back(s);
    cur.clear();
  };
  for (char c : params) {
    if (c == ',') { flush(); continue; }
    cur.push_back(c);
  }
  flush();
  return out;
}

void addScript(const std::string& src) {
  plugmeta::Meta m;
  if (!plugmeta::parse(src.c_str(), m)) {
    Serial.println("[plug] скрипт без шапки (name) пропущен");
    return;
  }
  Script s;
  s.name = m.name;
  s.description = m.description;
  s.label = m.label;
  s.schema = m.schema;
  s.source = src;
  s.params = paramNames(m.params);
  g_scripts.push_back(s);
}

void load() {
  g_scripts.clear();

  if (LittleFS.begin(false)) {
    File dir = LittleFS.open(kDir);
    if (dir && dir.isDirectory()) {
      File f = dir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          const String name = f.name();
          // В каталоге лежит ещё versions.txt — берём только скрипты.
          if (name.endsWith(".ais")) {
            const String content = f.readString();
            addScript(std::string(content.c_str()));
          }
        }
        f = dir.openNextFile();
      }
      dir.close();
    } else {
      Serial.println("[plug] нет каталога /plugins");
    }
  } else {
    Serial.println("[plug] LittleFS не смонтировалась");
  }

  // Таблица инструментов для модели — из шапок скриптов.
  g_table.clear();
  g_table.reserve(g_scripts.size());
  for (const Script& s : g_scripts) {
    toolx::Plugin p{};
    p.name = s.name.c_str();
    p.description = s.description.c_str();
    p.parameters = s.schema.c_str();
    // Поля старой схемы больше не используются: логика в скрипте.
    p.kind = toolx::Kind::Gpio;
    p.pin = -1;
    p.url = nullptr;
    p.answerAfter = false;
    g_table.push_back(p);
  }
  g_tools = toolx::buildTools(g_table.data(), g_table.size());
  Serial.printf("[plug] скриптов: %u\n", (unsigned)g_scripts.size());
  for (const Script& sc : g_scripts) {
    Serial.printf("[plug]   %s — «%s»\n", sc.name.c_str(),
                  labelOf(sc.name.c_str()));
  }
}

const Script* find(const std::string& name) {
  for (const Script& s : g_scripts) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

// Скрипт исполняется в отдельной задаче с большим стеком: TLS-рукопожатие
// внутри интерпретатора требует куда больше стека, чем есть у loopTask,
// и без этого устройство падало (Stack canary).
struct Job {
  const Script* sc = nullptr;
  std::map<std::string, script::Value> vars;
  scripthost::Host host;
  std::string err;
  volatile bool done = false;
};

void scriptTask(void* arg) {
  Job* j = (Job*)arg;
  script::Interp interp(j->host, j->vars, kMaxSteps);
  interp.run(j->sc->source.c_str(), j->err);
  j->done = true;
  vTaskDelete(nullptr);
}

// Общий запуск: заполняет переменные и исполняет скрипт.
Result runScript(const Script& sc, const std::map<std::string, script::Value>& in) {
  Result r;
  r.plugin = sc.name;

  Job* j = new Job();
  if (j == nullptr) {
    r.text = "нет памяти под плагин";
    return r;
  }
  j->sc = &sc;
  j->vars = in;

  // Стек задачи — двадцать килобайт непрерывной памяти, и её может не
  // оказаться: сессии модели и распознавания держат по сорок. Плагину они не
  // нужны — если он пойдёт в сеть, соединение поднимется заново. Поэтому при
  // неудаче отпускаем обе и пробуем ещё раз.
  TaskHandle_t task = nullptr;
  xTaskCreatePinnedToCore(scriptTask, "plug", 20480, j, 1, &task, 1);
  if (task == nullptr) {
    Serial.printf("[plug] нет памяти под задачу (блок %u) — отпускаю сессии\n",
                  (unsigned)ESP.getMaxAllocHeap());
    net::llmClient().stop();
    net::proxyClient().stop();
    delay(50);
    xTaskCreatePinnedToCore(scriptTask, "plug", 20480, j, 1, &task, 1);
  }
  if (task == nullptr) {
    delete j;
    r.text = "не запустить плагин";
    Serial.printf("[plug] и после этого не хватило (блок %u)\n",
                  (unsigned)ESP.getMaxAllocHeap());
    return r;
  }

  const uint32_t t0 = millis();
  while (!j->done && (millis() - t0) < kPluginMs) delay(10);
  const uint32_t took = millis() - t0;
  if (!j->done) {
    vTaskDelete(task);
    delete j;
    Serial.printf("[plug] %s: таймаут\n", sc.name.c_str());
    r.text = "плагин " + sc.name + ": таймаут";
    return r;
  }

  if (!j->err.empty()) {
    Serial.printf("[plug] %s: ошибка скрипта: %s\n", sc.name.c_str(),
                  j->err.c_str());
    r.text = "плагин " + sc.name + ": " + j->err;
    delete j;
    return r;
  }
  if (j->host.asked()) {
    r.ask = j->host.result().empty() ? std::string("уточните") : j->host.result();
    r.ok = true;
    delete j;
    return r;
  }
  r.text = j->host.result().empty() ? std::string("выполнено") : j->host.result();
  r.asAnswer = j->host.asAnswer();
  r.answerAfter = j->host.summarize();
  r.note = j->host.note();
  r.ok = true;
  // Результат поиска бывает на килобайты — в лог столько лить незачем.
  const char* tail = r.text.size() > 120 ? "…" : "";
  Serial.printf("[plug] %s за %u мс -> %.120s%s\n", sc.name.c_str(),
                (unsigned)took, r.text.c_str(), tail);
  delete j;
  return r;
}

}  // namespace

void begin() { load(); }

const toolx::Plugin* table() { return g_table.data(); }
size_t count() { return g_table.size(); }
const char* toolsJson() { return g_tools.c_str(); }

Result run(const char* name, const char* argsJson) {
  Result r;
  if (name == nullptr) { r.text = "нет имени плагина"; return r; }
  const Script* sc = find(name);
  if (sc == nullptr) {
    r.text = "нет плагина";
    Serial.printf("[plug] неизвестный плагин: %s\n", name);
    return r;
  }

  // Аргументы модели -> переменные скрипта. Параметры, которые модель не
  // передала, объявляем пустой строкой — скрипт должен их видеть.
  std::map<std::string, script::Value> vars;
  const std::string args(argsJson ? argsJson : "{}");
  for (const std::string& p : sc->params) {
    std::string v;
    if (jsonx::extractPath(args.c_str(), p.c_str(), v)) {
      vars[p] = script::Value::ofStr(v);
    } else {
      vars[p] = script::Value::ofStr("");
    }
  }
  return runScript(*sc, vars);
}

Result runInput(const char* name, const char* userText) {
  Result r;
  if (name == nullptr) { r.text = "нет имени плагина"; return r; }
  const Script* sc = find(name);
  if (sc == nullptr) { r.text = "нет плагина"; return r; }

  const std::string text(userText ? userText : "");
  std::map<std::string, script::Value> vars;
  vars["input"] = script::Value::ofStr(text);
  if (!sc->params.empty()) vars[sc->params[0]] = script::Value::ofStr(text);
  return runScript(*sc, vars);
}

const char* labelOf(const char* name) {
  if (name == nullptr) return "";
  const Script* sc = find(name);
  if (sc == nullptr) return "";
  return sc->label.empty() ? sc->name.c_str() : sc->label.c_str();
}

size_t names(std::vector<std::string>& out) {
  out.clear();
  for (const Script& s : g_scripts) out.push_back(s.name);
  return out.size();
}

}  // namespace plugins
