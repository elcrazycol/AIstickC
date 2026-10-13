// Манифест репозитория плагинов (index.txt).
//
// Репозиторий — это манифест и файлы плагинов рядом:
//     name = switch
//     description = Включить, выключить или переключить выключатель
//     params = channel=1|2|3|all, state*=on|off|toggle
//     file = switch.ais
//     version = 2
//
// Формат — те же блоки «ключ = значение», что и у локального файла плагинов,
// только добавляются `file` и `version`. Так манифест читается тем же
// кодом и не требует JSON.
//
// Модуль чистый — проверяется на Mac.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace plugrepo {

struct Entry {
  std::string name;
  std::string description;
  std::string params;
  std::string file;     // имя файла скрипта в репозитории
  int version = 1;
};

// Разбирает манифест. Записи без имени или файла отбрасываются.
// Возвращает число принятых записей.
inline size_t parse(const char* text, std::vector<Entry>& out) {
  out.clear();
  if (text == nullptr) return 0;

  Entry cur;
  bool has = false;
  size_t accepted = 0;

  auto flush = [&]() {
    if (has && !cur.name.empty() && !cur.file.empty()) {
      out.push_back(cur);
      ++accepted;
    }
    cur = Entry{};
    has = false;
  };

  size_t i = 0;
  while (text[i] != 0) {
    size_t end = i;
    while (text[end] != 0 && text[end] != '\n') ++end;
    std::string line(text + i, end - i);
    i = (text[end] == 0) ? end : end + 1;

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
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
    size_t b = 0;
    while (b < val.size() && (val[b] == ' ' || val[b] == '\t')) ++b;
    val.erase(0, b);

    has = true;
    if (key == "name") cur.name = val;
    else if (key == "description") cur.description = val;
    else if (key == "params") cur.params = val;
    else if (key == "file") cur.file = val;
    else if (key == "version") cur.version = std::atoi(val.c_str());
  }
  flush();
  return accepted;
}

}  // namespace plugrepo
