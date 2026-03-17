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

        void AddScrollDelta(float delta);
        float ConsumeScrollDelta();

    private:
        explicit ApplicationContext(GLFWwindow *window) : m_Window(window) {}

        static ApplicationContext *s_Instance;
        GLFWwindow *m_Window;
        float m_ScrollDelta = 0.0f;
    };

} // namespace ds
