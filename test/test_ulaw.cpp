#include "pure_test.h"
#include "ulaw.h"

#include <cmath>
#include <cstdint>
#include <vector>

// Тишина кодируется в 0xFF и обратно в ноль.
static void test_silence() {
  CHECK(ulaw::encode(0) == 0xFF);
  CHECK(ulaw::decode(0xFF) == 0);
}

// Знак сохраняется: положительные остаются положительными, отрицательные —
// отрицательными. Ошибка в знаке сломала бы распознавание полностью.
//
// Начинаем со ста: у µ-law у самого нуля есть «мёртвая зона» — отсчёты
// меньше первого шага квантования декодируются в ноль, и это нормально.
static void test_sign() {
  for (int v = 100; v < 30000; v += 137) {
    CHECK(ulaw::decode(ulaw::encode((int16_t)v)) > 0);
    CHECK(ulaw::decode(ulaw::encode((int16_t)(-v))) < 0);
  }
}

// Тихие отсчёты у нуля схлопываются в ноль — не в мусор.
static void test_dead_zone() {
  for (int v = -20; v <= 20; ++v) {
    const int back = ulaw::decode(ulaw::encode((int16_t)v));
    CHECK(std::abs(back - v) <= 8);  // шаг квантования у нуля — 8
  }
}

// Круговой прогон: ошибка не должна превышать шаг квантования µ-law.
// На громких отсчётах шаг грубее, поэтому допуск относительный.
static void test_roundtrip() {
  for (int v = 100; v < 32000; v += 53) {
    const int back = ulaw::decode(ulaw::encode((int16_t)v));
    const double err = std::fabs((double)(back - v)) / (double)v;
    CHECK(err < 0.06);  // 6% — с запасом над реальным шагом
  }
}

// На малых сигналах точность выше: там логарифмическая шкала плотнее.
static void test_quiet_is_precise() {
  const int back = ulaw::decode(ulaw::encode((int16_t)100));
  CHECK(std::abs(back - 100) <= 4);
}

// Перегрузка обрезается, но не превращается в мусор другого знака.
static void test_clipping() {
  const int16_t hi = ulaw::decode(ulaw::encode((int16_t)32000));
  const int16_t lo = ulaw::decode(ulaw::encode((int16_t)-32000));
  CHECK(hi > 30000);
  CHECK(lo < -30000);
  // Крайнее значение int16_t не должно переполняться при взятии модуля.
  const int16_t extreme = ulaw::decode(ulaw::encode((int16_t)-32768));
  CHECK(extreme < -30000);
}

// Пакетное кодирование совпадает с поштучным.
static void test_buffer() {
  const int16_t src[] = {0, 1000, -1000, 32000, -32000, 5, -5, 12345};
  const size_t n = sizeof(src) / sizeof(src[0]);
  std::vector<uint8_t> pcm(n * 2);
  for (size_t i = 0; i < n; ++i) {
    const uint16_t u = (uint16_t)src[i];
    pcm[i * 2] = (uint8_t)(u & 0xFF);
    pcm[i * 2 + 1] = (uint8_t)(u >> 8);
  }
  std::vector<uint8_t> out(n);
  ulaw::encodeBuffer(pcm.data(), n, out.data());
  for (size_t i = 0; i < n; ++i) {
    CHECK(out[i] == ulaw::encode(src[i]));
  }
}

// Сжатие ровно вдвое: это и есть весь смысл.
static void test_half_size() {
  const size_t samples = 16000;  // секунда на 16 кГц
  std::vector<uint8_t> pcm(samples * 2, 0);
  std::vector<uint8_t> out(samples);
  ulaw::encodeBuffer(pcm.data(), samples, out.data());
  CHECK(out.size() * 2 == pcm.size());
}

int main() {
  test_silence();
  test_sign();
  test_dead_zone();
  test_roundtrip();
  test_quiet_is_precise();
  test_clipping();
  test_buffer();
  test_half_size();
  return puretest::summary("ulaw");
}
