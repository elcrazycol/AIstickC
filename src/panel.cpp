#include "panel.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstring>
#include <string>

#include "panel_page.h"

namespace panel {

namespace {

constexpr uint16_t kPort = 80;
constexpr size_t kMaxJson = 8192;  // снимок истории: пять обменов с запасом

WebServer g_server(kPort);
bool g_active = false;
char g_code[5] = {0};
std::string g_url;
std::string g_json = "[]";
SemaphoreHandle_t g_lock = nullptr;
volatile uint32_t g_rev = 1;
volatile bool g_touched = false;

bool codeOk() {
  if (g_server.arg("k") != g_code) {
    g_server.send(403, "text/plain; charset=utf-8", "код неверный");
    return false;
  }
  g_touched = true;
  return true;
}

void handleRoot() {
  if (!codeOk()) return;
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

  // Код из четырёх цифр: его видно на экране рядом с QR, и он попадает в
  // ссылку. Кто не видит экран стика — на страницу не попадёт.
  const uint32_t r = esp_random() % 9000u;
  snprintf(g_code, sizeof(g_code), "%04u", (unsigned)(1000u + r));

  g_url = std::string("http://") + WiFi.localIP().toString().c_str() + "/?k=" + g_code;

  g_server.on("/", handleRoot);
  g_server.on("/api/chat", handleChat);
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
  if (g_active) g_server.handleClient();
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
