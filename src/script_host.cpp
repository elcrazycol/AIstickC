#include "script_host.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <cstdlib>
#include <cstring>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "ui.h"
#include "pure/http_response.h"
#include "pure/json_extract.h"
#include "pure/weather_text.h"
#include "webfetch.h"

namespace scripthost {

namespace {

std::string argStr(const std::vector<script::Value>& a, size_t i) {
  return i < a.size() ? a[i].toStr() : std::string();
}
double argNum(const std::vector<script::Value>& a, size_t i) {
  return i < a.size() ? a[i].num : 0;
}

// Длина строки в символах UTF-8 (не в байтах): для русского это привычнее.
size_t utf8Len(const std::string& s) {
  size_t n = 0;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = (unsigned char)s[i];
    size_t step = 1;
    if ((c & 0xE0) == 0xC0) step = 2;
    else if ((c & 0xF0) == 0xE0) step = 3;
    else if ((c & 0xF8) == 0xF0) step = 4;
    if (i + step > s.size()) step = 1;
    i += step;
    ++n;
  }
  return n;
}

bool pinAllowed(int pin) {
  for (size_t i = 0; i < kPluginPinsCount; ++i) {
    if (kPluginPins[i] == pin) return true;
  }
  return false;
}

// Кодирование пути: кириллица и пробелы -> %XX, остальное как есть.

}  // namespace

bool Host::call(const std::string& name,
                const std::vector<script::Value>& args, script::Value& out,
                std::string& err) {
  // --- вывод ---
  if (name == "log") {
    Serial.printf("[plug] %s\n", argStr(args, 0).c_str());
    out = script::Value::ofNum(0);
    return true;
  }
  if (name == "show") {
    result_ = argStr(args, 0);
    asAnswer_ = false;
    out = script::Value::ofNum(0);
    return true;
  }
  if (name == "answer") {
    result_ = argStr(args, 0);
    asAnswer_ = true;
    out = script::Value::ofNum(0);
    return true;
  }
  if (name == "summarize") {
    // Материал не показываем как есть (это может быть простыня со страниц),
    // а отдаём модели: она сделает из него внятный ответ.
    result_ = argStr(args, 0);
    asAnswer_ = false;
    summarize_ = true;
    out = script::Value::ofNum(0);
    return true;
  }
  if (name == "ask") {
    // Плагин чего-то не знает (например, города): спросим пользователя,
    // ответ придёт следующим запуском скрипта.
    result_ = argStr(args, 0);
    asAnswer_ = false;
    asked_ = true;
    out = script::Value::ofNum(0);
    return true;
  }

  // --- строки и числа ---
  if (name == "len") {
    out = script::Value::ofNum((double)utf8Len(argStr(args, 0)));
    return true;
  }
  if (name == "str") {
    out = script::Value::ofStr(argStr(args, 0));
    return true;
  }
  if (name == "num") {
    out = script::Value::ofNum(std::atof(argStr(args, 0).c_str()));
    return true;
  }
  if (name == "contains") {
    out = script::Value::ofBool(argStr(args, 0).find(argStr(args, 1)) !=
                                std::string::npos);
    return true;
  }
  if (name == "json") {
    std::string v;
    if (!jsonx::extractPath(argStr(args, 0).c_str(), argStr(args, 1).c_str(), v)) {
      err = "поле не найдено";
      return false;
    }
    out = script::Value::ofStr(v);
    return true;
  }

  // --- железо ---
  if (name == "gpio") {
    const int pin = (int)argNum(args, 0);
    if (!pinAllowed(pin)) {
      err = "пин " + std::to_string(pin) + " не разрешён";
      return false;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, args.size() > 1 && args[1].truthy() ? HIGH : LOW);
    out = script::Value::ofNum(0);
    return true;
  }
  if (name == "gpio_get") {
    const int pin = (int)argNum(args, 0);
    if (!pinAllowed(pin)) {
      err = "пин " + std::to_string(pin) + " не разрешён";
      return false;
    }
    out = script::Value::ofNum(digitalRead(pin) == HIGH ? 1 : 0);
    return true;
  }
  if (name == "sleep") {
    int ms = (int)argNum(args, 0);
    if (ms < 0) ms = 0;
    if (ms > (int)kPluginSleepMaxMs) ms = (int)kPluginSleepMaxMs;
    delay(ms);
    out = script::Value::ofNum(0);
    return true;
  }

  // --- сеть ---
  if (name == "http") {
    std::string resp;
    if (!webfetch::request(argStr(args, 0), argStr(args, 1), argStr(args, 2), "",
                           resp, err)) {
      return false;
    }
    out = script::Value::ofStr(resp);
    return true;
  }
  if (name == "proxy_post") {
    const std::string url = std::string("https://") + PROXY_HOST + ":" +
                            std::to_string(PROXY_PORT) + argStr(args, 0);
    std::string resp;
    if (!webfetch::request("POST", url, argStr(args, 1),
                           std::string("X-Auth: ") + PROXY_TOKEN, resp, err)) {
      return false;
    }
    out = script::Value::ofStr(resp);
    return true;
  }

  // --- предметная область ---
  if (name == "weather_text") {
    std::string ru;
    if (!weather::compose(argStr(args, 0).c_str(), ru)) {
      err = "не разобрал погоду";
      return false;
    }
    out = script::Value::ofStr(ru);
    return true;
  }

  err = "нет функции " + name;
  return false;
}

}  // namespace scripthost
