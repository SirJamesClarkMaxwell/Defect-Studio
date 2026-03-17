#include "Editor/SceneGroupingBackend.h"

#include "Layers/EditorLayer.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <random>

namespace ds
{

    namespace
    {
        SceneUUID GenerateSceneUUID()
        {
            static std::mt19937_64 rng(std::random_device{}());
            static std::uniform_int_distribution<std::uint64_t> dist(
                std::numeric_limits<std::uint64_t>::min() + 1,
                std::numeric_limits<std::uint64_t>::max());
            return dist(rng);
        }

    } // namespace

    bool SceneGroupingBackend::CreateGroupFromCurrentSelection(EditorLayer &editor)
    {
        if (editor.m_SelectedAtomIndices.empty() && editor.m_SelectedTransformEmptyIndex < 0)
        {
            return false;
        }

        EditorLayer::SceneGroup group;
        group.id = GenerateSceneUUID();

        char label[48] = {};
        std::snprintf(label, sizeof(label), "Group %d", editor.m_GroupCounter++);
        group.name = label;
        group.atomIndices = editor.m_SelectedAtomIndices;
        for (std::size_t atomIndex : group.atomIndices)
        {
            if (atomIndex < editor.m_AtomNodeIds.size())
            {
                group.atomIds.push_back(editor.m_AtomNodeIds[atomIndex]);
            }
        }

        if (editor.m_SelectedTransformEmptyIndex >= 0 && editor.m_SelectedTransformEmptyIndex < static_cast<int>(editor.m_TransformEmpties.size()))
        {
            group.emptyIndices.push_back(editor.m_SelectedTransformEmptyIndex);
            group.emptyIds.push_back(editor.m_TransformEmpties[static_cast<std::size_t>(editor.m_SelectedTransformEmptyIndex)].id);
        }

        editor.m_ObjectGroups.push_back(group);
        editor.m_ActiveGroupIndex = static_cast<int>(editor.m_ObjectGroups.size()) - 1;
        return true;
    }

    bool SceneGroupingBackend::DeleteGroup(EditorLayer &editor, int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= static_cast<int>(editor.m_ObjectGroups.size()))
        {
            return false;
        }

