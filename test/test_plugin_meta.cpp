#include "pure_test.h"
#include "plugin_meta.h"

#include <string>

static void test_full_header() {
  const char* src =
      "# name: switch\n"
      "# description: Включить или выключить\n"
      "# params: channel=1|2|3|all, state*=on|off|toggle\n"
      "\n"
      "body = \"x\"\n";
  plugmeta::Meta m;
  CHECK(plugmeta::parse(src, m));
  CHECK_STR_EQ(m.name, "switch");
  CHECK_STR_EQ(m.description, "Включить или выключить");
  CHECK_STR_EQ(m.params, "channel=1|2|3|all, state*=on|off|toggle");
  CHECK_STR_EQ(
      m.schema,
      "{\"type\":\"object\",\"properties\":{\"channel\":{\"type\":\"string\","
      "\"enum\":[\"1\",\"2\",\"3\",\"all\"]},\"state\":{\"type\":\"string\","
      "\"enum\":[\"on\",\"off\",\"toggle\"]}},\"required\":[\"state\"]}");
}

static void test_no_params() {
  plugmeta::Meta m;
  CHECK(plugmeta::parse("# name: ping\n# description: просто так\nx = 1\n", m));
  CHECK_STR_EQ(m.schema, "{\"type\":\"object\",\"properties\":{}}");
}

static void test_param_without_enum() {
  plugmeta::Meta m;
  CHECK(plugmeta::parse("# name: p\n# params: text\nx = 1\n", m));
  CHECK_STR_EQ(m.schema,
               "{\"type\":\"object\",\"properties\":{\"text\":"
               "{\"type\":\"string\"}}}");
}

static void test_required_param() {
  plugmeta::Meta m;
  CHECK(plugmeta::parse("# name: p\n# params: a*, b\nx = 1\n", m));
  CHECK_STR_EQ(m.schema,
               "{\"type\":\"object\",\"properties\":{\"a\":{\"type\":\"string\"},"
               "\"b\":{\"type\":\"string\"}},\"required\":[\"a\"]}");
}

static void test_missing_name_fails() {
  plugmeta::Meta m;
  CHECK(!plugmeta::parse("# description: без имени\nx = 1\n", m));
  CHECK(!plugmeta::parse(nullptr, m));
  CHECK(!plugmeta::parse("", m));
}

static void test_header_ends_at_code() {
  // '# name:' внутри тела скрипта не должно попадать в метаданные.
  const char* src =
      "# name: real\n"
      "x = 1\n"
      "# name: fake\n";
  plugmeta::Meta m;
  CHECK(plugmeta::parse(src, m));
  CHECK_STR_EQ(m.name, "real");
}

static void test_crlf_and_spaces() {
  plugmeta::Meta m;
  CHECK(plugmeta::parse("#   name :   spaced\r\n# description:  d \r\nx = 1\r\n", m));
  CHECK_STR_EQ(m.name, "spaced");
  CHECK_STR_EQ(m.description, "d");
}

static void test_empty_params_list() {
  plugmeta::Meta m;
  CHECK(plugmeta::parse("# name: p\n# params: \nx = 1\n", m));
  CHECK_STR_EQ(m.schema, "{\"type\":\"object\",\"properties\":{}}");
}

int main() {
  test_full_header();
  test_no_params();
  test_param_without_enum();
  test_required_param();
  test_missing_name_fails();
  test_header_ends_at_code();
  test_crlf_and_spaces();
  test_empty_params_list();
  return puretest::summary("plugin_meta");
}
