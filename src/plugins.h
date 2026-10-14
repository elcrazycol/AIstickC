// Плагины: скрипты в LittleFS, которые стик выполняет сам.
//
// Плагин — это файл `/plugins/<имя>.ais`: маленький скрипт с шапкой из
// комментариев (имя, описание, параметры). Из шапок собирается список
// инструментов для модели; при вызове нужный скрипт запускается, а его
// аргументы приходят переменными. Новый плагин — это файл, прошивку
// пересобирать не нужно.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "pure/tool_call.h"

namespace plugins {

// Загружает скрипты из LittleFS и собирает таблицу инструментов.
void begin();

const toolx::Plugin* table();
size_t count();
const char* toolsJson();

struct Result {
  bool ok = false;
  std::string plugin;    // имя плагина
  std::string text;      // текст панели или ответа
  bool asAnswer = false; // показать как обычный ответ, а не панелью
  std::string ask;       // непусто — плагин просит уточнить (например, город)
  bool answerAfter = false;
  std::string note;      // короткая пометка над ответом (что сделал плагин)
};

// Выполняет плагин по вызову модели.
Result run(const char* name, const char* argsJson);

// Ответ пользователя на вопрос плагина (ask). Текст уходит в переменную
// input и в первый параметр скрипта.
Result runInput(const char* name, const char* userText);

// Имена загруженных скриптов (для панели плагинов).
size_t names(std::vector<std::string>& out);

// Короткая надпись для экрана, пока плагин работает («ищу в интернете»).
// Пусто — плагин её не задал, покажем имя.
const char* labelOf(const char* name);

}  // namespace plugins
