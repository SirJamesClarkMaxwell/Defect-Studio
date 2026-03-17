#pragma once

namespace ds
{
    class EditorLayer;

    class SceneGroupingBackend
    {
    public:
        static bool CreateGroupFromCurrentSelection(EditorLayer &editor);
        static void SanitizeGroup(EditorLayer &editor, int groupIndex);
        static void SelectGroup(EditorLayer &editor, int groupIndex);
        static void AddCurrentSelectionToGroup(EditorLayer &editor, int groupIndex);
        static void RemoveCurrentSelectionFromGroup(EditorLayer &editor, int groupIndex);
    };

} // namespace ds
