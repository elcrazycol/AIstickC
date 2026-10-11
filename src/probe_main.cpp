// Милестоун 0: проверить PSRAM и PDM-микрофон.
//
// Пишет kRecordMs секунд, затем стримит запись по Serial в base64.
// Скрипт tools/capture.py собирает из этого WAV.
//
// Протокол вывода:
//   заголовок:    "PROBE META <dataBytes> <sampleRate>\n"
//   строки base64: по 64 символа, разделитель '\n'
//   конец:         "PROBE END\n"

#include <Arduino.h>
#include <M5Unified.h>

#include "probe.h"
#include "pure/base64.h"
#include "pure/wav.h"

namespace {

uint8_t* g_audio = nullptr;   // буфер в PSRAM: 44 байта заголовка + аудио
int16_t  g_chunk[probe::kDmaChunkSamples];  // DRAM: DMA не пишет в PSRAM

// Печатает data в base64 строками по 64 символа.
// Возвращает число напечатанных символов, чтобы вызывающий знал,
// осталась ли незакрытая строка.
size_t emitBase64(const uint8_t* data, size_t n) {
  static std::string line;
  line.clear();
  b64::encode(data, n, line);
  for (size_t i = 0; i < line.size(); ++i) {
    Serial.write((uint8_t)line[i]);
    if ((i + 1) % 64 == 0) Serial.write('\n');
  }
  return line.size();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  auto cfg = M5.config();
  M5.begin(cfg);

  // --- Шаг 1: PSRAM ---
  const uint32_t freePsram = ESP.getFreePsram();
  Serial.printf("[probe] PSRAM найдена: %u байт свободно\n", freePsram);
  Serial.printf("[probe] Кучи всего: %u байт\n", ESP.getFreeHeap());
  if (freePsram == 0) {
    Serial.println("[probe] ОШИБКА: PSRAM нет. Проверь -DBOARD_HAS_PSRAM.");
    while (true) delay(1000);
  }

  // --- Шаг 2: буфер ---
  const size_t total = wav::totalSize(probe::kAudioBytes);
  g_audio = (uint8_t*)ps_malloc(total);
  if (g_audio == nullptr) {
    Serial.printf("[probe] ОШИБКА: не удалось выделить %u байт в PSRAM\n",
                  (unsigned)total);
    while (true) delay(1000);
  }
  Serial.printf("[probe] Буфер: %u байт в PSRAM\n", (unsigned)total);

  // --- Шаг 3: микрофон ---
  if (!M5.Mic.begin()) {
    Serial.println("[probe] ОШИБКА: M5.Mic.begin() не удался");
    while (true) delay(1000);
  }
  Serial.printf("[probe] Микрофон запущен, %u Гц\n", probe::kSampleRate);

  // --- Шаг 4: прогрев и сигнал ---
  // Первые миллисекунды PDM дают DC-ступеньку (почти без переходов через
  // ноль) — это не звук. Захватываем и выбрасываем.
  {
    const size_t warmSamples = (size_t)probe::kSampleRate *
                               probe::kWarmupMs / 1000;
    size_t got = 0;
    while (got < warmSamples) {
      const size_t want = (warmSamples - got < probe::kDmaChunkSamples)
                              ? (warmSamples - got)
                              : probe::kDmaChunkSamples;
      if (!M5.Mic.record(g_chunk, want, probe::kSampleRate)) { delay(1); continue; }
      while (M5.Mic.isRecording()) delay(1);
      got += want;
    }
    Serial.printf("[probe] Прогрев: %u мс отброшено\n", probe::kWarmupMs);
  }

  // Два коротких пика, чтобы было слышно, когда говорить.
  M5.Speaker.tone(2000, 120);
  delay(300);
  M5.Speaker.tone(2600, 120);
  delay(300);

  // --- Шаг 5: захват ---
  Serial.printf("[probe] Запись: %.1f с\n", probe::kRecordMs / 1000.0f);
  const uint32_t started = millis();
  const size_t totalSamples = probe::kAudioBytes / 2;
  size_t doneSamples = 0;

  while (doneSamples < totalSamples &&
         (millis() - started) < (probe::kRecordMs + 2000)) {
    const size_t remain = totalSamples - doneSamples;
    const size_t want = (remain < probe::kDmaChunkSamples)
                            ? remain
                            : probe::kDmaChunkSamples;
    // record() ждёт длину в СЭМПЛАХ (int16), не в байтах.
    if (!M5.Mic.record(g_chunk, want, probe::kSampleRate)) {
      delay(1);
      continue;
    }
    while (M5.Mic.isRecording()) delay(1);
    // DMA не пишет в PSRAM — копируем вручную.
    memcpy(g_audio + wav::headerSize() + doneSamples * 2, g_chunk, want * 2);
    doneSamples += want;
  }
  M5.Mic.end();

  const size_t filled = doneSamples * 2;
  const uint32_t elapsed = millis() - started;
  Serial.printf("[probe] Записано %u байт за %u мс\n",
                (unsigned)filled, elapsed);
  if (filled == 0) {
    Serial.println("[probe] ОШИБКА: ни одного байта не записано");
    while (true) delay(1000);
  }

  // --- Шаг 6: заголовок и вывод ---
  wav::writeHeader(g_audio, (uint32_t)filled, probe::kSampleRate);
  Serial.printf("PROBE META %u %u\n", (unsigned)filled, probe::kSampleRate);

  const uint32_t t0 = millis();
  for (size_t off = 0; off < filled; off += probe::kBase64Chunk) {
    const size_t n = (filled - off < probe::kBase64Chunk)
                         ? (filled - off)
                         : probe::kBase64Chunk;
    const size_t chars =
        emitBase64(g_audio + wav::headerSize() + off, n);
    // Если число символов не кратно 64, строка осталась незакрытой.
    if (chars % 64 != 0) Serial.write('\n');
  }
  Serial.println("PROBE END");
  Serial.printf("[probe] Отправлено %u байт, кодирование %u мс\n",
                (unsigned)filled, millis() - t0);
  Serial.println("[probe] Готово.");
}

void loop() {
  delay(1000);
}
