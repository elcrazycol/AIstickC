// Ответ LLM через OpenCode Go (DeepSeek).
//
// OpenCode доступен из России напрямую, поэтому прокси не нужен: стик
// ходит на opencode.ai, а тот — к провайдеру модели.
#pragma once

#include <Arduino.h>

namespace llm {

struct Reply {
  bool ok = false;
  String text;       // ответ модели при ok == true
  String error;      // описание при ok == false
  int httpCode = 0;  // код ответа, если дошло до него
};

// Спрашивает модель по распознанному тексту. Пустой userText — ошибка.
Reply ask(const String& userText);

}  // namespace llm
