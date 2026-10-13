// Разбор файла плагинов (data/plugins.txt).
//
// Формат — блоки, разделённые пустой строкой; строки "ключ = значение";
// всё после # — комментарий. Модуль чистый: текст приходит снаружи, файловая
// система и железо тут ни при чём, поэтому парсер проверяется на Mac.
//
// Схема аргументов не пишется в файле, а выводится из вида действия: так
// в файле не надо держать длинный JSON, а модель всё равно получает схему.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace plugfile {

struct Def {
  std::string name;
  std::string description;
  std::string kind;     // gpio | screen | http
  int pin = -1;         // для gpio
  std::string url;      // для http
  bool answerAfter = false;
};

// Разбирает текст в список плагинов. Блок без name или с неизвестным kind
// отбрасывается. Возвращает число принятых записей.
inline size_t parse(const char* text, std::vector<Def>& out) {
  out.clear();
  if (text == nullptr) return 0;

  Def cur;
  bool hasCur = false;
  size_t accepted = 0;

  auto kindOk = [](const std::string& k) {
    return k == "gpio" || k == "screen" || k == "http" || k == "weather" ||
           k == "tuya";
  };

  auto flush = [&]() {
    if (hasCur && !cur.name.empty() && kindOk(cur.kind)) {
      out.push_back(cur);
      ++accepted;
    }
    cur = Def{};
    hasCur = false;
  };

  size_t i = 0;
  while (text[i] != 0) {
    // Строка целиком.
    size_t end = i;
    while (text[end] != 0 && text[end] != '\n') ++end;
    std::string line(text + i, end - i);
    i = (text[end] == 0) ? end : end + 1;

    // Комментарий отрезаем, хвостовые \r и пробелы убираем.
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                             line.back() == '\r')) {
      line.pop_back();
    }

    if (line.empty()) {  // пустая строка — конец блока
      flush();
      continue;
    }

    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;  // строка без '=' — пропускаем

    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
    size_t v = 0;
    while (v < val.size() && (val[v] == ' ' || val[v] == '\t')) ++v;
    val.erase(0, v);

    hasCur = true;
    if (key == "name") cur.name = val;
    else if (key == "description") cur.description = val;
    else if (key == "kind") cur.kind = val;
    else if (key == "url") cur.url = val;
    else if (key == "pin") cur.pin = atoi(val.c_str());
    else if (key == "answer") cur.answerAfter = (val == "true" || val == "1");
  }
  flush();  // последний блок без завершающей пустой строки
  return accepted;
}

// JSON-схема аргументов по виду действия.
inline const char* schemaFor(const char* kind) {
  if (kind != nullptr && std::string(kind) == "screen") {
    return "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},"
           "\"required\":[\"text\"]}";
  }
  if (kind != nullptr && std::string(kind) == "http") {
    return "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},"
           "\"required\":[]}";
  }
  if (kind != nullptr && std::string(kind) == "weather") {
    return "{\"type\":\"object\",\"properties\":{\"city\":{\"type\":\"string\"}},"
           "\"required\":[]}";
  }
  if (kind != nullptr && std::string(kind) == "tuya") {
    return "{\"type\":\"object\",\"properties\":{\"channel\":{\"type\":"
           "\"string\",\"enum\":[\"1\",\"2\",\"3\",\"all\"]},\"state\":{\"type\":"
           "\"string\",\"enum\":[\"on\",\"off\",\"toggle\"]}},"
           "\"required\":[\"state\"]}";
  }
  return "{\"type\":\"object\",\"properties\":{\"state\":{\"type\":\"string\","
         "\"enum\":[\"on\",\"off\",\"toggle\"]}},\"required\":[\"state\"]}";
}

}  // namespace plugfile
