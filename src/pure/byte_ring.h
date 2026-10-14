// Кольцевой буфер для одного писателя и одного читателя.
//
// Нужен, чтобы развязать запись и отправку: микрофон кладёт звук с одной
// скоростью, сокет забирает с другой (и иногда застревает на подтверждениях).
// Пока сеть молчит, буфер копит; как только сокет свободен — отдаёт.
//
// Один писатель, один читатель — блокировки не нужны. Индексы монотонные,
// поэтому переполнение счётчика не ломает логику.
//
// Модуль чистый: ни Arduino, ни сети, проверяется обычными тестами.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ring {

class ByteRing {
 public:
  ByteRing(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}

  size_t capacity() const { return cap_; }

  // Обнулить: буфер переиспользуется между вопросами.
  void reset() {
    wr_ = 0;
    rd_ = 0;
  }

  size_t size() const {
    if (cap_ == 0) return 0;
    const size_t used = (size_t)(wr_ - rd_);
    return used > cap_ ? cap_ : used;
  }

  size_t space() const { return cap_ - size(); }

  // Кладёт до n байт. Возвращает, сколько влезло (может быть меньше).
  size_t write(const uint8_t* data, size_t n) {
    if (cap_ == 0) return 0;
    const size_t room = space();
    if (n > room) n = room;
    for (size_t i = 0; i < n; ++i) {
      buf_[(wr_ + i) % cap_] = data[i];
    }
    wr_ += n;
    return n;
  }

  // Забирает до n байт. Возвращает, сколько прочитано.
  size_t read(uint8_t* out, size_t n) {
    if (cap_ == 0) return 0;
    const size_t have = size();
    if (n > have) n = have;
    for (size_t i = 0; i < n; ++i) {
      out[i] = buf_[(rd_ + i) % cap_];
    }
    rd_ += n;
    return n;
  }

 private:
  uint8_t* buf_;
  size_t cap_;
  volatile size_t wr_ = 0;  // докуда записано
  volatile size_t rd_ = 0;  // докуда прочитано
};

}  // namespace ring
