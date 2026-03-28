#include "Layers/ImGuiLayer.h"

#include "Core/ApplicationContext.h"
#include "Core/Logger.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ds
{
    namespace
    {
        constexpr const char *kImGuiStylePath = "config/imgui_style.ini";

        ImFont *s_UIFont = nullptr;
        ImFont *s_MonospaceFont = nullptr;
        ImFont *s_BondLabelFont = nullptr;

        std::string Trim(const std::string &value)
        {
            const std::size_t first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                return std::string();
            }
            const std::size_t last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        bool ParseFloat(const std::string &value, float &outValue)
        {
            try
            {
                outValue = std::stof(value);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool ParseVec2(const std::string &value, ImVec2 &outValue)
        {
            std::stringstream stream(value);
            std::string part;
            float components[2] = {};
            int index = 0;
            while (std::getline(stream, part, ',') && index < 2)
            {
                if (!ParseFloat(Trim(part), components[index]))
                {
                    return false;
                }
                ++index;
            }

            if (index != 2)
            {
                return false;
            }

            outValue = ImVec2(components[0], components[1]);
            return true;
        }

        bool ParseVec4(const std::string &value, ImVec4 &outValue)
        {
            std::stringstream stream(value);
            std::string part;
            float components[4] = {};
            int index = 0;
            while (std::getline(stream, part, ',') && index < 4)
            {
                if (!ParseFloat(Trim(part), components[index]))
                {
                    return false;
                }
                ++index;
            }

            if (index != 4)
            {
                return false;
            }

            outValue = ImVec4(components[0], components[1], components[2], components[3]);
            return true;
        }

        void SaveStyleToFile(const ImGuiStyle &style)
        {
            std::filesystem::create_directories("config");
            std::ofstream out(kImGuiStylePath, std::ios::trunc);
            if (!out.is_open())
            {
                return;
            }

            auto writeVec2 = [&](const char *key, const ImVec2 &value)
            {
                out << key << '=' << value.x << ',' << value.y << '\n';
            };

            out << "alpha=" << style.Alpha << '\n';
            out << "disabled_alpha=" << style.DisabledAlpha << '\n';
            writeVec2("window_padding", style.WindowPadding);
            out << "window_rounding=" << style.WindowRounding << '\n';
            out << "window_border_size=" << style.WindowBorderSize << '\n';
            writeVec2("window_min_size", style.WindowMinSize);
            writeVec2("window_title_align", style.WindowTitleAlign);
            out << "child_rounding=" << style.ChildRounding << '\n';
            out << "child_border_size=" << style.ChildBorderSize << '\n';
            out << "popup_rounding=" << style.PopupRounding << '\n';
            out << "popup_border_size=" << style.PopupBorderSize << '\n';
            writeVec2("frame_padding", style.FramePadding);
            out << "frame_rounding=" << style.FrameRounding << '\n';
            out << "frame_border_size=" << style.FrameBorderSize << '\n';
            writeVec2("item_spacing", style.ItemSpacing);
            writeVec2("item_inner_spacing", style.ItemInnerSpacing);
            writeVec2("cell_padding", style.CellPadding);
            out << "indent_spacing=" << style.IndentSpacing << '\n';
            out << "columns_min_spacing=" << style.ColumnsMinSpacing << '\n';
            out << "scrollbar_size=" << style.ScrollbarSize << '\n';
            out << "scrollbar_rounding=" << style.ScrollbarRounding << '\n';
            out << "grab_min_size=" << style.GrabMinSize << '\n';
            out << "grab_rounding=" << style.GrabRounding << '\n';
            out << "log_slider_deadzone=" << style.LogSliderDeadzone << '\n';
            out << "tab_rounding=" << style.TabRounding << '\n';
            out << "tab_border_size=" << style.TabBorderSize << '\n';
            out << "tab_bar_border_size=" << style.TabBarBorderSize << '\n';
            writeVec2("button_text_align", style.ButtonTextAlign);
            writeVec2("selectable_text_align", style.SelectableTextAlign);
            writeVec2("display_window_padding", style.DisplayWindowPadding);
            writeVec2("display_safe_area_padding", style.DisplaySafeAreaPadding);
            out << "mouse_cursor_scale=" << style.MouseCursorScale << '\n';
            out << "anti_aliased_lines=" << (style.AntiAliasedLines ? "1" : "0") << '\n';
            out << "anti_aliased_fill=" << (style.AntiAliasedFill ? "1" : "0") << '\n';
            out << "curve_tessellation_tol=" << style.CurveTessellationTol << '\n';
            out << "circle_tessellation_max_error=" << style.CircleTessellationMaxError << '\n';

            for (int colorIndex = 0; colorIndex < ImGuiCol_COUNT; ++colorIndex)
            {
                const ImVec4 &color = style.Colors[colorIndex];
                out << "color_" << colorIndex << '=' << color.x << ',' << color.y << ',' << color.z << ',' << color.w << '\n';
            }
        }

        bool LoadStyleFromFile(ImGuiStyle &style)
        {
            std::ifstream in(kImGuiStylePath);
            if (!in.is_open())
            {
                return false;
            }

            std::string line;
            while (std::getline(in, line))
            {
                const std::size_t separator = line.find('=');
                if (separator == std::string::npos)
                {
                    continue;
                }

                const std::string key = Trim(line.substr(0, separator));
                const std::string value = Trim(line.substr(separator + 1));
                if (key.empty())
                {
                    continue;
                }

                float scalarValue = 0.0f;
                ImVec2 vec2Value(0.0f, 0.0f);
                ImVec4 vec4Value(0.0f, 0.0f, 0.0f, 0.0f);

                if (key == "alpha" && ParseFloat(value, scalarValue))
                    style.Alpha = scalarValue;
                else if (key == "disabled_alpha" && ParseFloat(value, scalarValue))
                    style.DisabledAlpha = scalarValue;
                else if (key == "window_padding" && ParseVec2(value, vec2Value))
                    style.WindowPadding = vec2Value;
                else if (key == "window_rounding" && ParseFloat(value, scalarValue))
                    style.WindowRounding = scalarValue;
                else if (key == "window_border_size" && ParseFloat(value, scalarValue))
                    style.WindowBorderSize = scalarValue;
                else if (key == "window_min_size" && ParseVec2(value, vec2Value))
                    style.WindowMinSize = vec2Value;
                else if (key == "window_title_align" && ParseVec2(value, vec2Value))
                    style.WindowTitleAlign = vec2Value;
                else if (key == "child_rounding" && ParseFloat(value, scalarValue))
                    style.ChildRounding = scalarValue;
                else if (key == "child_border_size" && ParseFloat(value, scalarValue))
                    style.ChildBorderSize = scalarValue;
                else if (key == "popup_rounding" && ParseFloat(value, scalarValue))
                    style.PopupRounding = scalarValue;
                else if (key == "popup_border_size" && ParseFloat(value, scalarValue))
                    style.PopupBorderSize = scalarValue;
                else if (key == "frame_padding" && ParseVec2(value, vec2Value))
                    style.FramePadding = vec2Value;
                else if (key == "frame_rounding" && ParseFloat(value, scalarValue))
                    style.FrameRounding = scalarValue;
                else if (key == "frame_border_size" && ParseFloat(value, scalarValue))
                    style.FrameBorderSize = scalarValue;
                else if (key == "item_spacing" && ParseVec2(value, vec2Value))
                    style.ItemSpacing = vec2Value;
                else if (key == "item_inner_spacing" && ParseVec2(value, vec2Value))
                    style.ItemInnerSpacing = vec2Value;
                else if (key == "cell_padding" && ParseVec2(value, vec2Value))
                    style.CellPadding = vec2Value;
                else if (key == "indent_spacing" && ParseFloat(value, scalarValue))
                    style.IndentSpacing = scalarValue;
                else if (key == "columns_min_spacing" && ParseFloat(value, scalarValue))
                    style.ColumnsMinSpacing = scalarValue;
                else if (key == "scrollbar_size" && ParseFloat(value, scalarValue))
                    style.ScrollbarSize = scalarValue;
                else if (key == "scrollbar_rounding" && ParseFloat(value, scalarValue))
                    style.ScrollbarRounding = scalarValue;
                else if (key == "grab_min_size" && ParseFloat(value, scalarValue))
                    style.GrabMinSize = scalarValue;
                else if (key == "grab_rounding" && ParseFloat(value, scalarValue))
                    style.GrabRounding = scalarValue;
                else if (key == "log_slider_deadzone" && ParseFloat(value, scalarValue))
                    style.LogSliderDeadzone = scalarValue;
                else if (key == "tab_rounding" && ParseFloat(value, scalarValue))
                    style.TabRounding = scalarValue;
                else if (key == "tab_border_size" && ParseFloat(value, scalarValue))
                    style.TabBorderSize = scalarValue;
                else if (key == "tab_bar_border_size" && ParseFloat(value, scalarValue))
                    style.TabBarBorderSize = scalarValue;
                else if (key == "button_text_align" && ParseVec2(value, vec2Value))
                    style.ButtonTextAlign = vec2Value;
                else if (key == "selectable_text_align" && ParseVec2(value, vec2Value))
                    style.SelectableTextAlign = vec2Value;
                else if (key == "display_window_padding" && ParseVec2(value, vec2Value))
                    style.DisplayWindowPadding = vec2Value;
                else if (key == "display_safe_area_padding" && ParseVec2(value, vec2Value))
                    style.DisplaySafeAreaPadding = vec2Value;
                else if (key == "mouse_cursor_scale" && ParseFloat(value, scalarValue))
                    style.MouseCursorScale = scalarValue;
                else if (key == "anti_aliased_lines")
                    style.AntiAliasedLines = (value == "1");
                else if (key == "anti_aliased_fill")
                    style.AntiAliasedFill = (value == "1");
                else if (key == "curve_tessellation_tol" && ParseFloat(value, scalarValue))
                    style.CurveTessellationTol = scalarValue;
                else if (key == "circle_tessellation_max_error" && ParseFloat(value, scalarValue))
                    style.CircleTessellationMaxError = scalarValue;
                else if (key.rfind("color_", 0) == 0)
                {
                    try
                    {
                        const int colorIndex = std::stoi(key.substr(6));
                        if (colorIndex >= 0 && colorIndex < ImGuiCol_COUNT && ParseVec4(value, vec4Value))
                        {
                            style.Colors[colorIndex] = vec4Value;
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }

            return true;
        }
    }

    ImGuiLayer::ImGuiLayer()
        : Layer("ImGuiLayer") {}

    ImFont *ImGuiLayer::GetUIFont()
    {
        return s_UIFont;
    }

    ImFont *ImGuiLayer::GetMonospaceFont()
    {
        return s_MonospaceFont;
    }

    ImFont *ImGuiLayer::GetBondLabelFont()
    {
        return s_BondLabelFont;
    }

    bool ImGuiLayer::LoadSavedStyle()
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return false;
        }

        return LoadStyleFromFile(ImGui::GetStyle());
    }

    void ImGuiLayer::SaveCurrentStyle()
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return;
        }

        SaveStyleToFile(ImGui::GetStyle());
    }

    void ImGuiLayer::ResetSavedStyle()
    {
        std::error_code errorCode;
        std::filesystem::remove(kImGuiStylePath, errorCode);
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
        s_UIFont = io.Fonts->AddFontFromFileTTF("vendor/imgui/misc/fonts/DroidSans.ttf", 16.0f);
        if (s_UIFont == nullptr)
        {
            s_UIFont = io.FontDefault;
            LogWarn("Failed to load DroidSans.ttf for UI. Falling back to default ImGui font.");
        }

        s_MonospaceFont = io.Fonts->AddFontFromFileTTF("vendor/imgui/misc/fonts/Cousine-Regular.ttf", 15.0f);
        if (s_MonospaceFont == nullptr)
        {
            s_MonospaceFont = s_UIFont;
            LogWarn("Failed to load Cousine-Regular.ttf for console/shortcut views. Falling back to UI font.");
        }

        s_BondLabelFont = io.Fonts->AddFontFromFileTTF("vendor/imgui/misc/fonts/Roboto-Medium.ttf", 18.0f);
        if (s_BondLabelFont == nullptr)
        {
            s_BondLabelFont = s_UIFont;
            LogWarn("Failed to load Roboto-Medium.ttf for bond labels. Falling back to default ImGui font.");
        }
        io.FontDefault = s_UIFont;

        ImGui::StyleColorsDark();
        ImGuiStyle &style = ImGui::GetStyle();
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            // Keep platform windows visually consistent with the main viewport.
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
        LoadSavedStyle();

        GLFWwindow *window = ApplicationContext::Get().GetWindow();
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 330");

        LogInfo("ImGui layer initialized");
    }

    void ImGuiLayer::OnDetach()
    {
        LogInfo("ImGui layer shutdown");
        SaveCurrentStyle();
        s_UIFont = nullptr;
        s_MonospaceFont = nullptr;
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
