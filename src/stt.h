// Распознавание речи через Yandex SpeechKit.
#pragma once

#include <Arduino.h>

namespace stt {

struct Result {
  bool ok = false;
  String text;      // распознанный текст при ok == true
  String error;     // описание при ok == false
  int httpCode = 0; // код ответа, если дошло до него
};

// pcm — сырой LPCM: 16 бит, little-endian, знаковый, моно, 16 кГц.
// WAV-заголовок НЕ нужен, SpeechKit его не принимает.
Result transcribe(const uint8_t* pcm, size_t pcmLen);

}  // namespace stt
