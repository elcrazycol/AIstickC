#include "chat_settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include "pure/chat_options.h"
#include "settings.h"

namespace chatcfg {

namespace {

constexpr const char* kLengthKey = "len";
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

size_t g_length = chatopt::kDefaultLength;
size_t g_turns  = 0;  // 0 — авто
int    g_tokens = 0;  // 0 — авто

}  // namespace

void begin() {
  g_length = prefs().getUInt(kLengthKey, chatopt::kDefaultLength);
  g_turns  = prefs().getUInt(kTurnsKey, 0);
  g_tokens = prefs().getInt(kTokensKey, 0);

  // Всё, что не из списка, — от прошлой прошивки или из чужой сборки.
  // Молча возвращаем заводское, чтобы настройка не оказалась мусором.
  if (g_length >= chatopt::kLengthsCount) {
    Serial.printf("[cfg] длина %u не из списка — вернул кратко\n",
                  (unsigned)g_length);
    g_length = chatopt::kDefaultLength;
    prefs().remove(kLengthKey);
  }
  if (!chatopt::isValidTurns(g_turns)) {
    Serial.printf("[cfg] память %u не из списка — вернула авто\n",
                  (unsigned)g_turns);
    g_turns = 0;
    prefs().remove(kTurnsKey);
  }
  if (!chatopt::isValidTokens(g_tokens)) {
    Serial.printf("[cfg] лимит %d не из списка — вернул авто\n", g_tokens);
    g_tokens = 0;
    prefs().remove(kTokensKey);
  }

  Serial.printf("[cfg] ответ: %s, память: %s, лимит: %s\n",
                lengthLabel().c_str(), turnsLabel().c_str(),
                tokensLabel().c_str());
}

void reset() {
  g_length = chatopt::kDefaultLength;
  g_turns = 0;
  g_tokens = 0;
  prefs().remove(kLengthKey);
  prefs().remove(kTurnsKey);
  prefs().remove(kTokensKey);
  Serial.println("[cfg] настройки сброшены к заводским");
}

size_t lengthChoice() { return g_length; }
size_t turnsChoice() { return g_turns; }
int tokensChoice() { return g_tokens; }

std::string lengthLabel() { return chatopt::lengthLabel(g_length); }

std::string turnsLabel() {
  return g_turns == 0 ? std::string("авто") : std::to_string(g_turns);
}

std::string tokensLabel() {
  if (g_tokens != 0) return std::to_string(g_tokens);
  // «Авто» — это не пустое место, а конкретное число, вытекающее из длины
  // ответа. Показываем его, чтобы было видно, что именно уйдёт в запрос.
  return "авто " + std::to_string(chatopt::lengthMaxTokens(g_length));
}

void nextLength() {
  g_length = chatopt::nextLength(g_length);
  prefs().putUInt(kLengthKey, (uint32_t)g_length);
  Serial.printf("[cfg] длина ответа: %s\n", lengthLabel().c_str());
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
  Serial.printf("[cfg] лимит: %s\n", tokensLabel().c_str());
}

std::string systemPrompt() {
  std::string s = kLlmRole;
  s += " ";
  s += chatopt::lengthAsk(g_length);
  s += " ";
  s += kLlmToolRule;
  return s;
}

size_t effectiveTurns(const chat::History& h) {
  if (g_turns != 0) return g_turns;
  const chatopt::Stats st = chatopt::stats(h.messages());
  return chatopt::autoTurns(st.avgTurnChars, kChatMaxChars, kChatMaxTurns);
}

int effectiveTokens() {
  if (g_tokens != 0) return g_tokens;
  return chatopt::lengthMaxTokens(g_length);
}

}  // namespace chatcfg
