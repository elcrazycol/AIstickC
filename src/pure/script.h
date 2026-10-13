// Крошечный интерпретатор для плагинов.
//
// Плагин — это скрипт: данные и вызовы готовых функций. Скрипт не имеет
// доступа ни к чему, кроме того, что даёт хост (Host), поэтому скачанный
// плагин не может сделать ничего лишнего. Всё, что умеет язык:
//   - числа, строки, true/false, переменные;
//   - арифметика, сравнения, and/or/not, склейка строк через +;
//   - if / else, while (с жёстким лимитом шагов — от вечных циклов);
//   - вызовы встроенных функций хоста.
//
// Модуль чистый: ни Arduino, ни железа. Тестируется на Mac с пустышкой-хостом.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace script {

struct Value {
  enum class T { Num, Str, Bool } t = T::Num;
  double num = 0;
  std::string str;
  bool b = false;

  static Value ofNum(double v) { Value x; x.t = T::Num; x.num = v; return x; }
  static Value ofStr(const std::string& s) { Value x; x.t = T::Str; x.str = s; return x; }
  static Value ofBool(bool v) { Value x; x.t = T::Bool; x.b = v; return x; }

  bool truthy() const {
    switch (t) {
      case T::Num: return num != 0;
      case T::Bool: return b;
      default: return !str.empty();
    }
  }
  std::string toStr() const {
    if (t == T::Str) return str;
    if (t == T::Bool) return b ? "true" : "false";
    char buf[32];
    if (std::floor(num) == num && std::fabs(num) < 1e15) {
      std::snprintf(buf, sizeof(buf), "%lld", (long long)num);
    } else {
      std::snprintf(buf, sizeof(buf), "%g", num);
    }
    return buf;
  }
};

// Окружение: то, что умеет железо. Интерпретатор сам ничего не делает —
// только зовёт эти функции. Скачанный плагин ограничен их списком.
struct Host {
  virtual ~Host() {}
  // false — ошибка (неизвестная функция или отказ). err — описание.
  virtual bool call(const std::string& name, const std::vector<Value>& args,
                    Value& out, std::string& err) = 0;
};

class Interp {
 public:
  Interp(Host& host, std::map<std::string, Value>& vars, size_t limit = 5000)
      : host_(host), vars_(vars), limit_(limit) {}

  // Выполняет скрипт. Переменные-аргументы приходят через vars и могут
  // измениться. false — синтаксис или ошибка времени выполнения.
  bool run(const char* src, std::string& err) {
    s_ = (src != nullptr) ? src : "";
    i_ = 0;
    steps_ = 0;
    err.clear();
    while (true) {
      skipWs();
      if (eof()) return true;
      if (!statement(true, err)) return false;
    }
  }

 private:
  // --- лексер (потоковый: смотрим символы по мере разбора) ---
  const char* s_ = "";
  size_t i_ = 0;
  size_t steps_ = 0;
  Host& host_;
  std::map<std::string, Value>& vars_;
  size_t limit_;

