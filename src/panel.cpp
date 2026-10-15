#include "panel.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstring>
#include <string>

#include "panel_page.h"
#include "pure/panel_life.h"

namespace panel {

namespace {

constexpr uint16_t kPort = 80;
constexpr size_t kMaxJson = 8192;  // снимок истории: пять обменов с запасом

// Сколько панель имеет право жить. Считаем по трём поводам: нет запросов,
// прошёл общий потолок, подбирали код.
constexpr uint32_t kPanelIdleMs = 120000;   // 2 минуты без запросов
constexpr uint32_t kPanelMaxMs = 900000;    // 15 минут с момента открытия
constexpr uint8_t  kPanelWrongLimit = 3;    // столько РАЗНЫХ неверных кодов

const panellife::Policy g_policy(kPanelIdleMs, kPanelMaxMs, kPanelWrongLimit);

WebServer g_server(kPort);
bool g_active = false;
char g_code[8] = {0};
std::string g_url;
std::string g_json = "[]";
SemaphoreHandle_t g_lock = nullptr;
volatile uint32_t g_rev = 1;
volatile bool g_touched = false;
volatile bool g_closeRequested = false;  // закрыться, но не из обработчика
uint32_t g_openedAt = 0;
uint32_t g_lastRequestAt = 0;
uint8_t  g_wrongCodes = 0;      // сколько разных чужих кодов увидели
char     g_lastWrongCode[8] = {0};

// Код верный: продлеваем жизнь панели и отмечаем активность.
bool codeOk() {
  if (g_server.arg("k") != g_code) {
    g_server.send(403, "text/plain; charset=utf-8", "код неверный");
    return false;
  }
  g_wrongCodes = 0;
  g_lastRequestAt = millis();
  g_touched = true;
  return true;
}

// Запрос самой страницы с неверным кодом — вот это подбор: код приходит в
// ссылке, набирать его руками негде. Считаем только такие запросы. Опрос
// истории старым кодом бывает у забытой вкладки, и закрывать из-за неё панель
// нельзя — иначе старая вкладка гасит новую сессию.
bool pageCodeOk() {
  if (g_server.arg("k") == g_code) return true;

  // Подбор — это перебор разных кодов. Один и тот же чужой код подряд бывает
  // у забытой вкладки, которую браузер восстановил: наказывать за это панель
  // нельзя, иначе чужой старый экран гасит новую сессию.
  const String k = g_server.arg("k");
  if (strcmp(k.c_str(), g_lastWrongCode) != 0) {
    snprintf(g_lastWrongCode, sizeof(g_lastWrongCode), "%s", k.c_str());
    ++g_wrongCodes;
    Serial.printf("[panel] чужой код %s от %s (разных: %u)\n", k.c_str(),
                  g_server.client().remoteIP().toString().c_str(),
                  (unsigned)g_wrongCodes);
  }
  if (g_wrongCodes >= g_policy.wrongLimit) {
    Serial.printf("[panel] подбор кода (%u разных) — закрываю\n",
                  (unsigned)g_wrongCodes);
    g_closeRequested = true;
  }
  g_server.send(403, "text/plain; charset=utf-8", "код неверный");
  return false;
}

// Страница говорит «до свидания»: её закрыли или свернули. Браузер отправляет
// это маячком при уходе со страницы, поэтому панель гаснет сразу, а не ждёт
// таймаут. Маячок может и не дойти — на такой случай остаётся простой.
void handleBye() {
  if (g_server.arg("k") != g_code) {
    g_server.send(403, "text/plain; charset=utf-8", "код неверный");
    return;
  }
  g_server.send(200, "text/plain; charset=utf-8", "пока");
  g_closeRequested = true;
  Serial.println("[panel] страница закрылась — гашу");
}

void handleRoot() {
  if (!pageCodeOk()) return;
  g_lastRequestAt = millis();
  g_touched = true;
  g_server.send_P(200, "text/html; charset=utf-8", kPanelPage);
}

void handleChat() {
  if (!codeOk()) return;

  std::string body;
  body.reserve(256 + g_json.size());
  body += "{\"rev\":";
  body += std::to_string((unsigned)g_rev);
  body += ",\"msgs\":";
  if (xSemaphoreTake(g_lock, portMAX_DELAY) == pdTRUE) {
    body += g_json;
    xSemaphoreGive(g_lock);
  } else {
    body += "[]";
  }
  body += "}";
  g_server.send(200, "application/json; charset=utf-8", body.c_str());
}

void handleNotFound() {
  g_touched = true;  // попытка — тоже активность
  g_server.send(404, "text/plain; charset=utf-8", "нет такой страницы");
}

}  // namespace

bool begin() {
  if (g_active) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  if (g_lock == nullptr) g_lock = xSemaphoreCreateMutex();
  if (g_lock == nullptr) return false;

  // Код из шести цифр: его видно на экране рядом с QR, и он попадает в ссылку.
  // Кто не видит экран стика — на страницу не попадёт. Шесть цифр, а не
  // четыре: миллион вариантов перебирать бессмысленно, десять тысяч — реально.
  const uint32_t r = esp_random() % 900000u;
  snprintf(g_code, sizeof(g_code), "%06u", (unsigned)(100000u + r));

  g_url = std::string("http://") + WiFi.localIP().toString().c_str() + "/?k=" + g_code;

  // Отсчёт жизни начинается заново с каждым открытием.
  g_openedAt = millis();
  g_lastRequestAt = g_openedAt;
  g_wrongCodes = 0;
  g_lastWrongCode[0] = 0;
  g_closeRequested = false;

  g_server.on("/", handleRoot);
  g_server.on("/api/chat", handleChat);
  g_server.on("/bye", HTTP_POST, handleBye);
  g_server.onNotFound(handleNotFound);
  g_server.begin();
  g_active = true;

  Serial.printf("[panel] поднята на %s\n", g_url.c_str());
  return true;
}

void end() {
  if (!g_active) return;
  g_active = false;
  g_server.stop();
  // Снимок истории больше не нужен — отдаём память.
  if (g_lock != nullptr && xSemaphoreTake(g_lock, portMAX_DELAY) == pdTRUE) {
    g_json.clear();
    g_json.shrink_to_fit();
    xSemaphoreGive(g_lock);
  }
  Serial.println("[panel] погашена");
}

void releaseHistory() {
  if (g_lock == nullptr) return;
  if (xSemaphoreTake(g_lock, portMAX_DELAY) == pdTRUE) {
    g_json.clear();
    g_json.shrink_to_fit();
    xSemaphoreGive(g_lock);
  }
}

void poll() {
  if (!g_active) return;

  // Панель живёт только пока нужна: проверяем сроки до обслуживания запросов,
  // чтобы закрытие не случалось посреди обработки.
  const uint32_t now = millis();
  if (g_closeRequested ||
      g_policy.shouldClose(now, g_openedAt, g_lastRequestAt, g_wrongCodes)) {
    Serial.printf("[panel] закрываю: простой %u мс, возраст %u мс, "
                  "неверных кодов %u\n",
                  (unsigned)(now - g_lastRequestAt), (unsigned)(now - g_openedAt),
                  (unsigned)g_wrongCodes);
    end();
    return;
  }

  g_server.handleClient();
}

bool active() { return g_active; }

std::string url() { return g_url; }

const char* code() { return g_code; }

void setHistory(const char* json) {
  if (json == nullptr) return;
  if (g_lock == nullptr) return;
  const size_t n = strlen(json);
  if (n > kMaxJson) return;
  if (xSemaphoreTake(g_lock, portMAX_DELAY) == pdTRUE) {
    g_json.assign(json, n);
    ++g_rev;
    xSemaphoreGive(g_lock);
  }
}

bool touched() {
  const bool t = g_touched;
  g_touched = false;
  return t;
}

}  // namespace panel
