// Общие определения прошивки милестоуна 0.
#pragma once

#include <cstdint>
#include <cstddef>

namespace probe {

constexpr uint32_t kSampleRate   = 16000;
constexpr uint32_t kRecordMs     = 5000;   // 5 секунд: хватает сказать фразу
constexpr uint32_t kWarmupMs     = 200;    // отбросить переходный процесс PDM
constexpr size_t   kAudioBytes   = (size_t)kSampleRate * 2 * (kRecordMs / 1000);
// == 160000 байт аудио, 160044 байта с заголовком
constexpr size_t   kDmaChunkSamples = 4096;  // 8192 байта в DRAM
constexpr size_t   kBase64Chunk  = 768;    // кратно 3: без набивки в потоке

}  // namespace probe
