// Репозитории плагинов: скачать манифесты, поставить/удалить плагин.
//
// Источники — адреса манифестов (index.txt), перечисленные в config.h
// через запятую. Манифесты кэшируются в LittleFS, поэтому без сети панель
// показывает последний удачный список.
//
// Файл плагина лежит рядом с манифестом, поэтому адрес скачивания —
// каталог манифеста + имя файла.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace repo {

struct Item {
  std::string name;
  std::string description;
  std::string params;
  std::string file;    // имя файла в репозитории
  std::string base;    // каталог, где лежит манифест (для скачивания)
  int version = 1;
};

// Обновляет список: тянет манифесты, кэширует, разбирает.
// Возвращает число доступных плагинов (0 — ни сети, ни кэша).
size_t refresh();

// Текущий список доступных (после refresh или из кэша).
const std::vector<Item>& available();

// Ищет плагин в списке доступных.
const Item* find(const char* name);

// Скачивает плагин в /plugins и запоминает версию. false — ошибка (err).
bool install(const char* name, std::string& err);

// Удаляет установленный плагин.
bool remove(const char* name);

// Установлен ли плагин (файл на месте).
bool installed(const char* name);

// Версия установленного плагина (0 — неизвестна).
int installedVersion(const char* name);

}  // namespace repo
