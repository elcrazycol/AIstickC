#include "pure_test.h"
#include "json_extract.h"
#include "llm_request.h"
#include "tool_call.h"

#include <string>
#include <vector>

static const toolx::Plugin kPlugins[] = {
    {"set_lamp", "Включить или выключить лампу на G26",
     "{\"type\":\"object\",\"properties\":{\"state\":{\"type\":\"string\","
     "\"enum\":[\"on\",\"off\",\"toggle\"]}},\"required\":[\"state\"]}",
     toolx::Kind::Gpio, 26, nullptr, false},
    {"show_text", "Показать текст на экране стика",
     "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},"
     "\"required\":[\"text\"]}",
     toolx::Kind::Screen, -1, nullptr, true},
};

static void test_tools_shape() {
  const std::string t = toolx::buildTools(kPlugins, 2);
  CHECK_EQ(t.front(), '[');
  CHECK_EQ(t.back(), ']');
  CHECK(t.find("\"type\":\"function\"") != std::string::npos);
  CHECK(t.find("\"name\":\"set_lamp\"") != std::string::npos);
  CHECK(t.find("\"name\":\"show_text\"") != std::string::npos);
  CHECK(t.find("\"parameters\":{\"type\":\"object\"") != std::string::npos);
  // Между плагинами ровно одна запятая верхнего уровня.
  CHECK(t.find("}},") != std::string::npos);
}

static void test_tools_empty() {
  CHECK_STR_EQ(toolx::buildTools(nullptr, 0), "");
  CHECK_STR_EQ(toolx::buildTools(kPlugins, 0), "");
}

static void test_delta_name_and_args() {
  const std::string first =
      R"({"choices":[{"index":0,"delta":{"role":"assistant","content":"","tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"set_lamp","arguments":""}}]},"finish_reason":null}]})";
  std::string name, args;
  CHECK(toolx::deltaName(first.c_str(), name));
  CHECK_STR_EQ(name, "set_lamp");
  CHECK(toolx::deltaArgs(first.c_str(), args));
  CHECK_STR_EQ(args, "");

  const std::string piece =
      R"({"choices":[{"index":0,"delta":{"content":"","tool_calls":[{"index":0,"function":{"arguments":"{\"state\""}}]}}]})";
  CHECK(toolx::deltaArgs(piece.c_str(), args));
  CHECK_STR_EQ(args, "{\"state\"");
}

// Как в реальном стриме: arguments приходят посимвольно — склеиваем.
static void test_arguments_accumulate() {
  const char* pieces[] = {
      R"({"choices":[{"delta":{"tool_calls":[{"function":{"arguments":"{"}}]}}]})",
      R"({"choices":[{"delta":{"tool_calls":[{"function":{"arguments":"\"state\""}}]}}]})",
      R"({"choices":[{"delta":{"tool_calls":[{"function":{"arguments":":"}}]}}]})",
      R"({"choices":[{"delta":{"tool_calls":[{"function":{"arguments":" \"on\""}}]}}]})",
      R"({"choices":[{"delta":{"tool_calls":[{"function":{"arguments":"}"}}]}}]})",
  };
  std::string acc, part;
  for (const char* p : pieces) {
    if (toolx::deltaArgs(p, part)) acc += part;
  }
  CHECK_STR_EQ(acc, "{\"state\": \"on\"}");

  std::string state;
  CHECK(toolx::argString(acc.c_str(), "state", state));
  CHECK_STR_EQ(state, "on");
}

static void test_delta_without_tool_calls() {
  const std::string plain =
      R"({"choices":[{"delta":{"content":"привет"}}]})";
  std::string out;
  CHECK(!toolx::deltaName(plain.c_str(), out));
  CHECK(!toolx::deltaArgs(plain.c_str(), out));
}

static void test_arg_missing_key() {
  std::string out;
  CHECK(!toolx::argString(R"({"state":"on"})", "text", out));
}

static void test_request_carries_tools() {
  const std::vector<llmreq::Msg> msgs = {{"user", "включи лампу"}};
  const std::string tools = toolx::buildTools(kPlugins, 2);
  const std::string body =
      llmreq::buildChat("m", msgs, 100, true, "none", tools.c_str());
  CHECK(body.find("\"tools\":[{\"type\":\"function\"") != std::string::npos);
  CHECK(body.find("\"tool_choice\":\"auto\"") != std::string::npos);
  CHECK(body.find("\"name\":\"set_lamp\"") != std::string::npos);
  // Без tools поля нет.
  const std::string plain = llmreq::buildChat("m", msgs, 100, true, "none");
  CHECK(plain.find("\"tools\"") == std::string::npos);
}

static void test_delta_id() {
  const std::string first =
      R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_abc","type":"function","function":{"name":"set_lamp","arguments":""}}]}}]})";
  std::string id;
  CHECK(toolx::deltaId(first.c_str(), id));
  CHECK_STR_EQ(id, "call_abc");
}

static void test_followup_body() {
  const std::vector<llmreq::Msg> msgs = {{"system", "s"}, {"user", "включи лампу"}};
  const std::string body = llmreq::buildToolFollowUp(
      "m", msgs, "call_1", "set_lamp", "{\"state\": \"on\"}", "G26 вкл", 200,
      true, "none");
  CHECK(body.find("\"role\":\"assistant\"") != std::string::npos);
  CHECK(body.find("\"tool_calls\":[{\"id\":\"call_1\"") != std::string::npos);
  CHECK(body.find("\"role\":\"tool\"") != std::string::npos);
  CHECK(body.find("\"tool_call_id\":\"call_1\"") != std::string::npos);
  // Аргументы экранированы как строка JSON.
  CHECK(body.find("\"arguments\":\"{\\\"state\\\": \\\"on\\\"}\"") !=
        std::string::npos);
  CHECK(body.find("\"content\":\"G26 вкл\"") != std::string::npos);
  CHECK(body.find("\"stream\":true") != std::string::npos);
}

int main() {
  test_tools_shape();
  test_tools_empty();
  test_delta_name_and_args();
  test_arguments_accumulate();
  test_delta_without_tool_calls();
  test_arg_missing_key();
  test_request_carries_tools();
  test_delta_id();
  test_followup_body();
  return puretest::summary("tool_call");
}
