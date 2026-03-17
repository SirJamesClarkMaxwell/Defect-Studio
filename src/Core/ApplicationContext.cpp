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

} // namespace ds
