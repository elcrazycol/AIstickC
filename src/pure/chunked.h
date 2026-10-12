// Инкрементальный декодер Transfer-Encoding: chunked.
//
// Ответ OpenCode Go приходит кусками: "<hex-размер>\r\n<данные>\r\n", в
// конце "0\r\n\r\n". Раньше тело копилось целиком и декодировалось заново на
// каждой итерации чтения — O(n²) и лишняя память, из-за чего поток
// обрезался. Здесь состояние хранится между вызовами: наружу выдаются только
// готовые декодированные байты, а сырьё сразу выбрасывается.
//
// Модуль чистый — компилируется и тестируется на Mac.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace chunked {

class Decoder {
 public:
  // Добавляет очередной кусок сырого тела.
  void feed(const char* data, size_t len) {
    if (data != nullptr && len > 0) in_.append(data, len);
    parse();
  }
  void feed(const std::string& s) { feed(s.data(), s.size()); }

  // Забирает накопленный декодированный вывод. false — пока пусто.
  bool next(std::string& out) {
    if (out_.empty()) return false;
    out.swap(out_);
    out_.clear();
    return true;
  }

  // Последний чанк (нулевого размера) получен.
  bool done() const { return done_; }

  void reset() {
    in_.clear();
    out_.clear();
    need_ = 0;
    done_ = false;
  }

 private:
  void parse() {
    while (!done_) {
      if (need_ == 0) {
        // Строка размера чанка до CRLF.
        const size_t nl = in_.find("\r\n");
        if (nl == std::string::npos) break;  // строка ещё не собралась

        uint32_t size = 0;
        bool any = false;
        for (size_t k = 0; k < nl; ++k) {
          const char c = in_[k];
          uint32_t d;
          if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
          else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
          else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
          else break;  // расширения chunked (";...") игнорируем
          size = (size << 4) | d;
          any = true;
        }
        in_.erase(0, nl + 2);
        if (!any) {  // не размер — поток битый, прекращаем
          done_ = true;
          break;
        }
        if (size == 0) {  // последний чанк
          done_ = true;
          break;
        }
        need_ = size;
      } else {
        if (in_.size() < need_ + 2) break;  // ждём данные и завершающий CRLF
        out_.append(in_, 0, need_);
        in_.erase(0, need_ + 2);
        need_ = 0;
      }
    }
  }

  std::string in_;    // необработанное сырьё
  std::string out_;   // декодированные байты, ещё не выданные
  size_t need_ = 0;   // сколько байт данных ждём в текущем чанке
  bool done_ = false;
};

}  // namespace chunked
