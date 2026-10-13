// Настройки диалога, которые выбирает пользователь: сколько помнить и
// сколько просить у модели.
//
// Выбор живёт в NVS и переживает выключение. Числа и логика «авто» — в
// pure/chat_options.h; здесь только хранение и применение к текущей истории.
#pragma once

#include <cstddef>
#include <string>

#include "pure/chat_history.h"

namespace chatcfg {

// Читает выбор из NVS. Значение, оставшееся от прошлой прошивки и выпавшее
// из списка ступеней, превращается в «авто».
void begin();

// Обе настройки назад в «авто» — это и есть заводское состояние.
void reset();

size_t turnsChoice();   // 0 — авто, иначе число обменов
int    tokensChoice();  // 0 — авто, иначе токены

std::string turnsLabel();   // «авто» или число — для меню
std::string tokensLabel();

void nextTurns();   // следующая ступень по кругу
void nextTokens();

// Что применить прямо сейчас: выбранное значение либо подобранное по тому,
// как выглядит текущий диалог.
size_t effectiveTurns(const chat::History& h);
int    effectiveTokens(const chat::History& h);

}  // namespace chatcfg
