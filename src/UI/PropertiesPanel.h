#pragma once

namespace ds
{
    class EditorLayer;

    class PropertiesPanel
    {
    public:
        static void Draw(EditorLayer &editor, bool &settingsChanged);
    };

} // namespace ds
