#pragma once

#include "Core/LayerStack.h"

struct GLFWwindow;

namespace ds
{

    class ImGuiLayer;

    class Application
    {
    public:
        Application();
        ~Application();

        void Run();
        void PushLayer(Layer *layer);

        GLFWwindow *GetWindow() const { return m_Window; }

    private:
        GLFWwindow *m_Window = nullptr;
        bool m_Running = true;

        LayerStack m_LayerStack;
        ImGuiLayer *m_ImGuiLayer = nullptr;
    };

} // namespace ds
