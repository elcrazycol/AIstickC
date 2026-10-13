#include "pure_test.h"
#include "weather_text.h"

#include <cstring>
#include <string>

static void test_condition_mapping() {
  CHECK_STR_EQ(weather::conditionRu("Patchy rain nearby"), "местами дождь");
  CHECK_STR_EQ(weather::conditionRu("Partly Cloudy "), "облачно");
  CHECK_STR_EQ(weather::conditionRu("Sunny"), "солнечно");
  CHECK_STR_EQ(weather::conditionRu("Clear"), "ясно");
  CHECK_STR_EQ(weather::conditionRu("Overcast"), "пасмурно");
  CHECK_STR_EQ(weather::conditionRu("Light drizzle"), "слабый морось");
  CHECK_STR_EQ(weather::conditionRu("Heavy snow"), "сильный снег");
  CHECK_STR_EQ(weather::conditionRu("Thundery outbreaks possible"), "гроза");
  CHECK_STR_EQ(weather::conditionRu("Blizzard"), "метель");
  CHECK_STR_EQ(weather::conditionRu("Light sleet"), "слабый мокрый снег");
  CHECK_STR_EQ(weather::conditionRu("Mist"), "дымка");
  CHECK_STR_EQ(weather::conditionRu("Fog"), "туман");
}

static void test_condition_unknown_kept() {
  CHECK_STR_EQ(weather::conditionRu("Weird weather"), "Weird weather");
  CHECK_STR_EQ(weather::conditionRu(""), "");
}

static void test_wind() {
  CHECK_STR_EQ(weather::windRu("↓12km/h"), "12км/ч");
  CHECK_STR_EQ(weather::windRu("↑17km/h"), "17км/ч");
  CHECK_STR_EQ(weather::windRu("→16km/h"), "16км/ч");
  CHECK_STR_EQ(weather::windRu("5m/s"), "5м/с");
  CHECK_STR_EQ(weather::windRu(""), "");
}

static void test_compose_full() {
  std::string out;
  CHECK(weather::compose("Москва|Patchy rain nearby|+10°C|+9°C|↓12km/h|85%", out));
  CHECK_STR_EQ(out,
               "Москва: местами дождь, +10°C, ощущается +9°C, ветер 12км/ч, "
               "влажность 85%");
}

static void test_compose_minimal() {
  std::string out;
  CHECK(weather::compose("Kazan|Partly Cloudy |+19°C", out));
  CHECK_STR_EQ(out, "Kazan: облачно, +19°C");
}

static void test_compose_bad() {
  std::string out;
  CHECK(!weather::compose("", out));
  CHECK(!weather::compose("Москва|дождь", out));      // мало полей
  CHECK(!weather::compose("||+10°C", out));           // пустые поля
  CHECK(!weather::compose(nullptr, out));
}

static void test_compose_trims_and_keeps_cyrillic_city() {
  std::string out;
  CHECK(weather::compose("Новосибирск|Patchy rain nearby|+6°C|+3°C|→16km/h|61%",
                         out));
  CHECK_STR_EQ(out,
               "Новосибирск: местами дождь, +6°C, ощущается +3°C, ветер 16км/ч, "
               "влажность 61%");
}

static void test_clean_city() {
  CHECK_STR_EQ(weather::cleanCity(" Симферополь!"), "Симферополь");
  CHECK_STR_EQ(weather::cleanCity("в Москве"), "Москве");
  CHECK_STR_EQ(weather::cleanCity("В Москве"), "Москве");
  CHECK_STR_EQ(weather::cleanCity("город Казань"), "Казань");
  CHECK_STR_EQ(weather::cleanCity("Москва."), "Москва");
  CHECK_STR_EQ(weather::cleanCity("  Питер  "), "Питер");
  CHECK_STR_EQ(weather::cleanCity("Сочи?"), "Сочи");
  CHECK_STR_EQ(weather::cleanCity(""), "");
  CHECK_STR_EQ(weather::cleanCity(nullptr), "");
  // Город, начинающийся на «в», не должен пострадать.
  CHECK_STR_EQ(weather::cleanCity("Воронеж"), "Воронеж");
}

int main() {
  test_condition_mapping();
  test_condition_unknown_kept();
  test_wind();
  test_compose_full();
  test_compose_minimal();
  test_compose_bad();
  test_compose_trims_and_keeps_cyrillic_city();
  test_clean_city();
  return puretest::summary("weather_text");
}
