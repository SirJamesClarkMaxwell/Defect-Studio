#pragma once

#include <string>
#include <vector>

struct GLFWwindow;

namespace ds
{

    class ApplicationContext
    {
    public:
        static void Initialize(GLFWwindow *window);
        static ApplicationContext &Get();

        GLFWwindow *GetWindow() const { return m_Window; }

        void AddScrollDelta(float delta);
        float ConsumeScrollDelta();
        void AddDroppedPaths(const std::vector<std::string> &paths);
        std::vector<std::string> ConsumeDroppedPaths();

    private:
        explicit ApplicationContext(GLFWwindow *window) : m_Window(window) {}

        static ApplicationContext *s_Instance;
        GLFWwindow *m_Window;
        float m_ScrollDelta = 0.0f;
        std::vector<std::string> m_DroppedPaths;
    };

} // namespace ds
