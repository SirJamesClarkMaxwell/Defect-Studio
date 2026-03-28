#include "Layers/EditorLayerPrivate.h"

namespace ds
{
    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
        const char *defaultImportPath = "assets/samples/reduced_diamond_bulk";
        const char *defaultExportPath = "exports/CONTCAR.vasp";
        const char *defaultRenderImagePath = "exports/render.png";

        std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", defaultImportPath);
        std::snprintf(m_ExportPathBuffer.data(), m_ExportPathBuffer.size(), "%s", defaultExportPath);
        std::snprintf(m_RenderImagePathBuffer.data(), m_RenderImagePathBuffer.size(), "%s", defaultRenderImagePath);

        m_SceneSettings.clearColor = glm::vec3(0.16f, 0.18f, 0.22f);
        m_SceneSettings.gridLineWidth = 1.0f;
        m_SceneSettings.gridColor = glm::vec3(0.38f, 0.42f, 0.50f);
        m_SceneSettings.ambientStrength = 0.58f;
        m_SceneSettings.diffuseStrength = 0.78f;
        m_SceneSettings.atomBrightness = 1.15f;
        m_RenderSceneSettings = m_SceneSettings;
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

        LogInfo("Loading editor settings from config/editor_ui_settings.ini");
        LoadSettings();

        LogInfo("Loading scene state from config/scene_state.ini");
        LoadSceneState();

        LogInfo("Applying scene defaults and validation");
        EnsureSceneDefaults();
        SyncRenderAppearanceFromViewport();
        ApplyTheme(m_CurrentTheme);
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

        const std::string startupImportPath = std::string(m_ImportPathBuffer.data());
        if (!startupImportPath.empty() && std::filesystem::exists(startupImportPath))
        {
            LogInfo("Attempting startup import: " + startupImportPath);
            LoadStructureFromPath(startupImportPath);
        }
        else if (!startupImportPath.empty())
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Startup import skipped: file not found: " + startupImportPath;
            LogWarn(m_LastStructureMessage);
        }

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
