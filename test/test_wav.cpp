#include "pure_test.h"
#include "wav.h"

#include <cstring>

static void test_size_constants() {
  // 44 байта заголовка + данные
  CHECK_EQ(wav::headerSize(), 44u);
  CHECK_EQ(wav::totalSize(1000), 1044u);
  CHECK_EQ(wav::totalSize(0), 44u);
}

static void test_header_bytes_riff() {
  uint8_t buf[44];
  wav::writeHeader(buf, 1000);
  CHECK_EQ(std::memcmp(buf + 0, "RIFF", 4), 0);
  CHECK_EQ(std::memcmp(buf + 8, "WAVE", 4), 0);
  CHECK_EQ(std::memcmp(buf + 12, "fmt ", 4), 0);
  CHECK_EQ(std::memcmp(buf + 36, "data", 4), 0);
}

static void test_header_sizes_little_endian() {
  uint8_t buf[44];
  wav::writeHeader(buf, 1000);

  // ChunkSize = 36 + data, little-endian
  uint32_t chunk = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                   ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
  CHECK_EQ(chunk, 36u + 1000u);

  uint32_t data = (uint32_t)buf[40] | ((uint32_t)buf[41] << 8) |
                  ((uint32_t)buf[42] << 16) | ((uint32_t)buf[43] << 24);
  CHECK_EQ(data, 1000u);
}

static void test_header_pcm_format() {
  uint8_t buf[44];
  wav::writeHeader(buf, 16000);

  auto u16 = [&](int off) {
    return (uint16_t)((uint16_t)buf[off] | ((uint16_t)buf[off + 1] << 8));
  };
  auto u32 = [&](int off) {
    return (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
           ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24);
  };

  CHECK_EQ(u16(20), (uint16_t)1);        // PCM
  CHECK_EQ(u16(22), (uint16_t)1);        // моно
  CHECK_EQ(u32(24), 16000u);             // sample rate
  CHECK_EQ(u32(28), 16000u * 2);         // byte rate = rate * channels * 2
  CHECK_EQ(u16(32), (uint16_t)2);        // block align = channels * 2
  CHECK_EQ(u16(34), (uint16_t)16);       // bits per sample
}

static void test_real_capture_size() {
  // 8 секунд при 16 кГц моно 16 бит = 256000 байт данных
  const uint32_t audio = (uint32_t)16000 * 2 * 8;
  uint8_t buf[44];
  wav::writeHeader(buf, audio);
  auto u32 = [&](int off) {
    return (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
           ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24);
  };
  CHECK_EQ(audio, 256000u);
  CHECK_EQ(u32(40), 256000u);
  CHECK_EQ(u32(4), 36u + 256000u);
}

int main() {
  test_size_constants();
  test_header_bytes_riff();
  test_header_sizes_little_endian();
  test_header_pcm_format();
  test_real_capture_size();
  return puretest::summary("wav");
}
