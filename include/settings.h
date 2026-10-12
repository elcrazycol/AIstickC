// Настройки, которые не являются секретами и коммитятся.
#pragma once

#include <cstdint>
#include <cstddef>

// --- Аудио ---
constexpr int      kSampleRate      = 16000;   // Гц, моно, 16 бит
constexpr uint32_t kMaxRecordMs     = 15000;   // максимум записи, мс
constexpr uint32_t kMinRecordMs     = 300;     // короче — отбрасываем
constexpr size_t   kDmaChunkBytes   = 8192;    // кусок захвата в DRAM
constexpr size_t   kAudioBytes      = (size_t)kSampleRate * 2 *
                                      (kMaxRecordMs / 1000);
// kAudioBytes = 480000 при 15 с

// --- Кнопки ---
constexpr uint32_t kLongPressMs     = 800;     // порог «долгого» BtnB

// --- Экран ---
constexpr int      kScreenRotation  = 1;       // ландшафт: 1 или 3
constexpr int      kScreenW         = 240;
constexpr int      kScreenH         = 135;

// --- Прочее ---
constexpr uint32_t kSerialBaud      = 115200;

// --- STT через свой прокси на VPS ---
// Groq недоступен из России напрямую (403, гео-блок), поэтому стик ходит
// к своему прокси, а тот — в Groq Whisper. Тело запроса — сырой PCM
// (16 бит, little-endian, моно, 16 кГц), multipart собирает сервер.
// Хост, порт, путь и токен — в config.h: они зависят от развёртывания.
constexpr const char* kSttContentType = "application/octet-stream";
// Минимум, который принимает прокси: 50 мс.
constexpr size_t      kSttMinBytes    = 1600;

// --- LLM через OpenCode Go ---
// OpenCode доступен из России напрямую, поэтому прокси ему не нужен.
// Авторизация — ключ подписки (config.h), эндпоинт OpenAI-совместимый.
constexpr const char* kLlmHost     = "opencode.ai";
constexpr int         kLlmPort     = 443;
constexpr const char* kLlmPath     = "/zen/go/v1/chat/completions";
constexpr const char* kLlmModel    = "deepseek-v4.1-flash";
// Go просит клиентов представляться своим User-Agent, а не именем
// HTTP-библиотеки, и присылать стабильный x-opencode-session.
constexpr const char* kLlmUserAgent = "aistick/0.1";
// Ответ должен быть коротким: экран 135 px, а reasoning-модель тратит
// бюджет ещё и на reasoning_content. Поэтому лимит с запасом: при 150
// токенах размышления съедали его целиком, и content приходил пустым.
constexpr const char* kLlmSystem =
    "Ты карманный ассистент. Отвечай кратко, максимум 2-3 коротких "
    "предложения на языке пользователя.";
constexpr int         kLlmMaxTokens = 600;
// Все модели OpenCode Go по умолчанию «размышляют»: ответ идёт в
// reasoning_content, а content приходит пустым или в самом конце. Этот
// параметр отключает размышления — content идёт сразу, ответ вдвое быстрее.
constexpr const char* kLlmReasoningEffort = "none";

// --- Память диалога ---
// Сессионная: живёт в куче до выключения. Обрезка держит запрос компактным,
// иначе контекст дорожает и ответ замедляется.
constexpr size_t      kChatMaxTurns = 5;
// Ответы модели бывают по 300–400 символов, поэтому 1600 обрезали историю
// до 4 обменов. 2400 даёт держать все 5.
constexpr size_t      kChatMaxChars = 2400;

// --- Таймауты сети, мс ---
constexpr uint32_t kWifiTimeoutMs   = 15000;
constexpr uint32_t kHttpConnectMs   = 5000;
// Отправка с устройства идёт ~28 КБ/с. На 15 с записи (480 КБ) уходит ~17 с,
// плюс TLS-рукопожатие и обработка. На слабом Wi-Fi скорость падает, поэтому
// таймаут с большим запасом.
constexpr uint32_t kHttpReadMs      = 60000;
constexpr size_t   kMaxSttResponse  = 8192;    // больше ответ не бывает
// Потолок на длину ответа модели в символах. Поток читается инкрементально,
// поэтому память зависит от ответа, а не от размера SSE-обёртки.
constexpr size_t   kMaxAnswerChars  = 8192;
// Мягкая цель для индикатора во время стрима: считаем прогрессом принятые
// символы ответа. Точный объём заранее неизвестен, поэтому цель оценочная,
// а по завершении полоска всё равно доходит до 100%.
constexpr size_t   kStreamTargetChars = 220;
