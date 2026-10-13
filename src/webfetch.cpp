#include "webfetch.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>

#include "settings.h"
#include "net.h"
#include "pure/http_response.h"

namespace webfetch {

namespace {

// Кириллица и пробелы в пути -> %XX (для «wttr.in/Москва»).
std::string encodePath(const std::string& path) {
  std::string out;
  for (unsigned char c : path) {
    if (c >= 0x80 || c == ' ') {
      char b[8];
      snprintf(b, sizeof(b), "%%%02X", c);
      out += b;
    } else {
      out.push_back((char)c);
    }
  }
  return out;
}

}  // namespace

bool request(const std::string& method, const std::string& url,
             const std::string& body, const std::string& extraHeader,
             std::string& out, std::string& err) {
  out.clear();
  if (url.rfind("https://", 0) != 0) {
    err = "нужен https";
    return false;
  }
  const std::string rest = url.substr(8);
  const size_t slash = rest.find('/');
  std::string host = (slash == std::string::npos) ? rest : rest.substr(0, slash);
  const std::string path =
      encodePath((slash == std::string::npos) ? "/" : rest.substr(slash));
  if (host.empty()) { err = "нет хоста"; return false; }

  // Порт можно указать в URL: "host:9443".
  int port = 443;
  const size_t colon = host.find(':');
  if (colon != std::string::npos) {
    port = atoi(host.substr(colon + 1).c_str());
    host = host.substr(0, colon);
    if (port <= 0) port = 443;
  }

  WiFiClientSecure& c = net::tlsClient();
  c.setTimeout(kHttpConnectMs);
  if (!c.connect(host.c_str(), port)) {
    err = "нет связи с " + host;
    return false;
  }
  c.setTimeout(kHttpReadMs);

  std::string head = method + " " + path + " HTTP/1.1\r\nHost: " + host +
                     "\r\nUser-Agent: aistick/0.1\r\n";
  if (!extraHeader.empty()) head += extraHeader + "\r\n";
  head += "Content-Type: application/json\r\nContent-Length: " +
          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";

  const uint32_t t0 = millis();
  if (!net::writeAll(c, (const uint8_t*)head.data(), head.size(), t0) ||
      (!body.empty() &&
       !net::writeAll(c, (const uint8_t*)body.data(), body.size(), t0))) {
    c.stop();
    err = "обрыв отправки";
    return false;
  }
  const String raw = net::readAll(c, t0, kMaxFetchBytes);
  c.stop();

  const int code = httpr::parseStatus(raw.c_str());
  const int sep = raw.indexOf("\r\n\r\n");
  if (sep >= 0) out = std::string(raw.substring(sep + 4).c_str());
  if (code != 200) {
    err = "HTTP " + std::to_string(code);
    return false;
  }
  return true;
}

bool get(const std::string& url, std::string& out, std::string& err) {
  return request("GET", url, "", "", out, err);
}

}  // namespace webfetch
