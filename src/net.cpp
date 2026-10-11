#include "net.h"

#include <WiFi.h>

#include "config.h"
#include "settings.h"

namespace net {

namespace {
char g_error[96] = {0};
WiFiClientSecure g_client;
bool g_clientReady = false;
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

  snprintf(g_error, sizeof(g_error), "ok");
  return true;
}

}  // namespace net
