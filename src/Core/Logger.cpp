#include "Core/Logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ds
{

    namespace
    {
        std::string BuildShortFileName(std::string_view filePath)
        {
            if (filePath.empty())
            {
                return {};
            }

            return std::filesystem::path(filePath).filename().string();
        }

        std::string BuildShortFunctionName(std::string_view functionName)
        {
            if (functionName.empty())
            {
                return {};
            }

            std::string trimmed(functionName);
            const std::size_t parenPos = trimmed.find('(');
            if (parenPos != std::string::npos)
            {
                trimmed.erase(parenPos);
            }

            const std::size_t lastSpacePos = trimmed.find_last_of(' ');
            if (lastSpacePos != std::string::npos && lastSpacePos + 1 < trimmed.size())
            {
                trimmed.erase(0, lastSpacePos + 1);
            }

            return trimmed;
        }

        std::string BuildTimestampNow()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &nowTime);
#else
            localtime_r(&nowTime, &localTime);
#endif

            std::ostringstream stream;
            stream << std::put_time(&localTime, "%H:%M:%S");
            return stream.str();
        }

    } // namespace

    Logger &Logger::Get()
    {
        static Logger instance;
        return instance;
    }

    void Logger::Log(LogLevel level, std::string_view message, const std::source_location &location)
    {
        std::scoped_lock lock(m_Mutex);

        const std::string timestamp = BuildTimestampNow();
        const std::string file = BuildShortFileName(location.file_name());
        const std::string function = BuildShortFunctionName(location.function_name());
        const std::string text(message);
        m_Entries.push_back(LogEntry{level, timestamp, file, function, location.line(), text});
        if (level == LogLevel::Error || level == LogLevel::Fatal)
        {
            ++m_ErrorCount;
        }

        std::filesystem::create_directories("logs");
        std::ofstream out("logs/runtime.log", std::ios::app);
        if (out.is_open())
        {
            const char *levelText = "INFO";
            if (level == LogLevel::Trace)
            {
                levelText = "TRACE";
            }
            else if (level == LogLevel::Warn)
            {
                levelText = "WARN";
            }
            else if (level == LogLevel::Error)
            {
                levelText = "ERROR";
            }
            else if (level == LogLevel::Fatal)
            {
                levelText = "FATAL";
            }

            out << '[' << timestamp << "] [" << levelText << "] "
                << '[' << file << ':' << location.line() << "] "
                << '[' << function << "] "
                << text << '\n';
        }

        constexpr std::size_t maxEntries = 2000;
        if (m_Entries.size() > maxEntries)
        {
            const std::size_t dropCount = m_Entries.size() - maxEntries;
            m_Entries.erase(m_Entries.begin(), m_Entries.begin() + static_cast<std::ptrdiff_t>(dropCount));
        }
    }

    void Logger::Clear()
    {
        std::scoped_lock lock(m_Mutex);
        m_Entries.clear();
        m_ErrorCount = 0;
    }

    std::vector<LogEntry> Logger::GetEntriesSnapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Entries;
    }

    std::size_t Logger::GetErrorCount() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_ErrorCount;
    }

    void LogTrace(std::string_view message, const std::source_location &location)
    {
        Logger::Get().Log(LogLevel::Trace, message, location);
    }

    void LogInfo(std::string_view message, const std::source_location &location)
    {
        Logger::Get().Log(LogLevel::Info, message, location);
    }

    void LogWarn(std::string_view message, const std::source_location &location)
    {
        Logger::Get().Log(LogLevel::Warn, message, location);
    }

    void LogError(std::string_view message, const std::source_location &location)
    {
        Logger::Get().Log(LogLevel::Error, message, location);
    }

    void LogFatal(std::string_view message, const std::source_location &location)
    {
        Logger::Get().Log(LogLevel::Fatal, message, location);
    }

} // namespace ds
