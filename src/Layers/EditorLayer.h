#pragma once

#include "Core/Layer.h"
#include "DataModel/Structure.h"
#include "IO/PoscarParser.h"
#include "IO/PoscarSerializer.h"
#include "Renderer/IRenderBackend.h"

#include <array>
#include <memory>
#include <optional>
#include <string>

#include <glm/vec2.hpp>

namespace ds
{
    class IRenderBackend;
    class OrbitCamera;

    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        ~EditorLayer() override = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;
        void OnImGuiRender() override;

    private:
        enum class ThemePreset
        {
            Dark = 0,
            Light = 1,
            Classic = 2,
            PhotoshopStyle = 3,
            WarmSlate = 4
        };

        static constexpr const char *kSettingsPath = "config/editor_ui_settings.ini";

        void ApplyTheme(ThemePreset preset);
        void ApplyFontScale(float scale);
        void ApplyCameraSensitivity();
        void SaveSettings() const;
        void LoadSettings();
        const char *ThemeName(ThemePreset preset) const;
        bool LoadStructureFromPath(const std::string &path);
        bool ExportStructureToPath(const std::string &path, CoordinateMode mode, int precision);

        bool m_ShowDemoWindow = false;
        bool m_ShowLogPanel = true;
        ThemePreset m_CurrentTheme = ThemePreset::Dark;
        float m_FontScale = 1.0f;
        int m_LogFilter = 0;
        bool m_LogAutoScroll = true;

        bool m_ViewportHovered = false;
        bool m_ViewportFocused = false;
        glm::vec2 m_ViewportSize = glm::vec2(1.0f, 1.0f);

        float m_CameraOrbitSensitivity = 1.0f;
        float m_CameraPanSensitivity = 1.0f;
        float m_CameraZoomSensitivity = 1.0f;

        PoscarParser m_PoscarParser;
        PoscarSerializer m_PoscarSerializer;
        std::optional<Structure> m_OriginalStructure;
        Structure m_WorkingStructure;
        bool m_HasStructureLoaded = false;

        std::array<char, 512> m_ImportPathBuffer = {};
        std::array<char, 512> m_ExportPathBuffer = {};
        int m_ExportPrecision = 8;
        int m_ExportCoordinateModeIndex = 0;
        bool m_LastStructureOperationFailed = false;
        std::string m_LastStructureMessage;

        SceneRenderSettings m_SceneSettings;
        int m_ProjectionModeIndex = 0;
        bool m_ViewportSettingsOpen = true;

        std::unique_ptr<IRenderBackend> m_RenderBackend;
        std::unique_ptr<OrbitCamera> m_Camera;
    };

} // namespace ds
