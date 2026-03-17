#pragma once

#include "Core/Layer.h"

namespace ds
{

    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        ~EditorLayer() override = default;

        void OnAttach() override;
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
        void SaveSettings() const;
        void LoadSettings();
        const char *ThemeName(ThemePreset preset) const;

        bool m_ShowDemoWindow = false;
        bool m_ShowLogPanel = true;
        ThemePreset m_CurrentTheme = ThemePreset::Dark;
        float m_FontScale = 1.0f;
        int m_LogFilter = 0;
        bool m_LogAutoScroll = true;
    };

} // namespace ds
