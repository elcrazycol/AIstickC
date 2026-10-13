// Настройки диалога, которые выбирает пользователь: длина ответа, сколько
// помнить и сколько просить у модели.
//
// Выбор живёт в NVS и переживает выключение. Числа и логика — в
// pure/chat_options.h; здесь только хранение и применение к текущей истории.
#pragma once

#include <cstddef>
#include <string>

#include "pure/chat_history.h"

namespace chatcfg {

// Читает выбор из NVS. Значение, оставшееся от прошлой прошивки и выпавшее
// из списка, превращается в заводское.
void begin();

// Всё назад к заводскому: длина «кратко», память и лимит — авто.
void reset();

size_t lengthChoice();  // 0 — кратко, 1 — обычно, 2 — подробно
size_t turnsChoice();   // 0 — авто, иначе число обменов
int    tokensChoice();  // 0 — авто (следует за длиной), иначе токены

std::string lengthLabel();  // подписи для меню
std::string turnsLabel();
std::string tokensLabel();

void nextLength();  // следующая ступень по кругу
void nextTurns();
void nextTokens();

// Системный промпт под текущую длину ответа: роль + просьба + правило
// про инструменты.
std::string systemPrompt();

// Что применить прямо сейчас.
// Память: выбранное число обменов либо подобранное по текущему диалогу.
size_t effectiveTurns(const chat::History& h);
// Лимит: выбранное число токенов либо вытекающее из длины ответа.
int    effectiveTokens();

}  // namespace chatcfg
