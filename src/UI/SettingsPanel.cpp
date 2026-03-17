#include "UI/SettingsPanel.h"

#include "Layers/EditorLayer.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>

#include <glm/vec3.hpp>

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

    void SettingsPanel::Draw(EditorLayer &editor, bool &settingsChanged)
    {
        ImGui::Begin("Settings", &editor.m_ShowSettingsPanel);

        const char *gizmoOperations[] = {"Translate", "Rotate", "Scale"};
        const char *gizmoModes[] = {"Local (selection)", "World", "Relative (surrounding)"};

        ImGui::SeparatorText("Gizmo");
        if (ImGui::Checkbox("Enable gizmo", &editor.m_GizmoEnabled))
        {
            settingsChanged = true;
        }
        if (ImGui::Checkbox("Enable view gizmo overlay", &editor.m_ViewGuizmoEnabled))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("Transform gizmo size", &editor.m_TransformGizmoSize, 0.05f, 0.35f, "%.2f"))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("View gizmo scale", &editor.m_ViewGizmoScale, 0.35f, 2.20f, "%.2f"))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("Fallback marker scale", &editor.m_FallbackGizmoVisualScale, 0.5f, 6.0f, "%.2f"))
        {
            settingsChanged = true;
        }
        if (ImGui::Checkbox("Show transform empties", &editor.m_ShowTransformEmpties))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("Empty visual scale", &editor.m_TransformEmptyVisualScale, 0.06f, 0.55f, "%.2f"))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("View gizmo offset right", &editor.m_ViewGizmoOffsetRight, 0.0f, 220.0f, "%.0f"))
        {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("View gizmo offset top", &editor.m_ViewGizmoOffsetTop, 0.0f, 220.0f, "%.0f"))
        {
            settingsChanged = true;
        }
        if (ImGui::Checkbox("Drag view gizmo in viewport", &editor.m_ViewGizmoDragMode))
        {
            if (!editor.m_ViewGizmoDragMode)
            {
                editor.m_ViewGizmoDragging = false;
            }
            settingsChanged = true;
        }
        ImGui::Combo("Gizmo operation", &editor.m_GizmoOperationIndex, gizmoOperations, IM_ARRAYSIZE(gizmoOperations));
        ImGui::Combo("Gizmo mode", &editor.m_GizmoModeIndex, gizmoModes, IM_ARRAYSIZE(gizmoModes));
        if (ImGui::Checkbox("Show global XYZ overlay", &editor.m_ShowGlobalAxesOverlay))
        {
            settingsChanged = true;
        }

        if (ImGui::SliderFloat("UI spacing scale", &editor.m_UiSpacingScale, 0.75f, 1.80f, "%.2f"))
        {
            settingsChanged = true;
        }

        if (ImGui::Checkbox("Use temporary local axes", &editor.m_UseTemporaryLocalAxes))
        {
            settingsChanged = true;
        }

        if (editor.m_UseTemporaryLocalAxes)
        {
            int temporarySourceMode = (editor.m_TemporaryAxesSource == EditorLayer::TemporaryAxesSource::ActiveEmpty) ? 1 : 0;
            const char *temporarySourceModes[] = {"Selection atoms (A/B/C)", "Active Empty"};
            if (ImGui::Combo("Temporary frame source", &temporarySourceMode, temporarySourceModes, IM_ARRAYSIZE(temporarySourceModes)))
            {
                editor.m_TemporaryAxesSource = (temporarySourceMode == 1) ? EditorLayer::TemporaryAxesSource::ActiveEmpty : EditorLayer::TemporaryAxesSource::SelectionAtoms;
                settingsChanged = true;
            }

            ImGui::SeparatorText("Transform Empty");
            if (ImGui::Button("Add empty at 3D cursor"))
            {
                EditorLayer::TransformEmpty empty;
                empty.id = GenerateSceneUUID();
                empty.position = editor.m_CursorPosition;
                empty.collectionIndex = editor.m_ActiveCollectionIndex;
                empty.collectionId = editor.m_Collections[static_cast<std::size_t>(editor.m_ActiveCollectionIndex)].id;
                char label[32] = {};
                std::snprintf(label, sizeof(label), "Empty %d", editor.m_TransformEmptyCounter++);
                empty.name = label;
                editor.m_TransformEmpties.push_back(empty);
                editor.m_ActiveTransformEmptyIndex = static_cast<int>(editor.m_TransformEmpties.size()) - 1;
                editor.m_SelectedTransformEmptyIndex = editor.m_ActiveTransformEmptyIndex;
                settingsChanged = true;
            }

            ImGui::SameLine();
            const bool canAddFromSelection = !editor.m_SelectedAtomIndices.empty();
            if (!canAddFromSelection)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Add empty at selection center") && canAddFromSelection)
            {
                EditorLayer::TransformEmpty empty;
                empty.id = GenerateSceneUUID();
                empty.position = editor.ComputeSelectionCenter();
                editor.ComputeSelectionAxesAround(empty.position, empty.axes);
                empty.collectionIndex = editor.m_ActiveCollectionIndex;
                empty.collectionId = editor.m_Collections[static_cast<std::size_t>(editor.m_ActiveCollectionIndex)].id;
                char label[32] = {};
                std::snprintf(label, sizeof(label), "Empty %d", editor.m_TransformEmptyCounter++);
                empty.name = label;
                editor.m_TransformEmpties.push_back(empty);
                editor.m_ActiveTransformEmptyIndex = static_cast<int>(editor.m_TransformEmpties.size()) - 1;
                editor.m_SelectedTransformEmptyIndex = editor.m_ActiveTransformEmptyIndex;
                settingsChanged = true;
            }
            if (!canAddFromSelection)
            {
                ImGui::EndDisabled();
            }

            if (ImGui::BeginCombo("Active empty", editor.HasActiveTransformEmpty() ? editor.m_TransformEmpties[static_cast<std::size_t>(editor.m_ActiveTransformEmptyIndex)].name.c_str() : "(none)"))
            {
                for (std::size_t i = 0; i < editor.m_TransformEmpties.size(); ++i)
                {
                    const bool isSelected = (static_cast<int>(i) == editor.m_ActiveTransformEmptyIndex);
                    if (ImGui::Selectable(editor.m_TransformEmpties[i].name.c_str(), isSelected))
                    {
                        editor.m_ActiveTransformEmptyIndex = static_cast<int>(i);
                        editor.m_SelectedTransformEmptyIndex = editor.m_ActiveTransformEmptyIndex;
                        settingsChanged = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            bool hasActiveEmpty = editor.HasActiveTransformEmpty();
            if (!hasActiveEmpty)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Use active empty as tmp transform") && hasActiveEmpty)
            {
                editor.m_TemporaryAxesSource = EditorLayer::TemporaryAxesSource::ActiveEmpty;
                editor.m_CursorPosition = editor.m_TransformEmpties[static_cast<std::size_t>(editor.m_ActiveTransformEmptyIndex)].position;
                settingsChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete active empty") && hasActiveEmpty)
            {
                editor.DeleteTransformEmptyAtIndex(editor.m_ActiveTransformEmptyIndex);
                settingsChanged = true;
                hasActiveEmpty = editor.HasActiveTransformEmpty();
            }

            if (hasActiveEmpty)
            {
                EditorLayer::TransformEmpty &activeEmpty = editor.m_TransformEmpties[static_cast<std::size_t>(editor.m_ActiveTransformEmptyIndex)];
                ImGui::Text("Pos: (%.3f, %.3f, %.3f)", activeEmpty.position.x, activeEmpty.position.y, activeEmpty.position.z);

                if (ImGui::Button("Move empty to selection center") && canAddFromSelection)
                {
                    activeEmpty.position = editor.ComputeSelectionCenter();
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Move empty to 3D cursor"))
                {
                    activeEmpty.position = editor.m_CursorPosition;
                    settingsChanged = true;
                }

                if (ImGui::Button("Align empty axes to world"))
                {
                    activeEmpty.axes = {
                        glm::vec3(1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f)};
                    settingsChanged = true;
                }

                if (ImGui::Button("Align empty axes from selection") && canAddFromSelection)
                {
                    std::array<glm::vec3, 3> axes = activeEmpty.axes;
                    if (editor.ComputeSelectionAxesAround(activeEmpty.position, axes))
                    {
                        activeEmpty.axes = axes;
                        settingsChanged = true;
                    }
                }

                if (ImGui::Button("Align empty Z axis to selected atoms") && editor.m_SelectedAtomIndices.size() >= 2)
                {
                    if (editor.AlignEmptyZAxisFromSelectedAtoms(editor.m_ActiveTransformEmptyIndex))
                    {
                        settingsChanged = true;
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Selection -> Add empty + tmp transform") && canAddFromSelection)
                {
                    EditorLayer::TransformEmpty empty;
                    empty.id = GenerateSceneUUID();
                    empty.position = editor.ComputeSelectionCenter();
                    editor.ComputeSelectionAxesAround(empty.position, empty.axes);
                    empty.collectionIndex = editor.m_ActiveCollectionIndex;
                    empty.collectionId = editor.m_Collections[static_cast<std::size_t>(editor.m_ActiveCollectionIndex)].id;
                    char label[32] = {};
                    std::snprintf(label, sizeof(label), "Empty %d", editor.m_TransformEmptyCounter++);
                    empty.name = label;
                    editor.m_TransformEmpties.push_back(empty);
                    editor.m_ActiveTransformEmptyIndex = static_cast<int>(editor.m_TransformEmpties.size()) - 1;
                    editor.m_SelectedTransformEmptyIndex = editor.m_ActiveTransformEmptyIndex;
                    editor.m_TemporaryAxesSource = EditorLayer::TemporaryAxesSource::ActiveEmpty;
                    editor.m_CursorPosition = empty.position;
                    settingsChanged = true;
                }
            }

            if (!hasActiveEmpty)
            {
                ImGui::EndDisabled();
            }

            if (editor.m_TemporaryAxesSource == EditorLayer::TemporaryAxesSource::SelectionAtoms)
            {
                ImGui::SeparatorText("Selection-axes authoring");
                ImGui::TextUnformatted("Temporary frame: X = A -> B, C defines frame plane.");
                ImGui::Text("A=%d  B=%d  C=%d", editor.m_TemporaryAxisAtomA, editor.m_TemporaryAxisAtomB, editor.m_TemporaryAxisAtomC);

                const bool hasAtLeastTwoSelected = editor.m_SelectedAtomIndices.size() >= 2;
                const bool hasAtLeastThreeSelected = editor.m_SelectedAtomIndices.size() >= 3;

                if (!hasAtLeastTwoSelected)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Set A/B from first->last selected") && hasAtLeastTwoSelected)
                {
                    editor.m_TemporaryAxisAtomA = static_cast<int>(editor.m_SelectedAtomIndices.front());
                    editor.m_TemporaryAxisAtomB = static_cast<int>(editor.m_SelectedAtomIndices.back());
                    if (editor.m_TemporaryAxisAtomA == editor.m_TemporaryAxisAtomB && editor.m_SelectedAtomIndices.size() > 1)
                    {
                        editor.m_TemporaryAxisAtomB = static_cast<int>(editor.m_SelectedAtomIndices[editor.m_SelectedAtomIndices.size() - 2]);
                    }
                    settingsChanged = true;
                }
                if (!hasAtLeastTwoSelected)
                {
                    ImGui::EndDisabled();
                }

                if (!hasAtLeastThreeSelected)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Set A/B/C from last 3 selected") && hasAtLeastThreeSelected)
                {
                    const std::size_t n = editor.m_SelectedAtomIndices.size();
                    editor.m_TemporaryAxisAtomA = static_cast<int>(editor.m_SelectedAtomIndices[n - 3]);
                    editor.m_TemporaryAxisAtomB = static_cast<int>(editor.m_SelectedAtomIndices[n - 2]);
                    editor.m_TemporaryAxisAtomC = static_cast<int>(editor.m_SelectedAtomIndices[n - 1]);
                    settingsChanged = true;
                }
                if (!hasAtLeastThreeSelected)
                {
                    ImGui::EndDisabled();
                }

                if (ImGui::Button("Clear temporary frame"))
                {
                    editor.m_TemporaryAxisAtomA = -1;
                    editor.m_TemporaryAxisAtomB = -1;
                    editor.m_TemporaryAxisAtomC = -1;
                    settingsChanged = true;
                }
            }
        }

        if (ImGui::Checkbox("Gizmo snap", &editor.m_GizmoSnapEnabled))
        {
            settingsChanged = true;
        }
        if (editor.m_GizmoOperationIndex == 0)
        {
            ImGui::SliderFloat("Translate snap", &editor.m_GizmoTranslateSnap, 0.01f, 2.0f, "%.2f");
        }
        else if (editor.m_GizmoOperationIndex == 1)
        {
            ImGui::SliderFloat("Rotate snap (deg)", &editor.m_GizmoRotateSnapDeg, 1.0f, 90.0f, "%.1f");
        }
        else
        {
            ImGui::SliderFloat("Scale snap", &editor.m_GizmoScaleSnap, 0.01f, 1.0f, "%.2f");
        }

        if (ImGui::Checkbox("Selection debug log to file", &editor.m_SelectionDebugToFile))
        {
            settingsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear selection log file"))
        {
            std::filesystem::create_directories("logs");
            std::ofstream clearOut("logs/selection_debug.log", std::ios::trunc);
            if (clearOut.is_open())
            {
                clearOut << "";
            }
            editor.AppendSelectionDebugLog("Selection debug log file cleared");
        }

        if (ImGui::Button("Save UI settings"))
        {
            editor.SaveSettings();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("(auto-save on change enabled)");

        ImGui::End();
    }

} // namespace ds
