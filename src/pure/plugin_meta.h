// Метаданные плагина из шапки скрипта.
//
// Плагин — это скрипт, а в начале файла идут комментарии с описанием для
// модели:
//     # name: switch
//     # description: Включить, выключить или переключить выключатель
//     # params: channel=1|2|3|all, state*=on|off|toggle
//
// `*` после имени параметра — обязательный. Значения через `|` дают enum.
// Из этого собирается JSON-схема аргументов — ровно в том виде, который
// ждёт модель в поле tools.
//
// Модуль чистый — проверяется на Mac.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace plugmeta {

struct Meta {
  std::string name;         // имя инструмента для модели, латиницей
  std::string description;  // когда вызывать
  std::string params;       // исходная строка параметров (как в шапке)
  std::string schema;       // готовая JSON-схема аргументов
};

namespace detail {

inline std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' ||
                   s[e - 1] == '\n')) --e;
  return s.substr(b, e - b);
}

// JSON-строка в кавычках (для имён полей и значений enum — они простые,
// но кавычку и слэш экранируем).
inline std::string jsonStr(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

inline std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { out.push_back(cur); cur.clear(); continue; }
    cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

}  // namespace detail

// Собирает JSON-схему из строки параметров.
inline std::string schemaFromParams(const char* params) {
  const std::string src = detail::trim(params ? params : "");
  std::string props;
  std::string required;

  if (!src.empty()) {
    for (const std::string& raw : detail::split(src, ',')) {
      std::string item = detail::trim(raw);
      if (item.empty()) continue;

      std::string name = item;
      std::string values;
      const size_t eq = item.find('=');
      if (eq != std::string::npos) {
        name = item.substr(0, eq);
        values = item.substr(eq + 1);
      }
      name = detail::trim(name);

      bool isRequired = false;
      if (!name.empty() && name.back() == '*') {
        isRequired = true;
        name.pop_back();
        name = detail::trim(name);
      }
      if (name.empty()) continue;

      if (!props.empty()) props += ',';
      props += detail::jsonStr(name) + ":{\"type\":\"string\"";

      if (eq != std::string::npos) {
        std::string enums;
        for (const std::string& v : detail::split(values, '|')) {
          const std::string val = detail::trim(v);
          if (val.empty()) continue;
          if (!enums.empty()) enums += ',';
          enums += detail::jsonStr(val);
        }
        if (!enums.empty()) props += ",\"enum\":[" + enums + "]";
      }
      props += "}";

      if (isRequired) {
        if (!required.empty()) required += ',';
        required += detail::jsonStr(name);
      }
    }
  }

  std::string out = "{\"type\":\"object\",\"properties\":{" + props + "}";
  if (!required.empty()) out += ",\"required\":[" + required + "]";
  out += "}";
  return out;
}

// Разбирает шапку скрипта. false — нет имени (такой плагин бесполезен модели).
inline bool parse(const char* source, Meta& out) {
  out = Meta{};
  if (source == nullptr) return false;

  std::string text(source);
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const std::string line = detail::trim(text.substr(pos, end - pos));
    pos = end + 1;

    if (line.empty()) continue;       // пустые строки в шапке пропускаем
    if (line[0] != '#') break;        // начался код — шапка кончилась

    const std::string body = detail::trim(line.substr(1));
    const size_t colon = body.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = detail::trim(body.substr(0, colon));
    const std::string val = detail::trim(body.substr(colon + 1));

    if (key == "name") out.name = val;
    else if (key == "description") out.description = val;
    else if (key == "params") out.params = val;
  }

  if (out.name.empty()) return false;
  out.schema = schemaFromParams(out.params.c_str());
  return true;
}

}  // namespace plugmeta
