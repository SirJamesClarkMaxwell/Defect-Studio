#include "Core/Application.h"

#include "Core/ApplicationContext.h"
#include "Core/Logger.h"
#include "Core/Profiling.h"
#include "Layers/ImGuiLayer.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <windows.h>
#pragma comment(lib, "dwmapi.lib")
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>

namespace ds
{

    namespace
    {

        constexpr const char *kWindowStatePath = "config/window_state.ini";
        constexpr const char *kAppIconPath = "assets/icon.ico";

#if defined(_WIN32)
        HICON g_WindowIconSmall = nullptr;
        HICON g_WindowIconBig = nullptr;
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

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
                LogInfo("Window state file not found. Using default window placement.");
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

            // Windows can report minimized windows as (-32000, -32000) with zero size.
            // In that case we must ignore persisted placement, otherwise the app can appear "not launching".
            const bool invalidSize = (state.width < 640 || state.height < 480);
            const bool minimizedSentinelPos = (state.x <= -30000 || state.y <= -30000);
            if (invalidSize || minimizedSentinelPos)
            {
                LogWarn("Invalid persisted window state detected (possibly minimized snapshot). Resetting to defaults.");
                state = WindowState{};
            }

            LogInfo("Loaded window state: x=" + std::to_string(state.x) +
                    ", y=" + std::to_string(state.y) +
                    ", width=" + std::to_string(state.width) +
                    ", height=" + std::to_string(state.height) +
                    ", maximized=" + std::string(state.maximized ? "1" : "0"));

            return state;
        }

        void SaveWindowState(GLFWwindow *window)
        {
            if (window == nullptr)
            {
                return;
            }

            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE)
            {
                LogInfo("Window is minimized during shutdown. Skipping window_state.ini update.");
                return;
            }

            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            glfwGetWindowPos(window, &x, &y);
            glfwGetWindowSize(window, &width, &height);
            const bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;

            if (width < 640 || height < 480 || x <= -30000 || y <= -30000)
            {
                LogWarn("Refusing to save invalid window state values.");
                return;
            }

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

            LogInfo("Saved window state: x=" + std::to_string(x) +
                    ", y=" + std::to_string(y) +
                    ", width=" + std::to_string(width) +
                    ", height=" + std::to_string(height) +
                    ", maximized=" + std::string(maximized ? "1" : "0"));
        }

        void GLFWErrorCallback(int error, const char *description)
        {
            LogError("GLFW error " + std::to_string(error) + ": " + (description ? description : "<no description>"));
        }

        void GLFWScrollCallback(GLFWwindow * /*window*/, double /*xOffset*/, double yOffset)
        {
            ApplicationContext::Get().AddScrollDelta(static_cast<float>(yOffset));
        }

        void ApplyWindowIcon(GLFWwindow *window)
        {
#if defined(_WIN32)
            if (window == nullptr)
            {
                return;
            }

            HWND hwnd = glfwGetWin32Window(window);
            if (hwnd == nullptr)
            {
                return;
            }

            if (g_WindowIconSmall != nullptr)
            {
                DestroyIcon(g_WindowIconSmall);
                g_WindowIconSmall = nullptr;
            }
            if (g_WindowIconBig != nullptr)
            {
                DestroyIcon(g_WindowIconBig);
                g_WindowIconBig = nullptr;
            }

            g_WindowIconSmall = static_cast<HICON>(LoadImageA(nullptr, kAppIconPath, IMAGE_ICON, 16, 16, LR_LOADFROMFILE));
            g_WindowIconBig = static_cast<HICON>(LoadImageA(nullptr, kAppIconPath, IMAGE_ICON, 32, 32, LR_LOADFROMFILE));

            if (g_WindowIconSmall != nullptr)
            {
                SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_WindowIconSmall));
            }
            if (g_WindowIconBig != nullptr)
            {
                SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_WindowIconBig));
            }

            if (g_WindowIconSmall == nullptr && g_WindowIconBig == nullptr)
            {
                LogWarn("Could not load app icon from assets/icon.ico");
            }
#else
            (void)window;
