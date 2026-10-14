#include "timeouts.h"

#include <Arduino.h>
#include <Preferences.h>

#include "pure/timeouts.h"

namespace cfg {

namespace {

// Ключи короткие: в NVS имена ограничены по длине. Для глубокого сна ключ
// оставлен прежним ("sleep"), чтобы выбор не потерялся при обновлении.
constexpr const char* kScreenKey = "scr";
constexpr const char* kWifiKey   = "wifi";
constexpr const char* kLightKey  = "light";
constexpr const char* kDeepKey   = "sleep";
constexpr const char* kBackKey   = "back";
constexpr const char* kResetKey  = "rst";

Preferences& prefs() {
  static Preferences p;
  static bool opened = false;
  if (!opened) {
    p.begin("aistick", false);
    opened = true;
  }
  return p;
}

size_t g_screen = 0, g_wifi = 0, g_light = 0, g_deep = 0, g_back = 0, g_reset = 0;

// Читает ступень, а мусор из NVS превращает в заводскую.
size_t load(const char* key, const tmo::Table& t) {
  const size_t stored = prefs().getUInt(key, t.def);
  if (tmo::valid(t, stored)) return stored;
  Serial.printf("[cfg] %s=%u не из списка — вернул заводское\n", key,
                (unsigned)stored);
  prefs().remove(key);
  return t.def;
}

void save(const char* key, size_t v) { prefs().putUInt(key, (uint32_t)v); }

// Общий вид перебора: подпись, следующая ступень, значение.
std::string labelOf(size_t v, const tmo::Table& t) { return tmo::label(t, v); }

}  // namespace

void begin() {
  g_screen = load(kScreenKey, tmo::screen());
  g_wifi = load(kWifiKey, tmo::wifi());
  g_light = load(kLightKey, tmo::light());
  g_deep = load(kDeepKey, tmo::deep());
  g_back = load(kBackKey, tmo::back());
  g_reset = load(kResetKey, tmo::reset());

  Serial.printf("[cfg] экран %s, Wi-Fi %s, лёгкий сон %s, глубокий %s, "
                "назад %s, сброс %s\n",
                screenLabel().c_str(), wifiLabel().c_str(), lightLabel().c_str(),
                deepLabel().c_str(), backLabel().c_str(), resetLabel().c_str());
}

void reset() {
  g_screen = tmo::screen().def;
  g_wifi = tmo::wifi().def;
  g_light = tmo::light().def;
  g_deep = tmo::deep().def;
  g_back = tmo::back().def;
  g_reset = tmo::reset().def;
  for (const char* k : {kScreenKey, kWifiKey, kLightKey, kDeepKey, kBackKey,
                        kResetKey}) {
    prefs().remove(k);
  }
  Serial.println("[cfg] таймауты сброшены к заводским");
}

std::string screenLabel() { return labelOf(g_screen, tmo::screen()); }
std::string wifiLabel() { return labelOf(g_wifi, tmo::wifi()); }
std::string lightLabel() { return labelOf(g_light, tmo::light()); }
std::string deepLabel() { return labelOf(g_deep, tmo::deep()); }
std::string backLabel() { return labelOf(g_back, tmo::back()); }
std::string resetLabel() { return labelOf(g_reset, tmo::reset()); }

void nextScreen() { g_screen = tmo::next(tmo::screen(), g_screen); save(kScreenKey, g_screen); Serial.printf("[cfg] экран %s\n", screenLabel().c_str()); }
void nextWifi()   { g_wifi   = tmo::next(tmo::wifi(), g_wifi);     save(kWifiKey, g_wifi);     Serial.printf("[cfg] Wi-Fi %s\n", wifiLabel().c_str()); }
void nextLight()  { g_light  = tmo::next(tmo::light(), g_light);   save(kLightKey, g_light);   Serial.printf("[cfg] лёгкий сон %s\n", lightLabel().c_str()); }
void nextDeep()   { g_deep   = tmo::next(tmo::deep(), g_deep);     save(kDeepKey, g_deep);     Serial.printf("[cfg] глубокий сон %s\n", deepLabel().c_str()); }
void nextBack()   { g_back   = tmo::next(tmo::back(), g_back);     save(kBackKey, g_back);     Serial.printf("[cfg] назад %s\n", backLabel().c_str()); }
void nextReset()  { g_reset  = tmo::next(tmo::reset(), g_reset);   save(kResetKey, g_reset);   Serial.printf("[cfg] сброс сессии %s\n", resetLabel().c_str()); }

uint32_t screenMs() { return tmo::ms(tmo::screen(), g_screen); }
uint32_t wifiMs()   { return tmo::ms(tmo::wifi(), g_wifi); }
uint32_t lightMs()  { return tmo::ms(tmo::light(), g_light); }
uint32_t deepMs()   { return tmo::ms(tmo::deep(), g_deep); }
uint32_t backMs()   { return tmo::ms(tmo::back(), g_back); }
uint32_t resetMs()  { return tmo::ms(tmo::reset(), g_reset); }

}  // namespace cfg
