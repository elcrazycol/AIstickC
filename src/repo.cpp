#include "repo.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "settings.h"
#include "webfetch.h"
#include "pure/plugin_repo.h"

namespace repo {

namespace {

constexpr const char* kPluginsDir = "/plugins";
constexpr const char* kCacheDir = "/repo";

std::vector<Item> g_items;

// Адреса манифестов из config.h: "url1,url2".
std::vector<std::string> sources() {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    size_t b = 0, e = cur.size();
    while (b < e && (cur[b] == ' ' || cur[b] == '\t')) ++b;
    while (e > b && (cur[e - 1] == ' ' || cur[e - 1] == '\t' ||
                     cur[e - 1] == '\r' || cur[e - 1] == '\n')) --e;
    const std::string s = cur.substr(b, e - b);
    if (!s.empty()) out.push_back(s);
    cur.clear();
  };
  const char* p = PLUGIN_REPOS;
  for (; p != nullptr && *p != 0; ++p) {
    if (*p == ',') { flush(); continue; }
    cur.push_back(*p);
  }
  flush();
  return out;
}

// Каталог манифеста: всё до последнего слэша.
std::string dirOf(const std::string& url) {
  const size_t slash = url.rfind('/');
  return (slash == std::string::npos) ? url : url.substr(0, slash + 1);
}

std::string pluginPath(const char* name) {
  return std::string(kPluginsDir) + "/" + name + ".ais";
}

std::string cachePath(size_t i) {
  return std::string(kCacheDir) + "/" + std::to_string(i) + ".txt";
}

std::string readFile(const std::string& path) {
  File f = LittleFS.open(path.c_str(), "r");
  if (!f) return std::string();
  const String s = f.readString();
  f.close();
  return std::string(s.c_str());
}

bool writeFile(const std::string& path, const std::string& data) {
  File f = LittleFS.open(path.c_str(), "w");
  if (!f) return false;
  const size_t n = f.print(data.c_str());
  f.close();
  return n == data.size();
}

// Версии установленных плагинов: "имя версия" построчно.
std::string versionsPath() {
  return std::string(kPluginsDir) + "/versions.txt";
}

void saveVersion(const char* name, int version) {
  std::string text = readFile(versionsPath());
  std::string out;
  bool replaced = false;
  size_t i = 0;
  while (i < text.size()) {
    size_t end = text.find('\n', i);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(i, end - i);
    i = end + 1;
    if (line.empty()) continue;
    const size_t sp = line.find(' ');
    const std::string nm = (sp == std::string::npos) ? line : line.substr(0, sp);
    if (nm == name) {
      out += std::string(name) + " " + std::to_string(version) + "\n";
      replaced = true;
    } else {
      out += line + "\n";
    }
  }
  if (!replaced) out += std::string(name) + " " + std::to_string(version) + "\n";
  writeFile(versionsPath(), out);
}

void dropVersion(const char* name) {
  std::string text = readFile(versionsPath());
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    size_t end = text.find('\n', i);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(i, end - i);
    i = end + 1;
    if (line.empty()) continue;
    const size_t sp = line.find(' ');
    const std::string nm = (sp == std::string::npos) ? line : line.substr(0, sp);
    if (nm != name) out += line + "\n";
  }
  writeFile(versionsPath(), out);
}

}  // namespace

size_t refresh() {
  g_items.clear();
  const std::vector<std::string> src = sources();

  for (size_t i = 0; i < src.size(); ++i) {
    std::string text;
    std::string err;
    const bool online = webfetch::get(src[i], text, err);
    if (online && !text.empty()) {
      LittleFS.mkdir(kCacheDir);
      writeFile(cachePath(i), text);
      Serial.printf("[repo] %s: %u байт\n", src[i].c_str(), (unsigned)text.size());
    } else {
      text = readFile(cachePath(i));
      if (text.empty()) {
        Serial.printf("[repo] %s: нет (%s)\n", src[i].c_str(), err.c_str());
        continue;
      }
      Serial.printf("[repo] %s: из кэша\n", src[i].c_str());
    }

    std::vector<plugrepo::Entry> entries;
    plugrepo::parse(text.c_str(), entries);
    const std::string base = dirOf(src[i]);
    for (const plugrepo::Entry& e : entries) {
      Item it;
      it.name = e.name;
      it.description = e.description;
      it.params = e.params;
      it.file = e.file;
      it.base = base;
      it.version = e.version;
      g_items.push_back(it);
    }
  }
  Serial.printf("[repo] доступно плагинов: %u\n", (unsigned)g_items.size());
  return g_items.size();
}

const std::vector<Item>& available() { return g_items; }

const Item* find(const char* name) {
  if (name == nullptr) return nullptr;
  for (const Item& it : g_items) {
    if (it.name == name) return &it;
  }
  return nullptr;
}

bool install(const char* name, std::string& err) {
  const Item* it = find(name);
  if (it == nullptr) { err = "нет в репозитории"; return false; }

  std::string body;
  if (!webfetch::get(it->base + it->file, body, err)) return false;
  if (body.empty()) { err = "пустой файл"; return false; }

  LittleFS.mkdir(kPluginsDir);
  if (!writeFile(pluginPath(name), body)) {
    err = "не записать файл";
    return false;
  }
  saveVersion(name, it->version);
  Serial.printf("[repo] поставлен %s v%d\n", name, it->version);
  return true;
}

bool remove(const char* name) {
  if (name == nullptr) return false;
  const bool ok = LittleFS.remove(pluginPath(name).c_str());
  dropVersion(name);
  Serial.printf("[repo] удалён %s (%s)\n", name, ok ? "ок" : "не было");
  return ok;
}

bool installed(const char* name) {
  if (name == nullptr) return false;
  return LittleFS.exists(pluginPath(name).c_str());
}

int installedVersion(const char* name) {
  if (name == nullptr) return 0;
  const std::string text = readFile(versionsPath());
  size_t i = 0;
  while (i < text.size()) {
    size_t end = text.find('\n', i);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(i, end - i);
    i = end + 1;
    const size_t sp = line.find(' ');
    if (sp != std::string::npos && line.substr(0, sp) == name) {
      return atoi(line.substr(sp + 1).c_str());
    }
  }
  return 0;
}

}  // namespace repo
