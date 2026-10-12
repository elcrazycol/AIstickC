#include "pure_test.h"
#include "chunked.h"

#include <string>

static std::string drain(chunked::Decoder& d) {
  std::string all, piece;
  while (d.next(piece)) all += piece;
  return all;
}

static void test_single_chunk() {
  chunked::Decoder d;
  d.feed("5\r\nhello\r\n0\r\n\r\n");
  CHECK_STR_EQ(drain(d), "hello");
  CHECK(d.done());
}

static void test_multiple_chunks() {
  chunked::Decoder d;
  d.feed("5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
  CHECK_STR_EQ(drain(d), "hello world");
  CHECK(d.done());
}

static void test_hex_sizes() {
  // 0x1a = 26 байт.
  const std::string data = "abcdefghijklmnopqrstuvwxyz";
  chunked::Decoder d;
  d.feed("1a\r\n" + data + "\r\n0\r\n\r\n");
  CHECK_STR_EQ(drain(d), data);
  CHECK(d.done());
}

static void test_split_across_feeds() {
  chunked::Decoder d;
  d.feed("5\r\nhel");
  CHECK_STR_EQ(drain(d), "");       // чанк ещё не полон
  CHECK(!d.done());
  d.feed("lo\r\n");
  CHECK_STR_EQ(drain(d), "hello");
  CHECK(!d.done());                 // терминатор ещё не пришёл
  d.feed("0\r\n\r\n");
  CHECK(d.done());
}

static void test_size_line_split() {
  chunked::Decoder d;
  d.feed("5");
  CHECK_STR_EQ(drain(d), "");
  d.feed("\r\nhello\r\n0\r\n\r\n");
  CHECK_STR_EQ(drain(d), "hello");
  CHECK(d.done());
}

static void test_extensions_ignored() {
  chunked::Decoder d;
  d.feed("5;name=value\r\nhello\r\n0\r\n\r\n");
  CHECK_STR_EQ(drain(d), "hello");
  CHECK(d.done());
}

static void test_zero_chunk_ends() {
  chunked::Decoder d;
  d.feed("0\r\n\r\n");
  CHECK_STR_EQ(drain(d), "");
  CHECK(d.done());
}

static void test_binary_kept_verbatim() {
  const std::string bin("\x00\xFF\x7F\x80\x0A\x0D", 6);
  chunked::Decoder d;
  d.feed("6\r\n" + bin + "\r\n0\r\n\r\n");
  CHECK_STR_EQ(drain(d), bin);
}

static void test_garbage_ends_stream() {
  chunked::Decoder d;
  d.feed("не размер\r\n");
  CHECK_STR_EQ(drain(d), "");
  CHECK(d.done());
}

static void test_incremental_release() {
  // Выдача идёт по мере готовности чанков, а не только в конце.
  chunked::Decoder d;
  d.feed("3\r\nabc\r\n");
  CHECK_STR_EQ(drain(d), "abc");
  d.feed("3\r\ndef\r\n0\r\n\r\n");
  CHECK_STR_EQ(drain(d), "def");
  CHECK(d.done());
}

static void test_reset() {
  chunked::Decoder d;
  d.feed("3\r\nabc\r\n0\r\n\r\n");
  drain(d);
  d.reset();
  CHECK(!d.done());
  d.feed("2\r\nhi\r\n0\r\n\r\n");
  CHECK_STR_EQ(drain(d), "hi");
}

int main() {
  test_single_chunk();
  test_multiple_chunks();
  test_hex_sizes();
  test_split_across_feeds();
  test_size_line_split();
  test_extensions_ignored();
  test_zero_chunk_ends();
  test_binary_kept_verbatim();
  test_garbage_ends_stream();
  test_incremental_release();
  test_reset();
  return puretest::summary("chunked");
}
