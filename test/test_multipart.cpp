#include "pure_test.h"
#include "multipart.h"

#include <cstring>
#include <string>
#include <vector>

// Собирает тело целиком через чистые функции — то же, что делает
// MultipartStream, но без Arduino.
static std::string buildBody(const char* boundary, const uint8_t* data,
                             size_t dataLen) {
  std::vector<uint8_t> out(mp::totalSize(boundary, dataLen));
  size_t off = 0;
  off += mp::writeFileHeader(out.data() + off, boundary, "file", "audio.wav",
                             "audio/wav");
  std::memcpy(out.data() + off, data, dataLen);
  off += dataLen;
  off += mp::writeRaw(out.data() + off, "\r\n");
  off += mp::writeField(out.data() + off, boundary, "model",
                        "whisper-large-v3-turbo");
  off += mp::writeField(out.data() + off, boundary, "response_format", "json");
  off += mp::writeField(out.data() + off, boundary, "language", "ru");
  off += mp::writeClosing(out.data() + off, boundary);
  return std::string((const char*)out.data(), off);
}

static void test_total_size_matches_written() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = "AUDIO";
  std::string body = buildBody(b, payload, 5);
  CHECK_EQ(body.size(), mp::totalSize(b, 5));
}

static void test_opens_with_boundary() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = "AUDIO";
  std::string body = buildBody(b, payload, 5);
  CHECK_EQ(body.compare(0, 2, "--"), 0);
  CHECK(body.find(std::string("--") + b + "\r\n") == 0);
}

static void test_file_part_headers() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = "AUDIO";
  std::string body = buildBody(b, payload, 5);
  CHECK(body.find("Content-Disposition: form-data; name=\"file\"; "
                  "filename=\"audio.wav\"\r\n") != std::string::npos);
  CHECK(body.find("Content-Type: audio/wav\r\n\r\n") != std::string::npos);
}

static void test_audio_bytes_verbatim() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = {0x00, 0xFF, 0x7F, 0x80, 0x0A, 0x0D};
  std::string body = buildBody(b, payload, sizeof(payload));
  // Байты не должны быть экранированы или изменены.
  CHECK(body.find(std::string((const char*)payload, sizeof(payload))) !=
        std::string::npos);
}

static void test_fields_present() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = "AUDIO";
  std::string body = buildBody(b, payload, 5);
  CHECK(body.find("name=\"model\"\r\n\r\nwhisper-large-v3-turbo\r\n") !=
        std::string::npos);
  CHECK(body.find("name=\"response_format\"\r\n\r\njson\r\n") !=
        std::string::npos);
  CHECK(body.find("name=\"language\"\r\n\r\nru\r\n") != std::string::npos);
}

static void test_closing_boundary_has_dashes() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = "AUDIO";
  std::string body = buildBody(b, payload, 5);
  std::string closing = std::string("--") + b + "--\r\n";
  CHECK(body.size() >= closing.size());
  CHECK_EQ(body.compare(body.size() - closing.size(), closing.size(), closing),
           0);
}

static void test_empty_audio_still_valid() {
  const char* b = "----aistickBoundary";
  std::string body = buildBody(b, (const uint8_t*)"", 0);
  CHECK_EQ(body.size(), mp::totalSize(b, 0));
  CHECK(body.find("--") == 0);
  CHECK(body.size() > 44);
}

// MultipartStream отдаёт тело тремя отрезками: голова, аудио, хвост.
// Проверяем, что такая нарезка даёт ровно те же байты, что линейная сборка.
// Это единственный способ протестировать адаптер без Arduino.
static void test_segmented_matches_linear() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = {0x52, 0x49, 0x46, 0x46, 0x00, 0xFF, 0x80};
  const size_t payloadLen = sizeof(payload);

  // Голова
  const size_t headLen =
      mp::fileHeaderSize(b, "file", "audio.wav", "audio/wav");
  std::vector<uint8_t> head(headLen);
  CHECK_EQ(mp::writeFileHeader(head.data(), b, "file", "audio.wav",
                               "audio/wav"),
           headLen);

  // Хвост: CRLF + три поля + закрытие
  const size_t tailLen =
      2 + mp::fieldSize(b, "model", "whisper-large-v3-turbo") +
      mp::fieldSize(b, "response_format", "json") +
      mp::fieldSize(b, "language", "ru") + mp::closingSize(b);
  std::vector<uint8_t> tail(tailLen);
  size_t tn = 0;
  tn += mp::writeRaw(tail.data() + tn, "\r\n");
  tn += mp::writeField(tail.data() + tn, b, "model",
                       "whisper-large-v3-turbo");
  tn += mp::writeField(tail.data() + tn, b, "response_format", "json");
  tn += mp::writeField(tail.data() + tn, b, "language", "ru");
  tn += mp::writeClosing(tail.data() + tn, b);
  CHECK_EQ(tn, tailLen);

  // Склейка трёх отрезков
  std::string segmented;
  segmented.append((const char*)head.data(), headLen);
  segmented.append((const char*)payload, payloadLen);
  segmented.append((const char*)tail.data(), tailLen);

  // Должно совпасть с линейной сборкой побайтово.
  std::string linear = buildBody(b, payload, payloadLen);
  CHECK_EQ(segmented.size(), linear.size());
  CHECK(segmented == linear);
  CHECK_EQ(segmented.size(), mp::totalSize(b, payloadLen));
}

int main() {
  test_total_size_matches_written();
  test_opens_with_boundary();
  test_file_part_headers();
  test_audio_bytes_verbatim();
  test_fields_present();
  test_closing_boundary_has_dashes();
  test_empty_audio_still_valid();
  test_segmented_matches_linear();
  return puretest::summary("multipart");
}
