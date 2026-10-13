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
#include "settings.h"

namespace llm {

struct Reply {
  bool ok = false;
  String text;       // ответ модели при ok == true
  String error;      // описание при ok == false
  int httpCode = 0;  // код ответа, если дошло до него

  // Модель вместо ответа вызвала инструмент (плагин).
  bool toolCall = false;
  String toolId;     // id вызова — вернём его в tool_call_id
  String toolName;   // имя плагина
  String toolArgs;   // собранные аргументы (JSON)
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

// Опции запроса.
struct Options {
  // Системный промпт. Собирается в main из роли, выбранной длины ответа и
  // правила про инструменты; nullptr — только роль.
  const char* systemPrompt = nullptr;

  // Сколько токенов разрешено на ответ. Приходит из настроек: конкретное
  // значение или подобранное «авто» (см. src/chat_settings.h).
  int maxTokens = kLlmMaxTokens;

  // Список инструментов (плагинов) для модели. nullptr — обычный запрос.
  const char* toolsJson = nullptr;

  // Продолжение после вызова инструмента: результат уходит модели как
  // сообщение role:"tool", чтобы она могла его прокомментировать.
  const char* toolCallId = nullptr;
  const char* toolName = nullptr;
  const char* toolArgs = nullptr;
  const char* toolResult = nullptr;
};

// Спрашивает модель по распознанному тексту.
// history — предыдущие обмены (без текущего вопроса); уходит в запрос как
// последовательность сообщений system/user/assistant. Пустой userText — ошибка.
Reply ask(const std::vector<chat::Msg>& history, const String& userText,
          const Stream& stream = Stream(), const Options& opts = Options());

}  // namespace llm
