#pragma once

#include "Core/Layer.h"
#include "DataModel/Structure.h"
#include "IO/PoscarParser.h"
#include "IO/PoscarSerializer.h"
#include "Renderer/IRenderBackend.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

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

        enum class InteractionMode
        {
            Navigate = 0,
            Select = 1
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
        bool AddAtomToStructure(const std::string &elementSymbol, const glm::vec3 &position, CoordinateMode inputMode);
        bool IsAtomSelected(std::size_t index) const;
        void ToggleInteractionMode();
        void HandleViewportSelection();
        void SelectAtomsInScreenRect(const glm::vec2 &screenStart, const glm::vec2 &screenEnd, bool additiveSelection);
        void AppendSelectionDebugLog(const std::string &message) const;
        bool PickAtomAtScreenPoint(const glm::vec2 &mousePos, std::size_t &outAtomIndex) const;
        bool PickWorldPositionOnGrid(const glm::vec2 &mousePos, glm::vec3 &outWorldPosition) const;
        void Set3DCursorFromScreenPoint(const glm::vec2 &mousePos);
        void DrawPeriodicTableWindow();

        bool m_ShowDemoWindow = false;
        bool m_ShowLogPanel = true;
        ThemePreset m_CurrentTheme = ThemePreset::Dark;
        float m_FontScale = 1.0f;
        int m_LogFilter = 0;
        bool m_LogAutoScroll = true;

        bool m_ViewportHovered = false;
        bool m_ViewportFocused = false;
        glm::vec2 m_ViewportSize = glm::vec2(1.0f, 1.0f);
        glm::vec2 m_ViewportRectMin = glm::vec2(0.0f, 0.0f);
        glm::vec2 m_ViewportRectMax = glm::vec2(0.0f, 0.0f);

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
        std::array<char, 16> m_AddAtomElementBuffer = {'S', 'i', '\0'};
        glm::vec3 m_AddAtomPosition = glm::vec3(0.0f);
        int m_AddAtomCoordinateModeIndex = 1;
        bool m_PeriodicTableOpen = false;
        bool m_LastStructureOperationFailed = false;
        std::string m_LastStructureMessage;
        std::vector<std::size_t> m_SelectedAtomIndices;
        InteractionMode m_InteractionMode = InteractionMode::Navigate;
        glm::vec3 m_SelectionColor = glm::vec3(0.95f, 0.85f, 0.25f);
        float m_SelectionOutlineThickness = 2.0f;
        bool m_SelectionDebugToFile = true;
        bool m_BoxSelectArmed = false;
        bool m_BoxSelecting = false;
        glm::vec2 m_BoxSelectStart = glm::vec2(0.0f, 0.0f);
        glm::vec2 m_BoxSelectEnd = glm::vec2(0.0f, 0.0f);
        bool m_Show3DCursor = true;
        glm::vec3 m_CursorPosition = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 m_CursorColor = glm::vec3(0.22f, 0.95f, 0.95f);
        float m_CursorVisualScale = 0.20f;
        bool m_CursorSnapToGrid = true;

        bool m_HasPersistedCameraState = false;
        glm::vec3 m_CameraTargetPersisted = glm::vec3(0.0f, 0.0f, 0.0f);
        float m_CameraDistancePersisted = 6.0f;
        float m_CameraYawPersisted = 0.6f;
        float m_CameraPitchPersisted = 0.5f;

        SceneRenderSettings m_SceneSettings;
        int m_ProjectionModeIndex = 0;
        bool m_ViewportSettingsOpen = true;

        std::unique_ptr<IRenderBackend> m_RenderBackend;
        std::unique_ptr<OrbitCamera> m_Camera;
    };

} // namespace ds
