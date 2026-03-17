#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ds
{

    enum class LogLevel
    {
        Info,
        Warn,
        Error
    };

    struct LogEntry
    {
        LogLevel level;
        std::string timestamp;
        std::string message;
    };

    class Logger
    {
    public:
        static Logger &Get();

        void Log(LogLevel level, std::string_view message);
        void Clear();

        std::vector<LogEntry> GetEntriesSnapshot() const;
        std::size_t GetErrorCount() const;

    private:
        Logger() = default;

        mutable std::mutex m_Mutex;
        std::vector<LogEntry> m_Entries;
        std::size_t m_ErrorCount = 0;
    };

    void LogInfo(std::string_view message);
    void LogWarn(std::string_view message);
    void LogError(std::string_view message);

} // namespace ds
