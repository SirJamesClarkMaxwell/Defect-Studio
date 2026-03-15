#include "Core/Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ds
{

    namespace
    {

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

    void Logger::Log(LogLevel level, std::string_view message)
    {
        std::scoped_lock lock(m_Mutex);

        m_Entries.push_back(LogEntry{level, BuildTimestampNow(), std::string(message)});
        if (level == LogLevel::Error)
        {
            ++m_ErrorCount;
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

    void LogInfo(std::string_view message)
    {
        Logger::Get().Log(LogLevel::Info, message);
    }

    void LogWarn(std::string_view message)
    {
        Logger::Get().Log(LogLevel::Warn, message);
    }

    void LogError(std::string_view message)
    {
        Logger::Get().Log(LogLevel::Error, message);
    }

} // namespace ds
