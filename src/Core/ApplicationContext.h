#pragma once

struct GLFWwindow;

namespace ds
{

    class ApplicationContext
    {
    public:
        static void Initialize(GLFWwindow *window);
        static ApplicationContext &Get();

        GLFWwindow *GetWindow() const { return m_Window; }

    private:
        explicit ApplicationContext(GLFWwindow *window) : m_Window(window) {}

        static ApplicationContext *s_Instance;
        GLFWwindow *m_Window;
    };

} // namespace ds
