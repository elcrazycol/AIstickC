// Base64 (RFC 4648). Заголовок-only, чтобы компилироваться и на ESP32,
// и на Mac без общего .cpp.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace b64 {

inline const char* alphabet() {
  return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

inline int decodeChar(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

inline size_t encodedSize(size_t n) { return ((n + 2) / 3) * 4; }

inline size_t decodedSize(const char* s) {
  size_t len = 0;
  while (s[len] != 0) ++len;
  if (len == 0) return 0;
  size_t pad = 0;
  if (len >= 1 && s[len - 1] == '=') ++pad;
  if (len >= 2 && s[len - 2] == '=') ++pad;
  return (len / 4) * 3 - pad;
}

inline void encode(const uint8_t* data, size_t n, std::string& out) {
  out.clear();
  out.reserve(encodedSize(n));
  const char* a = alphabet();
  size_t i = 0;
  for (; i + 3 <= n; i += 3) {
    uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) |
                 (uint32_t)data[i + 2];
    out.push_back(a[(v >> 18) & 0x3F]);
    out.push_back(a[(v >> 12) & 0x3F]);
    out.push_back(a[(v >> 6) & 0x3F]);
    out.push_back(a[v & 0x3F]);
  }
  const size_t rem = n - i;
  if (rem == 1) {
    uint32_t v = (uint32_t)data[i] << 16;
    out.push_back(a[(v >> 18) & 0x3F]);
    out.push_back(a[(v >> 12) & 0x3F]);
    out.push_back('=');
    out.push_back('=');
  } else if (rem == 2) {
    uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    out.push_back(a[(v >> 18) & 0x3F]);
    out.push_back(a[(v >> 12) & 0x3F]);
    out.push_back(a[(v >> 6) & 0x3F]);
    out.push_back('=');
  }
}

inline std::string encodeStr(const uint8_t* data, size_t n) {
  std::string s;
  encode(data, n, s);
  return s;
}

inline std::vector<uint8_t> decode(const std::string& s) {
  std::vector<uint8_t> out;
  out.reserve(decodedSize(s.c_str()));
  uint32_t acc = 0;
  int bits = 0;
  for (char c : s) {
    if (c == '=') break;
    const int v = decodeChar(c);
    if (v < 0) continue;  // переносы строк и мусор пропускаем
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((uint8_t)((acc >> bits) & 0xFF));
    }
  }
  return out;
}

}  // namespace b64
