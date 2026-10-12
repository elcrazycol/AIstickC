#include "pure_test.h"
#include "sse.h"

#include <string>
#include <vector>

// Собирает все полезные нагрузки из потока целиком.
static std::vector<std::string> all(const std::string& stream) {
  sse::Parser p;
  p.feed(stream);
  std::vector<std::string> out;
  std::string s;
  while (p.next(s)) out.push_back(s);
  return out;
}

static void test_single_event() {
  auto v = all("data: hello\n\n");
  CHECK_EQ(v.size(), (size_t)1);
  CHECK_STR_EQ(v[0], "hello");
}

static void test_multiple_events() {
  auto v = all("data: one\n\ndata: two\n\ndata: [DONE]\n\n");
  CHECK_EQ(v.size(), (size_t)3);
  CHECK_STR_EQ(v[0], "one");
  CHECK_STR_EQ(v[1], "two");
  CHECK_STR_EQ(v[2], "[DONE]");
}

static void test_crlf() {
  auto v = all("data: json\r\n\r\n");
  CHECK_EQ(v.size(), (size_t)1);
  CHECK_STR_EQ(v[0], "json");
}

static void test_no_space_after_colon() {
  auto v = all("data:{\"a\":1}\n");
  CHECK_EQ(v.size(), (size_t)1);
  CHECK_STR_EQ(v[0], "{\"a\":1}");
}

static void test_service_lines_skipped() {
  auto v = all(": comment\nevent: message\nid: 7\ndata: real\n\n1a\r\n");
  CHECK_EQ(v.size(), (size_t)1);
  CHECK_STR_EQ(v[0], "real");
}

static void test_split_across_feeds() {
  sse::Parser p;
  std::string s;
  p.feed("da");
  CHECK(!p.next(s));            // строка не собралась
  p.feed("ta: half");
  CHECK(!p.next(s));            // нет перевода строки
  p.feed("-done\n");
  CHECK(p.next(s));
  CHECK_STR_EQ(s, "half-done");
  CHECK(!p.next(s));
}

static void test_incomplete_line_not_emitted() {
  sse::Parser p;
  p.feed("data: incomplete");
  std::string s;
  CHECK(!p.next(s));            // нет '\n' — отдавать нельзя
  p.feed("\n");
  CHECK(p.next(s));
  CHECK_STR_EQ(s, "incomplete");
}

static void test_empty_payload() {
  auto v = all("data:\n");
  CHECK_EQ(v.size(), (size_t)1);
  CHECK_STR_EQ(v[0], "");
}

static void test_blank_lines_skipped() {
  auto v = all("\n\n\ndata: x\n\n");
  CHECK_EQ(v.size(), (size_t)1);
  CHECK_STR_EQ(v[0], "x");
}

static void test_json_payload_kept_verbatim() {
  const std::string json =
      "{\"choices\":[{\"delta\":{\"content\":\"При\"}}]}";
  auto v = all("data: " + json + "\n\n");
  CHECK_EQ(v.size(), (size_t)1);
  CHECK_STR_EQ(v[0], json);
}

static void test_clear() {
  sse::Parser p;
  p.feed("data: x\n");
  p.clear();
  std::string s;
  CHECK(!p.next(s));
}

int main() {
  test_single_event();
  test_multiple_events();
  test_crlf();
  test_no_space_after_colon();
  test_service_lines_skipped();
  test_split_across_feeds();
  test_incomplete_line_not_emitted();
  test_empty_payload();
  test_blank_lines_skipped();
  test_json_payload_kept_verbatim();
  test_clear();
  return puretest::summary("sse");
}