#endif
        }

        void ApplyDarkTitleBar(GLFWwindow *window)
        {
#if defined(_WIN32)
            if (window == nullptr)
            {
                return;
            }

            HWND hwnd = glfwGetWin32Window(window);
            if (hwnd == nullptr)
            {
                return;
            }

            BOOL enabled = TRUE;
            HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
            if (FAILED(hr))
            {
                constexpr DWORD kLegacyDarkModeAttr = 19;
                hr = DwmSetWindowAttribute(hwnd, kLegacyDarkModeAttr, &enabled, sizeof(enabled));
            }

            if (FAILED(hr))
            {
                LogWarn("Could not enable dark title bar (DWM attribute unsupported on this system)");
            }
#else
            (void)window;
#endif
        }

        void CleanupWindowIcon()
        {
#if defined(_WIN32)
            if (g_WindowIconSmall != nullptr)
            {
                DestroyIcon(g_WindowIconSmall);
                g_WindowIconSmall = nullptr;
            }
            if (g_WindowIconBig != nullptr)
            {
                DestroyIcon(g_WindowIconBig);
                g_WindowIconBig = nullptr;
            }
#endif
        }

    } // namespace

    Application::Application()
    {
        DS_PROFILE_SCOPE_N("Application::Application");
        LogInfo("Application startup initiated");
        glfwSetErrorCallback(GLFWErrorCallback);

        const WindowState windowState = LoadWindowState();

        if (!glfwInit())
        {
            LogFatal("GLFW initialization failed");
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
            LogFatal("GLFW window creation failed");
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
        ApplyWindowIcon(m_Window);
        ApplyDarkTitleBar(m_Window);

        const int gladVersion = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
        if (gladVersion == 0)
        {
            glfwDestroyWindow(m_Window);
            glfwTerminate();
            LogFatal("GLAD initialization failed");
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
        DS_PROFILE_SCOPE_N("Application::~Application");
        LogInfo("Application shutdown");
        SaveWindowState(m_Window);
        CleanupWindowIcon();
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    void Application::PushLayer(Layer *layer)
    {
        DS_PROFILE_SCOPE_N("Application::PushLayer");
        m_LayerStack.PushLayer(layer);
        LogInfo("Layer attached: " + layer->GetName());
    }

    void Application::Run()
    {
        DS_PROFILE_SCOPE_N("Application::Run");
        auto lastTick = std::chrono::steady_clock::now();
        float traceAccumulatorSec = 0.0f;
        std::uint32_t traceFrames = 0;
        LogTrace("Main loop started.");

        while (m_Running && !glfwWindowShouldClose(m_Window))
        {
            DS_PROFILE_FRAME();
            DS_PROFILE_SCOPE_N("Application::Frame");
            const auto now = std::chrono::steady_clock::now();
            const float deltaTime = std::chrono::duration<float>(now - lastTick).count();
            lastTick = now;
            traceAccumulatorSec += deltaTime;
            ++traceFrames;

            if (traceAccumulatorSec >= 2.0f)
            {
                const float averageDt = traceAccumulatorSec / static_cast<float>(std::max<std::uint32_t>(traceFrames, 1u));
                const float fps = averageDt > 1e-6f ? (1.0f / averageDt) : 0.0f;
                LogTrace("Runtime perf snapshot: avg_dt=" + std::to_string(averageDt) + "s, fps=" + std::to_string(fps));
                traceAccumulatorSec = 0.0f;
                traceFrames = 0;
            }

            {
                DS_PROFILE_SCOPE_N("Application::PollEvents");
                glfwPollEvents();
            }

            int width = 0;
            int height = 0;
            {
                DS_PROFILE_SCOPE_N("Application::PrepareFramebuffer");
                glfwGetFramebufferSize(m_Window, &width, &height);
                glViewport(0, 0, width, height);
                glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }

            {
                DS_PROFILE_SCOPE_N("Application::LayerUpdate");
                for (Layer *layer : m_LayerStack.GetLayers())
                {
                    layer->OnUpdate(deltaTime);
                }
            }

            {
                DS_PROFILE_SCOPE_N("Application::ImGuiFrame");
                m_ImGuiLayer->Begin();
                for (Layer *layer : m_LayerStack.GetLayers())
                {
                    layer->OnImGuiRender();
                }

                m_ImGuiLayer->End();
            }

            {
                DS_PROFILE_SCOPE_N("Application::SwapBuffers");
                glfwSwapBuffers(m_Window);
            }
        }

        LogTrace("Main loop finished.");
    }

} // namespace ds
