// Отправка WAV в Groq Whisper и разбор ответа.
#pragma once

#include <Arduino.h>

namespace stt {

struct Result {
  bool ok = false;
  String text;      // распознанный текст при ok == true
  String error;     // описание при ok == false
  int httpCode = 0; // код ответа, если дошло до него
};

// wav — буфер с WAV целиком (заголовок + аудио) в PSRAM.
Result transcribe(const uint8_t* wav, size_t wavLen);

}  // namespace stt
