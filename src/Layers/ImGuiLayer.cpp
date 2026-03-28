#include "Layers/ImGuiLayer.h"

#include "Core/ApplicationContext.h"
#include "Core/Logger.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <filesystem>

namespace ds
{
    namespace
    {
        ImFont *s_BondLabelFont = nullptr;
    }

    ImGuiLayer::ImGuiLayer()
        : Layer("ImGuiLayer") {}

    ImFont *ImGuiLayer::GetBondLabelFont()
    {
        return s_BondLabelFont;
    }

    void ImGuiLayer::OnAttach()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        std::filesystem::create_directories("config");
        io.IniFilename = "config/imgui_layout.ini";
        s_BondLabelFont = io.Fonts->AddFontFromFileTTF("vendor/imgui/misc/fonts/Roboto-Medium.ttf", 18.0f);
        if (s_BondLabelFont == nullptr)
        {
            s_BondLabelFont = io.FontDefault;
            LogWarn("Failed to load Roboto-Medium.ttf for bond labels. Falling back to default ImGui font.");
        }

        ImGui::StyleColorsDark();
        ImGuiStyle &style = ImGui::GetStyle();
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            // Keep platform windows visually consistent with the main viewport.
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        GLFWwindow *window = ApplicationContext::Get().GetWindow();
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 330");

        LogInfo("ImGui layer initialized");
    }

    void ImGuiLayer::OnDetach()
    {
        LogInfo("ImGui layer shutdown");
        s_BondLabelFont = nullptr;
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiLayer::Begin()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::End()
    {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ImGuiIO &io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            GLFWwindow *backupCurrentContext = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backupCurrentContext);
        }
    }

} // namespace ds
