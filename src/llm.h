// Ответ LLM через OpenCode Go (DeepSeek), потоком.
//
// OpenCode доступен из России напрямую, поэтому прокси не нужен: стик
// ходит на opencode.ai, а тот — к провайдеру модели.
//
// Запрос идёт со stream:true, ответ — text/event-stream. Текст появляется
// по мере генерации, а счётчик принятых символов даёт честный индикатор.
#pragma once

#include <Arduino.h>

#include <vector>

#include "pure/chat_history.h"

namespace llm {

struct Reply {
  bool ok = false;
  String text;       // ответ модели при ok == true
  String error;      // описание при ok == false
  int httpCode = 0;  // код ответа, если дошло до него
};

// Обратные вызовы на время стрима.
struct Stream {
  // content/len — очередной кусок текста ответа (может быть пустым, если
  // пришёл только reasoning). activity — сколько символов принято всего
  // (ответ + размышления): по нему двигается индикатор.
  void (*onDelta)(const char* content, size_t len, size_t activity,
                  void* ctx) = nullptr;
  void* ctx = nullptr;
};

// Спрашивает модель по распознанному тексту.
// history — предыдущие обмены (без текущего вопроса); уходит в запрос как
// последовательность сообщений system/user/assistant. Пустой userText — ошибка.
Reply ask(const std::vector<chat::Msg>& history, const String& userText,
          const Stream& stream = Stream());

}  // namespace llm
