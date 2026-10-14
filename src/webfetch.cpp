#include "webfetch.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>

#include "config.h"
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

  // К прокси ходим тем же соединением, что и распознавание: хост тот же,
  // значит рукопожатие не нужно, и третья TLS-сессия не заводится — а на неё
  // уже не хватает непрерывной памяти (сессия LLM и распознавания держат
  // по ~40 КБ). К чужим хостам идём общим клиентом и после ответа рвём:
  // держать лишнюю сессию незачем.
  const bool toProxy =
      (host == PROXY_HOST && port == PROXY_PORT);
  WiFiClientSecure& c = toProxy ? net::proxyClient() : net::tlsClient();
  if (!c.connected()) {
    c.setTimeout(kHttpConnectMs);
    if (!c.connect(host.c_str(), port)) {
      // Частая причина отказа — не сеть, а нехватка непрерывной памяти под
      // новую TLS-сессию: скажем об этом прямо, иначе ищешь проблему не там.
      err = "нет связи с " + host;
      if (ESP.getMaxAllocHeap() < 60000) err += " (мало памяти под TLS)";
      return false;
    }
  }
  c.setTimeout(kHttpReadMs);

  std::string head = method + " " + path + " HTTP/1.1\r\nHost: " + host +
                     "\r\nUser-Agent: aistick/0.1\r\n";
  if (!extraHeader.empty()) head += extraHeader + "\r\n";
  // К прокси — keep-alive: соединение общее с распознаванием, рвать его
  // после каждого обращения к Tuya незачем. К чужим хостам — close.
  head += "Content-Type: application/json\r\nContent-Length: " +
          std::to_string(body.size()) + "\r\nConnection: " +
          (toProxy ? "keep-alive" : "close") + "\r\n\r\n";

  const uint32_t t0 = millis();
  if (!net::writeAll(c, (const uint8_t*)head.data(), head.size(), t0) ||
      (!body.empty() &&
       !net::writeAll(c, (const uint8_t*)body.data(), body.size(), t0))) {
    c.stop();
    err = "обрыв отправки";
    return false;
  }
  const String raw = net::readAll(c, t0, kMaxFetchBytes);
  if (!toProxy) c.stop();  // чужое соединение не держим

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
