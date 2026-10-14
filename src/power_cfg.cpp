#include "power_cfg.h"

#include <Arduino.h>
#include <Preferences.h>

#include "pure/sleep_options.h"

namespace powercfg {

namespace {

constexpr const char* kKey = "sleep";

Preferences& prefs() {
  static Preferences p;
  static bool opened = false;
  if (!opened) {
    p.begin("aistick", false);
    opened = true;
  }
  return p;
}

size_t g_choice = sleepopt::kDefault;

}  // namespace

void begin() {
  const size_t stored = prefs().getUInt(kKey, sleepopt::kDefault);
  g_choice = sleepopt::valid(stored) ? stored : sleepopt::kDefault;
  if (g_choice != stored) {
    Serial.printf("[cfg] сон %u не из списка — вернул %s\n", (unsigned)stored,
                  sleepopt::label(g_choice));
    prefs().remove(kKey);
  }
  Serial.printf("[cfg] глубокий сон: %s\n", label().c_str());
}

void reset() {
  g_choice = sleepopt::kDefault;
  prefs().remove(kKey);
  Serial.printf("[cfg] глубокий сон: %s (сброшено)\n", label().c_str());
}

size_t choice() { return g_choice; }

std::string label() { return sleepopt::label(g_choice); }

void next() {
  g_choice = sleepopt::next(g_choice);
  prefs().putUInt(kKey, (uint32_t)g_choice);
  Serial.printf("[cfg] глубокий сон: %s\n", label().c_str());
}

uint32_t sleepMs() { return sleepopt::ms(g_choice); }

}  // namespace powercfg
