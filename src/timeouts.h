// Таймауты сна и времена удержаний — выбор пользователя, хранится в NVS.
//
// Таблицы значений и подписи — в pure/timeouts.h; здесь только хранение и
// применение. Выбор переживает и выключение, и глубокий сон: настройки
// лежат во флеше.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cfg {

// Читает всё из NVS. Значения не из списка (остались от другой прошивки)
// превращаются в заводские.
void begin();

// Всё назад к заводскому.
void reset();

// Подписи для меню и перебор ступеней.
std::string screenLabel();
std::string wifiLabel();
std::string lightLabel();
std::string deepLabel();
std::string backLabel();
std::string resetLabel();

void nextScreen();
void nextWifi();
void nextLight();
void nextDeep();
void nextBack();
void nextReset();

// Значения в миллисекундах. Ноль — «никогда».
uint32_t screenMs();
uint32_t wifiMs();
uint32_t lightMs();
uint32_t deepMs();
uint32_t backMs();
uint32_t resetMs();

}  // namespace cfg
