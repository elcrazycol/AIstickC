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
    // Нагл задерживает мелкие пакеты, ожидая подтверждения предыдущего:
    // для короткого запроса к прокси это лишние round-trip'ы. Нам важнее
    // задержка, чем экономия на заголовках.
    g_client.setNoDelay(true);
    g_clientReady = true;
  }
  return g_client;
}

WiFiClientSecure& proxyClient() {
  if (!g_proxyReady) {
    g_proxy.setInsecure();
    g_proxy.setTimeout(kHttpReadMs);
    // Нагл задерживает мелкие пакеты, ожидая подтверждения предыдущего:
    // для короткого запроса к прокси это лишние round-trip'ы. Нам важнее
    // задержка, чем экономия на заголовках.
    g_proxy.setNoDelay(true);
    g_proxyReady = true;
  }
  return g_proxy;
}

WiFiClientSecure& llmClient() {
  if (!g_llmReady) {
    g_llm.setInsecure();
    g_llm.setTimeout(kHttpReadMs);
    // Нагл задерживает мелкие пакеты, ожидая подтверждения предыдущего:
    // для короткого запроса к прокси это лишние round-trip'ы. Нам важнее
    // задержка, чем экономия на заголовках.
    g_llm.setNoDelay(true);
    g_llmReady = true;
  }
  return g_llm;
}

namespace {
// Длина тела из заголовков ответа. 0 — не объявлена (chunked или без неё).
size_t contentLengthOf(const String& head) {
  const int p = head.indexOf("Content-Length:");
  if (p < 0) return 0;
  const int e = head.indexOf("\r\n", p);
  String v = head.substring(p + 15, e < 0 ? head.length() : e);
  v.trim();
  const long n = v.toInt();
  return n > 0 ? (size_t)n : 0;
}
}  // namespace

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
               void* ctx, uint32_t limitMs) {
  String out;
  uint32_t idleSince = millis();
  size_t need = 0;  // полная длина ответа, как только узнаем её из заголовков

  while (true) {
    if (millis() - started > limitMs) break;
    while (c.available() > 0) {
      const int ch = c.read();
      if (ch < 0) break;
      out += (char)ch;
      idleSince = millis();
      if (out.length() > maxLen) break;
    }
    if (out.length() > maxLen) break;

    // Заголовки собрались — значит длина тела известна, и ждать тишины
    // незачем. На keep-alive соединении сервер сокет не закрывает, поэтому
    // без этого мы каждый раз теряли бы полторы секунды впустую: именно
    // столько занимал вызов Tuya при 150 мс работы на сервере.
    if (need == 0) {
      const int sep = out.indexOf("\r\n\r\n");
      if (sep >= 0) {
        const size_t body = contentLengthOf(out.substring(0, sep));
        if (body > 0) need = (size_t)sep + 4 + body;
      }
    }
    if (need > 0 && out.length() >= need) break;

    if (!c.connected() && c.available() == 0) break;
    // Тело без объявленной длины: ждём закрытия или тишины.
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

  // Модуль уже поднимается — второй begin() сбросил бы ассоциацию и добавил
  // времени. Достаточно дождаться результата.
  const wl_status_t was = WiFi.status();
  if (was != WL_CONNECTED && was != WL_IDLE_STATUS) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, WIFI_PASS);
  }

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

  // Энергосбережение Wi-Fi выключаем. По умолчанию модем засыпает между
  // маячками и просыпается раз в ~100 мс — для голосового ассистента это
  // означает рваную отправку и в разы меньшую скорость. Платим за это
  // питанием, но Wi-Fi у нас и так поднимается только на время вопроса.
  Serial.printf("[net] power save был: %d\n", (int)WiFi.getSleep());
  WiFi.setSleep(false);
  Serial.printf("[net] power save стал: %d\n", (int)WiFi.getSleep());

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

void wakeWifi() {
  const char* ssid = WIFI_SSID;
  if (ssid == nullptr || ssid[0] == 0) return;

  // Уже подключены или уже подключаемся — второй begin() только сбросил бы
  // ассоциацию и добавил времени.
  const wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED || st == WL_IDLE_STATUS) return;

  // Не ждём: экран в этот момент только что проснулся, и заморозить его на
  // секунды подключения нельзя. Модуль ассоциируется в фоне, а connectWifi()
  // потом просто дождётся результата — обычно уже готового.
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, WIFI_PASS);
  WiFi.setSleep(false);
  Serial.println("[net] Wi-Fi поднимаю сразу при пробуждении");
}

}  // namespace net
