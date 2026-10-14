// Распознавание речи через свой прокси на VPS (Groq Whisper).
#pragma once

#include <Arduino.h>

namespace stt {

struct Result {
  bool ok = false;
  String text;      // распознанный текст при ok == true
  String error;     // описание при ok == false
  int httpCode = 0; // код ответа, если дошло до него
};

// Обратные вызовы для индикатора: прогресс отправки по байтам и «тик» во
// время ожидания ответа (прокси молчит — рисуем анимацию).
struct Hooks {
  void (*onUpload)(size_t sent, size_t total, void* ctx) = nullptr;
  void (*onTick)(void* ctx) = nullptr;
  void* ctx = nullptr;
};

// pcm — сырой LPCM: 16 бит, little-endian, знаковый, моно, 16 кГц.
// WAV-заголовок не нужен: его собирает прокси.
Result transcribe(const uint8_t* pcm, size_t pcmLen,
                  const Hooks& hooks = Hooks());

// --- потоковая отправка ---
//
// Сеть прокачивает около 92 КБ/с, а речь даёт 16 КБ/с в µ-law — значит,
// отправлять можно прямо во время говорения, не дожидаясь конца записи.
// К отпусканию кнопки почти весь звук уже на прокси.
//
// streamBegin — подключиться и отправить заголовки (отправкой занимается
// своя задача), streamPush — отдать очередной кусок µ-law, streamEnd —
// дослать хвост и получить ответ. Если streamBegin вернул false, работаем
// как раньше: копим запись и шлём её целиком.
bool streamBegin();
bool streamPush(const uint8_t* ulaw, size_t n);
Result streamEnd();
void streamAbort();

}  // namespace stt
