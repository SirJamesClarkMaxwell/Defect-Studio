#include "Layers/EditorLayerPrivate.h"

namespace ds
{
    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
        std::error_code pathError;
        std::filesystem::path startupPath = std::filesystem::current_path(pathError);
        if (pathError)
        {
            startupPath = std::filesystem::path(".");
        }
        startupPath = startupPath.lexically_normal();
        m_AppRootPath = startupPath.string();
        m_ProjectRootPath = m_AppRootPath;
        m_ProjectName = startupPath.filename().string().empty() ? "Default Project" : startupPath.filename().string();

        const char *defaultImportPath = kFallbackStartupImportPath;
        const char *defaultExportPath = "exports/CONTCAR.vasp";
        const char *defaultRenderImagePath = "exports/render.png";

        std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", defaultImportPath);
        std::snprintf(m_ExportPathBuffer.data(), m_ExportPathBuffer.size(), "%s", defaultExportPath);
        std::snprintf(m_RenderImagePathBuffer.data(), m_RenderImagePathBuffer.size(), "%s", defaultRenderImagePath);

        m_SceneSettings.clearColor = glm::vec3(0.10f, 0.10f, 0.10f);
        m_SceneSettings.gridLineWidth = 1.0f;
        m_SceneSettings.gridColor = glm::vec3(0.27f, 0.29f, 0.32f);
        m_SceneSettings.ambientStrength = 0.58f;
        m_SceneSettings.diffuseStrength = 0.78f;
        m_SceneSettings.atomBrightness = 1.15f;
        ApplyAtomDefaultsToSceneSettings();
        m_RenderSceneSettings = m_SceneSettings;
        m_HotkeyAddMenu = static_cast<std::uint32_t>(ImGuiKey_A);
        m_HotkeyOpenRender = static_cast<std::uint32_t>(ImGuiKey_F12);
        m_HotkeyToggleSidePanels = static_cast<std::uint32_t>(ImGuiKey_N);
        m_HotkeyDeleteSelection = static_cast<std::uint32_t>(ImGuiKey_Delete);
        m_HotkeyHideSelection = static_cast<std::uint32_t>(ImGuiKey_H);
        m_HotkeyBoxSelect = static_cast<std::uint32_t>(ImGuiKey_B);
        m_HotkeyCircleSelect = static_cast<std::uint32_t>(ImGuiKey_C);
        m_HotkeyTranslateModal = static_cast<std::uint32_t>(ImGuiKey_G);
        m_HotkeyTranslateGizmo = static_cast<std::uint32_t>(ImGuiKey_T);
        m_HotkeyRotateGizmo = static_cast<std::uint32_t>(ImGuiKey_R);
        m_HotkeyScaleGizmo = static_cast<std::uint32_t>(ImGuiKey_S);
    }

    void EditorLayer::SyncRenderAppearanceFromViewport()
    {
        m_RenderSceneSettings = m_SceneSettings;
        m_RenderSceneSettings.drawCellEdges = m_ShowCellEdges;
        m_RenderSceneSettings.cellEdgeColor = glm::clamp(m_CellEdgeColor, glm::vec3(0.0f), glm::vec3(1.0f));
        m_RenderSceneSettings.cellEdgeLineWidth = glm::clamp(m_CellEdgeLineWidth, 0.5f, 10.0f);
        m_RenderShowBondLengthLabels = m_ShowBondLengthLabels;
        m_RenderBondLabelPrecision = m_BondLabelPrecision;
        m_RenderBondLabelScaleMultiplier = 1.0f;
        m_RenderBondLabelTextColor = m_BondLabelTextColor;
        m_RenderBondLabelBackgroundColor = m_BondLabelBackgroundColor;
        m_RenderBondLabelBorderColor = m_BondLabelBorderColor;
    }

    void EditorLayer::OnAttach()
    {
        LogInfo("EditorLayer::OnAttach begin");
        m_ApplyDefaultDockLayoutOnNextFrame = !std::filesystem::exists(GetAppRootPath() / "config" / "imgui_layout.ini");

        LogInfo("Loading app defaults from config/default.yaml");
        LoadDefaultConfigYaml();

        LogInfo("Migrating legacy atom settings if needed");
        MigrateLegacyAtomIniIfNeeded();

        LogInfo("Loading atom defaults from config/atom_settings.yaml");
        LoadAtomSettings();

        LogInfo("Migrating legacy UI settings if needed");
        MigrateLegacyUiIniIfNeeded();

        LogInfo("Loading UI settings from config/ui_settings.yaml");
        LoadSettings();

        LogInfo("Loading current project manifest");
        LoadProjectManifest();

        LogInfo("Migrating legacy project appearance if needed");
        // The actual migration/load happens after project scene state is loaded.

        LogInfo("Applying scene defaults and validation");
        EnsureSceneDefaults();
        SyncRenderAppearanceFromViewport();
        ApplyTheme(m_CurrentTheme);
        ImGuiLayer::LoadSavedStyle();
        ApplyFontScale(m_FontScale);

        LogInfo("Initializing render backend");

        m_RenderBackend = std::make_unique<OpenGLRendererBackend>();
        if (!m_RenderBackend->Initialize())
        {
            LogError("Failed to initialize OpenGL backend.");
        }
        else
        {
            LogInfo("OpenGL backend initialized successfully");
        }

        m_RenderPreviewBackend = std::make_unique<OpenGLRendererBackend>();
        if (!m_RenderPreviewBackend->Initialize())
        {
            LogError("Failed to initialize OpenGL preview backend.");
        }
        else
        {
            LogInfo("OpenGL preview backend initialized successfully");
        }

        LogInfo("Creating orbit camera");
        m_Camera = std::make_unique<OrbitCamera>();
        m_Camera->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        m_Camera->SetProjectionMode(m_ProjectionModeIndex == 0 ? OrbitCamera::ProjectionMode::Perspective : OrbitCamera::ProjectionMode::Orthographic);
        if (m_HasPersistedCameraState)
        {
            m_Camera->SetOrbitState(m_CameraTargetPersisted, m_CameraDistancePersisted, m_CameraYawPersisted, m_CameraPitchPersisted);
            m_Camera->SetRoll(m_CameraRollPersisted);
        }
        ApplyCameraSensitivity();

        const std::filesystem::path startupImportPath = ResolveProjectStructurePath();
        if (!startupImportPath.empty() && std::filesystem::exists(startupImportPath))
        {
            LogInfo("Attempting startup import: " + startupImportPath.string());
            LoadStructureFromPath(startupImportPath.string());
        }
        else if (!startupImportPath.empty())
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Startup import skipped: file not found: " + startupImportPath.string();
            LogWarn(m_LastStructureMessage);
        }

        LogInfo("Loading scene state from " + GetProjectSceneStateFilePath().string());
        LoadSceneState();

        LogInfo("Migrating legacy project appearance if needed");
        MigrateLegacyProjectAppearanceFromSceneStateIfNeeded();

        LogInfo("Loading project appearance from " + GetProjectAppearanceFilePath().string());
        LoadProjectAppearanceYaml();

        EnsureSceneDefaults();
        SyncRenderAppearanceFromViewport();

        LogInfo("EditorLayer attached with theme: " + std::string(ThemeName(m_CurrentTheme)) +
                ", collections=" + std::to_string(m_Collections.size()) +
                ", empties=" + std::to_string(m_TransformEmpties.size()) +
                ", groups=" + std::to_string(m_ObjectGroups.size()));
    }

    void EditorLayer::OnDetach()
    {
        SaveSettings();

        if (m_RenderBackend)
        {
            m_RenderBackend->Shutdown();
            m_RenderBackend.reset();
        }

        if (m_RenderPreviewBackend)
        {
            m_RenderPreviewBackend->Shutdown();
            m_RenderPreviewBackend.reset();
        }

        m_Camera.reset();
    }


} // namespace ds
