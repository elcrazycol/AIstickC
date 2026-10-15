// Подключение к Wi-Fi и общий TLS-клиент.
#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include "settings.h"  // kHttpReadMs — срок чтения по умолчанию

namespace net {

// Подключается к Wi-Fi. Возвращает true при успехе.
// При неудаче текст причины доступен в lastError().
bool connectWifi(uint32_t timeoutMs);

// Поднять Wi-Fi, не дожидаясь подключения. Зовётся при пробуждении: пока
// человек тянется к кнопке, модуль уже ассоциируется, и вопрос начинается
// без ожидания. Ждать внутри нельзя — это заморозило бы экран.
void wakeWifi();

// Последняя ошибка Wi-Fi или сети.
const char* lastError();

// Общий TLS-клиент. setInsecure: без пиннинга сертификата.
// Для v1 принято осознанно (см. спеку, раздел 7).
WiFiClientSecure& tlsClient();

// Отдельный клиент для распознавания (наш прокси). Тоже держим открытым:
// рукопожатие к прокси стоит столько же, сколько к модели.
WiFiClientSecure& proxyClient();

// Отдельный клиент для ответов модели. Нужен, чтобы держать соединение с
// opencode.ai открытым между вопросами: рукопожатие TLS на ESP32 стоит
// полторы секунды, и платить их каждый раз незачем. У распознавания свой
// клиент — иначе они отбирали бы соединение друг у друга.
WiFiClientSecure& llmClient();

// Терпеливая запись: ждёт, пока сокет примет данные, и сдаётся только
// по общему таймауту kHttpReadMs от `started`. Возвращает false при обрыве
// или таймауте. progress (если задан) вызывается по мере отправки.
typedef void (*WriteProgress)(size_t sent, size_t total, void* ctx);

bool writeAll(WiFiClient& c, const uint8_t* data, size_t len, uint32_t started,
              WriteProgress progress = nullptr, void* ctx = nullptr);

// Вызывается на каждой итерации ожидания данных — для анимации индикатора.
typedef void (*TickFn)(void* ctx);

// Читает ответ, пока сервер не закроет соединение или не замолчит на
// 1500 мс после первых байт. Рост ограничен maxLen. Таймаут — kHttpReadMs
// от `started`. tick (если задан) вызывается во время ожидания.
String readAll(WiFiClient& c, uint32_t started, size_t maxLen,
               TickFn tick = nullptr, void* ctx = nullptr, uint32_t limitMs = kHttpReadMs);

}  // namespace net
