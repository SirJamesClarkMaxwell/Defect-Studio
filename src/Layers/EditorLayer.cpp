#include "Layers/EditorLayer.h"

#include "Core/ApplicationContext.h"
#include "Core/Logger.h"
#include "Renderer/OpenGLRendererBackend.h"
#include "Renderer/OrbitCamera.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>
#endif

namespace ds
{

    namespace
    {
        constexpr float kBaseOrbitSensitivity = 0.01f;
        constexpr float kBasePanSensitivity = 0.18f;
        constexpr float kBaseZoomSensitivity = 0.17f;
        constexpr const char *kSelectionDebugLogPath = "logs/selection_debug.log";

        std::string BuildDebugTimestampNow()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &nowTime);
#else
            localtime_r(&nowTime, &localTime);
#endif

            std::ostringstream stream;
            stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
            return stream.str();
        }

        bool OpenNativeFileDialog(std::string &outPath)
        {
#ifdef _WIN32
            char pathBuffer[MAX_PATH] = {};

            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = nullptr;
            dialog.lpstrFile = pathBuffer;
            dialog.nMaxFile = static_cast<DWORD>(sizeof(pathBuffer));
            dialog.lpstrFilter = "VASP files (*.vasp;*.poscar;*.contcar)\0*.vasp;*.poscar;*.contcar\0All files (*.*)\0*.*\0";
            dialog.nFilterIndex = 1;
            dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
            dialog.lpstrDefExt = "vasp";

            if (GetOpenFileNameA(&dialog) != FALSE)
            {
                outPath = pathBuffer;
                return true;
            }

            return false;
#else
            (void)outPath;
            return false;
#endif
        }

        bool SaveNativeFileDialog(std::string &outPath)
        {
#ifdef _WIN32
            char pathBuffer[MAX_PATH] = "CONTCAR.vasp";

            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = nullptr;
            dialog.lpstrFile = pathBuffer;
            dialog.nMaxFile = static_cast<DWORD>(sizeof(pathBuffer));
            dialog.lpstrFilter = "VASP files (*.vasp;*.poscar;*.contcar)\0*.vasp;*.poscar;*.contcar\0All files (*.*)\0*.*\0";
            dialog.nFilterIndex = 1;
            dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
            dialog.lpstrDefExt = "vasp";

            if (GetSaveFileNameA(&dialog) != FALSE)
            {
                outPath = pathBuffer;
                return true;
            }

            return false;
#else
            (void)outPath;
            return false;
#endif
        }

        glm::vec3 ColorFromElement(const std::string &element)
        {
            std::uint32_t hash = 2166136261u;
            for (unsigned char c : element)
            {
                hash ^= static_cast<std::uint32_t>(c);
                hash *= 16777619u;
            }

            const float r = 0.35f + 0.50f * static_cast<float>((hash >> 0) & 0xFF) / 255.0f;
            const float g = 0.35f + 0.50f * static_cast<float>((hash >> 8) & 0xFF) / 255.0f;
            const float b = 0.35f + 0.50f * static_cast<float>((hash >> 16) & 0xFF) / 255.0f;
            return glm::vec3(r, g, b);
        }
    }

    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
        const char *defaultImportPath = "assets/samples/POSCAR";
        const char *defaultExportPath = "exports/CONTCAR.vasp";

        std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", defaultImportPath);
        std::snprintf(m_ExportPathBuffer.data(), m_ExportPathBuffer.size(), "%s", defaultExportPath);

        m_SceneSettings.clearColor = glm::vec3(0.16f, 0.18f, 0.22f);
        m_SceneSettings.gridLineWidth = 1.0f;
        m_SceneSettings.gridColor = glm::vec3(0.38f, 0.42f, 0.50f);
        m_SceneSettings.ambientStrength = 0.58f;
        m_SceneSettings.diffuseStrength = 0.78f;
        m_SceneSettings.atomBrightness = 1.15f;
    }

    void EditorLayer::OnAttach()
    {
        LoadSettings();
        ApplyTheme(m_CurrentTheme);
        ApplyFontScale(m_FontScale);

        m_RenderBackend = std::make_unique<OpenGLRendererBackend>();
        if (!m_RenderBackend->Initialize())
        {
            LogError("Failed to initialize OpenGL backend.");
        }

        m_Camera = std::make_unique<OrbitCamera>();
        m_Camera->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        m_Camera->SetProjectionMode(m_ProjectionModeIndex == 0 ? OrbitCamera::ProjectionMode::Perspective : OrbitCamera::ProjectionMode::Orthographic);
        if (m_HasPersistedCameraState)
        {
            m_Camera->SetOrbitState(m_CameraTargetPersisted, m_CameraDistancePersisted, m_CameraYawPersisted, m_CameraPitchPersisted);
        }
        ApplyCameraSensitivity();

        LogInfo("EditorLayer attached with theme: " + std::string(ThemeName(m_CurrentTheme)));
    }

    void EditorLayer::OnDetach()
    {
        SaveSettings();

        if (m_RenderBackend)
        {
            m_RenderBackend->Shutdown();
            m_RenderBackend.reset();
        }

        m_Camera.reset();
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        if (!m_RenderBackend || !m_Camera)
        {
            return;
        }

        m_Camera->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        const bool allowCameraInput = m_ViewportHovered && m_ViewportFocused;
        const float scrollDelta = ApplicationContext::Get().ConsumeScrollDelta();
        m_Camera->OnUpdate(deltaTime, allowCameraInput, allowCameraInput ? scrollDelta : 0.0f);

        m_RenderBackend->ResizeViewport(static_cast<std::uint32_t>(m_ViewportSize.x), static_cast<std::uint32_t>(m_ViewportSize.y));
        m_RenderBackend->BeginFrame(m_SceneSettings);
        if (m_HasStructureLoaded && !m_WorkingStructure.atoms.empty())
        {
            std::vector<glm::vec3> atomPositions;
            std::vector<glm::vec3> atomColors;
            atomPositions.reserve(m_WorkingStructure.atoms.size());
            atomColors.reserve(m_WorkingStructure.atoms.size());

            std::vector<glm::vec3> selectedPositions;
            selectedPositions.reserve(m_SelectedAtomIndices.size());
            std::vector<glm::vec3> selectedColors;
            selectedColors.reserve(m_SelectedAtomIndices.size());

            for (std::size_t i = 0; i < m_WorkingStructure.atoms.size(); ++i)
            {
                const Atom &atom = m_WorkingStructure.atoms[i];
                glm::vec3 position = atom.position;
                if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
                {
                    position = m_WorkingStructure.DirectToCartesian(position);
                }

                atomPositions.push_back(position);
                atomColors.push_back(ColorFromElement(atom.element));

                if (IsAtomSelected(i))
                {
                    selectedPositions.push_back(position);
                    selectedColors.push_back(m_SelectionColor);
                }
            }

            m_RenderBackend->RenderAtomsScene(m_Camera->GetViewProjectionMatrix(), atomPositions, atomColors, m_SceneSettings);

            if (!selectedPositions.empty())
            {
                SceneRenderSettings highlightSettings = m_SceneSettings;
                highlightSettings.drawGrid = false;
                highlightSettings.overrideAtomColor = true;
                highlightSettings.atomOverrideColor = m_SelectionColor;
                highlightSettings.atomScale = m_SceneSettings.atomScale * 1.02f;
                highlightSettings.atomBrightness = std::max(1.0f, m_SceneSettings.atomBrightness);
                highlightSettings.atomWireframe = true;
                highlightSettings.atomWireframeWidth = m_SelectionOutlineThickness;
                m_RenderBackend->RenderAtomsScene(m_Camera->GetViewProjectionMatrix(), selectedPositions, selectedColors, highlightSettings);
            }
        }
        else
        {
            m_RenderBackend->RenderDemoScene(m_Camera->GetViewProjectionMatrix(), m_SceneSettings);
        }
        m_RenderBackend->EndFrame();
    }

    bool EditorLayer::IsAtomSelected(std::size_t index) const
    {
        return std::find(m_SelectedAtomIndices.begin(), m_SelectedAtomIndices.end(), index) != m_SelectedAtomIndices.end();
    }

    void EditorLayer::ToggleInteractionMode()
    {
        if (m_InteractionMode == InteractionMode::Navigate)
        {
            if (!m_HasStructureLoaded || m_WorkingStructure.atoms.empty())
            {
                m_LastStructureOperationFailed = true;
                m_LastStructureMessage = "Selection mode unavailable: load POSCAR/CONTCAR first.";
                LogWarn(m_LastStructureMessage);
                AppendSelectionDebugLog("Mode switch denied: no loaded structure");
                return;
            }

            m_InteractionMode = InteractionMode::Select;
            LogInfo("Interaction mode: Select");
            AppendSelectionDebugLog("Mode switched to Select");
            return;
        }

        m_InteractionMode = InteractionMode::Navigate;
        LogInfo("Interaction mode: Navigate");
        AppendSelectionDebugLog("Mode switched to Navigate");
    }

    void EditorLayer::AppendSelectionDebugLog(const std::string &message) const
    {
        if (!m_SelectionDebugToFile)
        {
            return;
        }

        std::filesystem::create_directories("logs");
        std::ofstream out(kSelectionDebugLogPath, std::ios::app);
        if (!out.is_open())
        {
            return;
        }

        out << '[' << BuildDebugTimestampNow() << "] " << message << '\n';
    }

    bool EditorLayer::PickAtomAtScreenPoint(const glm::vec2 &mousePos, std::size_t &outAtomIndex) const
    {
        if (!m_Camera || !m_HasStructureLoaded || m_WorkingStructure.atoms.empty())
        {
            return false;
        }

        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return false;
        }

        const glm::mat4 viewProjection = m_Camera->GetViewProjectionMatrix();
        bool found = false;
        float bestDepth = std::numeric_limits<float>::max();
        float bestDistancePixels = std::numeric_limits<float>::max();
        const float pickRadiusPixels = 14.0f + m_SceneSettings.atomScale * 20.0f;
        const float pickRadiusPixelsSq = pickRadiusPixels * pickRadiusPixels;

        for (std::size_t i = 0; i < m_WorkingStructure.atoms.size(); ++i)
        {
            glm::vec3 center = m_WorkingStructure.atoms[i].position;
            if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
            {
                center = m_WorkingStructure.DirectToCartesian(center);
            }

            const glm::vec4 clip = viewProjection * glm::vec4(center, 1.0f);
            if (clip.w <= 1e-6f)
            {
                continue;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.05f || ndc.x > 1.05f || ndc.y < -1.05f || ndc.y > 1.05f || ndc.z < -1.0f || ndc.z > 1.0f)
            {
                continue;
            }

            const float screenX = m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width;
            const float screenY = m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height;
            const float dx = mousePos.x - screenX;
            const float dy = mousePos.y - screenY;
            const float distSq = dx * dx + dy * dy;
            if (distSq > pickRadiusPixelsSq)
            {
                continue;
            }

            const float distPixels = std::sqrt(distSq);
            if (!found || ndc.z < bestDepth - 1e-4f || (std::abs(ndc.z - bestDepth) <= 1e-4f && distPixels < bestDistancePixels))
            {
                bestDepth = ndc.z;
                bestDistancePixels = distPixels;
                outAtomIndex = i;
                found = true;
            }
        }

        return found;
    }

    void EditorLayer::HandleViewportSelection()
    {
        const ImGuiIO &io = ImGui::GetIO();
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            return;
        }

        std::ostringstream clickLog;
        clickLog << "LMB click: mode=" << (m_InteractionMode == InteractionMode::Select ? "Select" : "Navigate")
                 << " focused=" << (m_ViewportFocused ? "1" : "0")
                 << " hovered=" << (m_ViewportHovered ? "1" : "0")
                 << " hasStructure=" << (m_HasStructureLoaded ? "1" : "0")
                 << " atomCount=" << m_WorkingStructure.atoms.size()
                 << " mouse=(" << io.MousePos.x << "," << io.MousePos.y << ")"
                 << " viewportMin=(" << m_ViewportRectMin.x << "," << m_ViewportRectMin.y << ")"
                 << " viewportMax=(" << m_ViewportRectMax.x << "," << m_ViewportRectMax.y << ")";
        AppendSelectionDebugLog(clickLog.str());

        if (m_InteractionMode != InteractionMode::Select)
        {
            AppendSelectionDebugLog("Ignored click: not in Select mode");
            return;
        }

        if (!m_ViewportFocused || !m_ViewportHovered)
        {
            AppendSelectionDebugLog("Ignored click: viewport not focused/hovered");
            return;
        }

        if (!m_HasStructureLoaded || m_WorkingStructure.atoms.empty())
        {
            AppendSelectionDebugLog("Ignored click: no structure or empty atoms");
            return;
        }

        const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
        const bool insideViewport =
            mousePos.x >= m_ViewportRectMin.x && mousePos.x <= m_ViewportRectMax.x &&
            mousePos.y >= m_ViewportRectMin.y && mousePos.y <= m_ViewportRectMax.y;
        if (!insideViewport)
        {
            AppendSelectionDebugLog("Ignored click: outside viewport rect");
            return;
        }

        std::size_t pickedAtomIndex = 0;
        const bool hasHit = PickAtomAtScreenPoint(mousePos, pickedAtomIndex);
        const bool multiSelect = io.KeyCtrl;

        {
            std::ostringstream pickLog;
            pickLog << "Pick result: hasHit=" << (hasHit ? "1" : "0")
                    << " pickedIndex=" << pickedAtomIndex
                    << " ctrl=" << (multiSelect ? "1" : "0");
            AppendSelectionDebugLog(pickLog.str());
        }

        if (!hasHit)
        {
            if (!multiSelect)
            {
                m_SelectedAtomIndices.clear();
                AppendSelectionDebugLog("Selection cleared: no hit and Ctrl not pressed");
            }
            return;
        }

        if (!multiSelect)
        {
            m_SelectedAtomIndices.clear();
            m_SelectedAtomIndices.push_back(pickedAtomIndex);
            AppendSelectionDebugLog("Selection set to single atom index=" + std::to_string(pickedAtomIndex));
            return;
        }

        auto it = std::find(m_SelectedAtomIndices.begin(), m_SelectedAtomIndices.end(), pickedAtomIndex);
        if (it == m_SelectedAtomIndices.end())
        {
            m_SelectedAtomIndices.push_back(pickedAtomIndex);
            AppendSelectionDebugLog("Selection added atom index=" + std::to_string(pickedAtomIndex));
        }
        else
        {
            m_SelectedAtomIndices.erase(it);
            AppendSelectionDebugLog("Selection removed atom index=" + std::to_string(pickedAtomIndex));
        }

        AppendSelectionDebugLog("Selection size now=" + std::to_string(m_SelectedAtomIndices.size()));
    }

    bool EditorLayer::LoadStructureFromPath(const std::string &path)
    {
        Structure parsed;
        std::string error;
        if (!m_PoscarParser.ParseFromFile(path, parsed, error))
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Import failed: " + error;
            LogError(m_LastStructureMessage);
            return false;
        }

        m_WorkingStructure = parsed;
        m_OriginalStructure = parsed;
        m_HasStructureLoaded = true;
        m_SelectedAtomIndices.clear();
        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Imported structure from: " + path;

        if (m_Camera && !m_WorkingStructure.atoms.empty())
        {
            glm::vec3 boundsMin(std::numeric_limits<float>::max());
            glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

            for (const Atom &atom : m_WorkingStructure.atoms)
            {
                glm::vec3 position = atom.position;
                if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
                {
                    position = m_WorkingStructure.DirectToCartesian(position);
                }

                boundsMin = glm::min(boundsMin, position);
                boundsMax = glm::max(boundsMax, position);
            }

            const glm::vec3 center = 0.5f * (boundsMin + boundsMax);
            m_SceneSettings.gridOrigin = glm::vec3(center.x, boundsMin.y, center.z);

            const float halfSpanXZ = 0.5f * std::max(boundsMax.x - boundsMin.x, boundsMax.z - boundsMin.z);
            const int recommendedHalfExtent = static_cast<int>(std::ceil(halfSpanXZ / std::max(0.1f, m_SceneSettings.gridSpacing))) + 2;
            m_SceneSettings.gridHalfExtent = std::max(m_SceneSettings.gridHalfExtent, recommendedHalfExtent);

            m_Camera->FrameBounds(boundsMin, boundsMax);
        }

        LogInfo(m_LastStructureMessage + " (atoms=" + std::to_string(m_WorkingStructure.GetAtomCount()) + ")");
        return true;
    }

    bool EditorLayer::ExportStructureToPath(const std::string &path, CoordinateMode mode, int precision)
    {
        if (!m_HasStructureLoaded)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Export failed: no structure loaded.";
            LogWarn(m_LastStructureMessage);
            return false;
        }

        PoscarWriteOptions options;
        options.coordinateMode = mode;
        options.precision = precision;

        std::string error;
        if (!m_PoscarSerializer.WriteToFile(m_WorkingStructure, path, options, error))
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Export failed: " + error;
            LogError(m_LastStructureMessage);
            return false;
        }

        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Exported structure to: " + path;
        LogInfo(m_LastStructureMessage);
        return true;
    }

    void EditorLayer::ApplyFontScale(float scale)
    {
        if (scale < 0.7f)
            scale = 0.7f;
        if (scale > 2.0f)
            scale = 2.0f;

        m_FontScale = scale;
        ImGui::GetIO().FontGlobalScale = m_FontScale;
    }

    void EditorLayer::ApplyCameraSensitivity()
    {
        if (!m_Camera)
        {
            return;
        }

        m_Camera->SetSensitivity(
            m_CameraOrbitSensitivity * kBaseOrbitSensitivity,
            m_CameraPanSensitivity * kBasePanSensitivity,
            m_CameraZoomSensitivity * kBaseZoomSensitivity);
    }

    const char *EditorLayer::ThemeName(ThemePreset preset) const
    {
        switch (preset)
        {
        case ThemePreset::Dark:
            return "Dark";
        case ThemePreset::Light:
            return "Light";
        case ThemePreset::Classic:
            return "Classic";
        case ThemePreset::PhotoshopStyle:
            return "PhotoshopStyle";
        case ThemePreset::WarmSlate:
            return "WarmSlate";
        default:
            return "Dark";
        }
    }

    void EditorLayer::ApplyTheme(ThemePreset preset)
    {
        ImGuiStyle &style = ImGui::GetStyle();

        switch (preset)
        {
        case ThemePreset::Dark:
        {
            ImGui::StyleColorsDark();
            break;
        }
        case ThemePreset::Light:
        {
            ImGui::StyleColorsLight();
            break;
        }
        case ThemePreset::Classic:
        {
            ImGui::StyleColorsClassic();
            break;
        }
        case ThemePreset::PhotoshopStyle:
        {
            ImGui::StyleColorsDark();
            style.FrameRounding = 3.0f;
            style.WindowRounding = 2.0f;
            style.GrabRounding = 2.0f;
            style.ScrollbarRounding = 2.0f;

            ImVec4 *colors = style.Colors;
            colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.16f, 0.17f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.23f, 0.39f, 0.55f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.46f, 0.64f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.34f, 0.48f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.24f, 0.41f, 0.59f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.29f, 0.49f, 0.69f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.35f, 0.51f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.20f, 0.22f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.26f, 0.29f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.30f, 0.33f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.21f, 0.25f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.41f, 0.56f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.23f, 0.35f, 0.48f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.36f, 0.67f, 0.97f, 1.00f);
            break;
        }
        case ThemePreset::WarmSlate:
        {
            ImGui::StyleColorsDark();
            style.FrameRounding = 5.0f;
            style.WindowRounding = 4.0f;

            ImVec4 *colors = style.Colors;
            colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.10f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.54f, 0.34f, 0.22f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.63f, 0.41f, 0.27f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.47f, 0.29f, 0.19f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.49f, 0.31f, 0.20f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.60f, 0.39f, 0.25f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.44f, 0.27f, 0.18f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.74f, 0.39f, 1.00f);
            break;
        }
        default:
        {
            ImGui::StyleColorsDark();
            break;
        }
        }
    }

    void EditorLayer::LoadSettings()
    {
        std::ifstream in(kSettingsPath);
        if (!in.is_open())
        {
            return;
        }

        bool hasSensitivityMode = false;
        bool relativeSensitivity = true;

        std::string line;
        while (std::getline(in, line))
        {
            const std::size_t sep = line.find('=');
            if (sep == std::string::npos)
            {
                continue;
            }

            const std::string key = line.substr(0, sep);
            const std::string value = line.substr(sep + 1);

            if (key == "theme")
            {
                if (value == "Dark")
                    m_CurrentTheme = ThemePreset::Dark;
                else if (value == "Light")
                    m_CurrentTheme = ThemePreset::Light;
                else if (value == "Classic")
                    m_CurrentTheme = ThemePreset::Classic;
                else if (value == "PhotoshopStyle")
                    m_CurrentTheme = ThemePreset::PhotoshopStyle;
                else if (value == "WarmSlate")
                    m_CurrentTheme = ThemePreset::WarmSlate;
            }
            else if (key == "show_demo_window")
            {
                m_ShowDemoWindow = (value == "1");
            }
            else if (key == "show_log_panel")
            {
                m_ShowLogPanel = (value == "1");
            }
            else if (key == "font_scale")
            {
                try
                {
                    ApplyFontScale(std::stof(value));
                }
                catch (...)
                {
                    ApplyFontScale(1.0f);
                }
            }
            else if (key == "log_filter")
            {
                try
                {
                    m_LogFilter = std::stoi(value);
                }
                catch (...)
                {
                    m_LogFilter = 0;
                }
            }
            else if (key == "log_auto_scroll")
            {
                m_LogAutoScroll = (value == "1");
            }
            else if (key == "camera_orbit_sensitivity")
            {
                try
                {
                    m_CameraOrbitSensitivity = std::stof(value);
                }
                catch (...)
                {
                    m_CameraOrbitSensitivity = 1.0f;
                }
            }
            else if (key == "camera_pan_sensitivity")
            {
                try
                {
                    m_CameraPanSensitivity = std::stof(value);
                }
                catch (...)
                {
                    m_CameraPanSensitivity = 1.0f;
                }
            }
            else if (key == "camera_zoom_sensitivity")
            {
                try
                {
                    m_CameraZoomSensitivity = std::stof(value);
                }
                catch (...)
                {
                    m_CameraZoomSensitivity = 1.0f;
                }
            }
            else if (key == "camera_target_x")
            {
                try
                {
                    m_CameraTargetPersisted.x = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_target_y")
            {
                try
                {
                    m_CameraTargetPersisted.y = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_target_z")
            {
                try
                {
                    m_CameraTargetPersisted.z = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_distance")
            {
                try
                {
                    m_CameraDistancePersisted = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_yaw")
            {
                try
                {
                    m_CameraYawPersisted = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_pitch")
            {
                try
                {
                    m_CameraPitchPersisted = std::stof(value);
                    m_HasPersistedCameraState = true;
                }
                catch (...)
                {
                }
            }
            else if (key == "camera_sensitivity_mode")
            {
                hasSensitivityMode = true;
                relativeSensitivity = (value == "relative_v2");
            }
            else if (key == "viewport_bg_r")
            {
                try
                {
                    m_SceneSettings.clearColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bg_g")
            {
                try
                {
                    m_SceneSettings.clearColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_bg_b")
            {
                try
                {
                    m_SceneSettings.clearColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_enabled")
            {
                m_SceneSettings.drawGrid = (value == "1");
            }
            else if (key == "viewport_grid_extent")
            {
                try
                {
                    m_SceneSettings.gridHalfExtent = std::stoi(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_spacing")
            {
                try
                {
                    m_SceneSettings.gridSpacing = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_line_width")
            {
                try
                {
                    m_SceneSettings.gridLineWidth = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_origin_x")
            {
                try
                {
                    m_SceneSettings.gridOrigin.x = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_origin_y")
            {
                try
                {
                    m_SceneSettings.gridOrigin.y = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_origin_z")
            {
                try
                {
                    m_SceneSettings.gridOrigin.z = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_color_r")
            {
                try
                {
                    m_SceneSettings.gridColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_color_g")
            {
                try
                {
                    m_SceneSettings.gridColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_color_b")
            {
                try
                {
                    m_SceneSettings.gridColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_grid_opacity")
            {
                try
                {
                    m_SceneSettings.gridOpacity = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_dir_x")
            {
                try
                {
                    m_SceneSettings.lightDirection.x = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_dir_y")
            {
                try
                {
                    m_SceneSettings.lightDirection.y = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_dir_z")
            {
                try
                {
                    m_SceneSettings.lightDirection.z = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_ambient")
            {
                try
                {
                    m_SceneSettings.ambientStrength = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_light_diffuse")
            {
                try
                {
                    m_SceneSettings.diffuseStrength = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_scale")
            {
                try
                {
                    m_SceneSettings.atomScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_override")
            {
                m_SceneSettings.overrideAtomColor = (value == "1");
            }
            else if (key == "viewport_atom_override_r")
            {
                try
                {
                    m_SceneSettings.atomOverrideColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_override_g")
            {
                try
                {
                    m_SceneSettings.atomOverrideColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_override_b")
            {
                try
                {
                    m_SceneSettings.atomOverrideColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_atom_brightness")
            {
                try
                {
                    m_SceneSettings.atomBrightness = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "selection_color_r")
            {
                try
                {
                    m_SelectionColor.r = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "selection_color_g")
            {
                try
                {
                    m_SelectionColor.g = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "selection_color_b")
            {
                try
                {
                    m_SelectionColor.b = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "selection_outline_thickness")
            {
                try
                {
                    m_SelectionOutlineThickness = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "selection_debug_to_file")
            {
                m_SelectionDebugToFile = (value == "1");
            }
            else if (key == "viewport_projection_mode")
            {
                try
                {
                    m_ProjectionModeIndex = std::stoi(value);
                }
                catch (...)
                {
                    m_ProjectionModeIndex = 0;
                }
            }
        }

        if (!hasSensitivityMode || !relativeSensitivity)
        {
            // Legacy migration: old settings stored absolute values.
            m_CameraOrbitSensitivity = m_CameraOrbitSensitivity / kBaseOrbitSensitivity;
            m_CameraPanSensitivity = m_CameraPanSensitivity / kBasePanSensitivity;
            m_CameraZoomSensitivity = m_CameraZoomSensitivity / kBaseZoomSensitivity;
        }

        if (m_CameraOrbitSensitivity < 0.05f)
            m_CameraOrbitSensitivity = 0.05f;
        if (m_CameraPanSensitivity < 0.05f)
            m_CameraPanSensitivity = 0.05f;
        if (m_CameraZoomSensitivity < 0.05f)
            m_CameraZoomSensitivity = 0.05f;

        if (m_ProjectionModeIndex < 0)
            m_ProjectionModeIndex = 0;
        if (m_ProjectionModeIndex > 1)
            m_ProjectionModeIndex = 1;

        if (m_SelectionOutlineThickness < 1.0f)
            m_SelectionOutlineThickness = 1.0f;
        if (m_SelectionOutlineThickness > 8.0f)
            m_SelectionOutlineThickness = 8.0f;
    }

    void EditorLayer::SaveSettings() const
    {
        std::filesystem::create_directories("config");

        std::ofstream out(kSettingsPath, std::ios::trunc);
        if (!out.is_open())
        {
            return;
        }

        out << "theme=" << ThemeName(m_CurrentTheme) << '\n';
        out << "show_demo_window=" << (m_ShowDemoWindow ? "1" : "0") << '\n';
        out << "show_log_panel=" << (m_ShowLogPanel ? "1" : "0") << '\n';
        out << "font_scale=" << m_FontScale << '\n';
        out << "log_filter=" << m_LogFilter << '\n';
        out << "log_auto_scroll=" << (m_LogAutoScroll ? "1" : "0") << '\n';
        out << "camera_orbit_sensitivity=" << m_CameraOrbitSensitivity << '\n';
        out << "camera_pan_sensitivity=" << m_CameraPanSensitivity << '\n';
        out << "camera_zoom_sensitivity=" << m_CameraZoomSensitivity << '\n';
        out << "camera_sensitivity_mode=relative_v2" << '\n';
        if (m_Camera)
        {
            out << "camera_target_x=" << m_Camera->GetTarget().x << '\n';
            out << "camera_target_y=" << m_Camera->GetTarget().y << '\n';
            out << "camera_target_z=" << m_Camera->GetTarget().z << '\n';
            out << "camera_distance=" << m_Camera->GetDistance() << '\n';
            out << "camera_yaw=" << m_Camera->GetYaw() << '\n';
            out << "camera_pitch=" << m_Camera->GetPitch() << '\n';
        }
        else
        {
            out << "camera_target_x=" << m_CameraTargetPersisted.x << '\n';
            out << "camera_target_y=" << m_CameraTargetPersisted.y << '\n';
            out << "camera_target_z=" << m_CameraTargetPersisted.z << '\n';
            out << "camera_distance=" << m_CameraDistancePersisted << '\n';
            out << "camera_yaw=" << m_CameraYawPersisted << '\n';
            out << "camera_pitch=" << m_CameraPitchPersisted << '\n';
        }
        out << "viewport_bg_r=" << m_SceneSettings.clearColor.r << '\n';
        out << "viewport_bg_g=" << m_SceneSettings.clearColor.g << '\n';
        out << "viewport_bg_b=" << m_SceneSettings.clearColor.b << '\n';
        out << "viewport_grid_enabled=" << (m_SceneSettings.drawGrid ? "1" : "0") << '\n';
        out << "viewport_grid_extent=" << m_SceneSettings.gridHalfExtent << '\n';
        out << "viewport_grid_spacing=" << m_SceneSettings.gridSpacing << '\n';
        out << "viewport_grid_line_width=" << m_SceneSettings.gridLineWidth << '\n';
        out << "viewport_grid_origin_x=" << m_SceneSettings.gridOrigin.x << '\n';
        out << "viewport_grid_origin_y=" << m_SceneSettings.gridOrigin.y << '\n';
        out << "viewport_grid_origin_z=" << m_SceneSettings.gridOrigin.z << '\n';
        out << "viewport_grid_color_r=" << m_SceneSettings.gridColor.r << '\n';
        out << "viewport_grid_color_g=" << m_SceneSettings.gridColor.g << '\n';
        out << "viewport_grid_color_b=" << m_SceneSettings.gridColor.b << '\n';
        out << "viewport_grid_opacity=" << m_SceneSettings.gridOpacity << '\n';
        out << "viewport_light_dir_x=" << m_SceneSettings.lightDirection.x << '\n';
        out << "viewport_light_dir_y=" << m_SceneSettings.lightDirection.y << '\n';
        out << "viewport_light_dir_z=" << m_SceneSettings.lightDirection.z << '\n';
        out << "viewport_light_ambient=" << m_SceneSettings.ambientStrength << '\n';
        out << "viewport_light_diffuse=" << m_SceneSettings.diffuseStrength << '\n';
        out << "viewport_atom_scale=" << m_SceneSettings.atomScale << '\n';
        out << "viewport_atom_override=" << (m_SceneSettings.overrideAtomColor ? "1" : "0") << '\n';
        out << "viewport_atom_override_r=" << m_SceneSettings.atomOverrideColor.r << '\n';
        out << "viewport_atom_override_g=" << m_SceneSettings.atomOverrideColor.g << '\n';
        out << "viewport_atom_override_b=" << m_SceneSettings.atomOverrideColor.b << '\n';
        out << "viewport_atom_brightness=" << m_SceneSettings.atomBrightness << '\n';
        out << "selection_color_r=" << m_SelectionColor.r << '\n';
        out << "selection_color_g=" << m_SelectionColor.g << '\n';
        out << "selection_color_b=" << m_SelectionColor.b << '\n';
        out << "selection_outline_thickness=" << m_SelectionOutlineThickness << '\n';
        out << "selection_debug_to_file=" << (m_SelectionDebugToFile ? "1" : "0") << '\n';
        out << "viewport_projection_mode=" << m_ProjectionModeIndex << '\n';
    }

    void EditorLayer::OnImGuiRender()
    {
        bool settingsChanged = false;
        const ImGuiIO &io = ImGui::GetIO();

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        windowFlags |= ImGuiWindowFlags_NoTitleBar;
        windowFlags |= ImGuiWindowFlags_NoCollapse;
        windowFlags |= ImGuiWindowFlags_NoResize;
        windowFlags |= ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
        windowFlags |= ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("DockSpaceRoot", nullptr, windowFlags);
        ImGui::PopStyleVar(2);

        ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open POSCAR/CONTCAR", "Ctrl+O"))
                {
                    std::string selectedPath;
                    if (OpenNativeFileDialog(selectedPath))
                    {
                        std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", selectedPath.c_str());
                        LoadStructureFromPath(selectedPath);
                    }
                }

                if (ImGui::MenuItem("Export POSCAR/CONTCAR", "Ctrl+S", false, m_HasStructureLoaded))
                {
                    std::string selectedPath;
                    if (SaveNativeFileDialog(selectedPath))
                    {
                        std::snprintf(m_ExportPathBuffer.data(), m_ExportPathBuffer.size(), "%s", selectedPath.c_str());
                        const CoordinateMode exportMode = (m_ExportCoordinateModeIndex == 0)
                                                              ? CoordinateMode::Direct
                                                              : CoordinateMode::Cartesian;
                        ExportStructureToPath(selectedPath, exportMode, m_ExportPrecision);
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Log / Errors", nullptr, &m_ShowLogPanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Viewport Settings", nullptr, &m_ViewportSettingsOpen))
                {
                    settingsChanged = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        ImGui::Begin("Viewport");
        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        if (m_ViewportFocused && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
        {
            ToggleInteractionMode();
        }

        ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x < 1.0f)
            size.x = 1.0f;
        if (size.y < 1.0f)
            size.y = 1.0f;
        m_ViewportSize = glm::vec2(size.x, size.y);

        if (m_RenderBackend)
        {
            const std::uint32_t texture = m_RenderBackend->GetColorAttachmentRendererID();
            if (texture != 0)
            {
                ImTextureID textureID = (ImTextureID)(std::uintptr_t)texture;
                ImGui::Image(textureID, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                const ImVec2 rectMin = ImGui::GetItemRectMin();
                const ImVec2 rectMax = ImGui::GetItemRectMax();
                m_ViewportRectMin = glm::vec2(rectMin.x, rectMin.y);
                m_ViewportRectMax = glm::vec2(rectMax.x, rectMax.y);
            }
            else
            {
                ImGui::TextUnformatted("Viewport render target not ready.");
                m_ViewportRectMin = glm::vec2(0.0f, 0.0f);
                m_ViewportRectMax = glm::vec2(0.0f, 0.0f);
            }
        }
        else
        {
            ImGui::TextUnformatted("OpenGL backend unavailable.");
            m_ViewportRectMin = glm::vec2(0.0f, 0.0f);
            m_ViewportRectMax = glm::vec2(0.0f, 0.0f);
        }

        HandleViewportSelection();
        ImGui::End();

        ImGui::Begin("Viewport Info");
        ImGui::Text("Size: %.0f x %.0f", size.x, size.y);
        ImGui::Text("Focused: %s | Hovered: %s", m_ViewportFocused ? "yes" : "no", m_ViewportHovered ? "yes" : "no");
        ImGui::Text("ImGui capture: mouse=%s keyboard=%s", io.WantCaptureMouse ? "yes" : "no", io.WantCaptureKeyboard ? "yes" : "no");
        ImGui::Separator();
        ImGui::TextUnformatted("Navigation:");
        ImGui::BulletText("MMB orbit");
        ImGui::BulletText("Shift + MMB pan");
        ImGui::BulletText("Mouse Wheel zoom");
        ImGui::End();

        if (m_ViewportSettingsOpen)
        {
            ImGui::Begin("Viewport Settings", &m_ViewportSettingsOpen);

            if (ImGui::ColorEdit3("Background", &m_SceneSettings.clearColor.x))
            {
                settingsChanged = true;
            }

            if (ImGui::Checkbox("Draw grid", &m_SceneSettings.drawGrid))
            {
                settingsChanged = true;
            }

            if (ImGui::SliderInt("Grid half extent", &m_SceneSettings.gridHalfExtent, 1, 64))
            {
                settingsChanged = true;
            }

            if (ImGui::SliderFloat("Grid spacing", &m_SceneSettings.gridSpacing, 0.1f, 5.0f, "%.2f"))
            {
                settingsChanged = true;
            }

            if (ImGui::SliderFloat("Grid line width", &m_SceneSettings.gridLineWidth, 1.0f, 4.0f, "%.1f"))
            {
                settingsChanged = true;
            }

            if (ImGui::ColorEdit3("Grid color", &m_SceneSettings.gridColor.x))
            {
                settingsChanged = true;
            }

            if (ImGui::SliderFloat("Grid opacity", &m_SceneSettings.gridOpacity, 0.05f, 1.0f, "%.2f"))
            {
                settingsChanged = true;
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Lighting");

            if (ImGui::SliderFloat3("Light direction", &m_SceneSettings.lightDirection.x, -1.0f, 1.0f, "%.2f"))
            {
                settingsChanged = true;
            }

            if (ImGui::SliderFloat("Ambient", &m_SceneSettings.ambientStrength, 0.0f, 1.5f, "%.2f"))
            {
                settingsChanged = true;
            }

            if (ImGui::SliderFloat("Diffuse", &m_SceneSettings.diffuseStrength, 0.0f, 2.0f, "%.2f"))
            {
                settingsChanged = true;
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Projection");

            const char *projectionModes[] = {"Perspective", "Orthographic"};
            if (ImGui::Combo("Mode", &m_ProjectionModeIndex, projectionModes, IM_ARRAYSIZE(projectionModes)))
            {
                if (m_Camera)
                {
                    m_Camera->SetProjectionMode(m_ProjectionModeIndex == 0 ? OrbitCamera::ProjectionMode::Perspective : OrbitCamera::ProjectionMode::Orthographic);
                }
                settingsChanged = true;
            }

            if (m_Camera && m_ProjectionModeIndex == 0)
            {
                float fov = m_Camera->GetPerspectiveFovDegrees();
                if (ImGui::SliderFloat("FOV", &fov, 10.0f, 100.0f, "%.1f deg"))
                {
                    m_Camera->SetPerspectiveFovDegrees(fov);
                }
            }

            if (m_Camera && m_ProjectionModeIndex == 1)
            {
                float orthoSize = m_Camera->GetOrthographicSize();
                if (ImGui::SliderFloat("Ortho size", &orthoSize, 0.1f, 40.0f, "%.2f"))
                {
                    m_Camera->SetOrthographicSize(orthoSize);
                }
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Atoms");

            if (ImGui::SliderFloat("Atom size", &m_SceneSettings.atomScale, 0.05f, 1.25f, "%.2f"))
            {
                settingsChanged = true;
            }

            if (ImGui::SliderFloat("Atom brightness", &m_SceneSettings.atomBrightness, 0.3f, 2.2f, "%.2f"))
            {
                settingsChanged = true;
            }

            if (ImGui::Checkbox("Override atom color", &m_SceneSettings.overrideAtomColor))
            {
                settingsChanged = true;
            }

            if (m_SceneSettings.overrideAtomColor && ImGui::ColorEdit3("Atom color", &m_SceneSettings.atomOverrideColor.x))
            {
                settingsChanged = true;
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Selection highlight");
            if (ImGui::ColorEdit3("Selection color", &m_SelectionColor.x))
            {
                settingsChanged = true;
            }
            if (ImGui::SliderFloat("Selection outline", &m_SelectionOutlineThickness, 1.0f, 8.0f, "%.1f"))
            {
                settingsChanged = true;
            }

            ImGui::End();
        }

        ImGui::Begin("Tools");
        ImGui::TextUnformatted("Structure I/O (T04)");
        ImGui::InputText("Import path", m_ImportPathBuffer.data(), m_ImportPathBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("Browse##Import"))
        {
            std::string selectedPath;
            if (OpenNativeFileDialog(selectedPath))
            {
                std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", selectedPath.c_str());
            }
        }

        if (ImGui::Button("Load POSCAR/CONTCAR"))
        {
            LoadStructureFromPath(std::string(m_ImportPathBuffer.data()));
        }

        ImGui::SameLine();
        if (ImGui::Button("Load sample"))
        {
            const char *samplePath = "assets/samples/POSCAR";
            std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", samplePath);
            LoadStructureFromPath(samplePath);
        }

        ImGui::Separator();
        const char *interactionModeLabel = (m_InteractionMode == InteractionMode::Navigate) ? "Navigate" : "Select";
        ImGui::Text("Mode: %s", interactionModeLabel);
        ImGui::TextUnformatted("Tab: toggle mode (when viewport focused)");

        if (!m_HasStructureLoaded || m_WorkingStructure.atoms.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.77f, 0.36f, 1.0f));
            ImGui::TextUnformatted("Select mode requires loaded structure.");
            ImGui::PopStyleColor();
        }

        if (ImGui::Button("Toggle mode (Tab)"))
        {
            ToggleInteractionMode();
        }

        if (ImGui::Checkbox("Selection debug log to file", &m_SelectionDebugToFile))
        {
            settingsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear selection log file"))
        {
            std::filesystem::create_directories("logs");
            std::ofstream clearOut("logs/selection_debug.log", std::ios::trunc);
            if (clearOut.is_open())
            {
                clearOut << "";
            }
            AppendSelectionDebugLog("Selection debug log file cleared");
        }
        ImGui::TextUnformatted("Log path: logs/selection_debug.log");

        ImGui::Separator();
        ImGui::Text("Selection: %zu atoms", m_SelectedAtomIndices.size());
        ImGui::TextUnformatted("LMB: select | Ctrl+LMB: add/remove");
        if (ImGui::Button("Clear selection"))
        {
            m_SelectedAtomIndices.clear();
        }

        ImGui::InputText("Export path", m_ExportPathBuffer.data(), m_ExportPathBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("Browse##Export"))
        {
            std::string selectedPath;
            if (SaveNativeFileDialog(selectedPath))
            {
                std::snprintf(m_ExportPathBuffer.data(), m_ExportPathBuffer.size(), "%s", selectedPath.c_str());
            }
        }

        const char *coordinateModes[] = {"Direct", "Cartesian"};
        ImGui::Combo("Export coordinates", &m_ExportCoordinateModeIndex, coordinateModes, IM_ARRAYSIZE(coordinateModes));
        ImGui::SliderInt("Export precision", &m_ExportPrecision, 1, 16);

        if (ImGui::Button("Export POSCAR/CONTCAR"))
        {
            const CoordinateMode exportMode = (m_ExportCoordinateModeIndex == 0)
                                                  ? CoordinateMode::Direct
                                                  : CoordinateMode::Cartesian;
            ExportStructureToPath(std::string(m_ExportPathBuffer.data()), exportMode, m_ExportPrecision);
        }

        ImGui::SameLine();
        if (ImGui::Button("Restore original state"))
        {
            if (m_OriginalStructure.has_value())
            {
                m_WorkingStructure = *m_OriginalStructure;
                m_HasStructureLoaded = true;
                m_LastStructureOperationFailed = false;
                m_LastStructureMessage = "Original file state restored.";
                LogInfo(m_LastStructureMessage);
            }
            else
            {
                m_LastStructureOperationFailed = true;
                m_LastStructureMessage = "Restore failed: no original structure captured yet.";
                LogWarn(m_LastStructureMessage);
            }
        }

        if (m_HasStructureLoaded)
        {
            const char *modeLabel = m_WorkingStructure.coordinateMode == CoordinateMode::Direct ? "Direct" : "Cartesian";
            ImGui::Text("Loaded: %d atoms | %zu species | mode: %s",
                        m_WorkingStructure.GetAtomCount(),
                        m_WorkingStructure.species.size(),
                        modeLabel);
            ImGui::Text("Title: %s", m_WorkingStructure.title.empty() ? "(empty)" : m_WorkingStructure.title.c_str());
        }
        else
        {
            ImGui::TextUnformatted("Loaded: none");
        }

        if (!m_LastStructureMessage.empty())
        {
            const ImVec4 color = m_LastStructureOperationFailed
                                     ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                                     : ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", m_LastStructureMessage.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Theme-Style & color customization");

        int currentTheme = static_cast<int>(m_CurrentTheme);
        const char *items[] = {
            "Dark",
            "Light",
            "Classic",
            "PhotoshopStyle",
            "WarmSlate"};

        if (ImGui::Combo("Style preset", &currentTheme, items, IM_ARRAYSIZE(items)))
        {
            m_CurrentTheme = static_cast<ThemePreset>(currentTheme);
            ApplyTheme(m_CurrentTheme);
            settingsChanged = true;
        }

        float uiScale = m_FontScale;
        if (ImGui::SliderFloat("Font size scale", &uiScale, 0.7f, 2.0f, "%.2f"))
        {
            ApplyFontScale(uiScale);
            settingsChanged = true;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Camera sensitivity");

        float orbitSensitivity = m_CameraOrbitSensitivity;
        if (ImGui::SliderFloat("Orbit sensitivity", &orbitSensitivity, 0.05f, 4.0f, "%.2fx"))
        {
            m_CameraOrbitSensitivity = orbitSensitivity;
            ApplyCameraSensitivity();
            settingsChanged = true;
        }

        float panSensitivity = m_CameraPanSensitivity;
        if (ImGui::SliderFloat("Pan sensitivity", &panSensitivity, 0.05f, 4.0f, "%.2fx"))
        {
            m_CameraPanSensitivity = panSensitivity;
            ApplyCameraSensitivity();
            settingsChanged = true;
        }

        float zoomSensitivity = m_CameraZoomSensitivity;
        if (ImGui::SliderFloat("Zoom sensitivity", &zoomSensitivity, 0.05f, 4.0f, "%.2fx"))
        {
            m_CameraZoomSensitivity = zoomSensitivity;
            ApplyCameraSensitivity();
            settingsChanged = true;
        }

        if (ImGui::Button("Save UI settings"))
        {
            SaveSettings();
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("(auto-save on change enabled)");

        std::size_t errorCount = Logger::Get().GetErrorCount();
        ImGui::Separator();
        ImGui::Text("Errors captured: %zu", errorCount);
        ImGui::End();

        if (m_ShowLogPanel)
        {
            ImGui::Begin("Log / Errors", &m_ShowLogPanel);

            const char *filterItems[] = {
                "All",
                "Warnings + Errors",
                "Errors only"};

            if (ImGui::Combo("Filter", &m_LogFilter, filterItems, IM_ARRAYSIZE(filterItems)))
            {
                settingsChanged = true;
            }

            if (ImGui::Checkbox("Auto-scroll", &m_LogAutoScroll))
            {
                settingsChanged = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Clear log"))
            {
                Logger::Get().Clear();
            }

            ImGui::Separator();

            ImGui::BeginChild("LogEntries", ImVec2(0.0f, 0.0f), true);
            const auto entries = Logger::Get().GetEntriesSnapshot();
            for (const auto &entry : entries)
            {
                if (m_LogFilter == 1 && entry.level == LogLevel::Info)
                {
                    continue;
                }
                if (m_LogFilter == 2 && entry.level != LogLevel::Error)
                {
                    continue;
                }

                ImVec4 color = ImVec4(0.86f, 0.88f, 0.90f, 1.0f);
                const char *levelText = "INFO";

                if (entry.level == LogLevel::Warn)
                {
                    color = ImVec4(0.96f, 0.76f, 0.35f, 1.0f);
                    levelText = "WARN";
                }
                else if (entry.level == LogLevel::Error)
                {
                    color = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
                    levelText = "ERROR";
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::Text("[%s] [%s] %s", entry.timestamp.c_str(), levelText, entry.message.c_str());
                ImGui::PopStyleColor();
            }

            if (m_LogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            {
                ImGui::SetScrollHereY(1.0f);
            }

            ImGui::EndChild();
            ImGui::End();
        }

        ImGui::End();

        if (m_ShowDemoWindow)
        {
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
        }

        if (settingsChanged)
        {
            SaveSettings();
        }
    }

} // namespace ds
