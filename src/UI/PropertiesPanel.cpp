#include "UI/PropertiesPanel.h"

#include "Editor/SceneGroupingBackend.h"
#include "Layers/EditorLayer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <numeric>

#include <glm/gtc/quaternion.hpp>

namespace ds
{

    namespace
    {
        bool DrawVec3Control(const char *label, glm::vec3 &value, float resetValue = 0.0f, float speed = 0.01f)
        {
            bool changed = false;

            ImGui::PushID(label);
            ImGui::Columns(2, nullptr, false);
            ImGui::SetColumnWidth(0, 110.0f);
            ImGui::TextUnformatted(label);
            ImGui::NextColumn();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 0.0f});

            const float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
            const ImVec2 buttonSize = {lineHeight + 2.0f, lineHeight};
            const float itemWidth = std::max(10.0f, (ImGui::GetContentRegionAvail().x - buttonSize.x * 3.0f - 8.0f) / 3.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.23f, 0.20f, 1.0f));
            if (ImGui::Button("X", buttonSize))
            {
                value.x = resetValue;
                changed = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(itemWidth);
            changed |= ImGui::DragFloat("##X", &value.x, speed);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.62f, 0.29f, 1.0f));
            if (ImGui::Button("Y", buttonSize))
            {
                value.y = resetValue;
                changed = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(itemWidth);
            changed |= ImGui::DragFloat("##Y", &value.y, speed);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.40f, 0.75f, 1.0f));
            if (ImGui::Button("Z", buttonSize))
            {
                value.z = resetValue;
                changed = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(itemWidth);
            changed |= ImGui::DragFloat("##Z", &value.z, speed);

            ImGui::PopStyleVar();
            ImGui::Columns(1);
            ImGui::PopID();

            return changed;
        }

    } // namespace

    void PropertiesPanel::Draw(EditorLayer &editor, bool &settingsChanged)
    {
        ImGui::Begin("Properties", &editor.m_ShowObjectPropertiesPanel);

        const bool hasSelectedEmpty =
            editor.m_SelectedTransformEmptyIndex >= 0 &&
            editor.m_SelectedTransformEmptyIndex < static_cast<int>(editor.m_TransformEmpties.size());

        if (hasSelectedEmpty)
        {
            EditorLayer::TransformEmpty &selectedEmpty = editor.m_TransformEmpties[static_cast<std::size_t>(editor.m_SelectedTransformEmptyIndex)];

            ImGui::TextUnformatted("Entity");
            ImGui::Separator();

            std::array<char, 128> emptyNameBuffer = {};
            std::snprintf(emptyNameBuffer.data(), emptyNameBuffer.size(), "%s", selectedEmpty.name.c_str());
            if (ImGui::InputText("Tag", emptyNameBuffer.data(), emptyNameBuffer.size()))
            {
                selectedEmpty.name = std::string(emptyNameBuffer.data());
                settingsChanged = true;
            }

            ImGui::TextDisabled("Type: Empty");

            auto wouldCreateParentCycle = [&](SceneUUID candidateParentId) -> bool
            {
                if (candidateParentId == 0)
                {
                    return false;
                }
                if (candidateParentId == selectedEmpty.id)
                {
                    return true;
                }

                SceneUUID cursor = candidateParentId;
                int guard = 0;
                while (cursor != 0 && guard < 1024)
                {
                    if (cursor == selectedEmpty.id)
                    {
                        return true;
                    }

                    auto it = std::find_if(
                        editor.m_TransformEmpties.begin(),
                        editor.m_TransformEmpties.end(),
                        [&](const EditorLayer::TransformEmpty &candidate)
                        {
                            return candidate.id == cursor;
                        });
                    if (it == editor.m_TransformEmpties.end())
                    {
                        break;
                    }

                    cursor = it->parentEmptyId;
                    ++guard;
                }

                return false;
            };

            ImGui::SeparatorText("Transform");
            glm::vec3 location = selectedEmpty.position;
            if (DrawVec3Control("Location", location, 0.0f, 0.01f))
            {
                selectedEmpty.position = location;
                settingsChanged = true;
            }

            glm::vec3 basisX = selectedEmpty.axes[0];
            glm::vec3 basisY = selectedEmpty.axes[1];
            glm::vec3 basisZ = selectedEmpty.axes[2];

            glm::vec3 scale(
                glm::length(basisX),
                glm::length(basisY),
                glm::length(basisZ));
            if (scale.x < 1e-6f)
                scale.x = 1.0f;
            if (scale.y < 1e-6f)
                scale.y = 1.0f;
            if (scale.z < 1e-6f)
                scale.z = 1.0f;

            basisX = glm::normalize(basisX);
            basisY = glm::normalize(basisY);
            basisZ = glm::normalize(basisZ);
            const glm::mat3 rotationBasis(basisX, basisY, basisZ);
            const glm::quat rotationQuat = glm::normalize(glm::quat_cast(rotationBasis));
            glm::vec3 rotationDeg = glm::degrees(glm::eulerAngles(rotationQuat));

            bool transformChanged = false;
            transformChanged |= DrawVec3Control("Rotation", rotationDeg, 0.0f, 0.2f);
            transformChanged |= DrawVec3Control("Scale", scale, 1.0f, 0.01f);

            if (transformChanged)
            {
                scale.x = glm::max(0.001f, scale.x);
                scale.y = glm::max(0.001f, scale.y);
                scale.z = glm::max(0.001f, scale.z);

                const glm::quat nextRotation = glm::quat(glm::radians(rotationDeg));
                const glm::mat3 nextBasis = glm::mat3_cast(nextRotation);
                selectedEmpty.axes[0] = glm::normalize(glm::vec3(nextBasis[0])) * scale.x;
                selectedEmpty.axes[1] = glm::normalize(glm::vec3(nextBasis[1])) * scale.y;
                selectedEmpty.axes[2] = glm::normalize(glm::vec3(nextBasis[2])) * scale.z;
                settingsChanged = true;
            }

            ImGui::SeparatorText("Scene");
            const char *currentParentName = "(none)";
            if (selectedEmpty.parentEmptyId != 0)
            {
                auto parentIt = std::find_if(
                    editor.m_TransformEmpties.begin(),
                    editor.m_TransformEmpties.end(),
                    [&](const EditorLayer::TransformEmpty &candidate)
                    {
                        return candidate.id == selectedEmpty.parentEmptyId;
                    });
                if (parentIt != editor.m_TransformEmpties.end())
                {
                    currentParentName = parentIt->name.c_str();
                }
            }

            if (ImGui::BeginCombo("Parent Empty", currentParentName))
            {
                if (ImGui::Selectable("(none)", selectedEmpty.parentEmptyId == 0))
                {
                    selectedEmpty.parentEmptyId = 0;
                    settingsChanged = true;
                }

                for (const EditorLayer::TransformEmpty &candidate : editor.m_TransformEmpties)
                {
                    if (candidate.id == selectedEmpty.id)
                    {
                        continue;
                    }

                    const bool isSelected = (selectedEmpty.parentEmptyId == candidate.id);
                    const bool invalidParent = wouldCreateParentCycle(candidate.id);
                    if (invalidParent)
                    {
                        ImGui::BeginDisabled();
                    }
                    if (ImGui::Selectable(candidate.name.c_str(), isSelected) && !invalidParent)
                    {
                        selectedEmpty.parentEmptyId = candidate.id;
                        settingsChanged = true;
                    }
                    if (invalidParent)
                    {
                        ImGui::EndDisabled();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Parent Empty defines hierarchy for this Empty.\nChild follows parent transform and appears under it in Scene tree.");
            }

            const char *currentCollectionName =
                (selectedEmpty.collectionIndex >= 0 && selectedEmpty.collectionIndex < static_cast<int>(editor.m_Collections.size()))
                    ? editor.m_Collections[static_cast<std::size_t>(selectedEmpty.collectionIndex)].name.c_str()
                    : "(invalid)";
            if (ImGui::BeginCombo("Collection", currentCollectionName))
            {
                for (std::size_t i = 0; i < editor.m_Collections.size(); ++i)
                {
                    const bool selected = (selectedEmpty.collectionIndex == static_cast<int>(i));
                    if (ImGui::Selectable(editor.m_Collections[i].name.c_str(), selected))
                    {
                        selectedEmpty.collectionIndex = static_cast<int>(i);
                        selectedEmpty.collectionId = editor.m_Collections[i].id;
                        settingsChanged = true;
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Checkbox("Visible", &selectedEmpty.visible))
            {
                settingsChanged = true;
            }
            if (ImGui::Checkbox("Selectable", &selectedEmpty.selectable))
            {
                settingsChanged = true;
            }

            if (ImGui::Button("Align axes to world"))
            {
                selectedEmpty.axes = {
                    glm::vec3(1.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f)};
                settingsChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Align Z to selected atoms") && editor.m_SelectedAtomIndices.size() >= 2)
            {
                if (editor.AlignEmptyZAxisFromSelectedAtoms(editor.m_SelectedTransformEmptyIndex))
                {
                    settingsChanged = true;
                }
            }

            if (ImGui::Button("Delete Empty"))
            {
                editor.DeleteTransformEmptyAtIndex(editor.m_SelectedTransformEmptyIndex);
                settingsChanged = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Move Empty to 3D Cursor"))
            {
                selectedEmpty.position = editor.m_CursorPosition;
                settingsChanged = true;
            }
        }
        else
        {
            ImGui::TextUnformatted("Entity");
            ImGui::Separator();
            ImGui::TextDisabled("Type: Atoms / Group");
            ImGui::Text("Selected atoms: %zu", editor.m_SelectedAtomIndices.size());

            if (!editor.m_SelectedAtomIndices.empty())
            {
                ImGui::SeparatorText("Atoms");

                if (editor.m_SelectedAtomIndices.size() == 1)
                {
                    const std::size_t atomIndex = editor.m_SelectedAtomIndices.front();
                    if (atomIndex < editor.m_WorkingStructure.atoms.size())
                    {
                        auto &atom = editor.m_WorkingStructure.atoms[atomIndex];
                        std::array<char, 16> elementBuffer = {};
                        std::snprintf(elementBuffer.data(), elementBuffer.size(), "%s", atom.element.c_str());
                        if (ImGui::InputText("Element", elementBuffer.data(), elementBuffer.size()))
                        {
                            atom.element = std::string(elementBuffer.data());
                            editor.m_WorkingStructure.RebuildSpeciesFromAtoms();
                            settingsChanged = true;
                        }

                        glm::vec3 atomPosition = editor.GetAtomCartesianPosition(atomIndex);
                        if (DrawVec3Control("Atom Position", atomPosition, 0.0f, 0.01f))
                        {
                            editor.SetAtomCartesianPosition(atomIndex, atomPosition);
                            settingsChanged = true;
                        }
                    }
                }

                if (ImGui::Button(editor.m_SelectedAtomIndices.size() == 1 ? "Move Atom to 3D Cursor" : "Move Atoms to 3D Cursor"))
                {
                    glm::vec3 selectionCenter = glm::vec3(0.0f);
                    std::size_t validCount = 0;
                    for (std::size_t atomIndex : editor.m_SelectedAtomIndices)
                    {
                        if (atomIndex >= editor.m_WorkingStructure.atoms.size())
                        {
                            continue;
                        }
                        selectionCenter += editor.GetAtomCartesianPosition(atomIndex);
                        ++validCount;
                    }

                    if (validCount > 0)
                    {
                        selectionCenter /= static_cast<float>(validCount);
                        const glm::vec3 delta = editor.m_CursorPosition - selectionCenter;
                        for (std::size_t atomIndex : editor.m_SelectedAtomIndices)
                        {
                            if (atomIndex >= editor.m_WorkingStructure.atoms.size())
                            {
                                continue;
                            }
                            editor.SetAtomCartesianPosition(atomIndex, editor.GetAtomCartesianPosition(atomIndex) + delta);
                        }
                        settingsChanged = true;
                    }
                }
            }

            if (!editor.m_SelectedAtomIndices.empty())
            {
                if (ImGui::Button("Create Group from Selection"))
                {
                    settingsChanged |= SceneGroupingBackend::CreateGroupFromCurrentSelection(editor);
                }
            }

            if (editor.m_ActiveGroupIndex >= 0 && editor.m_ActiveGroupIndex < static_cast<int>(editor.m_ObjectGroups.size()))
            {
                ImGui::SeparatorText("Group");
                SceneGroupingBackend::SanitizeGroup(editor, editor.m_ActiveGroupIndex);
                EditorLayer::SceneGroup &group = editor.m_ObjectGroups[static_cast<std::size_t>(editor.m_ActiveGroupIndex)];

                std::array<char, 128> groupNameBuffer = {};
                std::snprintf(groupNameBuffer.data(), groupNameBuffer.size(), "%s", group.name.c_str());
                if (ImGui::InputText("Group Tag", groupNameBuffer.data(), groupNameBuffer.size()))
                {
                    group.name = std::string(groupNameBuffer.data());
                    settingsChanged = true;
                }

                ImGui::Text("Atoms: %zu", group.atomIndices.size());
                ImGui::Text("Empties: %zu", group.emptyIndices.size());

                if (ImGui::Button("Select Group"))
                {
                    SceneGroupingBackend::SelectGroup(editor, editor.m_ActiveGroupIndex);
                }

                const bool hasSelection = !editor.m_SelectedAtomIndices.empty() || editor.m_SelectedTransformEmptyIndex >= 0;
                if (!hasSelection)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Add Selection"))
                {
                    SceneGroupingBackend::AddCurrentSelectionToGroup(editor, editor.m_ActiveGroupIndex);
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Selection"))
                {
                    SceneGroupingBackend::RemoveCurrentSelectionFromGroup(editor, editor.m_ActiveGroupIndex);
                    settingsChanged = true;
                }
                if (!hasSelection)
                {
                    ImGui::EndDisabled();
                }

                if (ImGui::Button("Delete Group"))
                {
                    settingsChanged |= SceneGroupingBackend::DeleteGroup(editor, editor.m_ActiveGroupIndex);
                }
            }
        }

        ImGui::End();
    }

} // namespace ds
