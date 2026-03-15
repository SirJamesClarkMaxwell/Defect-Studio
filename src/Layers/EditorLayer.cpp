#include "Layers/EditorLayer.h"

#include "Core/ApplicationContext.h"
#include "Core/Logger.h"
#include "Renderer/OpenGLRendererBackend.h"
#include "Renderer/OrbitCamera.h"

#include <algorithm>

#include <imgui.h>
#include <ImGuizmo.h>
#define IMVIEWGUIZMO_IMPLEMENTATION
#include <ImViewGuizmo.h>

#include <cctype>
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
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
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

        float NormalizeAngleRadians(float angle)
        {
            const float twoPi = glm::two_pi<float>();
            while (angle > glm::pi<float>())
            {
                angle -= twoPi;
            }
            while (angle < -glm::pi<float>())
            {
                angle += twoPi;
            }
            return angle;
        }

        float LerpAngleRadians(float from, float to, float t)
        {
            return from + NormalizeAngleRadians(to - from) * t;
        }

        float EaseOutCubic(float t)
        {
            const float x = glm::clamp(t, 0.0f, 1.0f);
            const float inv = 1.0f - x;
            return 1.0f - inv * inv * inv;
        }

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

        const std::string startupImportPath = std::string(m_ImportPathBuffer.data());
        if (!startupImportPath.empty() && std::filesystem::exists(startupImportPath))
        {
            LoadStructureFromPath(startupImportPath);
        }
        else if (!startupImportPath.empty())
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Startup import skipped: file not found: " + startupImportPath;
            LogWarn(m_LastStructureMessage);
        }

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

        if (allowCameraInput && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || std::abs(scrollDelta) > 0.0001f))
        {
            m_CameraTransitionActive = false;
        }
        UpdateCameraOrbitTransition(deltaTime);

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

        if (m_Show3DCursor)
        {
            const std::vector<glm::vec3> cursorPosition = {m_CursorPosition};
            const std::vector<glm::vec3> cursorColor = {m_CursorColor};
            SceneRenderSettings cursorSettings = m_SceneSettings;
            cursorSettings.drawGrid = false;
            cursorSettings.overrideAtomColor = true;
            cursorSettings.atomOverrideColor = m_CursorColor;
            cursorSettings.atomScale = m_CursorVisualScale;
            cursorSettings.atomBrightness = std::max(1.0f, m_SceneSettings.atomBrightness + 0.2f);
            cursorSettings.atomWireframe = true;
            cursorSettings.atomWireframeWidth = std::max(1.0f, m_SelectionOutlineThickness);
            m_RenderBackend->RenderAtomsScene(m_Camera->GetViewProjectionMatrix(), cursorPosition, cursorColor, cursorSettings);
        }

        m_RenderBackend->EndFrame();
    }

    bool EditorLayer::IsAtomSelected(std::size_t index) const
    {
        return std::find(m_SelectedAtomIndices.begin(), m_SelectedAtomIndices.end(), index) != m_SelectedAtomIndices.end();
    }

    void EditorLayer::ToggleInteractionMode()
    {
        if (m_TranslateModeActive)
        {
            m_TranslateModeActive = false;
            m_TranslateConstraintAxis = -1;
            m_TranslatePlaneLockAxis = -1;
            m_TranslateIndices.clear();
            m_TranslateInitialCartesian.clear();
            m_TranslateCurrentOffset = glm::vec3(0.0f);
            AppendSelectionDebugLog("Translate mode exited by Tab");
        }

        if (m_InteractionMode == InteractionMode::Navigate)
        {
            if (m_HasStructureLoaded && !m_WorkingStructure.atoms.empty())
            {
                m_InteractionMode = InteractionMode::Select;
                LogInfo("Interaction mode: Select");
                AppendSelectionDebugLog("Mode switched to Select");
                return;
            }

            m_InteractionMode = InteractionMode::ViewSet;
            LogInfo("Interaction mode: ViewSet");
            AppendSelectionDebugLog("Mode switched to ViewSet (Select unavailable)");
            return;
        }

        if (m_InteractionMode == InteractionMode::Select)
        {
            m_InteractionMode = InteractionMode::ViewSet;
            LogInfo("Interaction mode: ViewSet");
            AppendSelectionDebugLog("Mode switched to ViewSet");
            return;
        }

        m_InteractionMode = InteractionMode::Navigate;
        LogInfo("Interaction mode: Navigate");
        AppendSelectionDebugLog("Mode switched to Navigate");
    }

    void EditorLayer::StartCameraOrbitTransition(const glm::vec3 &target, float distance, float yaw, float pitch)
    {
        if (!m_Camera)
        {
            return;
        }

        m_CameraTransitionActive = true;
        m_CameraTransitionElapsed = 0.0f;

        m_CameraTransitionStartTarget = m_Camera->GetTarget();
        m_CameraTransitionEndTarget = target;

        m_CameraTransitionStartDistance = m_Camera->GetDistance();
        m_CameraTransitionEndDistance = distance;

        m_CameraTransitionStartYaw = m_Camera->GetYaw();
        m_CameraTransitionEndYaw = yaw;

        m_CameraTransitionStartPitch = m_Camera->GetPitch();
        m_CameraTransitionEndPitch = pitch;
    }

    void EditorLayer::UpdateCameraOrbitTransition(float deltaTime)
    {
        if (!m_CameraTransitionActive || !m_Camera)
        {
            return;
        }

        m_CameraTransitionElapsed += std::max(0.0f, deltaTime);
        const float duration = std::max(0.01f, m_CameraTransitionDuration);
        const float alpha = glm::clamp(m_CameraTransitionElapsed / duration, 0.0f, 1.0f);
        const float t = EaseOutCubic(alpha);

        const glm::vec3 target = glm::mix(m_CameraTransitionStartTarget, m_CameraTransitionEndTarget, t);
        const float distance = glm::mix(m_CameraTransitionStartDistance, m_CameraTransitionEndDistance, t);
        const float yaw = LerpAngleRadians(m_CameraTransitionStartYaw, m_CameraTransitionEndYaw, t);
        const float pitch = LerpAngleRadians(m_CameraTransitionStartPitch, m_CameraTransitionEndPitch, t);

        m_Camera->SetOrbitState(target, distance, yaw, pitch);

        if (alpha >= 1.0f)
        {
            m_CameraTransitionActive = false;
        }
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

    glm::vec3 EditorLayer::GetAtomCartesianPosition(std::size_t atomIndex) const
    {
        if (atomIndex >= m_WorkingStructure.atoms.size())
        {
            return glm::vec3(0.0f);
        }

        glm::vec3 position = m_WorkingStructure.atoms[atomIndex].position;
        if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
        {
            position = m_WorkingStructure.DirectToCartesian(position);
        }
        return position;
    }

    void EditorLayer::SetAtomCartesianPosition(std::size_t atomIndex, const glm::vec3 &position)
    {
        if (atomIndex >= m_WorkingStructure.atoms.size())
        {
            return;
        }

        if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
        {
            m_WorkingStructure.atoms[atomIndex].position = m_WorkingStructure.CartesianToDirect(position);
            return;
        }

        m_WorkingStructure.atoms[atomIndex].position = position;
    }

    bool EditorLayer::PickWorldPositionOnGrid(const glm::vec2 &mousePos, glm::vec3 &outWorldPosition) const
    {
        if (!m_Camera)
        {
            return false;
        }

        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return false;
        }

        const float x = (mousePos.x - m_ViewportRectMin.x) / width;
        const float y = (mousePos.y - m_ViewportRectMin.y) / height;
        if (x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f)
        {
            return false;
        }

        const float ndcX = x * 2.0f - 1.0f;
        const float ndcY = 1.0f - y * 2.0f;

        const glm::mat4 inverseViewProjection = glm::inverse(m_Camera->GetViewProjectionMatrix());
        glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

        if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f)
        {
            return false;
        }

        const glm::vec3 worldNear = glm::vec3(nearPoint) / nearPoint.w;
        const glm::vec3 worldFar = glm::vec3(farPoint) / farPoint.w;
        const glm::vec3 ray = worldFar - worldNear;
        const float rayZ = ray.z;
        if (std::abs(rayZ) < 1e-6f)
        {
            return false;
        }

        const float planeZ = m_SceneSettings.gridOrigin.z;
        const float t = (planeZ - worldNear.z) / rayZ;
        if (t < 0.0f)
        {
            return false;
        }

        glm::vec3 hit = worldNear + ray * t;
        hit.z = planeZ;

        if (m_CursorSnapToGrid)
        {
            const float spacing = std::max(0.0001f, m_SceneSettings.gridSpacing);
            hit.x = std::round((hit.x - m_SceneSettings.gridOrigin.x) / spacing) * spacing + m_SceneSettings.gridOrigin.x;
            hit.y = std::round((hit.y - m_SceneSettings.gridOrigin.y) / spacing) * spacing + m_SceneSettings.gridOrigin.y;
        }

        outWorldPosition = hit;
        return true;
    }

    void EditorLayer::Set3DCursorFromScreenPoint(const glm::vec2 &mousePos)
    {
        glm::vec3 worldHit(0.0f);
        if (!PickWorldPositionOnGrid(mousePos, worldHit))
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "3D cursor placement failed: could not project onto grid plane.";
            LogWarn(m_LastStructureMessage);
            return;
        }

        m_CursorPosition = worldHit;
        m_AddAtomPosition = worldHit;
        m_AddAtomCoordinateModeIndex = 1;
        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "3D cursor moved to (" + std::to_string(worldHit.x) + ", " + std::to_string(worldHit.y) + ", " + std::to_string(worldHit.z) + ")";
        LogInfo(m_LastStructureMessage);
    }

    void EditorLayer::DrawPeriodicTableWindow()
    {
        if (!m_PeriodicTableOpen)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(760.0f, 430.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Periodic Table", &m_PeriodicTableOpen))
        {
            ImGui::End();
            return;
        }

        const char *tableRows[7][18] = {
            {"H", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "He"},
            {"Li", "Be", "", "", "", "", "", "", "", "", "", "", "B", "C", "N", "O", "F", "Ne"},
            {"Na", "Mg", "", "", "", "", "", "", "", "", "", "", "Al", "Si", "P", "S", "Cl", "Ar"},
            {"K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr"},
            {"Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe"},
            {"Cs", "Ba", "", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn"},
            {"Fr", "Ra", "", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "", "", "", "", "", "", ""}};

        const char *lanthanoids[15] = {"La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu"};
        const char *actinoids[15] = {"Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr"};

        const float cellWidth = 36.0f;
        const float cellHeight = 32.0f;

        auto setSelectedElement = [&](const char *symbol)
        {
            std::snprintf(m_AddAtomElementBuffer.data(), m_AddAtomElementBuffer.size(), "%s", symbol);
            m_PeriodicTableOpen = false;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Selected element: " + std::string(symbol);
        };

        for (int row = 0; row < 7; ++row)
        {
            for (int col = 0; col < 18; ++col)
            {
                if (col > 0)
                {
                    ImGui::SameLine();
                }

                const char *symbol = tableRows[row][col];
                if (symbol[0] == '\0')
                {
                    ImGui::Dummy(ImVec2(cellWidth, cellHeight));
                    continue;
                }

                const bool isSelected = std::string(m_AddAtomElementBuffer.data()) == symbol;
                if (isSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 0.92f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.34f, 0.64f, 0.98f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.48f, 0.84f, 1.0f));
                }

                if (ImGui::Button(symbol, ImVec2(cellWidth, cellHeight)))
                {
                    setSelectedElement(symbol);
                }

                if (isSelected)
                {
                    ImGui::PopStyleColor(3);
                }
            }
        }

        ImGui::Separator();

        ImGui::TextUnformatted("Lanthanoids");
        ImGui::SameLine();
        for (int i = 0; i < 15; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine();
            }

            const char *symbol = lanthanoids[i];
            if (ImGui::Button(symbol, ImVec2(cellWidth, cellHeight)))
            {
                setSelectedElement(symbol);
            }
        }

        ImGui::TextUnformatted("Actinoids");
        ImGui::SameLine();
        for (int i = 0; i < 15; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine();
            }

            const char *symbol = actinoids[i];
            if (ImGui::Button(symbol, ImVec2(cellWidth, cellHeight)))
            {
                setSelectedElement(symbol);
            }
        }

        ImGui::End();
    }

    void EditorLayer::SelectAtomsInScreenRect(const glm::vec2 &screenStart, const glm::vec2 &screenEnd, bool additiveSelection)
    {
        if (!m_Camera || !m_HasStructureLoaded || m_WorkingStructure.atoms.empty())
        {
            return;
        }

        const float left = std::min(screenStart.x, screenEnd.x);
        const float right = std::max(screenStart.x, screenEnd.x);
        const float top = std::min(screenStart.y, screenEnd.y);
        const float bottom = std::max(screenStart.y, screenEnd.y);

        if (!additiveSelection)
        {
            m_SelectedAtomIndices.clear();
        }

        const glm::mat4 viewProjection = m_Camera->GetViewProjectionMatrix();
        std::size_t addedCount = 0;

        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return;
        }

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
            if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < -1.0f || ndc.z > 1.0f)
            {
                continue;
            }

            const float screenX = m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width;
            const float screenY = m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height;

            if (screenX >= left && screenX <= right && screenY >= top && screenY <= bottom)
            {
                if (!IsAtomSelected(i))
                {
                    m_SelectedAtomIndices.push_back(i);
                    ++addedCount;
                }
            }
        }

        AppendSelectionDebugLog(
            "Box selection: rect=(" + std::to_string(left) + "," + std::to_string(top) + ")-" +
            "(" + std::to_string(right) + "," + std::to_string(bottom) + ")" +
            " additive=" + (additiveSelection ? std::string("1") : std::string("0")) +
            " selected=" + std::to_string(m_SelectedAtomIndices.size()) +
            " added=" + std::to_string(addedCount));
    }

    void EditorLayer::HandleViewportSelection()
    {
        if (m_TranslateModeActive)
        {
            return;
        }

        if (m_BlockSelectionThisFrame)
        {
            return;
        }

        if (m_GizmoConsumedMouseThisFrame)
        {
            return;
        }

        if (m_BoxSelectArmed)
        {
            return;
        }

        const ImGuiIO &io = ImGui::GetIO();
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            return;
        }

        const bool gizmoConsumesInput = (m_GizmoEnabled && (ImGuizmo::IsOver() || ImGuizmo::IsUsing())) ||
                                        (m_ViewGuizmoEnabled && (ImViewGuizmo::IsOver() || ImViewGuizmo::IsUsing()));
        if (gizmoConsumesInput)
        {
            AppendSelectionDebugLog("Ignored click: gizmo consumed input");
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
                bool preserveSelectionForGizmo = false;
                if (m_GizmoEnabled && !m_SelectedAtomIndices.empty() && m_Camera)
                {
                    glm::vec3 pivot(0.0f);
                    std::size_t validCount = 0;
                    for (std::size_t atomIndex : m_SelectedAtomIndices)
                    {
                        if (atomIndex >= m_WorkingStructure.atoms.size())
                        {
                            continue;
                        }

                        pivot += GetAtomCartesianPosition(atomIndex);
                        ++validCount;
                    }

                    if (validCount > 0)
                    {
                        pivot /= static_cast<float>(validCount);

                        const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(pivot, 1.0f);
                        if (clip.w > 0.0001f)
                        {
                            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                            const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                            const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                            const glm::vec2 pivotScreen(
                                m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                                m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);

                            const float distanceToPivot = glm::length(mousePos - pivotScreen);
                            preserveSelectionForGizmo = distanceToPivot <= 48.0f;
                        }
                    }
                }

                if (preserveSelectionForGizmo)
                {
                    AppendSelectionDebugLog("Selection preserved: click near gizmo pivot");
                    return;
                }

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

    bool EditorLayer::AddAtomToStructure(const std::string &elementSymbol, const glm::vec3 &position, CoordinateMode inputMode)
    {
        if (!m_HasStructureLoaded)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Add atom failed: no structure loaded.";
            LogWarn(m_LastStructureMessage);
            return false;
        }

        std::string symbol;
        symbol.reserve(elementSymbol.size());
        for (char c : elementSymbol)
        {
            if (!std::isspace(static_cast<unsigned char>(c)))
            {
                symbol.push_back(c);
            }
        }

        if (symbol.empty() || !std::isalpha(static_cast<unsigned char>(symbol[0])))
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Add atom failed: invalid element symbol.";
            LogWarn(m_LastStructureMessage);
            return false;
        }

        symbol[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(symbol[0])));
        for (std::size_t i = 1; i < symbol.size(); ++i)
        {
            symbol[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(symbol[i])));
        }

        Atom atom;
        atom.element = symbol;
        atom.selectiveDynamics = false;
        atom.selectiveFlags = {true, true, true};

        if (inputMode == m_WorkingStructure.coordinateMode)
        {
            atom.position = position;
        }
        else if (inputMode == CoordinateMode::Direct)
        {
            atom.position = m_WorkingStructure.DirectToCartesian(position);
        }
        else
        {
            atom.position = m_WorkingStructure.CartesianToDirect(position);
        }

        m_WorkingStructure.atoms.push_back(atom);
        m_WorkingStructure.RebuildSpeciesFromAtoms();

        m_SelectedAtomIndices.clear();
        m_SelectedAtomIndices.push_back(m_WorkingStructure.atoms.size() - 1);

        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Added atom " + symbol + " at (" +
                                 std::to_string(position.x) + ", " +
                                 std::to_string(position.y) + ", " +
                                 std::to_string(position.z) + ") [" +
                                 (inputMode == CoordinateMode::Direct ? "Direct" : "Cartesian") + "]";
        LogInfo(m_LastStructureMessage + ", atom count=" + std::to_string(m_WorkingStructure.GetAtomCount()));
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
            else if (key == "viewport_cursor_show")
            {
                m_Show3DCursor = (value == "1");
            }
            else if (key == "viewport_cursor_x")
            {
                try
                {
                    m_CursorPosition.x = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cursor_y")
            {
                try
                {
                    m_CursorPosition.y = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cursor_z")
            {
                try
                {
                    m_CursorPosition.z = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cursor_scale")
            {
                try
                {
                    m_CursorVisualScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_cursor_snap")
            {
                m_CursorSnapToGrid = (value == "1");
            }
            else if (key == "viewport_view_gizmo")
            {
                m_ViewGuizmoEnabled = (value == "1");
            }
            else if (key == "viewport_transform_gizmo_size")
            {
                try
                {
                    m_TransformGizmoSize = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_view_gizmo_scale")
            {
                try
                {
                    m_ViewGizmoScale = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_view_gizmo_offset_right")
            {
                try
                {
                    m_ViewGizmoOffsetRight = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_view_gizmo_offset_top")
            {
                try
                {
                    m_ViewGizmoOffsetTop = std::stof(value);
                }
                catch (...)
                {
                }
            }
            else if (key == "viewport_fallback_marker_scale")
            {
                try
                {
                    m_FallbackGizmoVisualScale = std::stof(value);
                }
                catch (...)
                {
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
        if (m_CursorVisualScale < 0.05f)
            m_CursorVisualScale = 0.05f;
        if (m_CursorVisualScale > 2.0f)
            m_CursorVisualScale = 2.0f;
        if (m_TransformGizmoSize < 0.05f)
            m_TransformGizmoSize = 0.05f;
        if (m_TransformGizmoSize > 0.35f)
            m_TransformGizmoSize = 0.35f;
        if (m_ViewGizmoScale < 0.35f)
            m_ViewGizmoScale = 0.35f;
        if (m_ViewGizmoScale > 2.20f)
            m_ViewGizmoScale = 2.20f;
        if (m_ViewGizmoOffsetRight < 0.0f)
            m_ViewGizmoOffsetRight = 0.0f;
        if (m_ViewGizmoOffsetTop < 0.0f)
            m_ViewGizmoOffsetTop = 0.0f;
        if (m_FallbackGizmoVisualScale < 0.5f)
            m_FallbackGizmoVisualScale = 0.5f;
        if (m_FallbackGizmoVisualScale > 6.0f)
            m_FallbackGizmoVisualScale = 6.0f;
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
        out << "viewport_cursor_show=" << (m_Show3DCursor ? "1" : "0") << '\n';
        out << "viewport_cursor_x=" << m_CursorPosition.x << '\n';
        out << "viewport_cursor_y=" << m_CursorPosition.y << '\n';
        out << "viewport_cursor_z=" << m_CursorPosition.z << '\n';
        out << "viewport_cursor_scale=" << m_CursorVisualScale << '\n';
        out << "viewport_cursor_snap=" << (m_CursorSnapToGrid ? "1" : "0") << '\n';
        out << "viewport_view_gizmo=" << (m_ViewGuizmoEnabled ? "1" : "0") << '\n';
        out << "viewport_transform_gizmo_size=" << m_TransformGizmoSize << '\n';
        out << "viewport_view_gizmo_scale=" << m_ViewGizmoScale << '\n';
        out << "viewport_view_gizmo_offset_right=" << m_ViewGizmoOffsetRight << '\n';
        out << "viewport_view_gizmo_offset_top=" << m_ViewGizmoOffsetTop << '\n';
        out << "viewport_fallback_marker_scale=" << m_FallbackGizmoVisualScale << '\n';
    }

    void EditorLayer::OnImGuiRender()
    {
        bool settingsChanged = false;
        const ImGuiIO &io = ImGui::GetIO();

        static bool s_LastCanRenderTransformGizmo = false;
        static bool s_LastValidPivot = false;
        static bool s_LastGizmoOver = false;
        static bool s_LastGizmoUsing = false;
        static bool s_LastGizmoManipulated = false;
        static bool s_GizmoStateInitialized = false;
        static std::size_t s_LastSelectedCount = 0;

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
        m_BlockSelectionThisFrame = false;
        m_GizmoConsumedMouseThisFrame = false;

        if (m_ViewportFocused && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
        {
            ToggleInteractionMode();
        }

        auto beginTranslateMode = [&]()
        {
            if (!m_Camera || !m_HasStructureLoaded || m_SelectedAtomIndices.empty())
            {
                AppendSelectionDebugLog("Translate mode start denied: missing camera/structure/selection");
                return;
            }

            m_TranslateIndices.clear();
            m_TranslateInitialCartesian.clear();
            m_TranslateCurrentOffset = glm::vec3(0.0f);
            m_TranslateConstraintAxis = -1;
            m_TranslatePlaneLockAxis = -1;

            for (std::size_t atomIndex : m_SelectedAtomIndices)
            {
                if (atomIndex >= m_WorkingStructure.atoms.size())
                {
                    continue;
                }

                m_TranslateIndices.push_back(atomIndex);
                m_TranslateInitialCartesian.push_back(GetAtomCartesianPosition(atomIndex));
            }

            if (m_TranslateIndices.empty())
            {
                AppendSelectionDebugLog("Translate mode start denied: no valid selected indices");
                return;
            }

            m_TranslateModeActive = true;
            m_InteractionMode = InteractionMode::Translate;
            m_TranslateLastMousePos = glm::vec2(io.MousePos.x, io.MousePos.y);
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Translate mode active: move mouse, X/Y/Z constrain, Shift+X/Y/Z plane lock, Ctrl snap.";
            AppendSelectionDebugLog("Translate mode started (G)");
        };

        auto cancelTranslateMode = [&]()
        {
            if (!m_TranslateModeActive)
            {
                return;
            }

            const std::size_t count = std::min(m_TranslateIndices.size(), m_TranslateInitialCartesian.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                SetAtomCartesianPosition(m_TranslateIndices[i], m_TranslateInitialCartesian[i]);
            }

            m_TranslateModeActive = false;
            m_InteractionMode = InteractionMode::Select;
            m_TranslateIndices.clear();
            m_TranslateInitialCartesian.clear();
            m_TranslateCurrentOffset = glm::vec3(0.0f);
            m_TranslateConstraintAxis = -1;
            m_TranslatePlaneLockAxis = -1;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Translate mode canceled.";
            AppendSelectionDebugLog("Translate mode canceled (Esc/RMB)");
        };

        auto commitTranslateMode = [&]()
        {
            if (!m_TranslateModeActive)
            {
                return;
            }

            m_TranslateModeActive = false;
            m_InteractionMode = InteractionMode::Select;
            m_TranslateIndices.clear();
            m_TranslateInitialCartesian.clear();
            m_TranslateCurrentOffset = glm::vec3(0.0f);
            m_TranslateConstraintAxis = -1;
            m_TranslatePlaneLockAxis = -1;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Translate mode applied.";
            AppendSelectionDebugLog("Translate mode applied (LMB/Enter)");
        };

        if (m_ViewportFocused && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_G, false))
        {
            if (m_TranslateModeActive)
            {
                commitTranslateMode();
            }
            else
            {
                beginTranslateMode();
            }
        }

        if (m_ViewportFocused && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Q, false))
        {
            m_ModePiePopupPos = glm::vec2(io.MousePos.x, io.MousePos.y);
            ImGui::OpenPopup("InteractionModePie");
        }

        ImGui::SetNextWindowPos(ImVec2(m_ModePiePopupPos.x, m_ModePiePopupPos.y), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopup("InteractionModePie"))
        {
            ImGui::TextUnformatted("Interaction Mode");
            ImGui::Separator();

            if (ImGui::Selectable("Select"))
            {
                m_InteractionMode = InteractionMode::Select;
                m_TranslateModeActive = false;
            }
            if (ImGui::Selectable("Navigate"))
            {
                m_InteractionMode = InteractionMode::Navigate;
                m_TranslateModeActive = false;
            }
            if (ImGui::Selectable("ViewSet"))
            {
                m_InteractionMode = InteractionMode::ViewSet;
                m_TranslateModeActive = false;
            }
            if (ImGui::Selectable("Translate (G)"))
            {
                beginTranslateMode();
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Q: open pie");
            ImGui::TextUnformatted("G: start/confirm translate");
            ImGui::EndPopup();
        }

        if (m_TranslateModeActive && m_Camera)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                cancelTranslateMode();
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                commitTranslateMode();
            }
            else
            {
                if (ImGui::IsKeyPressed(ImGuiKey_X, false))
                {
                    if (io.KeyShift)
                    {
                        m_TranslatePlaneLockAxis = 0;
                        m_TranslateConstraintAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: plane lock X (Shift+X)");
                    }
                    else
                    {
                        m_TranslateConstraintAxis = 0;
                        m_TranslatePlaneLockAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: axis X");
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                {
                    if (io.KeyShift)
                    {
                        m_TranslatePlaneLockAxis = 1;
                        m_TranslateConstraintAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: plane lock Y (Shift+Y)");
                    }
                    else
                    {
                        m_TranslateConstraintAxis = 1;
                        m_TranslatePlaneLockAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: axis Y");
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                {
                    if (io.KeyShift)
                    {
                        m_TranslatePlaneLockAxis = 2;
                        m_TranslateConstraintAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: plane lock Z (Shift+Z)");
                    }
                    else
                    {
                        m_TranslateConstraintAxis = 2;
                        m_TranslatePlaneLockAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: axis Z");
                    }
                }

                const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                const glm::vec2 mouseDelta = mousePos - m_TranslateLastMousePos;
                m_TranslateLastMousePos = mousePos;

                const glm::vec3 forward = glm::normalize(glm::vec3(
                    std::cos(m_Camera->GetPitch()) * std::sin(m_Camera->GetYaw()),
                    std::cos(m_Camera->GetPitch()) * std::cos(m_Camera->GetYaw()),
                    std::sin(m_Camera->GetPitch())));
                const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
                const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
                const glm::vec3 up = glm::normalize(glm::cross(right, forward));

                const float panSpeed = 0.006f * m_Camera->GetDistance();
                glm::vec3 frameDelta = (-right * mouseDelta.x + up * mouseDelta.y) * panSpeed;

                const glm::vec3 axis[3] = {
                    glm::vec3(1.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f)};

                if (m_TranslateConstraintAxis >= 0 && m_TranslateConstraintAxis < 3)
                {
                    frameDelta = axis[m_TranslateConstraintAxis] * glm::dot(frameDelta, axis[m_TranslateConstraintAxis]);
                }
                else if (m_TranslatePlaneLockAxis >= 0 && m_TranslatePlaneLockAxis < 3)
                {
                    frameDelta -= axis[m_TranslatePlaneLockAxis] * glm::dot(frameDelta, axis[m_TranslatePlaneLockAxis]);
                }

                m_TranslateCurrentOffset += frameDelta;

                glm::vec3 appliedOffset = m_TranslateCurrentOffset;
                if (io.KeyCtrl)
                {
                    const float snapStep = std::max(0.001f, m_GizmoTranslateSnap);

                    if (m_TranslateConstraintAxis >= 0 && m_TranslateConstraintAxis < 3)
                    {
                        const float scalar = glm::dot(m_TranslateCurrentOffset, axis[m_TranslateConstraintAxis]);
                        const float snapped = std::round(scalar / snapStep) * snapStep;
                        appliedOffset = axis[m_TranslateConstraintAxis] * snapped;
                    }
                    else
                    {
                        appliedOffset.x = std::round(appliedOffset.x / snapStep) * snapStep;
                        appliedOffset.y = std::round(appliedOffset.y / snapStep) * snapStep;
                        appliedOffset.z = std::round(appliedOffset.z / snapStep) * snapStep;

                        if (m_TranslatePlaneLockAxis >= 0 && m_TranslatePlaneLockAxis < 3)
                        {
                            if (m_TranslatePlaneLockAxis == 0)
                                appliedOffset.x = 0.0f;
                            if (m_TranslatePlaneLockAxis == 1)
                                appliedOffset.y = 0.0f;
                            if (m_TranslatePlaneLockAxis == 2)
                                appliedOffset.z = 0.0f;
                        }
                    }
                }

                const std::size_t count = std::min(m_TranslateIndices.size(), m_TranslateInitialCartesian.size());
                for (std::size_t i = 0; i < count; ++i)
                {
                    SetAtomCartesianPosition(m_TranslateIndices[i], m_TranslateInitialCartesian[i] + appliedOffset);
                }

                m_BlockSelectionThisFrame = true;
                m_GizmoConsumedMouseThisFrame = true;
            }
        }

        if (m_ViewportFocused && m_InteractionMode == InteractionMode::ViewSet && !io.WantTextInput && m_Camera)
        {
            const glm::vec3 target = m_Camera->GetTarget();
            const float distance = m_Camera->GetDistance();
            bool viewKeyUsed = false;
            float yaw = m_Camera->GetYaw();
            float pitch = m_Camera->GetPitch();

            if (ImGui::IsKeyPressed(ImGuiKey_T, false))
            {
                pitch = glm::half_pi<float>() - 0.01f;
                yaw = 0.0f;
                viewKeyUsed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_B, false))
            {
                pitch = -glm::half_pi<float>() + 0.01f;
                yaw = 0.0f;
                viewKeyUsed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_L, false))
            {
                yaw = -glm::half_pi<float>();
                pitch = 0.0f;
                viewKeyUsed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            {
                yaw = glm::half_pi<float>();
                pitch = 0.0f;
                viewKeyUsed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_P, false))
            {
                yaw = 0.0f;
                pitch = 0.0f;
                viewKeyUsed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_K, false))
            {
                yaw = glm::pi<float>();
                pitch = 0.0f;
                viewKeyUsed = true;
            }

            if (viewKeyUsed)
            {
                StartCameraOrbitTransition(target, distance, yaw, pitch);
                m_LastStructureOperationFailed = false;
                m_LastStructureMessage = "ViewSet: smooth camera transition started from keyboard.";
            }
        }

        if (m_ViewportFocused && m_InteractionMode == InteractionMode::Select && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_B, false))
        {
            m_BoxSelectArmed = true;
            m_BoxSelecting = false;
            AppendSelectionDebugLog("Box select armed (press LMB and drag)");
        }

        if (m_BoxSelectArmed && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            m_BoxSelectArmed = false;
            m_BoxSelecting = false;
            AppendSelectionDebugLog("Box select canceled with Escape");
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

                const bool canRenderTransformGizmo = m_GizmoEnabled && m_Camera && !m_SelectedAtomIndices.empty() && m_HasStructureLoaded;
                if (canRenderTransformGizmo != s_LastCanRenderTransformGizmo || m_SelectedAtomIndices.size() != s_LastSelectedCount)
                {
                    std::ostringstream gizmoPreconditionsLog;
                    gizmoPreconditionsLog << "Gizmo preconditions: canRender=" << (canRenderTransformGizmo ? "1" : "0")
                                          << " gizmoEnabled=" << (m_GizmoEnabled ? "1" : "0")
                                          << " hasCamera=" << (m_Camera ? "1" : "0")
                                          << " hasStructure=" << (m_HasStructureLoaded ? "1" : "0")
                                          << " selectedCount=" << m_SelectedAtomIndices.size();
                    AppendSelectionDebugLog(gizmoPreconditionsLog.str());
                    s_LastCanRenderTransformGizmo = canRenderTransformGizmo;
                    s_LastSelectedCount = m_SelectedAtomIndices.size();
                }

                if (m_GizmoEnabled && m_Camera && !m_SelectedAtomIndices.empty() && m_HasStructureLoaded)
                {
                    glm::vec3 pivot(0.0f);
                    std::size_t validCount = 0;
                    for (std::size_t atomIndex : m_SelectedAtomIndices)
                    {
                        if (atomIndex >= m_WorkingStructure.atoms.size())
                        {
                            continue;
                        }

                        pivot += GetAtomCartesianPosition(atomIndex);
                        ++validCount;
                    }

                    if (validCount > 0)
                    {
                        pivot /= static_cast<float>(validCount);
                        glm::vec2 pivotScreen(0.0f, 0.0f);
                        bool pivotInViewport = false;

                        if (!s_LastValidPivot)
                        {
                            std::ostringstream pivotLog;
                            pivotLog << "Gizmo pivot ready: validCount=" << validCount
                                     << " pivot=(" << pivot.x << "," << pivot.y << "," << pivot.z << ")";
                            AppendSelectionDebugLog(pivotLog.str());

                            const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(pivot, 1.0f);
                            if (clip.w > 0.0001f)
                            {
                                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                                const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                                const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                                const glm::vec2 pivotScreen(
                                    m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                                    m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
                                const bool inViewport =
                                    pivotScreen.x >= m_ViewportRectMin.x && pivotScreen.x <= m_ViewportRectMax.x &&
                                    pivotScreen.y >= m_ViewportRectMin.y && pivotScreen.y <= m_ViewportRectMax.y;

                                std::ostringstream projectionLog;
                                projectionLog << "Gizmo pivot projection: ndc=(" << ndc.x << "," << ndc.y << "," << ndc.z << ")"
                                              << " screen=(" << pivotScreen.x << "," << pivotScreen.y << ")"
                                              << " inViewport=" << (inViewport ? "1" : "0");
                                AppendSelectionDebugLog(projectionLog.str());
                            }
                            else
                            {
                                AppendSelectionDebugLog("Gizmo pivot projection: clip.w <= 0 (behind camera).");
                            }
                        }
                        s_LastValidPivot = true;

                        {
                            const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(pivot, 1.0f);
                            if (clip.w > 0.0001f)
                            {
                                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                                const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                                const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                                pivotScreen = glm::vec2(
                                    m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                                    m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
                                pivotInViewport =
                                    pivotScreen.x >= m_ViewportRectMin.x && pivotScreen.x <= m_ViewportRectMax.x &&
                                    pivotScreen.y >= m_ViewportRectMin.y && pivotScreen.y <= m_ViewportRectMax.y;
                                m_FallbackPivotScreen = pivotScreen;
                            }
                        }

                        glm::vec2 fallbackAxisScreenDir[3] = {
                            glm::vec2(1.0f, 0.0f),
                            glm::vec2(0.0f, 1.0f),
                            glm::vec2(0.0f, -1.0f)};
                        glm::vec2 fallbackAxisEnd[3] = {
                            pivotScreen,
                            pivotScreen,
                            pivotScreen};
                        float fallbackPixelsPerWorld[3] = {1.0f, 1.0f, 1.0f};
                        bool fallbackAxisVisible[3] = {false, false, false};
                        int hoveredFallbackAxis = -1;

                        if (pivotInViewport)
                        {
                            const glm::vec3 axisWorld[3] = {
                                glm::vec3(1.0f, 0.0f, 0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f),
                                glm::vec3(0.0f, 0.0f, 1.0f)};
                            const ImU32 axisColor[3] = {
                                IM_COL32(230, 70, 70, 245),
                                IM_COL32(70, 230, 70, 245),
                                IM_COL32(70, 120, 245, 245)};

                            const float markerScale = glm::clamp(m_FallbackGizmoVisualScale, 0.5f, 6.0f);
                            const float centerRadius = (m_FallbackGizmoDragging ? 9.0f : 7.5f) * markerScale;
                            const float thickness = (m_FallbackGizmoDragging ? 3.2f : 2.6f) * markerScale;
                            const float headLength = 12.0f * markerScale;
                            const float headWidth = 8.0f * markerScale;
                            const float axisWorldLen = 1.0f;

                            ImDrawList *overlay = ImGui::GetForegroundDrawList();
                            const ImVec2 p(pivotScreen.x, pivotScreen.y);

                            for (int axis = 0; axis < 3; ++axis)
                            {
                                const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(pivot + axisWorld[axis] * axisWorldLen, 1.0f);
                                if (clip.w <= 0.0001f)
                                {
                                    continue;
                                }

                                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                                const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                                const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                                const glm::vec2 endScreen(
                                    m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                                    m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);

                                glm::vec2 axisVec = endScreen - pivotScreen;
                                const float axisPixels = glm::length(axisVec);
                                if (axisPixels < 8.0f)
                                {
                                    continue;
                                }

                                axisVec /= axisPixels;
                                fallbackAxisVisible[axis] = true;
                                fallbackAxisScreenDir[axis] = axisVec;
                                fallbackAxisEnd[axis] = endScreen;
                                fallbackPixelsPerWorld[axis] = axisPixels / axisWorldLen;

                                const ImVec2 a(p.x, p.y);
                                const ImVec2 b(endScreen.x, endScreen.y);
                                overlay->AddLine(a, b, axisColor[axis], thickness);

                                const glm::vec2 perp(-axisVec.y, axisVec.x);
                                const ImVec2 tip(endScreen.x, endScreen.y);
                                const ImVec2 left(endScreen.x - axisVec.x * headLength + perp.x * headWidth,
                                                 endScreen.y - axisVec.y * headLength + perp.y * headWidth);
                                const ImVec2 right(endScreen.x - axisVec.x * headLength - perp.x * headWidth,
                                                  endScreen.y - axisVec.y * headLength - perp.y * headWidth);
                                overlay->AddTriangleFilled(tip, left, right, axisColor[axis]);
                            }

                            if (!m_FallbackGizmoDragging && m_GizmoOperationIndex == 0)
                            {
                                const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                                const float pickRadius = 10.0f * markerScale;
                                float bestDist = std::numeric_limits<float>::max();

                                for (int axis = 0; axis < 3; ++axis)
                                {
                                    if (!fallbackAxisVisible[axis])
                                    {
                                        continue;
                                    }

                                    const glm::vec2 a = pivotScreen;
                                    const glm::vec2 b = fallbackAxisEnd[axis];
                                    const glm::vec2 ab = b - a;
                                    const float abLen2 = glm::dot(ab, ab);
                                    if (abLen2 < 1.0f)
                                    {
                                        continue;
                                    }

                                    const float t = glm::clamp(glm::dot(mousePos - a, ab) / abLen2, 0.0f, 1.0f);
                                    const glm::vec2 closest = a + ab * t;
                                    const float dist = glm::length(mousePos - closest);
                                    if (dist < pickRadius && dist < bestDist)
                                    {
                                        bestDist = dist;
                                        hoveredFallbackAxis = axis;
                                    }
                                }
                            }

                            if (hoveredFallbackAxis >= 0 || m_FallbackGizmoDragging)
                            {
                                const int axisToHighlight = m_FallbackGizmoDragging ? m_FallbackGizmoAxis : hoveredFallbackAxis;
                                if (axisToHighlight >= 0 && axisToHighlight < 3 && fallbackAxisVisible[axisToHighlight])
                                {
                                    const ImVec2 tip(fallbackAxisEnd[axisToHighlight].x, fallbackAxisEnd[axisToHighlight].y);
                                    overlay->AddCircle(tip, 8.0f * markerScale, IM_COL32(255, 255, 255, 235), 0, 2.0f * markerScale);
                                }
                            }

                            overlay->AddCircleFilled(p, centerRadius, IM_COL32(245, 245, 245, 235));
                            overlay->AddCircle(p, centerRadius + 2.5f * markerScale, IM_COL32(20, 20, 20, 230), 0, 1.6f * markerScale);
                        }

                        glm::mat4 gizmoTransform = glm::translate(glm::mat4(1.0f), pivot);
                        glm::mat4 deltaTransform(1.0f);

                        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
                        ImGuizmo::BeginFrame();
                        ImGuizmo::Enable(true);
                        ImGuizmo::PushID(0);
                        ImGuizmo::SetOrthographic(m_ProjectionModeIndex == 1);
                        ImGuizmo::SetRect(rectMin.x, rectMin.y, size.x, size.y);
                        const glm::vec3 cameraDirection = glm::normalize(glm::vec3(
                            std::cos(m_Camera->GetPitch()) * std::sin(m_Camera->GetYaw()),
                            std::cos(m_Camera->GetPitch()) * std::cos(m_Camera->GetYaw()),
                            std::sin(m_Camera->GetPitch())));
                        const glm::vec3 cameraPosition = m_Camera->GetTarget() - cameraDirection * m_Camera->GetDistance();
                        const float distanceToPivot = glm::max(0.25f, glm::length(cameraPosition - pivot));
                        const float distanceCompensation = glm::clamp(6.0f / distanceToPivot, 0.55f, 1.85f);
                        const float gizmoSize = glm::clamp(m_TransformGizmoSize * distanceCompensation, 0.07f, 0.40f);
                        ImGuizmo::SetGizmoSizeClipSpace(gizmoSize);

                        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
                        if (m_GizmoOperationIndex == 1)
                        {
                            operation = ImGuizmo::ROTATE;
                        }
                        else if (m_GizmoOperationIndex == 2)
                        {
                            operation = ImGuizmo::SCALE;
                        }

                        ImGuizmo::MODE mode = (m_GizmoModeIndex == 0) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

                        float snapValues[3] = {m_GizmoTranslateSnap, m_GizmoTranslateSnap, m_GizmoTranslateSnap};
                        if (operation == ImGuizmo::ROTATE)
                        {
                            snapValues[0] = m_GizmoRotateSnapDeg;
                            snapValues[1] = m_GizmoRotateSnapDeg;
                            snapValues[2] = m_GizmoRotateSnapDeg;
                        }
                        else if (operation == ImGuizmo::SCALE)
                        {
                            snapValues[0] = m_GizmoScaleSnap;
                            snapValues[1] = m_GizmoScaleSnap;
                            snapValues[2] = m_GizmoScaleSnap;
                        }

                        const bool manipulated = ImGuizmo::Manipulate(
                            glm::value_ptr(m_Camera->GetViewMatrix()),
                            glm::value_ptr(m_Camera->GetProjectionMatrix()),
                            operation,
                            mode,
                            glm::value_ptr(gizmoTransform),
                            glm::value_ptr(deltaTransform),
                            m_GizmoSnapEnabled ? snapValues : nullptr);
                        ImGuizmo::PopID();

                        const bool gizmoOver = ImGuizmo::IsOver();
                        const bool gizmoUsing = ImGuizmo::IsUsing();
                        if (!s_GizmoStateInitialized || gizmoOver != s_LastGizmoOver || gizmoUsing != s_LastGizmoUsing || manipulated != s_LastGizmoManipulated)
                        {
                            std::ostringstream gizmoStateLog;
                            gizmoStateLog << "Gizmo state: over=" << (gizmoOver ? "1" : "0")
                                          << " using=" << (gizmoUsing ? "1" : "0")
                                          << " manipulated=" << (manipulated ? "1" : "0")
                                          << " size=" << gizmoSize
                                          << " distPivot=" << distanceToPivot
                                          << " op=" << m_GizmoOperationIndex
                                          << " mode=" << m_GizmoModeIndex;
                            AppendSelectionDebugLog(gizmoStateLog.str());
                            s_LastGizmoOver = gizmoOver;
                            s_LastGizmoUsing = gizmoUsing;
                            s_LastGizmoManipulated = manipulated;
                            s_GizmoStateInitialized = true;
                        }

                        if (gizmoOver || gizmoUsing)
                        {
                            m_GizmoConsumedMouseThisFrame = true;
                        }

                        if (!gizmoOver && !gizmoUsing && pivotInViewport && !m_FallbackGizmoDragging && m_GizmoOperationIndex == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (hoveredFallbackAxis >= 0)
                            {
                                m_FallbackGizmoDragging = true;
                                m_FallbackGizmoAxis = hoveredFallbackAxis;
                                m_FallbackLastMousePos = glm::vec2(io.MousePos.x, io.MousePos.y);
                                m_FallbackDragAxisScreenDir = fallbackAxisScreenDir[hoveredFallbackAxis];
                                m_FallbackDragAxisWorldDir = (hoveredFallbackAxis == 0) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                                                           : (hoveredFallbackAxis == 1) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                                                                                     : glm::vec3(0.0f, 0.0f, 1.0f);
                                m_FallbackDragPixelsPerWorld = std::max(1.0f, fallbackPixelsPerWorld[hoveredFallbackAxis]);
                                m_FallbackDragAccumulated = 0.0f;
                                m_FallbackDragApplied = 0.0f;
                                m_GizmoConsumedMouseThisFrame = true;

                                std::ostringstream dragStartLog;
                                dragStartLog << "Fallback axis drag started: axis=" << m_FallbackGizmoAxis
                                             << " pxPerWorld=" << m_FallbackDragPixelsPerWorld;
                                AppendSelectionDebugLog(dragStartLog.str());
                            }
                        }

                        if (m_FallbackGizmoDragging)
                        {
                            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                            {
                                const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                                const glm::vec2 delta = mousePos - m_FallbackLastMousePos;
                                m_FallbackLastMousePos = mousePos;

                                const float deltaOnAxisPixels = glm::dot(delta, m_FallbackDragAxisScreenDir);
                                const float deltaOnAxisWorld = deltaOnAxisPixels / std::max(1.0f, m_FallbackDragPixelsPerWorld);

                                float worldToApply = deltaOnAxisWorld;
                                if (m_GizmoSnapEnabled)
                                {
                                    const float snapStep = std::max(0.001f, m_GizmoTranslateSnap);
                                    m_FallbackDragAccumulated += deltaOnAxisWorld;
                                    const float snappedTarget = std::round(m_FallbackDragAccumulated / snapStep) * snapStep;
                                    worldToApply = snappedTarget - m_FallbackDragApplied;
                                    m_FallbackDragApplied = snappedTarget;
                                }

                                const glm::vec3 worldDelta = m_FallbackDragAxisWorldDir * worldToApply;
                                if (glm::length(worldDelta) > 0.000001f)
                                {
                                    for (std::size_t atomIndex : m_SelectedAtomIndices)
                                    {
                                        if (atomIndex >= m_WorkingStructure.atoms.size())
                                        {
                                            continue;
                                        }

                                        const glm::vec3 atomCartesian = GetAtomCartesianPosition(atomIndex);
                                        SetAtomCartesianPosition(atomIndex, atomCartesian + worldDelta);
                                    }

                                    m_LastStructureOperationFailed = false;
                                    m_LastStructureMessage = "Fallback gizmo drag applied to selection (" + std::to_string(m_SelectedAtomIndices.size()) + " atoms).";
                                }

                                m_GizmoConsumedMouseThisFrame = true;
                            }
                            else
                            {
                                m_FallbackGizmoDragging = false;
                                m_FallbackGizmoAxis = -1;
                                m_FallbackDragAccumulated = 0.0f;
                                m_FallbackDragApplied = 0.0f;
                                AppendSelectionDebugLog("Fallback axis drag ended");
                            }
                        }

                        if (manipulated && ImGuizmo::IsUsing())
                        {
                            for (std::size_t atomIndex : m_SelectedAtomIndices)
                            {
                                if (atomIndex >= m_WorkingStructure.atoms.size())
                                {
                                    continue;
                                }

                                const glm::vec3 atomCartesian = GetAtomCartesianPosition(atomIndex);
                                const glm::vec3 transformed = glm::vec3(deltaTransform * glm::vec4(atomCartesian, 1.0f));
                                SetAtomCartesianPosition(atomIndex, transformed);
                            }

                            m_LastStructureOperationFailed = false;
                            m_LastStructureMessage = "Gizmo transform applied to selection (" + std::to_string(m_SelectedAtomIndices.size()) + " atoms).";
                        }
                    }
                    else if (s_LastValidPivot)
                    {
                        AppendSelectionDebugLog("Gizmo pivot unavailable: selected atoms are not valid indices.");
                        s_LastValidPivot = false;
                    }
                }
                else if (s_LastValidPivot)
                {
                    AppendSelectionDebugLog("Gizmo pivot reset: transform gizmo preconditions no longer met.");
                    s_LastValidPivot = false;
                    m_FallbackGizmoDragging = false;
                    m_FallbackGizmoAxis = -1;
                }

                if ((m_GizmoEnabled && (ImGuizmo::IsOver() || ImGuizmo::IsUsing())) ||
                    (m_ViewGuizmoEnabled && (ImViewGuizmo::IsOver() || ImViewGuizmo::IsUsing())))
                {
                    m_BlockSelectionThisFrame = true;
                    m_GizmoConsumedMouseThisFrame = true;
                }

                if (m_ViewGuizmoEnabled && m_Camera)
                {
                    ImViewGuizmo::BeginFrame();
                    ImViewGuizmo::GetStyle().scale = m_ViewGizmoScale;

                    glm::vec3 target = m_Camera->GetTarget();
                    const float yaw = m_Camera->GetYaw();
                    const float pitch = m_Camera->GetPitch();
                    const float distance = m_Camera->GetDistance();

                    const glm::vec3 cameraDirection = glm::normalize(glm::vec3(
                        std::cos(pitch) * std::sin(yaw),
                        std::sin(pitch),
                        std::cos(pitch) * std::cos(yaw)));

                    glm::vec3 cameraPosition = target - cameraDirection * distance;
                    glm::quat cameraRotation = glm::quatLookAt(cameraDirection, glm::vec3(0.0f, 0.0f, 1.0f));

                    const float rotateWidgetWidth = 256.0f * m_ViewGizmoScale;
                    const ImVec2 rotatePos(rectMax.x - rotateWidgetWidth - m_ViewGizmoOffsetRight, rectMin.y + m_ViewGizmoOffsetTop);

                    bool viewChanged = false;
                    viewChanged |= ImViewGuizmo::Rotate(cameraPosition, cameraRotation, target, rotatePos, 0.0125f);

                    if (viewChanged)
                    {
                        const glm::vec3 viewDir = glm::normalize(target - cameraPosition);
                        const float nextDistance = glm::max(0.5f, glm::length(target - cameraPosition));
                        const float nextYaw = std::atan2(viewDir.x, viewDir.y);
                        const float nextPitch = std::asin(glm::clamp(viewDir.z, -1.0f, 1.0f));
                        m_Camera->SetOrbitState(target, nextDistance, nextYaw, nextPitch);
                    }

                    if (ImViewGuizmo::IsOver() || ImViewGuizmo::IsUsing())
                    {
                        m_BlockSelectionThisFrame = true;
                        m_GizmoConsumedMouseThisFrame = true;
                    }
                }

                if (ImGui::BeginPopupContextItem("ViewportSelectionContext", ImGuiPopupFlags_MouseButtonRight))
                {
                    const bool hasLoadedAtoms = m_HasStructureLoaded && !m_WorkingStructure.atoms.empty();
                    const glm::vec2 contextMouse(io.MousePos.x, io.MousePos.y);

                    if (ImGui::MenuItem("Select All", nullptr, false, hasLoadedAtoms))
                    {
                        m_SelectedAtomIndices.clear();
                        m_SelectedAtomIndices.reserve(m_WorkingStructure.atoms.size());
                        for (std::size_t i = 0; i < m_WorkingStructure.atoms.size(); ++i)
                        {
                            m_SelectedAtomIndices.push_back(i);
                        }
                        AppendSelectionDebugLog("Context menu: Select All");
                    }

                    if (ImGui::MenuItem("Clear Selection", nullptr, false, !m_SelectedAtomIndices.empty()))
                    {
                        m_SelectedAtomIndices.clear();
                        AppendSelectionDebugLog("Context menu: Clear Selection");
                    }

                    if (ImGui::MenuItem("Invert Selection", nullptr, false, hasLoadedAtoms))
                    {
                        std::vector<std::size_t> inverted;
                        inverted.reserve(m_WorkingStructure.atoms.size());
                        for (std::size_t i = 0; i < m_WorkingStructure.atoms.size(); ++i)
                        {
                            if (!IsAtomSelected(i))
                            {
                                inverted.push_back(i);
                            }
                        }
                        m_SelectedAtomIndices = std::move(inverted);
                        AppendSelectionDebugLog("Context menu: Invert Selection");
                    }

                    if (ImGui::MenuItem("Frame Selected", nullptr, false, !m_SelectedAtomIndices.empty() && m_Camera))
                    {
                        glm::vec3 boundsMin(std::numeric_limits<float>::max());
                        glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

                        for (std::size_t atomIndex : m_SelectedAtomIndices)
                        {
                            if (atomIndex >= m_WorkingStructure.atoms.size())
                            {
                                continue;
                            }

                            glm::vec3 position = m_WorkingStructure.atoms[atomIndex].position;
                            if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
                            {
                                position = m_WorkingStructure.DirectToCartesian(position);
                            }

                            boundsMin = glm::min(boundsMin, position);
                            boundsMax = glm::max(boundsMax, position);
                        }

                        m_Camera->FrameBounds(boundsMin, boundsMax);
                        AppendSelectionDebugLog("Context menu: Frame Selected");
                    }

                    if (ImGui::MenuItem("Set 3D Cursor Here (grid plane)", nullptr, false, m_Camera != nullptr))
                    {
                        Set3DCursorFromScreenPoint(contextMouse);
                        AppendSelectionDebugLog("Context menu: Set 3D Cursor Here");
                        settingsChanged = true;
                    }

                    ImGui::EndPopup();
                }

                if (m_BoxSelectArmed && m_InteractionMode == InteractionMode::Select && m_ViewportFocused)
                {
                    const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                    const bool insideViewport =
                        mousePos.x >= m_ViewportRectMin.x && mousePos.x <= m_ViewportRectMax.x &&
                        mousePos.y >= m_ViewportRectMin.y && mousePos.y <= m_ViewportRectMax.y;

                    if (!m_BoxSelecting && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && insideViewport)
                    {
                        m_BoxSelecting = true;
                        m_BoxSelectStart = mousePos;
                        m_BoxSelectEnd = mousePos;
                        AppendSelectionDebugLog("Box select drag started");
                    }

                    if (m_BoxSelecting)
                    {
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        {
                            m_BoxSelectEnd = mousePos;
                        }
                        else
                        {
                            m_BoxSelectEnd = mousePos;
                            SelectAtomsInScreenRect(m_BoxSelectStart, m_BoxSelectEnd, io.KeyCtrl);
                            m_BoxSelecting = false;
                            m_BoxSelectArmed = false;
                            AppendSelectionDebugLog("Box select drag finished");
                        }
                    }

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        m_BoxSelecting = false;
                        m_BoxSelectArmed = false;
                        AppendSelectionDebugLog("Box select canceled with RMB");
                    }
                }

                if (m_BoxSelecting)
                {
                    ImDrawList *drawList = ImGui::GetWindowDrawList();
                    const ImVec2 rectA(
                        std::min(m_BoxSelectStart.x, m_BoxSelectEnd.x),
                        std::min(m_BoxSelectStart.y, m_BoxSelectEnd.y));
                    const ImVec2 rectB(
                        std::max(m_BoxSelectStart.x, m_BoxSelectEnd.x),
                        std::max(m_BoxSelectStart.y, m_BoxSelectEnd.y));
                    drawList->AddRectFilled(rectA, rectB, IM_COL32(85, 160, 255, 40));
                    drawList->AddRect(rectA, rectB, IM_COL32(95, 185, 255, 220), 0.0f, 0, 1.5f);
                }
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
        ImGui::Text("3D Cursor: (%.3f, %.3f, %.3f)", m_CursorPosition.x, m_CursorPosition.y, m_CursorPosition.z);
        ImGui::Separator();
        ImGui::TextUnformatted("Navigation:");
        ImGui::BulletText("MMB orbit");
        ImGui::BulletText("Shift + MMB pan");
        ImGui::BulletText("Mouse Wheel zoom");
        ImGui::BulletText("RMB on viewport: Set 3D Cursor Here");
        ImGui::BulletText("Top-right axis gizmo: rotate view");
        ImGui::BulletText("ViewSet mode keys: T top, B bottom, L left, R right, P front, K back");
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
        ImGui::SeparatorText("Structure I/O");
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

        ImGui::SeparatorText("Selection & Gizmo");
        const char *interactionModeLabel = "Navigate";
        if (m_InteractionMode == InteractionMode::Select)
        {
            interactionModeLabel = "Select";
        }
        else if (m_InteractionMode == InteractionMode::ViewSet)
        {
            interactionModeLabel = "ViewSet";
        }
        else if (m_InteractionMode == InteractionMode::Translate)
        {
            interactionModeLabel = "Translate";
        }
        ImGui::Text("Mode: %s", interactionModeLabel);
        ImGui::TextUnformatted("Tab: toggle mode (when viewport focused)");
        if (m_InteractionMode == InteractionMode::ViewSet)
        {
            ImGui::TextUnformatted("ViewSet keys: T(top), B(bottom), L(left), R(right), P(front), K(back)");
        }
        if (m_TranslateModeActive)
        {
            ImGui::TextUnformatted("Translate: mouse move | X/Y/Z axis | Shift+X/Y/Z plane lock | Ctrl snap");
            ImGui::TextUnformatted("Confirm: LMB/Enter/G | Cancel: RMB/Esc");
        }

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
        ImGui::TextUnformatted("LMB: select | Ctrl+LMB: add/remove | B+LMB drag: box select");
        ImGui::TextUnformatted("RMB on viewport: selection context menu");
        if (ImGui::Button("Clear selection"))
        {
            m_SelectedAtomIndices.clear();
        }

        const char *gizmoOperations[] = {"Translate", "Rotate", "Scale"};
        const char *gizmoModes[] = {"Local", "World"};
        if (ImGui::Checkbox("Enable gizmo", &m_GizmoEnabled))
        {
            settingsChanged = true;
        }
        if (ImGui::Checkbox("Enable view gizmo overlay", &m_ViewGuizmoEnabled))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("Transform gizmo size", &m_TransformGizmoSize, 0.05f, 0.35f, "%.2f"))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("View gizmo scale", &m_ViewGizmoScale, 0.35f, 2.20f, "%.2f"))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("Fallback marker scale", &m_FallbackGizmoVisualScale, 0.5f, 6.0f, "%.2f"))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("View gizmo offset right", &m_ViewGizmoOffsetRight, 0.0f, 220.0f, "%.0f"))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("View gizmo offset top", &m_ViewGizmoOffsetTop, 0.0f, 220.0f, "%.0f"))
        {
            settingsChanged = true;
        }
        ImGui::Combo("Gizmo operation", &m_GizmoOperationIndex, gizmoOperations, IM_ARRAYSIZE(gizmoOperations));
        ImGui::Combo("Gizmo mode", &m_GizmoModeIndex, gizmoModes, IM_ARRAYSIZE(gizmoModes));
        if (ImGui::Checkbox("Gizmo snap", &m_GizmoSnapEnabled))
        {
            settingsChanged = true;
        }
        if (m_GizmoOperationIndex == 0)
        {
            ImGui::SliderFloat("Translate snap", &m_GizmoTranslateSnap, 0.01f, 2.0f, "%.2f");
        }
        else if (m_GizmoOperationIndex == 1)
        {
            ImGui::SliderFloat("Rotate snap (deg)", &m_GizmoRotateSnapDeg, 1.0f, 90.0f, "%.1f");
        }
        else
        {
            ImGui::SliderFloat("Scale snap", &m_GizmoScaleSnap, 0.01f, 1.0f, "%.2f");
        }

        const char *coordinateModes[] = {"Direct", "Cartesian"};

        ImGui::SeparatorText("Add Atom");
        ImGui::InputText("Element", m_AddAtomElementBuffer.data(), m_AddAtomElementBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("Periodic table"))
        {
            m_PeriodicTableOpen = true;
        }
        ImGui::DragFloat3("Position", &m_AddAtomPosition.x, 0.01f, -1000.0f, 1000.0f, "%.5f");
        ImGui::Combo("Input coordinates", &m_AddAtomCoordinateModeIndex, coordinateModes, IM_ARRAYSIZE(coordinateModes));

        if (ImGui::Button("Use camera target") && m_Camera)
        {
            m_AddAtomPosition = m_Camera->GetTarget();
            m_AddAtomCoordinateModeIndex = 1;
        }

        ImGui::SameLine();
        if (ImGui::Button("Use 3D cursor"))
        {
            m_AddAtomPosition = m_CursorPosition;
            m_AddAtomCoordinateModeIndex = 1;
        }

        ImGui::SameLine();
        const bool canAddAtom = m_HasStructureLoaded;
        if (!canAddAtom)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Add atom"))
        {
            const CoordinateMode inputMode = (m_AddAtomCoordinateModeIndex == 0)
                                                 ? CoordinateMode::Direct
                                                 : CoordinateMode::Cartesian;
            AddAtomToStructure(std::string(m_AddAtomElementBuffer.data()), m_AddAtomPosition, inputMode);
        }
        if (!canAddAtom)
        {
            ImGui::EndDisabled();
        }

        ImGui::SeparatorText("3D Cursor");
        if (ImGui::Checkbox("Show 3D cursor", &m_Show3DCursor))
        {
            settingsChanged = true;
        }
        if (ImGui::Checkbox("Snap cursor to grid", &m_CursorSnapToGrid))
        {
            settingsChanged = true;
        }
        if (ImGui::DragFloat3("Cursor position", &m_CursorPosition.x, 0.01f, -1000.0f, 1000.0f, "%.5f"))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("Cursor size", &m_CursorVisualScale, 0.05f, 1.00f, "%.2f"))
        {
            settingsChanged = true;
        }
        if (ImGui::ColorEdit3("Cursor color", &m_CursorColor.x))
        {
            settingsChanged = true;
        }
        if (ImGui::Button("Cursor <- camera target") && m_Camera)
        {
            m_CursorPosition = m_Camera->GetTarget();
            settingsChanged = true;
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

        ImGui::SeparatorText("Appearance & Camera");
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

        DrawPeriodicTableWindow();

        if (settingsChanged)
        {
            SaveSettings();
        }
    }

} // namespace ds
