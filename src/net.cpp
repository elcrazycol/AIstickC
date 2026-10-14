#include "net.h"

#include <WiFi.h>

#include "config.h"
#include "settings.h"

namespace net {

namespace {
char g_error[96] = {0};
WiFiClientSecure g_client;
WiFiClientSecure g_llm;
WiFiClientSecure g_proxy;
bool g_clientReady = false;
bool g_llmReady = false;
bool g_proxyReady = false;
}  // namespace

const char* lastError() { return g_error; }

WiFiClientSecure& tlsClient() {
  if (!g_clientReady) {
    // Без пиннинга сертификата: просто и работает, но MITM-незащищено.
    g_client.setInsecure();
    g_client.setTimeout(kHttpReadMs);
    g_clientReady = true;
  }
  return g_client;
}

WiFiClientSecure& proxyClient() {
  if (!g_proxyReady) {
    g_proxy.setInsecure();
    g_proxy.setTimeout(kHttpReadMs);
    g_proxyReady = true;
  }
  return g_proxy;
}

WiFiClientSecure& llmClient() {
  if (!g_llmReady) {
    g_llm.setInsecure();
    g_llm.setTimeout(kHttpReadMs);
    g_llmReady = true;
  }
  return g_llm;
}

bool writeAll(WiFiClient& c, const uint8_t* data, size_t len, uint32_t started,
              WriteProgress progress, void* ctx) {
  size_t sent = 0;
  while (sent < len) {
    if (!c.connected() && c.available() == 0) return false;
    if (millis() - started > kHttpReadMs) return false;

    const size_t n = c.write(data + sent, len - sent);
    if (n > 0) sent += n;
    // Прогресс зовём и в простое: по нему анимируется индикатор.
    if (progress) progress(sent, len, ctx);
    if (n == 0) delay(2);  // сокет занят — даём ему время
  }
  return true;
}

String readAll(WiFiClient& c, uint32_t started, size_t maxLen, TickFn tick,
               void* ctx) {
  String out;
  uint32_t idleSince = millis();
  while (true) {
    if (millis() - started > kHttpReadMs) break;
    while (c.available() > 0) {
      const int ch = c.read();
      if (ch < 0) break;
      out += (char)ch;
      idleSince = millis();
      if (out.length() > maxLen) break;
    }
    if (out.length() > maxLen) break;
    if (!c.connected() && c.available() == 0) break;
    // Сервер ответил и молчит — не ждём таймаут целиком.
    if (out.length() > 0 && (millis() - idleSince) > 1500) break;
    if (tick) tick(ctx);
    delay(5);
  }
  return out;
}

bool connectWifi(uint32_t timeoutMs) {
  const char* ssid = WIFI_SSID;
  if (ssid == nullptr || ssid[0] == 0) {
    snprintf(g_error, sizeof(g_error), "SSID пуст — заполни config.h");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, WIFI_PASS);

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - started > timeoutMs) {
      snprintf(g_error, sizeof(g_error), "Wi-Fi: таймаут (код %d)",
               (int)WiFi.status());
      WiFi.disconnect(true);
      return false;
    }
    delay(100);
  }

  // Уровень сигнала: если он слабый, отправка аудио будет медленной.
  const int rssi = WiFi.RSSI();
  Serial.printf("[net] Wi-Fi ок, IP %s, RSSI %d dBm\n",
                WiFi.localIP().toString().c_str(), rssi);
  if (rssi < -80) {
    Serial.println("[net] ВНИМАНИЕ: слабый сигнал, отправка будет медленной");
  }

  snprintf(g_error, sizeof(g_error), "ok");
  return true;
}

}  // namespace net
