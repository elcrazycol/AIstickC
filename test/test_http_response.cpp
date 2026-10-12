#include "pure_test.h"
#include "http_response.h"

#include <string>
#include <vector>

static void test_parse_status_ok() {
  CHECK_EQ(httpr::parseStatus("HTTP/1.1 200 OK"), 200);
  CHECK_EQ(httpr::parseStatus("HTTP/1.0 404 Not Found"), 404);
  CHECK_EQ(httpr::parseStatus("HTTP/1.1 500 Internal Server Error"), 500);
}

static void test_parse_status_bad() {
  CHECK_EQ(httpr::parseStatus(""), -1);
  CHECK_EQ(httpr::parseStatus("HTTP/1.1"), -1);
  CHECK_EQ(httpr::parseStatus("мусор"), -1);
  // Двузначный код — не HTTP-статус
  CHECK_EQ(httpr::parseStatus("HTTP/1.1 20 OK"), -1);
}

static void test_split_head_body() {
  std::string raw = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                    "{\"text\":\"привет\"}";
  std::string head, body;
  httpr::splitHeadBody(raw, head, body);
  CHECK_STR_EQ(body, "{\"text\":\"привет\"}");
  CHECK(head.find("Content-Type") != std::string::npos);
  CHECK(head.find("200 OK") != std::string::npos);
}

static void test_split_no_separator() {
  std::string head, body;
  httpr::splitHeadBody("HTTP/1.1 200 OK", head, body);
  CHECK_STR_EQ(head, "");
  CHECK_STR_EQ(body, "HTTP/1.1 200 OK");
}

static void test_is_chunked() {
  CHECK(httpr::isChunked("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked"));
  CHECK(httpr::isChunked("transfer-encoding: CHUNKED"));
  CHECK(!httpr::isChunked("HTTP/1.1 200 OK\r\nContent-Length: 42"));
  CHECK(!httpr::isChunked(""));
}

static void test_dechunk_single() {
  // Один кусок "7" = 7 байт
  std::string body = "7\r\n{\"text\"\r\n0\r\n\r\n";
  CHECK_STR_EQ(httpr::dechunk(body), "{\"text\"");
}

static void test_dechunk_multiple() {
  std::string body = "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
  CHECK_STR_EQ(httpr::dechunk(body), "hello world");
}

// Собирает chunked-тело из кусков, считая размеры в байтах сам.
// Так тест не может разойтись с реальностью из-за ручного пересчёта
// многобайтных символов.
static std::string chunked(const std::vector<std::string>& parts) {
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (const std::string& p : parts) {
    size_t n = p.size();
    std::string size;
    if (n == 0) size = "0";
    while (n > 0) { size.insert(size.begin(), hex[n & 0xF]); n >>= 4; }
    out += size + "\r\n" + p + "\r\n";
  }
  out += "0\r\n\r\n";
  return out;
}

static void test_dechunk_json_split() {
  // Реальный случай: JSON разрезан на несколько кусков, причём разрез
  // попадает внутрь кириллицы.
  const std::string whole = "{\"text\":\"при\"}";
  const std::string first = "{\"text\":\"пр";
  const std::string second = whole.substr(first.size());
  CHECK_STR_EQ(httpr::dechunk(chunked({first, second})), whole);
}

static void test_dechunk_truncated_returns_partial() {
  // Обрыв: заявлено 10 байт, пришло 3 — отдаём что есть, не падаем.
  std::string body = "a\r\nabc";
  CHECK_STR_EQ(httpr::dechunk(body), "abc");
}

static void test_dechunk_empty() {
  CHECK_STR_EQ(httpr::dechunk(""), "");
  CHECK_STR_EQ(httpr::dechunk("0\r\n\r\n"), "");
}

int main() {
  test_parse_status_ok();
  test_parse_status_bad();
  test_split_head_body();
  test_split_no_separator();
  test_is_chunked();
  test_dechunk_single();
  test_dechunk_multiple();
  test_dechunk_json_split();
  test_dechunk_truncated_returns_partial();
  test_dechunk_empty();
  return puretest::summary("http_response");
}
