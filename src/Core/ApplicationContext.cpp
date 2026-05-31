#include "Core/ApplicationContext.h"

#include <stdexcept>

namespace ds
{

    ApplicationContext *ApplicationContext::s_Instance = nullptr;

    void ApplicationContext::Initialize(GLFWwindow *window)
    {
        if (s_Instance == nullptr)
        {
            s_Instance = new ApplicationContext(window);
            return;
        }

        throw std::runtime_error("ApplicationContext already initialized");
    }

    ApplicationContext &ApplicationContext::Get()
    {
        if (s_Instance == nullptr)
        {
            throw std::runtime_error("ApplicationContext not initialized");
        }

        return *s_Instance;
    }

    void ApplicationContext::AddScrollDelta(float delta)
    {
        m_ScrollDelta += delta;
    }

    float ApplicationContext::ConsumeScrollDelta()
    {
        const float value = m_ScrollDelta;
        m_ScrollDelta = 0.0f;
        return value;
    }

    void ApplicationContext::AddDroppedPaths(const std::vector<std::string> &paths)
    {
        if (paths.empty())
        {
            return;
        }

        m_DroppedPaths.insert(m_DroppedPaths.end(), paths.begin(), paths.end());
    }

    std::vector<std::string> ApplicationContext::ConsumeDroppedPaths()
    {
        std::vector<std::string> paths = std::move(m_DroppedPaths);
        m_DroppedPaths.clear();
        return paths;
    }

} // namespace ds
