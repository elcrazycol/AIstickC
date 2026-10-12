// Инкрементальный разбор потока Server-Sent Events.
//
// OpenCode Go отдаёт ответ модели как text/event-stream: строки вида
//   data: {"choices":[{"delta":{"content":"Hel"}}]}
// и в конце
//   data: [DONE]
// Поток приходит кусками и может разрезать строку где угодно, поэтому
// парсер копит байты и выдаёт только целые строки.
//
// Модуль чистый — компилируется и тестируется на Mac.
#pragma once

#include <cstddef>
#include <string>

namespace sse {

class Parser {
 public:
  void clear() { buf_.clear(); }

  // Добавляет очередной кусок потока.
  void feed(const char* data, size_t len) {
    if (data != nullptr && len > 0) buf_.append(data, len);
  }
  void feed(const std::string& s) { buf_.append(s); }

  // Достаёт следующую полезную нагрузку строки "data:".
  // false — полных подходящих строк больше нет.
  // Служебные строки (event:, id:, комментарии, размеры chunked) пропускает.
  bool next(std::string& out) {
    while (true) {
      const size_t nl = buf_.find('\n');
      if (nl == std::string::npos) return false;  // строка ещё не собралась

      std::string line = buf_.substr(0, nl);
      buf_.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();

      if (line.rfind("data:", 0) != 0) continue;  // не наш тип строки

      size_t i = 5;
      while (i < line.size() && line[i] == ' ') ++i;
      out = line.substr(i);
      return true;
    }
  }

 private:
  std::string buf_;
};

}  // namespace sse
