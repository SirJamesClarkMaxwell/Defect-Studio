#pragma once

namespace ds
{
    class EditorLayer;

    class SettingsPanel
    {
    public:
        static void Draw(EditorLayer &editor, bool &settingsChanged);
    };

} // namespace ds
