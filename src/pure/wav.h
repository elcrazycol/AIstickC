// Сборка WAV-заголовка (RIFF/WAVE, PCM 16 бит).
// Заголовок пишется в начало буфера, аудио идёт с 44-го байта.
#pragma once

#include <cstdint>
#include <cstddef>

namespace wav {

constexpr size_t headerSize() { return 44; }
constexpr size_t totalSize(size_t dataBytes) { return headerSize() + dataBytes; }

inline void putU32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

inline void putU16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

inline void putTag(uint8_t* p, const char* tag) {
  p[0] = (uint8_t)tag[0];
  p[1] = (uint8_t)tag[1];
  p[2] = (uint8_t)tag[2];
  p[3] = (uint8_t)tag[3];
}

// Заголовок для dataBytes байт аудио при sampleRate и 1 канале, 16 бит.
inline void writeHeader(uint8_t* buf, uint32_t dataBytes,
                        uint32_t sampleRate = 16000) {
  const uint16_t channels = 1;
  const uint16_t bits = 16;
  const uint32_t byteRate = sampleRate * channels * (bits / 8);
  const uint16_t blockAlign = (uint16_t)(channels * (bits / 8));

  putTag(buf + 0, "RIFF");
  putU32(buf + 4, 36 + dataBytes);
  putTag(buf + 8, "WAVE");

  putTag(buf + 12, "fmt ");
  putU32(buf + 16, 16);            // размер fmt-блока
  putU16(buf + 20, 1);             // формат PCM
  putU16(buf + 22, channels);
  putU32(buf + 24, sampleRate);
  putU32(buf + 28, byteRate);
  putU16(buf + 32, blockAlign);
  putU16(buf + 34, bits);

  putTag(buf + 36, "data");
  putU32(buf + 40, dataBytes);
}

}  // namespace wav