        editor.m_ObjectGroups.erase(editor.m_ObjectGroups.begin() + groupIndex);
        if (editor.m_ObjectGroups.empty())
        {
            editor.m_ActiveGroupIndex = -1;
        }
        else
        {
            editor.m_ActiveGroupIndex = std::min(groupIndex, static_cast<int>(editor.m_ObjectGroups.size()) - 1);
        }
        return true;
    }

    void SceneGroupingBackend::SanitizeGroup(EditorLayer &editor, int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= static_cast<int>(editor.m_ObjectGroups.size()))
        {
            return;
        }

        auto uniquePushIndex = [](std::vector<std::size_t> &container, std::size_t value)
        {
            if (std::find(container.begin(), container.end(), value) == container.end())
            {
                container.push_back(value);
            }
        };

        auto uniquePushIndexInt = [](std::vector<int> &container, int value)
        {
            if (std::find(container.begin(), container.end(), value) == container.end())
            {
                container.push_back(value);
            }
        };

        auto uniquePushUUID = [](std::vector<SceneUUID> &container, SceneUUID value)
        {
            if (value == 0)
            {
                return;
            }
            if (std::find(container.begin(), container.end(), value) == container.end())
            {
                container.push_back(value);
            }
        };

        auto findEmptyIndexById = [&](SceneUUID id) -> int
        {
            if (id == 0)
            {
                return -1;
            }
            for (std::size_t i = 0; i < editor.m_TransformEmpties.size(); ++i)
            {
                if (editor.m_TransformEmpties[i].id == id)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };

        auto findAtomIndexById = [&](SceneUUID id) -> int
        {
            if (id == 0)
            {
                return -1;
            }
            for (std::size_t i = 0; i < editor.m_AtomNodeIds.size(); ++i)
            {
                if (editor.m_AtomNodeIds[i] == id)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };

        EditorLayer::SceneGroup &group = editor.m_ObjectGroups[static_cast<std::size_t>(groupIndex)];
        if (group.id == 0)
        {
            group.id = GenerateSceneUUID();
        }

        group.atomIndices.erase(
            std::remove_if(group.atomIndices.begin(), group.atomIndices.end(), [&](std::size_t atomIndex)
                           { return atomIndex >= editor.m_WorkingStructure.atoms.size(); }),
            group.atomIndices.end());

        for (std::size_t atomIndex : group.atomIndices)
        {
            if (atomIndex < editor.m_AtomNodeIds.size())
            {
                uniquePushUUID(group.atomIds, editor.m_AtomNodeIds[atomIndex]);
            }
        }

        group.atomIds.erase(
            std::remove_if(group.atomIds.begin(), group.atomIds.end(), [&](SceneUUID atomId)
                           { return findAtomIndexById(atomId) < 0; }),
            group.atomIds.end());
        std::sort(group.atomIds.begin(), group.atomIds.end());
        group.atomIds.erase(std::unique(group.atomIds.begin(), group.atomIds.end()), group.atomIds.end());

        for (SceneUUID atomId : group.atomIds)
        {
            const int resolvedIndex = findAtomIndexById(atomId);
            if (resolvedIndex >= 0)
            {
                uniquePushIndex(group.atomIndices, static_cast<std::size_t>(resolvedIndex));
            }
        }
        std::sort(group.atomIndices.begin(), group.atomIndices.end());
        group.atomIndices.erase(std::unique(group.atomIndices.begin(), group.atomIndices.end()), group.atomIndices.end());

        for (int emptyIndex : group.emptyIndices)
        {
            if (emptyIndex < 0 || emptyIndex >= static_cast<int>(editor.m_TransformEmpties.size()))
            {
                continue;
            }
            uniquePushUUID(group.emptyIds, editor.m_TransformEmpties[static_cast<std::size_t>(emptyIndex)].id);
        }

        group.emptyIds.erase(
            std::remove_if(group.emptyIds.begin(), group.emptyIds.end(), [&](SceneUUID emptyId)
                           { return findEmptyIndexById(emptyId) < 0; }),
            group.emptyIds.end());
        std::sort(group.emptyIds.begin(), group.emptyIds.end());
        group.emptyIds.erase(std::unique(group.emptyIds.begin(), group.emptyIds.end()), group.emptyIds.end());

        group.emptyIndices.erase(
            std::remove_if(group.emptyIndices.begin(), group.emptyIndices.end(), [&](int emptyIndex)
                           { return emptyIndex < 0 || emptyIndex >= static_cast<int>(editor.m_TransformEmpties.size()); }),
            group.emptyIndices.end());

        for (SceneUUID emptyId : group.emptyIds)
        {
            const int resolvedIndex = findEmptyIndexById(emptyId);
            if (resolvedIndex >= 0)
            {
                uniquePushIndexInt(group.emptyIndices, resolvedIndex);
            }
        }
        std::sort(group.emptyIndices.begin(), group.emptyIndices.end());
        group.emptyIndices.erase(std::unique(group.emptyIndices.begin(), group.emptyIndices.end()), group.emptyIndices.end());
    }

    void SceneGroupingBackend::SelectGroup(EditorLayer &editor, int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= static_cast<int>(editor.m_ObjectGroups.size()))
        {
            return;
        }

        EditorLayer::SceneGroup &group = editor.m_ObjectGroups[static_cast<std::size_t>(groupIndex)];
        SanitizeGroup(editor, groupIndex);

        editor.m_SelectedAtomIndices = group.atomIndices;
        editor.m_SelectedTransformEmptyIndex = group.emptyIndices.empty() ? -1 : group.emptyIndices.front();
        if (editor.m_SelectedTransformEmptyIndex >= 0)
        {
            editor.m_ActiveTransformEmptyIndex = editor.m_SelectedTransformEmptyIndex;
        }

        editor.m_GizmoEnabled = true;
        editor.m_InteractionMode = EditorLayer::InteractionMode::Select;
    }

    void SceneGroupingBackend::AddCurrentSelectionToGroup(EditorLayer &editor, int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= static_cast<int>(editor.m_ObjectGroups.size()))
        {
            return;
        }

        EditorLayer::SceneGroup &group = editor.m_ObjectGroups[static_cast<std::size_t>(groupIndex)];
        for (std::size_t atomIndex : editor.m_SelectedAtomIndices)
        {
            if (atomIndex < editor.m_WorkingStructure.atoms.size())
            {
                if (std::find(group.atomIndices.begin(), group.atomIndices.end(), atomIndex) == group.atomIndices.end())
                {
                    group.atomIndices.push_back(atomIndex);
                }
                if (atomIndex < editor.m_AtomNodeIds.size())
                {
                    const SceneUUID atomId = editor.m_AtomNodeIds[atomIndex];
                    if (atomId != 0 && std::find(group.atomIds.begin(), group.atomIds.end(), atomId) == group.atomIds.end())
                    {
                        group.atomIds.push_back(atomId);
                    }
                }
            }
        }

        if (editor.m_SelectedTransformEmptyIndex >= 0 && editor.m_SelectedTransformEmptyIndex < static_cast<int>(editor.m_TransformEmpties.size()))
        {
            if (std::find(group.emptyIndices.begin(), group.emptyIndices.end(), editor.m_SelectedTransformEmptyIndex) == group.emptyIndices.end())
            {
                group.emptyIndices.push_back(editor.m_SelectedTransformEmptyIndex);
            }
            const SceneUUID selectedEmptyId = editor.m_TransformEmpties[static_cast<std::size_t>(editor.m_SelectedTransformEmptyIndex)].id;
            if (selectedEmptyId != 0 && std::find(group.emptyIds.begin(), group.emptyIds.end(), selectedEmptyId) == group.emptyIds.end())
            {
                group.emptyIds.push_back(selectedEmptyId);
            }
        }

        SanitizeGroup(editor, groupIndex);
    }

    void SceneGroupingBackend::RemoveCurrentSelectionFromGroup(EditorLayer &editor, int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= static_cast<int>(editor.m_ObjectGroups.size()))
        {
            return;
        }

        EditorLayer::SceneGroup &group = editor.m_ObjectGroups[static_cast<std::size_t>(groupIndex)];
        for (std::size_t atomIndex : editor.m_SelectedAtomIndices)
        {
            group.atomIndices.erase(std::remove(group.atomIndices.begin(), group.atomIndices.end(), atomIndex), group.atomIndices.end());
            if (atomIndex < editor.m_AtomNodeIds.size())
            {
                const SceneUUID atomId = editor.m_AtomNodeIds[atomIndex];
                group.atomIds.erase(std::remove(group.atomIds.begin(), group.atomIds.end(), atomId), group.atomIds.end());
            }
        }

        if (editor.m_SelectedTransformEmptyIndex >= 0)
        {
            group.emptyIndices.erase(std::remove(group.emptyIndices.begin(), group.emptyIndices.end(), editor.m_SelectedTransformEmptyIndex), group.emptyIndices.end());
            if (editor.m_SelectedTransformEmptyIndex < static_cast<int>(editor.m_TransformEmpties.size()))
            {
                const SceneUUID selectedEmptyId = editor.m_TransformEmpties[static_cast<std::size_t>(editor.m_SelectedTransformEmptyIndex)].id;
                group.emptyIds.erase(std::remove(group.emptyIds.begin(), group.emptyIds.end(), selectedEmptyId), group.emptyIds.end());
            }
        }

        SanitizeGroup(editor, groupIndex);
    }

} // namespace ds
