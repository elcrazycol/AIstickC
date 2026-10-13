// HTTPS-запросы для плагинов и репозиториев.
//
// Один общий путь вместо нескольких копий: кириллица в пути кодируется,
// тело ответа отдаётся вызывающему, код ответа проверяется.
#pragma once

#include <string>

namespace webfetch {

// method — "GET" или "POST". body — тело (может быть пустым).
// extraHeader — дополнительная строка заголовка целиком (например,
// "X-Auth: ..."), может быть пустой. out — тело ответа. false — ошибка.
bool request(const std::string& method, const std::string& url,
             const std::string& body, const std::string& extraHeader,
             std::string& out, std::string& err);

// Удобная обёртка для GET.
bool get(const std::string& url, std::string& out, std::string& err);

}  // namespace webfetch
