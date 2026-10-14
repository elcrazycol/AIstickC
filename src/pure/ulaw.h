// G.711 µ-law: 8 бит вместо 16 на отсчёт.
//
// Для речи это штатный формат телефонии: логарифмическая шкала даёт почти
// такое же качество, как 16 бит линейно, но байт вдвое меньше — значит и
// отправка на распознавание вдвое быстрее.
//
// Модуль чистый: ни Arduino, ни сети, проверяется обычными тестами.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ulaw {

// Порог, за которым отсчёт уже не различим на слух; стандарт G.711.
constexpr int kClip = 32635;
constexpr int kBias = 0x84;

inline uint8_t encode(int16_t sample) {
  int s = sample;
  int sign = 0;
  if (s < 0) {
    sign = 0x80;
    s = -s;  // в int, а не в int16_t: -(-32768) иначе переполнится
  }
  if (s > kClip) s = kClip;
  s += kBias;

  int exponent = 7;
  for (int mask = 0x4000; (s & mask) == 0 && exponent > 0; mask >>= 1) {
    --exponent;
  }
  const int mantissa = (s >> (exponent + 3)) & 0x0F;
  return (uint8_t)~(sign | (exponent << 4) | mantissa);
}

inline int16_t decode(uint8_t u) {
  const int v = ~u & 0xFF;
  int t = ((v & 0x0F) << 3) + kBias;
  t <<= (v & 0x70) >> 4;
  return (int16_t)((v & 0x80) ? (kBias - t) : (t - kBias));
}

// Сжимает count отсчётов: на входе 16-битный PCM (little-endian), на выходе
// count байт µ-law.
inline void encodeBuffer(const uint8_t* pcm, size_t count, uint8_t* out) {
  for (size_t i = 0; i < count; ++i) {
    const int16_t s = (int16_t)((uint16_t)pcm[i * 2] |
                                ((uint16_t)pcm[i * 2 + 1] << 8));
    out[i] = encode(s);
  }
}

}  // namespace ulaw
