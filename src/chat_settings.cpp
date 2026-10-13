#include "chat_settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include "pure/chat_options.h"
#include "settings.h"

namespace chatcfg {

namespace {

constexpr const char* kTurnsKey  = "turns";
constexpr const char* kTokensKey = "tokens";

// Своя ручка NVS: в main.cpp уже есть одна для счётчика сообщений, ключи
// не пересекаются, а общая ручка связала бы модули зря.
Preferences& prefs() {
  static Preferences p;
  static bool opened = false;
  if (!opened) {
    p.begin("aistick", false);
    opened = true;
  }
  return p;
}

size_t g_turns  = 0;  // 0 — авто
int    g_tokens = 0;  // 0 — авто

}  // namespace

void begin() {
  g_turns  = prefs().getUInt(kTurnsKey, 0);
  g_tokens = prefs().getInt(kTokensKey, 0);

  const size_t t = g_turns;
  const int k = g_tokens;
  if (!chatopt::isValidTurns(g_turns)) {
    g_turns = 0;
    prefs().remove(kTurnsKey);
    Serial.printf("[cfg] память %u не из списка — вернул авто\n", (unsigned)t);
  }
  if (!chatopt::isValidTokens(g_tokens)) {
    g_tokens = 0;
    prefs().remove(kTokensKey);
    Serial.printf("[cfg] лимит %d не из списка — вернул авто\n", k);
  }

  Serial.printf("[cfg] память: %s, лимит ответа: %s\n", turnsLabel().c_str(),
                tokensLabel().c_str());
}

void reset() {
  g_turns = 0;
  g_tokens = 0;
  prefs().remove(kTurnsKey);
  prefs().remove(kTokensKey);
  Serial.println("[cfg] настройки сброшены в авто");
}

size_t turnsChoice() { return g_turns; }
int tokensChoice() { return g_tokens; }

std::string turnsLabel() {
  return g_turns == 0 ? std::string("авто") : std::to_string(g_turns);
}

std::string tokensLabel() {
  return g_tokens == 0 ? std::string("авто") : std::to_string(g_tokens);
}

void nextTurns() {
  g_turns = chatopt::nextStep(chatopt::kTurnSteps, chatopt::kTurnStepsCount,
                              g_turns);
  prefs().putUInt(kTurnsKey, (uint32_t)g_turns);
  Serial.printf("[cfg] память: %s\n", turnsLabel().c_str());
}

void nextTokens() {
  g_tokens = chatopt::nextStep(chatopt::kTokenSteps, chatopt::kTokenStepsCount,
                               g_tokens);
  prefs().putInt(kTokensKey, g_tokens);
  Serial.printf("[cfg] лимит ответа: %s\n", tokensLabel().c_str());
}

size_t effectiveTurns(const chat::History& h) {
  if (g_turns != 0) return g_turns;
  const chatopt::Stats st = chatopt::stats(h.messages());
  return chatopt::autoTurns(st.avgTurnChars, kChatMaxChars, kChatMaxTurns);
}

int effectiveTokens(const chat::History& h) {
  if (g_tokens != 0) return g_tokens;
  const chatopt::Stats st = chatopt::stats(h.messages());
  return chatopt::autoTokens(st.avgAnswerChars, kLlmMaxTokens);
}

}  // namespace chatcfg
