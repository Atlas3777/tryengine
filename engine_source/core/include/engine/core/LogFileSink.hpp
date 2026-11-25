#pragma once

#include <EASTL/string.h>
#include <cstdio>

#include "engine/core/Log.hpp"

namespace tryengine::core {

//! Файловый sink: `logs/engine-YYYYMMDD-HHMMSS.log` на запуск.
//!
//! Поток открывается один раз при установке и не закрывается до выхода (деструктор
//! закрывает), а каждая строка сразу сбрасывается в файл — `tail -f` видит её без
//! задержки. Время файла дополняет кольцо в памяти: окно редактора живёт до закрытия,
//! файл остаётся после и переживает падение.
class FileLogSink final : public ILogSink {
public:
    explicit FileLogSink(eastl::string path);
    ~FileLogSink() override;

    FileLogSink(const FileLogSink&) = delete;
    FileLogSink& operator=(const FileLogSink&) = delete;

    void write(const LogRecord& record) override;
    void flush() override;

    [[nodiscard]] bool is_open() const {
        return file_ != nullptr;
    }

    [[nodiscard]] const eastl::string& path() const {
        return path_;
    }

private:
    std::FILE* file_ = nullptr;
    eastl::string path_;
};

//! Открывает файл журнала текущего запуска и регистрирует sink в Logger. Идемпотентно:
//! повторный вызов возвращает уже открытый путь. Путь пуст, если logs/ недоступен —
//! тогда пишется LogError, и файловое логирование остаётся выключенным.
const eastl::string& InstallLogFileSink();

//! Путь файла журнала текущего запуска; пустая строка, если sink не установлен.
const eastl::string& ActiveLogFilePath();

}  // namespace tryengine::core
