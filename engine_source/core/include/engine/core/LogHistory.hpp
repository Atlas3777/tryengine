#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <mutex>

#include "engine/core/Log.hpp"

namespace tryengine::core {

//! Одна запись кольцевого буфера журнала.
//!
//! Строки скопированы: запись переживает вызов write() и сообщение, из которого она
//! пришла. time — локальное время "HH:MM:SS.mmm" на момент записи.
struct LogEntry {
    uint64_t sequence = 0;
    LogLevel level = LogLevel::Info;
    LogCategory category = LogCategory::General;
    uint32_t line = 0;
    eastl::string time;
    eastl::string message;
    eastl::string file;
};

//! Кольцевой буфер последних записей журнала и отфильтрованное представление для UI.
//!
//! Sink для Logger регистрируется в Logger::instance() рядом с консолью, поэтому один и
//! тот же журнал видят C++ и daslang: скрипт пишет через tryLog, C++ — через LogInfo и
//! компанию. Буфер ограничен CAPACITY записями; новая запись затирает самую старую.
//!
//! Окно редактора не копирует весь буфер в скрипт каждую запись: фильтр (маски уровней
//! и категорий) применяется здесь, а view_* отдают только прошедшие записи. Результат
//! отфильтрованного обхода кешируется до следующей записи/смены фильтра, поэтому кадр с
//! тысячами строк стоит O(видимых), а не O(всего буфера).
class LogHistory {
public:
    static constexpr uint32_t CAPACITY = 4096;

    static LogHistory& instance();

    LogHistory(const LogHistory&) = delete;
    LogHistory& operator=(const LogHistory&) = delete;

    void append(const LogRecord& record);
    void clear();

    //! Сколько записей сейчас живо; при переполнении равно CAPACITY.
    uint32_t count();

    //! Маски: бит N включает уровень/категорию с числовым значением N.
    //! 0 — не показывать ничего, ~0u — показывать всё.
    void set_filter(uint32_t level_mask, uint32_t category_mask);

    //! Число записей, прошедших текущий фильтр, от старых к новым.
    uint32_t view_count();

    //! sequence самой новой записи, прошедшей фильтр; 0 — представление пусто. Окно
    //! редактора цепляется за неё, чтобы «Follow» работал и на полном кольце, где
    //! view_count уже не растёт.
    uint64_t view_newest_sequence();

    //! Запись из отфильтрованного представления. Индекс мог устареть: кольцо затирает
    //! старые записи между кадрами. Тогда view_time/view_message/view_file возвращают
    //! false, а level/category/line — значения по умолчанию, и строка не рисуется.
    LogLevel view_level(uint32_t index);
    LogCategory view_category(uint32_t index);
    uint32_t view_line(uint32_t index);
    bool view_time(uint32_t index, eastl::string& out);
    bool view_message(uint32_t index, eastl::string& out);
    bool view_file(uint32_t index, eastl::string& out);

private:
    LogHistory();

    //! Ссылка на слот кольца вместе с sequence: по ней видно, не затёрла ли запись новая.
    struct Ref {
        uint32_t slot = 0;
        uint64_t sequence = 0;
    };

    void rebuild_view_locked();
    void ensure_view_locked();
    //! Запись по индексу представления или nullptr, если индекс вне диапазона/устарел.
    //! Вызывать под mutex_.
    const LogEntry* view_entry_locked(uint32_t index);

    std::mutex mutex_;
    eastl::vector<LogEntry> entries_;
    uint32_t head_ = 0;
    uint32_t count_ = 0;
    uint64_t next_sequence_ = 1;
    uint64_t revision_ = 0;

    uint32_t filter_levels_ = ~0u;
    uint32_t filter_categories_ = ~0u;

    // Кеш отфильтрованного обхода. view_revision_ == revision_ значит, что refs_ описывает
    // текущее содержимое кольца; view_levels_/view_categories_ — фильтр, под который он
    // построен.
    eastl::vector<Ref> refs_;
    uint64_t view_revision_ = ~uint64_t{0};
    uint32_t view_levels_ = 0;
    uint32_t view_categories_ = 0;
};

//! Sink, складывающий записи в LogHistory. Регистрируется автоматически при первом
//! обращении к Logger::instance(), поэтому кольцо доступно любому процессу, который
//! линкует engine_core (тесты, gate, оба приложения).
class HistoryLogSink final : public ILogSink {
public:
    void write(const LogRecord& record) override;
};

}  // namespace tryengine::core
