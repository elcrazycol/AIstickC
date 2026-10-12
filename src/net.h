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

// Терпеливая запись: ждёт, пока сокет примет данные, и сдаётся только
// по общему таймауту kHttpReadMs от `started`. Возвращает false при обрыве
// или таймауте.
bool writeAll(WiFiClient& c, const uint8_t* data, size_t len, uint32_t started);

// Читает ответ, пока сервер не закроет соединение или не замолчит на
// 1500 мс после первых байт. Рост ограничен maxLen. Таймаут — kHttpReadMs
// от `started`.
String readAll(WiFiClient& c, uint32_t started, size_t maxLen);

}  // namespace net
