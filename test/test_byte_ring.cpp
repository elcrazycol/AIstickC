#include "pure_test.h"
#include "byte_ring.h"

#include <cstdint>
#include <vector>

// Пустой буфер ничего не отдаёт.
static void test_empty() {
  uint8_t mem[16];
  ring::ByteRing r(mem, sizeof(mem));
  CHECK(r.size() == 0);
  CHECK(r.space() == 16);
  uint8_t out[4];
  CHECK(r.read(out, sizeof(out)) == 0);
}

// Записали — прочитали то же самое, в том же порядке.
static void test_order() {
  uint8_t mem[8];
  ring::ByteRing r(mem, sizeof(mem));
  const uint8_t in[] = {1, 2, 3, 4, 5};
  CHECK(r.write(in, sizeof(in)) == sizeof(in));
  CHECK(r.size() == 5);
  uint8_t out[5];
  CHECK(r.read(out, sizeof(out)) == sizeof(out));
  for (size_t i = 0; i < sizeof(in); ++i) CHECK(out[i] == in[i]);
  CHECK(r.size() == 0);
}

// Переполнение: лишнее не влезает и не портит уже записанное.
static void test_overflow() {
  uint8_t mem[4];
  ring::ByteRing r(mem, sizeof(mem));
  const uint8_t in[] = {1, 2, 3, 4, 5, 6};
  CHECK(r.write(in, sizeof(in)) == 4);  // влезло только четыре
  CHECK(r.size() == 4);
  uint8_t out[4];
  CHECK(r.read(out, sizeof(out)) == 4);
  CHECK(out[0] == 1);
  CHECK(out[3] == 4);
}

// Кольцо: пишем через край и читаем по кускам — данные не должны путаться.
static void test_wraparound() {
  uint8_t mem[8];
  ring::ByteRing r(mem, sizeof(mem));

  const uint8_t a[] = {1, 2, 3, 4, 5, 6};
  CHECK(r.write(a, sizeof(a)) == sizeof(a));
  uint8_t out[4];
  CHECK(r.read(out, 4) == 4);
  CHECK(out[0] == 1 && out[3] == 4);

  // Теперь свободно 6 байт, но хвост кольца в другом месте.
  const uint8_t b[] = {7, 8, 9, 10, 11, 12};
  CHECK(r.write(b, sizeof(b)) == sizeof(b));
  CHECK(r.size() == 8);
  uint8_t rest[8];
  CHECK(r.read(rest, sizeof(rest)) == 8);
  CHECK(rest[0] == 5);
  CHECK(rest[1] == 6);
  CHECK(rest[2] == 7);
  CHECK(rest[7] == 12);
}

// Много оборотов кольца: индексы растут, логика не ломается.
static void test_many_turns() {
  uint8_t mem[7];
  ring::ByteRing r(mem, sizeof(mem));
  uint8_t next = 0;
  for (int turn = 0; turn < 200; ++turn) {
    uint8_t in[5];
    for (size_t i = 0; i < sizeof(in); ++i) in[i] = next++;
    const size_t w = r.write(in, sizeof(in));
    uint8_t out[5];
    const size_t got = r.read(out, w);
    CHECK(got == w);
    for (size_t i = 0; i < got; ++i) CHECK(out[i] == in[i]);
  }
}

// Нулевая ёмкость не должна делить на ноль.
static void test_zero_capacity() {
  uint8_t mem[1];
  ring::ByteRing r(mem, 0);
  const uint8_t in[] = {1};
  CHECK(r.write(in, 1) == 0);
  uint8_t out[1];
  CHECK(r.read(out, 1) == 0);
}

int main() {
  test_empty();
  test_order();
  test_overflow();
  test_wraparound();
  test_many_turns();
  test_zero_capacity();
  return puretest::summary("byte_ring");
}
