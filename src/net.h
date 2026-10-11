// Подключение к Wi-Fi и общий TLS-клиент.
#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

namespace net {

// Подключается к Wi-Fi. Возвращает true при успехе.
// При неудаче текст причины доступен в lastError().
bool connectWifi(uint32_t timeoutMs);

// Последняя ошибка Wi-Fi или сети.
const char* lastError();

// Общий TLS-клиент. setInsecure: без пиннинга сертификата.
// Для v1 принято осознанно (см. спеку, раздел 7).
WiFiClientSecure& tlsClient();

}  // namespace net
