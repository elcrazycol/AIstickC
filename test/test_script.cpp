#include "pure_test.h"
#include "script.h"

#include <map>
#include <string>
#include <vector>

// Хост-пустышка: то, что в прошивке делают железо и сеть.
struct MockHost : script::Host {
  std::vector<std::string> log;
  int calls = 0;

  bool call(const std::string& name, const std::vector<script::Value>& args,
            script::Value& out, std::string& err) override {
    ++calls;
    if (name == "log") {
      log.push_back(args.empty() ? "" : args[0].toStr());
      out = script::Value::ofNum(0);
      return true;
    }
    if (name == "add") {
      out = script::Value::ofNum(args[0].num + args[1].num);
      return true;
    }
    if (name == "cat") {
      out = script::Value::ofStr(args[0].toStr() + args[1].toStr());
      return true;
    }
    if (name == "boom") {
      err = "нельзя";
      return false;
    }
    err = "нет функции " + name;
    return false;
  }
};

static bool run(const char* src, MockHost& h, std::map<std::string, script::Value>& vars,
                std::string& err, size_t limit = 5000) {
  script::Interp in(h, vars, limit);
  return in.run(src, err);
}

static double num(const std::map<std::string, script::Value>& v, const char* k) {
  auto it = v.find(k);
  return it == v.end() ? -1 : it->second.num;
}

static void test_arithmetic_precedence() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  CHECK(run("x = 1 + 2 * 3\n", h, v, err));
  CHECK_EQ(num(v, "x"), 7.0);
  CHECK(run("y = (1 + 2) * 3\n", h, v, err));
  CHECK_EQ(num(v, "y"), 9.0);
  CHECK(run("z = 10 % 3\n", h, v, err));
  CHECK_EQ(num(v, "z"), 1.0);
  CHECK(run("w = -5 + 2\n", h, v, err));
  CHECK_EQ(num(v, "w"), -3.0);
}

static void test_strings() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  CHECK(run("s = \"a\" + 1 + \"b\"\n", h, v, err));
  CHECK_STR_EQ(v["s"].toStr(), "a1b");
  CHECK(run("t = \"он сказал \\\"да\\\"\"\n", h, v, err));
  CHECK_STR_EQ(v["t"].toStr(), "он сказал \"да\"");
}

static void test_comparisons_and_logic() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  CHECK(run("a = 2 > 1 and 3 >= 3\n", h, v, err));
  CHECK(v["a"].truthy());
  CHECK(run("b = 1 == 2 or 5 != 6\n", h, v, err));
  CHECK(v["b"].truthy());
  CHECK(run("c = not (1 == 1)\n", h, v, err));
  CHECK(!v["c"].truthy());
  CHECK(run("d = \"abc\" == \"abc\"\n", h, v, err));
  CHECK(v["d"].truthy());
}

static void test_if_else() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  CHECK(run("x = 5\nif x > 3 { log(\"больше\") } else { log(\"меньше\") }\n",
            h, v, err));
  CHECK_EQ(h.log.size(), (size_t)1);
  CHECK_STR_EQ(h.log[0], "больше");

  h.log.clear();
  CHECK(run("x = 1\nif x > 3 { log(\"да\") } else { log(\"нет\") }\n", h, v, err));
  CHECK_STR_EQ(h.log[0], "нет");
}

static void test_else_if_chain() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  CHECK(run("x = 2\n"
            "if x == 1 { log(\"один\") }\n"
            "else if x == 2 { log(\"два\") }\n"
            "else { log(\"другое\") }\n",
            h, v, err));
  CHECK_EQ(h.log.size(), (size_t)1);
  CHECK_STR_EQ(h.log[0], "два");
}

static void test_skipped_branch_no_side_effects() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  CHECK(run("if false { log(\"нельзя\") }\nlog(\"можно\")\n", h, v, err));
  CHECK_EQ(h.log.size(), (size_t)1);
  CHECK_STR_EQ(h.log[0], "можно");
  // В пропущенной ветке не должно быть и ошибок про неизвестные переменные.
  h.log.clear();
  CHECK(run("if false { log(missing_variable) }\nlog(\"ок\")\n", h, v, err));
  CHECK_STR_EQ(h.log[0], "ок");
}

static void test_while() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  CHECK(run("i = 0\nn = 0\nwhile i < 3 { n = n + 1\ni = i + 1 }\n", h, v, err));
  CHECK_EQ(num(v, "n"), 3.0);
  CHECK_EQ(num(v, "i"), 3.0);
}

static void test_while_limit() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  CHECK(!run("while true { }\n", h, v, err, 200));
  CHECK(!err.empty());
}

static void test_host_calls() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  CHECK(run("log(\"привет\" + \" \" + \"мир\")\n", h, v, err));
  CHECK_STR_EQ(h.log[0], "привет мир");
  CHECK(run("s = add(2, 3)\n", h, v, err));
  CHECK_EQ(num(v, "s"), 5.0);
  CHECK(run("t = cat(\"a\", 7)\n", h, v, err));
  CHECK_STR_EQ(v["t"].toStr(), "a7");
}

static void test_input_variables() {
  MockHost h;
  std::map<std::string, script::Value> v;
  v["city"] = script::Value::ofStr("Москва");
  std::string err;
  CHECK(run("log(city)\nout = city + \"!\"\n", h, v, err));
  CHECK_STR_EQ(h.log[0], "Москва");
  CHECK_STR_EQ(v["out"].toStr(), "Москва!");
}

static void test_comments() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  CHECK(run("# это комментарий\nx = 1 # и это\n", h, v, err));
  CHECK_EQ(num(v, "x"), 1.0);
}

static void test_errors() {
  MockHost h;
  std::map<std::string, script::Value> v;
  std::string err;
  // Нет переменной.
  CHECK(!run("x = missing\n", h, v, err));
  CHECK(!err.empty());
  // Неизвестная функция.
  err.clear();
  CHECK(!run("x = nosuch(1)\n", h, v, err));
  // Функция отказала.
  err.clear();
  CHECK(!run("x = boom()\n", h, v, err));
  CHECK(err.find("нельзя") != std::string::npos);
  // Деление на ноль.
  err.clear();
  CHECK(!run("x = 1 / 0\n", h, v, err));
  // Незакрытый блок.
  err.clear();
  CHECK(!run("if true { log(\"a\")\n", h, v, err));
}

int main() {
  test_arithmetic_precedence();
  test_strings();
  test_comparisons_and_logic();
  test_if_else();
  test_else_if_chain();
  test_skipped_branch_no_side_effects();
  test_while();
  test_while_limit();
  test_host_calls();
  test_input_variables();
  test_comments();
  test_errors();
  return puretest::summary("script");
}
