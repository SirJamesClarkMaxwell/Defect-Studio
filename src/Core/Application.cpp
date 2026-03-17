#include "Core/Application.h"

#include "Core/ApplicationContext.h"
#include "Core/Logger.h"
#include "Layers/ImGuiLayer.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>

namespace ds
{

    namespace
    {

        constexpr const char *kWindowStatePath = "config/window_state.ini";

        struct WindowState
        {
            int x = 100;
            int y = 100;
            int width = 1600;
            int height = 900;
            bool hasPosition = false;
            bool maximized = true;
        };

        WindowState LoadWindowState()
        {
            WindowState state;

            std::ifstream in(kWindowStatePath);
            if (!in.is_open())
            {
                return state;
            }

            std::string line;
            while (std::getline(in, line))
            {
                const std::size_t eq = line.find('=');
                if (eq == std::string::npos)
                {
                    continue;
                }

                const std::string key = line.substr(0, eq);
                const std::string value = line.substr(eq + 1);

                try
                {
                    if (key == "x")
                    {
                        state.x = std::stoi(value);
                        state.hasPosition = true;
                    }
                    else if (key == "y")
                    {
                        state.y = std::stoi(value);
                        state.hasPosition = true;
                    }
                    else if (key == "width")
                    {
                        state.width = std::max(640, std::stoi(value));
                    }
                    else if (key == "height")
                    {
                        state.height = std::max(480, std::stoi(value));
                    }
                    else if (key == "open_maximized")
                    {
                        state.maximized = (value == "1");
                    }
                }
                catch (...)
                {
                }
            }

            return state;
        }

        void SaveWindowState(GLFWwindow *window)
        {
            if (window == nullptr)
            {
                return;
            }

            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            glfwGetWindowPos(window, &x, &y);
            glfwGetWindowSize(window, &width, &height);
            const bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;

            std::filesystem::create_directories("config");
            std::ofstream out(kWindowStatePath, std::ios::trunc);
            if (!out.is_open())
            {
                return;
            }

            out << "x=" << x << '\n';
            out << "y=" << y << '\n';
            out << "width=" << width << '\n';
            out << "height=" << height << '\n';
            out << "open_maximized=" << (maximized ? "1" : "0") << '\n';
        }

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

        const WindowState windowState = LoadWindowState();

        if (!glfwInit())
        {
            LogError("GLFW initialization failed");
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_MAXIMIZED, windowState.maximized ? GLFW_TRUE : GLFW_FALSE);

        m_Window = glfwCreateWindow(windowState.width, windowState.height, "DefectsStudio", nullptr, nullptr);
        if (m_Window == nullptr)
        {
            glfwTerminate();
            LogError("GLFW window creation failed");
            throw std::runtime_error("Failed to create GLFW window");
        }

        if (windowState.hasPosition)
        {
            glfwSetWindowPos(m_Window, windowState.x, windowState.y);
        }

        if (windowState.maximized)
        {
            glfwMaximizeWindow(m_Window);
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
        SaveWindowState(m_Window);
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
