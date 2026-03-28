#include "UI/SettingsPanel.h"

#include "Layers/EditorLayer.h"
#include "Layers/ImGuiLayer.h"
#include "Renderer/OrbitCamera.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>

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

        auto drawHelpMarker = [](const char *description)
        {
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                ImGui::TextUnformatted(description);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        };

        const char *gizmoOperations[] = {"Translate", "Rotate", "Scale"};
        const char *gizmoModes[] = {"Local (selection)", "World", "Relative (surrounding)"};
        const ImGuiTreeNodeFlags defaultOpenFlags = ImGuiTreeNodeFlags_DefaultOpen;
        auto keyDisplayName = [](std::uint32_t key) -> std::string
        {
            if (key == 0u)
            {
                return std::string("(unbound)");
            }

            const char *name = ImGui::GetKeyName(static_cast<ImGuiKey>(key));
            if (name == nullptr || name[0] == '\0')
            {
                return "Key " + std::to_string(key);
            }

            return std::string(name);
        };
        static const ImGuiKey kHotkeyOptions[] = {
            ImGuiKey_None,
            ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E, ImGuiKey_F, ImGuiKey_G,
            ImGuiKey_H, ImGuiKey_I, ImGuiKey_J, ImGuiKey_K, ImGuiKey_L, ImGuiKey_M, ImGuiKey_N,
            ImGuiKey_O, ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R, ImGuiKey_S, ImGuiKey_T, ImGuiKey_U,
            ImGuiKey_V, ImGuiKey_W, ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z,
            ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5, ImGuiKey_F6,
            ImGuiKey_F7, ImGuiKey_F8, ImGuiKey_F9, ImGuiKey_F10, ImGuiKey_F11, ImGuiKey_F12,
            ImGuiKey_Delete};
        auto drawHotkeyCombo = [&](const char *label, std::uint32_t &binding, const char *description)
        {
            const std::string preview = keyDisplayName(binding);
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                for (ImGuiKey key : kHotkeyOptions)
                {
                    const std::uint32_t keyValue = static_cast<std::uint32_t>(key);
                    const bool isSelected = (binding == keyValue);
                    const std::string optionLabel = keyDisplayName(keyValue);
                    if (ImGui::Selectable(optionLabel.c_str(), isSelected))
                    {
                        binding = keyValue;
                        settingsChanged = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            drawHelpMarker(description);
        };
        auto beginSettingsTable = [&](const char *id) -> bool
        {
            return ImGui::BeginTable(
                id,
                2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings);
        };
        auto beginSettingsRow = [&](const char *label, const char *description = nullptr)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            if (description != nullptr)
            {
                ImGui::SameLine();
                drawHelpMarker(description);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
        };
        auto drawHotkeyComboRow = [&](const char *comboId, std::uint32_t &binding)
        {
            const std::string preview = keyDisplayName(binding);
            if (ImGui::BeginCombo(comboId, preview.c_str()))
            {
                for (ImGuiKey key : kHotkeyOptions)
                {
                    const std::uint32_t keyValue = static_cast<std::uint32_t>(key);
                    const bool isSelected = (binding == keyValue);
                    const std::string optionLabel = keyDisplayName(keyValue);
                    if (ImGui::Selectable(optionLabel.c_str(), isSelected))
                    {
                        binding = keyValue;
                        settingsChanged = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        };
        auto resetInputDefaults = [&]()
        {
            editor.m_TouchpadNavigationEnabled = true;
            editor.m_InvertViewportZoom = false;
            editor.m_InvertCircleSelectWheel = false;
            editor.m_CircleSelectWheelStep = 4.0f;
            editor.m_HotkeyAddMenu = static_cast<std::uint32_t>(ImGuiKey_A);
            editor.m_HotkeyOpenRender = static_cast<std::uint32_t>(ImGuiKey_F12);
            editor.m_HotkeyToggleSidePanels = static_cast<std::uint32_t>(ImGuiKey_N);
            editor.m_HotkeyDeleteSelection = static_cast<std::uint32_t>(ImGuiKey_Delete);
            editor.m_HotkeyHideSelection = static_cast<std::uint32_t>(ImGuiKey_H);
            editor.m_HotkeyBoxSelect = static_cast<std::uint32_t>(ImGuiKey_B);
            editor.m_HotkeyCircleSelect = static_cast<std::uint32_t>(ImGuiKey_C);
            editor.m_HotkeyTranslateModal = static_cast<std::uint32_t>(ImGuiKey_G);
            editor.m_HotkeyTranslateGizmo = static_cast<std::uint32_t>(ImGuiKey_T);
            editor.m_HotkeyRotateGizmo = static_cast<std::uint32_t>(ImGuiKey_R);
            editor.m_HotkeyScaleGizmo = static_cast<std::uint32_t>(ImGuiKey_S);
            settingsChanged = true;
        };
        auto resetDockLayoutDefaults = [&]()
        {
            editor.m_ShowDemoWindow = false;
            editor.m_ShowLogPanel = true;
            editor.m_ShowStatsPanel = true;
            editor.m_ShowViewportInfoPanel = true;
            editor.m_ShowShortcutReferencePanel = false;
            editor.m_ShowSceneOutlinerPanel = true;
            editor.m_ShowObjectPropertiesPanel = true;
            editor.m_ShowActionsPanel = true;
            editor.m_ShowAppearancePanel = true;
            editor.m_ViewportSettingsOpen = true;
            editor.m_ShowRenderPreviewWindow = true;
            editor.m_ShowSettingsPanel = true;
            editor.m_RequestDockLayoutReset = true;
            settingsChanged = true;
        };
        auto resetViewportPerformanceDefaults = [&]()
        {
            editor.m_RenderScaleMin = 0.25f;
            editor.m_RenderScaleMax = 1.0f;
            editor.m_ViewportRenderScale = 1.0f;
            settingsChanged = true;
        };

        if (ImGui::CollapsingHeader("Gizmo", defaultOpenFlags))
        {
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
            if (ImGui::SliderFloat("Viewport rotate step (deg)", &editor.m_ViewportRotateStepDeg, 0.1f, 180.0f, "%.1f"))
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
            ImGui::SameLine();
            drawHelpMarker("Temporary local axes let you build a transform frame from selected atoms or an active Empty.\nUseful for defect-local edits when world axes are not convenient.");

            if (editor.m_UseTemporaryLocalAxes && ImGui::CollapsingHeader("Temporary Axes & Transform Empty", defaultOpenFlags))
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
                    editor.PushUndoSnapshot("Add empty at 3D cursor");
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
                    editor.PushUndoSnapshot("Add empty at selection center");
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
                    editor.m_UseTemporaryLocalAxes = true;
                    editor.m_TemporaryAxesSource = EditorLayer::TemporaryAxesSource::ActiveEmpty;
                    editor.m_GizmoModeIndex = 0;
                    editor.m_CursorPosition = editor.m_TransformEmpties[static_cast<std::size_t>(editor.m_ActiveTransformEmptyIndex)].position;
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete active empty") && hasActiveEmpty)
                {
                    editor.PushUndoSnapshot("Delete active empty");
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
                        editor.PushUndoSnapshot("Move empty to selection center");
                        activeEmpty.position = editor.ComputeSelectionCenter();
                        settingsChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Move empty to 3D cursor"))
                    {
                        editor.PushUndoSnapshot("Move empty to 3D cursor");
                        activeEmpty.position = editor.m_CursorPosition;
                        settingsChanged = true;
                    }

                    if (ImGui::Button("Align empty axes to world"))
                    {
                        editor.PushUndoSnapshot("Align empty axes to world");
                        activeEmpty.axes = {
                            glm::vec3(1.0f, 0.0f, 0.0f),
                            glm::vec3(0.0f, 1.0f, 0.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f)};
                        settingsChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Align empty axes to camera view"))
                    {
                        editor.PushUndoSnapshot("Align empty axes to camera view");
                        if (editor.AlignEmptyAxesToCameraView(editor.m_ActiveTransformEmptyIndex))
                        {
                            settingsChanged = true;
                        }
                    }

                    if (ImGui::Button("Align empty axes from selection") && canAddFromSelection)
                    {
                        editor.PushUndoSnapshot("Align empty axes from selection");
                        std::array<glm::vec3, 3> axes = activeEmpty.axes;
                        if (editor.ComputeSelectionAxesAround(activeEmpty.position, axes))
                        {
                            activeEmpty.axes = axes;
                            settingsChanged = true;
                        }
                    }

                    if (ImGui::Button("Align empty Z axis to selected atoms") && editor.m_SelectedAtomIndices.size() >= 2)
                    {
                        editor.PushUndoSnapshot("Align empty Z axis from selected atoms");
                        if (editor.AlignEmptyZAxisFromSelectedAtoms(editor.m_ActiveTransformEmptyIndex))
                        {
                            settingsChanged = true;
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Selection -> Add empty + tmp transform") && canAddFromSelection)
                    {
                        editor.PushUndoSnapshot("Create empty from selection");
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
                    ImGui::SameLine();
                    drawHelpMarker("A and B define the X axis direction.\nC supplies the reference plane so the remaining axes can be resolved consistently.");
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
        }

        if (ImGui::CollapsingHeader("Snap", defaultOpenFlags))
        {
            if (beginSettingsTable("SettingsSnapTable"))
            {
                beginSettingsRow("Gizmo snap", "Keeps snapping always on. Holding Ctrl in the viewport also enables temporary snap.");
                if (ImGui::Checkbox("##GizmoSnap", &editor.m_GizmoSnapEnabled))
                {
                    settingsChanged = true;
                }

                if (editor.m_GizmoOperationIndex == 0)
                {
                    beginSettingsRow("Translate snap");
                    if (ImGui::SliderFloat("##TranslateSnap", &editor.m_GizmoTranslateSnap, 0.01f, 2.0f, "%.2f"))
                    {
                        settingsChanged = true;
                    }
                }
                else if (editor.m_GizmoOperationIndex == 1)
                {
                    beginSettingsRow("Rotate snap (deg)");
                    if (ImGui::SliderFloat("##RotateSnap", &editor.m_GizmoRotateSnapDeg, 1.0f, 90.0f, "%.1f"))
                    {
                        settingsChanged = true;
                    }
                }
                else
                {
                    beginSettingsRow("Scale snap");
                    if (ImGui::SliderFloat("##ScaleSnap", &editor.m_GizmoScaleSnap, 0.01f, 1.0f, "%.2f"))
                    {
                        settingsChanged = true;
                    }
                }

                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Selection Debug", defaultOpenFlags))
        {
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
        }

        if (ImGui::CollapsingHeader("Input & Navigation", defaultOpenFlags))
        {
            if (beginSettingsTable("SettingsInputNavigationTable"))
            {
                beginSettingsRow("Touchpad-friendly navigation", "Enables Alt+LMB orbit, Alt+Shift+LMB pan and Alt+RMB zoom for laptop / touchpad workflows.");
                if (ImGui::Checkbox("##TouchpadNavigation", &editor.m_TouchpadNavigationEnabled))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Invert viewport zoom", "Flips mouse-wheel and touchpad zoom direction in the 3D viewport.");
                if (ImGui::Checkbox("##InvertViewportZoom", &editor.m_InvertViewportZoom))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Invert circle-select wheel", "Reverses how the mouse wheel changes the circle-select radius while the C tool is armed.");
                if (ImGui::Checkbox("##InvertCircleWheel", &editor.m_InvertCircleSelectWheel))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Circle-select wheel step", "How much the selection circle grows or shrinks per wheel tick.");
                if (ImGui::SliderFloat("##CircleWheelStep", &editor.m_CircleSelectWheelStep, 1.0f, 32.0f, "%.0f px"))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Offset duplicates after paste", "When enabled, Duplicate uses a small automatic offset so the copy is immediately visible. When disabled, duplicates stay in place.");
                if (ImGui::Checkbox("##DuplicateAppliesOffset", &editor.m_DuplicateAppliesOffset))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Focus distance multiplier", "Base zoom multiplier used by . when focusing the current selection. Falls back to the 3D cursor when nothing is selected.");
                if (ImGui::SliderFloat("##CursorFocusDistanceFactor", &editor.m_CursorFocusDistanceFactor, 0.25f, 2.50f, "%.2fx"))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Focus min distance", "Hard lower bound for . so single atoms or empties are not framed uncomfortably close.");
                if (ImGui::SliderFloat("##CursorFocusMinDistance", &editor.m_CursorFocusMinDistance, 0.10f, 20.0f, "%.2f"))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Focus selection padding", "Extra breathing room kept around the selected object or group. Increase for looser framing, decrease for tighter zoom.");
                if (ImGui::SliderFloat("##CursorFocusSelectionPadding", &editor.m_CursorFocusSelectionPadding, 1.0f, 8.0f, "%.2fx"))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Scene clip near padding", "Extra front-plane safety around visible atoms and empties. Increase this if geometry still clips after using . to focus.");
                if (ImGui::SliderFloat("##CameraClipNearPadding", &editor.m_CameraClipNearPadding, 0.5f, 8.0f, "%.2fx"))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Scene clip far padding", "Extra far-plane safety added around the visible scene when the camera clip range is computed automatically.");
                if (ImGui::SliderFloat("##CameraClipFarPadding", &editor.m_CameraClipFarPadding, 1.0f, 12.0f, "%.2fx"))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Current clip range", "Read-only camera clip range after the scene-aware clip calculation.");
                if (editor.m_Camera)
                {
                    ImGui::Text("%.4f .. %.2f", editor.m_Camera->GetNearClip(), editor.m_Camera->GetFarClip());
                }
                else
                {
                    ImGui::TextUnformatted("n/a");
                }

                beginSettingsRow("Add object popup", "Used together with Shift. Default: Shift+A.");
                drawHotkeyComboRow("##HotkeyAddMenu", editor.m_HotkeyAddMenu);

                beginSettingsRow("Render image dialog", "Opens the Render Image popup and preview. Default: F12.");
                drawHotkeyComboRow("##HotkeyOpenRender", editor.m_HotkeyOpenRender);

                beginSettingsRow("Toggle side panels", "Shows or hides the docked workflow sidebars. Default: N.");
                drawHotkeyComboRow("##HotkeyTogglePanels", editor.m_HotkeyToggleSidePanels);

                beginSettingsRow("Delete selection", "Deletes the current selection or active bond label. Default: Delete.");
                drawHotkeyComboRow("##HotkeyDeleteSelection", editor.m_HotkeyDeleteSelection);

                beginSettingsRow("Hide selection", "H hides, Alt+same key unhides all scene elements. Default: H.");
                drawHotkeyComboRow("##HotkeyHideSelection", editor.m_HotkeyHideSelection);

                beginSettingsRow("Arm box select", "Arms rectangle selection in the viewport. Default: B.");
                drawHotkeyComboRow("##HotkeyBoxSelect", editor.m_HotkeyBoxSelect);

                beginSettingsRow("Arm circle select", "Arms circle selection in the viewport. Default: C.");
                drawHotkeyComboRow("##HotkeyCircleSelect", editor.m_HotkeyCircleSelect);

                beginSettingsRow("Modal translate", "Starts Blender-style modal translate. Default: G.");
                drawHotkeyComboRow("##HotkeyTranslateModal", editor.m_HotkeyTranslateModal);

                beginSettingsRow("Translate gizmo", "Switches the transform gizmo to translate mode. Default: T.");
                drawHotkeyComboRow("##HotkeyTranslateGizmo", editor.m_HotkeyTranslateGizmo);

                beginSettingsRow("Rotate gizmo", "Switches the transform gizmo to rotate mode. Default: R.");
                drawHotkeyComboRow("##HotkeyRotateGizmo", editor.m_HotkeyRotateGizmo);

                beginSettingsRow("Scale gizmo", "Switches the transform gizmo to scale mode. Default: S.");
                drawHotkeyComboRow("##HotkeyScaleGizmo", editor.m_HotkeyScaleGizmo);

                beginSettingsRow("Reset input defaults");
                if (ImGui::Button("Reset##InputDefaults"))
                {
                    resetInputDefaults();
                }

                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Windows & Diagnostics", defaultOpenFlags))
        {
            if (beginSettingsTable("SettingsWindowsDiagnosticsTable"))
            {
                beginSettingsRow("Show log / errors");
                if (ImGui::Checkbox("##ShowLogPanel", &editor.m_ShowLogPanel))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Show stats");
                if (ImGui::Checkbox("##ShowStatsPanel", &editor.m_ShowStatsPanel))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Show viewport info");
                if (ImGui::Checkbox("##ShowViewportInfoPanel", &editor.m_ShowViewportInfoPanel))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Show shortcuts");
                if (ImGui::Checkbox("##ShowShortcutReferencePanel", &editor.m_ShowShortcutReferencePanel))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Show ImGui Demo");
                if (ImGui::Checkbox("##ShowDemoWindow", &editor.m_ShowDemoWindow))
                {
                    settingsChanged = true;
                }

                beginSettingsRow("Open diagnostics windows");
                if (ImGui::Button("Open##DiagnosticsWindows"))
                {
                    editor.m_ShowLogPanel = true;
                    editor.m_ShowStatsPanel = true;
                    editor.m_ShowViewportInfoPanel = true;
                    editor.m_ShowShortcutReferencePanel = true;
                    settingsChanged = true;
                }

                ImGui::EndTable();
            }

            ImGui::SeparatorText("Profiler");
#ifdef TRACY_ENABLE
            ImGui::TextUnformatted("Tracy instrumentation: enabled in this build");
#else
            ImGui::TextUnformatted("Tracy instrumentation: disabled in this build");
#endif
            ImGui::SameLine();
            drawHelpMarker("Use the Stats panel for quick frame timing in-app. Tracy captures are available only in builds compiled with TRACY_ENABLE.");
        }

        if (ImGui::CollapsingHeader("Persistence", defaultOpenFlags))
        {
            if (beginSettingsTable("SettingsPersistenceTable"))
            {
                beginSettingsRow("Current project");
                ImGui::TextWrapped("%s", editor.GetProjectRootPath().string().c_str());

                beginSettingsRow("Save UI settings");
                if (ImGui::Button("Save##UiSettings"))
                {
                    editor.SaveSettings();
                }
                ImGui::SameLine();
                ImGui::TextUnformatted("Auto-save on change is enabled");

                beginSettingsRow("Save current ImGui style", "Use after tweaking colors, rounding or spacing in the ImGui Demo style editor.");
                if (ImGui::Button("Save##ImGuiStyle"))
                {
                    ImGuiLayer::SaveCurrentStyle();
                }

                beginSettingsRow("Load saved ImGui style");
                if (ImGui::Button("Load##ImGuiStyle"))
                {
                    if (ImGuiLayer::LoadSavedStyle())
                    {
                        settingsChanged = true;
                    }
                }

                beginSettingsRow("Reset saved ImGui style");
                if (ImGui::Button("Reset##ImGuiStyle"))
                {
                    ImGuiLayer::ResetSavedStyle();
                    editor.ApplyTheme(editor.m_CurrentTheme);
                    settingsChanged = true;
                }

                beginSettingsRow("Reset viewport performance", "Restores native viewport rendering scale and the default allowed render-scale range.");
                if (ImGui::Button("Reset##ViewportPerformance"))
                {
                    resetViewportPerformanceDefaults();
                }

                beginSettingsRow("Reset dock layout", "Restores the default Hazel-like docking arrangement and panel visibility.");
                if (ImGui::Button("Reset##DockLayout"))
                {
                    resetDockLayoutDefaults();
                }

                ImGui::EndTable();
            }
        }

        ImGui::End();
    }

} // namespace ds
