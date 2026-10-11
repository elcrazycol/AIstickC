// Отдаёт тело multipart/form-data как Arduino Stream, не собирая его
// целиком в памяти. HTTPClient::sendRequest(type, Stream*, size) читает
// отсюда и пишет прямо в сокет.
//
// Тело = [голова][аудио][хвост], где:
//   голова — заголовок файловой части (~120 байт);
//   хвост  — CRLF + три поля + закрывающая граница (~250 байт).
//
// Голова и хвост рендерятся один раз в два маленьких буфера, аудио
// отдаётся прямо из PSRAM. Второго буфера на запись целиком не нужно.
#pragma once

#include <Arduino.h>
#include <cstdlib>
#include <cstring>

#include "pure/multipart.h"

class MultipartStream : public Stream {
 public:
  MultipartStream(const char* boundary, const uint8_t* wav, size_t wavLen)
      : boundary_(boundary), wav_(wav), wavLen_(wavLen) {
    headLen_ = mp::fileHeaderSize(boundary_, "file", "audio.wav", "audio/wav");
    tailLen_ = 2 +  // CRLF после аудио
               mp::fieldSize(boundary_, "model", "whisper-large-v3-turbo") +
               mp::fieldSize(boundary_, "response_format", "json") +
               mp::fieldSize(boundary_, "language", "ru") +
               mp::closingSize(boundary_);

    head_ = (uint8_t*)malloc(headLen_);
    tail_ = (uint8_t*)malloc(tailLen_);

    if (head_ != nullptr) {
      mp::writeFileHeader(head_, boundary_, "file", "audio.wav", "audio/wav");
    }
    if (tail_ != nullptr) {
      size_t n = 0;
      n += mp::writeRaw(tail_ + n, "\r\n");
      n += mp::writeField(tail_ + n, boundary_, "model",
                          "whisper-large-v3-turbo");
      n += mp::writeField(tail_ + n, boundary_, "response_format", "json");
      n += mp::writeField(tail_ + n, boundary_, "language", "ru");
      n += mp::writeClosing(tail_ + n, boundary_);
    }

    total_ = headLen_ + wavLen_ + tailLen_;
    pos_ = 0;
  }

  ~MultipartStream() {
    free(head_);
    free(tail_);
  }

  // false, если не хватило памяти — тогда отправлять нельзя.
  bool valid() const { return head_ != nullptr && tail_ != nullptr; }

  size_t totalSize() const { return total_; }

  int available() override { return (int)(total_ - pos_); }

  int read() override {
    uint8_t b;
    return readBytes(&b, 1) == 1 ? (int)b : -1;
  }

  int peek() override {
    if (pos_ >= total_) return -1;
    const size_t save = pos_;
    uint8_t b;
    readBytes(&b, 1);
    pos_ = save;
    return (int)b;
  }

  size_t readBytes(uint8_t* buffer, size_t length) override {
    size_t written = 0;
    while (written < length && pos_ < total_) {
      size_t n;
      if (pos_ < headLen_) {
        n = copySegment(buffer + written, length - written, head_, headLen_,
                        0);
      } else if (pos_ < headLen_ + wavLen_) {
        n = copySegment(buffer + written, length - written, wav_, wavLen_,
                        headLen_);
      } else {
        n = copySegment(buffer + written, length - written, tail_, tailLen_,
                        headLen_ + wavLen_);
      }
      if (n == 0) break;  // защита от зацикливания
      written += n;
      pos_ += n;
    }
    return written;
  }

  size_t write(uint8_t) override { return 0; }  // только чтение
  size_t write(const uint8_t*, size_t) override { return 0; }

 private:
  // Копирует из src кусок, соответствующий текущей позиции в теле.
  // srcStart — смещение src внутри общего тела.
  size_t copySegment(uint8_t* dst, size_t room, const uint8_t* src,
                     size_t srcLen, size_t srcStart) {
    const size_t off = pos_ - srcStart;
    if (off >= srcLen) return 0;
    const size_t n = (room < srcLen - off) ? room : (srcLen - off);
    memcpy(dst, src + off, n);
    return n;
  }

  const char* boundary_;
  const uint8_t* wav_;
  size_t wavLen_;

  uint8_t* head_ = nullptr;
  size_t headLen_ = 0;
  uint8_t* tail_ = nullptr;
  size_t tailLen_ = 0;

  size_t total_ = 0;
  size_t pos_ = 0;
};
