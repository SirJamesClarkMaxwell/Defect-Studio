#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace ds
{

    enum class LogLevel
    {
        Trace,
        Info,
        Warn,
        Error,
        Fatal
    };

    struct LogEntry
    {
        LogLevel level;
        std::string timestamp;
        std::string file;
        std::string function;
        std::uint_least32_t line = 0;
        std::string message;
    };

    class Logger
    {
    public:
        static Logger &Get();

        void Log(LogLevel level,
                 std::string_view message,
                 const std::source_location &location = std::source_location::current());
        void LogProfiling(std::string_view category,
                          std::string_view message,
                          const std::source_location &location = std::source_location::current());
        void Clear();

        std::vector<LogEntry> GetEntriesSnapshot() const;
        std::size_t GetErrorCount() const;

    private:
        Logger() = default;

        mutable std::mutex m_Mutex;
        std::vector<LogEntry> m_Entries;
        std::size_t m_ErrorCount = 0;
    };

    void LogInfo(std::string_view message, const std::source_location &location = std::source_location::current());
    void LogTrace(std::string_view message, const std::source_location &location = std::source_location::current());
    void LogWarn(std::string_view message, const std::source_location &location = std::source_location::current());
    void LogError(std::string_view message, const std::source_location &location = std::source_location::current());
    void LogFatal(std::string_view message, const std::source_location &location = std::source_location::current());
    void LogProfiling(std::string_view category,
                      std::string_view message,
                      const std::source_location &location = std::source_location::current());

} // namespace ds
