#include "Core/Application.h"

#include "Core/ApplicationContext.h"
#include "Core/Logger.h"
#include "Layers/ImGuiLayer.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <string>
#include <stdexcept>

namespace ds
{

    namespace
    {

        void GLFWErrorCallback(int error, const char *description)
        {
            LogError("GLFW error " + std::to_string(error) + ": " + (description ? description : "<no description>"));
        }

        void GLFWScrollCallback(GLFWwindow * /*window*/, double /*xOffset*/, double yOffset)
        {
            ApplicationContext::Get().AddScrollDelta(static_cast<float>(yOffset));
        }

    } // namespace

    Application::Application()
    {
        LogInfo("Application startup initiated");
        glfwSetErrorCallback(GLFWErrorCallback);

        if (!glfwInit())
        {
            LogError("GLFW initialization failed");
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        m_Window = glfwCreateWindow(1600, 900, "DefectsStudio", nullptr, nullptr);
        if (m_Window == nullptr)
        {
            glfwTerminate();
            LogError("GLFW window creation failed");
            throw std::runtime_error("Failed to create GLFW window");
        }

        glfwMakeContextCurrent(m_Window);
        glfwSwapInterval(1);

        const int gladVersion = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
        if (gladVersion == 0)
        {
            glfwDestroyWindow(m_Window);
            glfwTerminate();
            LogError("GLAD initialization failed");
            throw std::runtime_error("Failed to initialize OpenGL function loader");
        }

        ApplicationContext::Initialize(m_Window);
        glfwSetScrollCallback(m_Window, GLFWScrollCallback);

        m_ImGuiLayer = new ImGuiLayer();
        PushLayer(m_ImGuiLayer);

        LogInfo("Application initialized successfully");
    }

    Application::~Application()
    {
        LogInfo("Application shutdown");
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    void Application::PushLayer(Layer *layer)
    {
        m_LayerStack.PushLayer(layer);
        LogInfo("Layer attached: " + layer->GetName());
    }

    void Application::Run()
    {
        auto lastTick = std::chrono::steady_clock::now();

        while (m_Running && !glfwWindowShouldClose(m_Window))
        {
            const auto now = std::chrono::steady_clock::now();
            const float deltaTime = std::chrono::duration<float>(now - lastTick).count();
            lastTick = now;

            glfwPollEvents();

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(m_Window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            for (Layer *layer : m_LayerStack.GetLayers())
            {
                layer->OnUpdate(deltaTime);
            }

            m_ImGuiLayer->Begin();
            for (Layer *layer : m_LayerStack.GetLayers())
            {
                layer->OnImGuiRender();
            }
            m_ImGuiLayer->End();

            glfwSwapBuffers(m_Window);
        }
    }

} // namespace ds
