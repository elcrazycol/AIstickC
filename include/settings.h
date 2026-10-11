// Настройки, которые не являются секретами и коммитятся.
#pragma once

#include <cstdint>
#include <cstddef>

// --- Аудио ---
constexpr int      kSampleRate      = 16000;   // Гц, моно, 16 бит
constexpr uint32_t kMaxRecordMs     = 8000;    // максимум записи, мс
constexpr uint32_t kMinRecordMs     = 300;     // короче — отбрасываем
constexpr size_t   kDmaChunkBytes   = 8192;    // кусок захвата в DRAM
constexpr size_t   kAudioBytes      = (size_t)kSampleRate * 2 *
                                      (kMaxRecordMs / 1000);
// kAudioBytes = 256000 при 8 с

// --- Кнопки ---
constexpr uint32_t kLongPressMs     = 800;     // порог «долгого» BtnB

// --- Экран ---
constexpr int      kScreenRotation  = 1;       // ландшафт: 1 или 3
constexpr int      kScreenW         = 240;
constexpr int      kScreenH         = 135;

// --- Прочее ---
constexpr uint32_t kSerialBaud      = 115200;
