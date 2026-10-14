// Погода: сборка русского текста из ответа wttr.in.
//
// wttr.in по формату "%l|%C|%t|%f|%w|%h" отдаёт строку вида
//     Москва|Patchy rain nearby|+10°C|+9°C|↓12km/h|85%
// Условие приходит по-английски (локализация у сервиса не работает), а
// стрелка ветра — символ, которого нет в нашем шрифте. Поэтому собираем
// ответ сами: переводим условие по ключевому слову и чистим ветер.
//
// Модуль чистый — проверяется на Mac.
#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace weather {

// Чистит название города из распознанной фразы: пробелы по краям, хвостовая
// пунктуация и ведущее «в»/«город». Без этого «в Москве» не находится, а
// «Симферополь!» уходит в ответ вместе с восклицательным знаком.
inline std::string cleanCity(const char* in) {
  if (in == nullptr) return std::string();
  std::string s(in);
  auto isSpace = [](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  };
  auto isPunct = [](char c) {
    return c == '!' || c == '?' || c == '.' || c == ',' || c == ';' ||
           c == ':' || c == '"' || c == '\'';
  };

  size_t b = 0;
  size_t e = s.size();
  while (b < e && isSpace(s[b])) ++b;
  while (e > b && (isSpace(s[e - 1]) || isPunct(s[e - 1]))) --e;
  s = s.substr(b, e - b);

  // Ведущее «в»/«во»/«город»/«г.» — убираем (регистр не важен).
  // Складываем и кириллицу: в UTF-8 «А»..«Я» — это 0xD0 0x90..0xAF,
  // «а»..«я» — 0xD0 0xB0..0xBF, разница во втором байте 0x20.
  auto lower = [](char c) -> char {
    const unsigned char u = (unsigned char)c;
    if (u >= 'A' && u <= 'Z') return (char)(u + 32);
    if (u >= 0x90 && u <= 0xAF) return (char)(u + 0x20);
    return c;
  };
  static const char* kPrefixes[] = {"в ", "во ", "город ", "г. ", "г "};
  for (const char* p : kPrefixes) {
    const size_t n = strlen(p);
    if (s.size() <= n) continue;
    bool match = true;
    for (size_t i = 0; i < n; ++i) {
      if (lower(s[i]) != lower(p[i])) { match = false; break; }
    }
    if (!match) continue;
    s = s.substr(n);
    while (!s.empty() && isSpace(s.front())) s.erase(0, 1);
    break;
  }
  return s;
}

// Английское условие wttr.in -> русское, по ключевому слову. Порядок важен:
// более точные случаи проверяются раньше. Неизвестное возвращаем как есть.
inline std::string conditionRu(const char* en) {
  if (en == nullptr) return std::string();
  std::string s(en);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  if (s.empty()) return s;

  // "местами", "слабый", "сильный" — приставки-уточнения.
  std::string prefix;
  std::string low;
  for (char c : s) low.push_back((char)((c >= 'A' && c <= 'Z') ? c + 32 : c));
  if (low.rfind("patchy", 0) == 0) prefix = "местами ";
  else if (low.rfind("light", 0) == 0) prefix = "слабый ";
  else if (low.rfind("heavy", 0) == 0) prefix = "сильный ";

  struct Pair { const char* key; const char* ru; };
  static const Pair kMap[] = {
      {"thunder", "гроза"},   {"blizzard", "метель"},
      {"sleet", "мокрый снег"}, {"drizzle", "морось"},
      {"snow", "снег"},       {"rain", "дождь"},
      {"fog", "туман"},       {"mist", "дымка"},
      {"overcast", "пасмурно"}, {"cloud", "облачно"},
      {"clear", "ясно"},      {"sunny", "солнечно"},
  };
  for (const Pair& p : kMap) {
    if (low.find(p.key) != std::string::npos) return prefix + p.ru;
  }
  return s;
}

// Ветер: "↓12km/h" -> "12 км/ч" (убираем символы, которых нет в шрифте).
inline std::string windRu(const char* raw) {
  std::string s;
  if (raw != nullptr) {
    for (const char* p = raw; *p != 0; ++p) {
      const unsigned char c = (unsigned char)*p;
      if (c < 0x80) s.push_back((char)c);  // стрелки и прочее — мимо
    }
  }
  const size_t k = s.find("km/h");
  if (k != std::string::npos) s.replace(k, 4, "км/ч");
  const size_t m = s.find("m/s");
  if (m != std::string::npos) s.replace(m, 3, "м/с");
  return s;
}

// Разбирает строку wttr.in и собирает русский ответ. false — полей не хватает.
inline bool compose(const char* raw, std::string& out) {
  out.clear();
  if (raw == nullptr) return false;

  std::vector<std::string> f;
  std::string cur;
  for (const char* p = raw; ; ++p) {
    if (*p == '|' || *p == 0) {
      f.push_back(cur);
      cur.clear();
      if (*p == 0) break;
      continue;
    }
    if (*p != '\r' && *p != '\n') cur.push_back(*p);
  }
  if (f.size() < 3) return false;

  for (std::string& s : f) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  }
  if (f[0].empty() || f[1].empty() || f[2].empty()) return false;

  out = f[0] + ": " + conditionRu(f[1].c_str()) + ", " + f[2];
  if (f.size() > 3 && !f[3].empty()) out += ", ощущается " + f[3];
  if (f.size() > 4 && !f[4].empty()) {
    const std::string w = windRu(f[4].c_str());
    if (!w.empty()) out += ", ветер " + w;
  }
  if (f.size() > 5 && !f[5].empty()) out += ", влажность " + f[5];
  return true;
}

}  // namespace weather