  bool eof() const { return s_[i_] == 0; }
  char peek() const { return s_[i_]; }
  void skipWs() {
    while (true) {
      const char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i_; continue; }
      if (c == '#') {  // комментарий до конца строки
        while (s_[i_] != 0 && s_[i_] != '\n') ++i_;
        continue;
      }
      break;
    }
  }

  bool eat(char c) {
    if (s_[i_] != c) return false;
    ++i_;
    return true;
  }

  // Многосимвольный оператор: "==", "<=", "!=" и т.п.
  bool eatOp(const char* op) {
    const size_t n = std::string(op).size();
    if (std::string(s_ + i_, n) != op) return false;
    i_ += n;
    return true;
  }

  // Ключевое слово целиком: до него не буква/цифра/подчёркивание, после — тоже.
  bool eatKw(const char* kw) {
    const size_t n = std::string(kw).size();
    if (std::string(s_ + i_, n) != kw) return false;
    const char after = s_[i_ + n];
    const bool wordAfter = (after >= 'a' && after <= 'z') ||
                           (after >= 'A' && after <= 'Z') ||
                           (after >= '0' && after <= '9') || after == '_';
    if (wordAfter) return false;
    i_ += n;
    return true;
  }

  bool readIdent(std::string& out) {
    const char c = s_[i_];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
      return false;
    }
    out.clear();
    while (true) {
      const char d = s_[i_];
      if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') ||
          (d >= '0' && d <= '9') || d == '_') {
        out.push_back(d);
        ++i_;
        continue;
      }
      break;
    }
    return true;
  }

  bool readNumber(double& out) {
    const char c = s_[i_];
    if (!((c >= '0' && c <= '9') || (c == '.' && s_[i_ + 1] >= '0' &&
                                     s_[i_ + 1] <= '9'))) {
      return false;
    }
    char* end = nullptr;
    out = std::strtod(s_ + i_, &end);
    i_ = (size_t)(end - s_);
    return true;
  }

  bool readString(std::string& out) {
    if (s_[i_] != '"') return false;
    ++i_;
    out.clear();
    while (s_[i_] != 0 && s_[i_] != '"') {
      if (s_[i_] == '\\') {
        ++i_;
        const char e = s_[i_];
        switch (e) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          default: out.push_back(e); break;
        }
        ++i_;
        continue;
      }
      out.push_back(s_[i_++]);
    }
    if (s_[i_] != '"') return false;  // не закрылась
    ++i_;
    return true;
  }

  bool error(const std::string& msg, std::string& err) {
    if (err.empty()) err = msg;
    return false;
  }

  // --- выражения ---
  // exec=false — разбор без вычисления (пропуск ветки или цикла).
  Value expr(bool exec, bool& ok, std::string& err) { return orExpr(exec, ok, err); }

  Value orExpr(bool exec, bool& ok, std::string& err) {
    Value a = andExpr(exec, ok, err);
    while (ok) {
      skipWs();
      if (!eatKw("or")) break;
      Value b = andExpr(exec, ok, err);
      if (!ok) break;
      if (exec) a = Value::ofBool(a.truthy() || b.truthy());
    }
    return a;
  }

  Value andExpr(bool exec, bool& ok, std::string& err) {
    Value a = cmpExpr(exec, ok, err);
    while (ok) {
      skipWs();
      if (!eatKw("and")) break;
      Value b = cmpExpr(exec, ok, err);
      if (!ok) break;
      if (exec) a = Value::ofBool(a.truthy() && b.truthy());
    }
    return a;
  }

  Value cmpExpr(bool exec, bool& ok, std::string& err) {
    Value a = addExpr(exec, ok, err);
    while (ok) {
      skipWs();
      const char* op = nullptr;
      if (eatOp("==")) op = "==";
      else if (eatOp("!=")) op = "!=";
      else if (eatOp("<=")) op = "<=";
      else if (eatOp(">=")) op = ">=";
      else if (eat('<')) op = "<";
      else if (eat('>')) op = ">";
      if (op == nullptr) break;
      Value b = addExpr(exec, ok, err);
      if (!ok) break;
      if (!exec) continue;
      if (std::string(op) == "==" || std::string(op) == "!=") {
        bool eq;
        if (a.t == Value::T::Str || b.t == Value::T::Str) {
          eq = (a.toStr() == b.toStr());
        } else if (a.t == Value::T::Bool || b.t == Value::T::Bool) {
          eq = (a.truthy() == b.truthy());
        } else {
          eq = (a.num == b.num);
        }
        a = Value::ofBool(std::string(op) == "==" ? eq : !eq);
      } else {
        const double x = a.num, y = b.num;
        if (std::string(op) == "<") a = Value::ofBool(x < y);
        else if (std::string(op) == ">") a = Value::ofBool(x > y);
        else if (std::string(op) == "<=") a = Value::ofBool(x <= y);
        else a = Value::ofBool(x >= y);
      }
    }
    return a;
  }

  Value addExpr(bool exec, bool& ok, std::string& err) {
    Value a = mulExpr(exec, ok, err);
    while (ok) {
      skipWs();
      if (eat('+')) {
        Value b = mulExpr(exec, ok, err);
        if (!ok) break;
        if (!exec) continue;
        // Если хоть одна сторона строка — склеиваем строки.
        if (a.t == Value::T::Str || b.t == Value::T::Str) {
          a = Value::ofStr(a.toStr() + b.toStr());
        } else {
          a = Value::ofNum(a.num + b.num);
        }
      } else if (eat('-')) {
        Value b = mulExpr(exec, ok, err);
        if (!ok) break;
        if (exec) a = Value::ofNum(a.num - b.num);
      } else {
        break;
      }
    }
    return a;
  }

  Value mulExpr(bool exec, bool& ok, std::string& err) {
    Value a = unaryExpr(exec, ok, err);
    while (ok) {
      skipWs();
      if (eat('*')) {
        Value b = unaryExpr(exec, ok, err);
        if (!ok) break;
        if (exec) a = Value::ofNum(a.num * b.num);
      } else if (eat('/')) {
        Value b = unaryExpr(exec, ok, err);
        if (!ok) break;
        if (exec) {
          if (b.num == 0) { error("деление на ноль", err); ok = false; return Value(); }
          a = Value::ofNum(a.num / b.num);
        }
      } else if (eat('%')) {
        Value b = unaryExpr(exec, ok, err);
        if (!ok) break;
        if (exec) {
          if (b.num == 0) { error("деление на ноль", err); ok = false; return Value(); }
          a = Value::ofNum(std::fmod(a.num, b.num));
        }
      } else {
        break;
      }
    }
    return a;
  }

  Value unaryExpr(bool exec, bool& ok, std::string& err) {
    skipWs();
    if (eatKw("not")) {
      Value v = unaryExpr(exec, ok, err);
      return exec ? Value::ofBool(!v.truthy()) : v;
    }
    if (eat('-')) {
      Value v = unaryExpr(exec, ok, err);
      return exec ? Value::ofNum(-v.num) : v;
    }
    return primary(exec, ok, err);
  }

  Value primary(bool exec, bool& ok, std::string& err) {
    skipWs();
    ++steps_;
    if (steps_ > limit_) {
      error("слишком много шагов (защита от зависания)", err);
      ok = false;
      return Value();
    }

    // Скобки.
    if (eat('(')) {
      Value v = expr(exec, ok, err);
      if (!ok) return v;
      skipWs();
      if (!eat(')')) { error("ожидалась ')'", err); ok = false; }
      return v;
    }

    // Число.
    double n = 0;
    if (readNumber(n)) return Value::ofNum(n);

    // Строка.
    std::string str;
    if (readString(str)) return Value::ofStr(str);

    // Ключевые слова-литералы.
    if (eatKw("true")) return Value::ofBool(true);
    if (eatKw("false")) return Value::ofBool(false);

    // Идентификатор: переменная или вызов функции.
    std::string name;
    if (!readIdent(name)) {
      error(std::string("непонятный символ '") + peek() + "'", err);
      ok = false;
      return Value();
    }

    skipWs();
    if (eat('(')) {  // вызов функции
      std::vector<Value> args;
      skipWs();
      if (!eat(')')) {
        while (true) {
          Value a = expr(exec, ok, err);
          if (!ok) return a;
          args.push_back(a);
          skipWs();
          if (eat(',')) continue;
          if (eat(')')) break;
          error("ожидалась ',' или ')'", err);
          ok = false;
          return Value();
        }
      }
      if (!exec) return Value::ofNum(0);
      Value out;
      std::string cerr;
      if (!host_.call(name, args, out, cerr)) {
        error("функция " + name + ": " + cerr, err);
        ok = false;
        return Value();
      }
      return out;
    }

    // Переменная.
    if (!exec) return Value::ofNum(0);
    auto it = vars_.find(name);
    if (it == vars_.end()) {
      error("нет переменной " + name, err);
      ok = false;
      return Value();
    }
    return it->second;
  }

  // --- операторы ---
  bool block(bool exec, std::string& err) {
    skipWs();
    if (!eat('{')) return error("ожидалась '{'", err);
    while (true) {
      skipWs();
      if (eat('}')) return true;
      if (eof()) return error("не закрыт блок '}'", err);
      if (!statement(exec, err)) return false;
    }
  }

  bool statement(bool exec, std::string& err) { return statementImpl(exec, err); }

  bool statementImpl(bool exec, std::string& err) {
    skipWs();
    if (eof()) return true;

    if (eatKw("if")) {
      bool ok = true;
      Value cond = expr(exec, ok, err);  // в пропускаемой ветке не вычисляем
      if (!ok) return false;
      const bool take = cond.truthy();
      if (!ifBranch(take && exec, err)) return false;
      skipWs();
      if (eatKw("else")) {
        if (!ifBranch(!take && exec, err)) return false;
      }
      return true;
    }

    if (eatKw("while")) {
      const size_t start = i_;
      if (!exec) {
        bool ok = true;
        expr(false, ok, err);
        if (!ok) return false;
        return block(false, err);
      }
      while (true) {
        i_ = start;
        bool ok = true;
        Value cond = expr(true, ok, err);
        if (!ok) return false;
        if (!cond.truthy()) break;
        if (!block(true, err)) return false;
        if (++steps_ > limit_) return error("цикл не завершился", err);
      }
      // Пройти мимо всего оператора целиком, уже без выполнения.
      i_ = start;
      bool ok = true;
      expr(false, ok, err);
      if (!ok) return false;
      return block(false, err);
    }

    // Присваивание или выражение.
    skipWs();
    const size_t save = i_;
    std::string name;
    if (readIdent(name)) {
      const size_t afterIdent = i_;
      skipWs();
      if (eat('=')) {  // именно '=', не '=='
        if (!exec) {
          bool ok = true;
          expr(false, ok, err);
          return ok;
        }
        bool ok = true;
        Value v = expr(true, ok, err);
        if (!ok) return false;
        vars_[name] = v;
        return true;
      }
      i_ = save;
      (void)afterIdent;
    }

    bool ok = true;
    expr(exec, ok, err);
    return ok;
  }

  // Разбор ветки if/else: либо { ... }, либо один оператор.
  bool ifBranch(bool exec, std::string& err) {
    skipWs();
    if (peek() == '{') return block(exec, err);
    return statement(exec, err);
  }
};

}  // namespace script
