#include "pure_test.h"
#include "base64.h"

#include <vector>
#include <string>
#include <cstdint>

static void test_encode_known() {
  // Эталоны из RFC 4648, раздел 10.
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"", 0), "");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"f", 1), "Zg==");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"fo", 2), "Zm8=");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"foo", 3), "Zm9v");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"foob", 4), "Zm9vYg==");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"fooba", 5), "Zm9vYmE=");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"foobar", 6), "Zm9vYmFy");
}

static void test_encode_no_padding_when_multiple_of_three() {
  // Кусок в 768 байт (кратно 3) даёт ровно 1024 символа без '='.
  std::vector<uint8_t> data(768, 0xAB);
  std::string enc = b64::encodeStr(data.data(), data.size());
  CHECK_EQ(enc.size(), (size_t)1024);
  CHECK(enc.find('=') == std::string::npos);
}

static void test_decode_roundtrip() {
  std::vector<uint8_t> data(1000);
  for (size_t i = 0; i < data.size(); ++i) data[i] = (uint8_t)(i * 7 + 3);
  std::string enc = b64::encodeStr(data.data(), data.size());
  std::vector<uint8_t> back = b64::decode(enc);
  CHECK_EQ(back.size(), data.size());
  CHECK(back == data);
}

static void test_decoded_size() {
  CHECK_EQ(b64::decodedSize("Zg=="), (size_t)1);
  CHECK_EQ(b64::decodedSize("Zm8="), (size_t)2);
  CHECK_EQ(b64::decodedSize("Zm9v"), (size_t)3);
  CHECK_EQ(b64::decodedSize(""), (size_t)0);
}

int main() {
  test_encode_known();
  test_encode_no_padding_when_multiple_of_three();
  test_decode_roundtrip();
  test_decoded_size();
  return puretest::summary("base64");
}
