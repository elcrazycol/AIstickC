#include "pure_test.h"
#include "plugin_repo.h"

#include <string>
#include <vector>

static const char* kManifest =
    "# Репозиторий плагинов\n"
    "\n"
    "name = switch\n"
    "description = Включить или выключить выключатель # сбоку\n"
    "params = channel=1|2|3|all, state*=on|off|toggle\n"
    "file = switch.ais\n"
    "version = 3\n"
    "\n"
    "name = weather\n"
    "description = Погода на сегодня\n"
    "file = weather.ais\n";

static void test_two_entries() {
  std::vector<plugrepo::Entry> e;
  CHECK_EQ(plugrepo::parse(kManifest, e), (size_t)2);
  CHECK_EQ(e.size(), (size_t)2);

  CHECK_STR_EQ(e[0].name, "switch");
  CHECK_STR_EQ(e[0].description, "Включить или выключить выключатель");
  CHECK_STR_EQ(e[0].params, "channel=1|2|3|all, state*=on|off|toggle");
  CHECK_STR_EQ(e[0].file, "switch.ais");
  CHECK_EQ(e[0].version, 3);

  CHECK_STR_EQ(e[1].name, "weather");
  CHECK_STR_EQ(e[1].file, "weather.ais");
  CHECK_EQ(e[1].version, 1);  // по умолчанию
}

static void test_entry_without_file_skipped() {
  std::vector<plugrepo::Entry> e;
  const char* text =
      "name = a\ndescription = без файла\nversion = 1\n\n"
      "name = b\nfile = b.ais\n";
  CHECK_EQ(plugrepo::parse(text, e), (size_t)1);
  CHECK_STR_EQ(e[0].name, "b");
}

static void test_entry_without_name_skipped() {
  std::vector<plugrepo::Entry> e;
  const char* text = "file = a.ais\n\nname = b\nfile = b.ais\n";
  CHECK_EQ(plugrepo::parse(text, e), (size_t)1);
  CHECK_STR_EQ(e[0].name, "b");
}

static void test_last_block_without_blank_line() {
  std::vector<plugrepo::Entry> e;
  CHECK_EQ(plugrepo::parse("name = a\nfile = a.ais\nversion = 2\n", e), (size_t)1);
  CHECK_EQ(e[0].version, 2);
}

static void test_empty_and_null() {
  std::vector<plugrepo::Entry> e;
  CHECK_EQ(plugrepo::parse("", e), (size_t)0);
  CHECK_EQ(plugrepo::parse("\n\n", e), (size_t)0);
  CHECK_EQ(plugrepo::parse(nullptr, e), (size_t)0);
}

static void test_crlf() {
  std::vector<plugrepo::Entry> e;
  CHECK_EQ(plugrepo::parse("name = a\r\nfile = a.ais\r\n\r\n", e), (size_t)1);
  CHECK_STR_EQ(e[0].file, "a.ais");
}

int main() {
  test_two_entries();
  test_entry_without_file_skipped();
  test_entry_without_name_skipped();
  test_last_block_without_blank_line();
  test_empty_and_null();
  test_crlf();
  return puretest::summary("plugin_repo");
}
