// Плагины: таблица инструментов для модели и выполнение её вызова.
//
// Плагин — запись в таблице: имя функции (для модели), описание, JSON-схема
// аргументов и действие (GPIO / экран / HTTP). Модель сама решает, когда
// вызвать инструмент; сюда приходит уже готовый вызов.
//
// Позже таблица переедет в LittleFS — тогда плагин можно будет закинуть
// файлом без пересборки. Пока всё компилируется в прошивку.
#pragma once

#include <cstddef>
#include <string>

#include "pure/tool_call.h"

namespace plugins {

// Читает плагины из LittleFS (/plugins.txt); если файла нет — берёт
// встроенный набор. Вызывается один раз при старте.
void begin();

// Таблица и её размер.
const toolx::Plugin* table();
size_t count();

// JSON-массив tools для запроса к модели (собирается один раз).
const char* toolsJson();

// Результат выполнения плагина.
struct Result {
  bool ok = false;
  const char* plugin = "";     // имя плагина
  std::string text;            // текст результата (панель или ответ)
  bool asAnswer = false;       // показать как обычный ответ, а не панелью
  const char* ask = nullptr;   // спросить пользователя этим текстом
  bool answerAfter = false;    // просить ли модель прокомментировать
};

// Выполняет вызванный плагином действие.
// name — имя функции, argsJson — аргументы от модели (собраны из стрима).
Result run(const char* name, const char* argsJson);

// Ответ пользователя на вопрос плагина (например, город для погоды).
Result runInput(const char* name, const char* userText);

}  // namespace plugins
