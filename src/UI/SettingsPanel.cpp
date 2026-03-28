#include "UI/SettingsPanel.h"

#include "Layers/EditorLayer.h"
#include "Layers/ImGuiLayer.h"

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
                    editor.m_TemporaryAxesSource = EditorLayer::TemporaryAxesSource::ActiveEmpty;
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
            if (ImGui::Checkbox("Touchpad-friendly navigation", &editor.m_TouchpadNavigationEnabled))
            {
                settingsChanged = true;
            }
            ImGui::SameLine();
            drawHelpMarker("Enables Alt+LMB orbit, Alt+Shift+LMB pan and Alt+RMB zoom for laptop / touchpad workflows.");

            if (ImGui::Checkbox("Invert viewport zoom", &editor.m_InvertViewportZoom))
            {
                settingsChanged = true;
            }
            ImGui::SameLine();
            drawHelpMarker("Flips mouse-wheel and touchpad zoom direction in the 3D viewport.");

            if (ImGui::Checkbox("Invert circle-select wheel", &editor.m_InvertCircleSelectWheel))
            {
                settingsChanged = true;
            }
            ImGui::SameLine();
            drawHelpMarker("Reverses how the mouse wheel changes the circle-select radius while the C tool is armed.");

            if (ImGui::SliderFloat("Circle-select wheel step", &editor.m_CircleSelectWheelStep, 1.0f, 32.0f, "%.0f px"))
            {
                settingsChanged = true;
            }
            ImGui::SameLine();
            drawHelpMarker("How much the selection circle grows or shrinks per wheel tick.");

            ImGui::SeparatorText("Hotkeys");
            drawHotkeyCombo("Add object popup", editor.m_HotkeyAddMenu, "Used together with Shift. Default: Shift+A.");
            drawHotkeyCombo("Render image dialog", editor.m_HotkeyOpenRender, "Opens the Render Image popup and preview. Default: F12.");
            drawHotkeyCombo("Toggle side panels", editor.m_HotkeyToggleSidePanels, "Shows or hides the docked workflow sidebars. Default: N.");
            drawHotkeyCombo("Delete selection", editor.m_HotkeyDeleteSelection, "Deletes the current selection or active bond label. Default: Delete.");
            drawHotkeyCombo("Hide selection", editor.m_HotkeyHideSelection, "H hides, Alt+same key unhides all scene elements. Default: H.");
            drawHotkeyCombo("Arm box select", editor.m_HotkeyBoxSelect, "Arms rectangle selection in the viewport. Default: B.");
            drawHotkeyCombo("Arm circle select", editor.m_HotkeyCircleSelect, "Arms circle selection in the viewport. Default: C.");
            drawHotkeyCombo("Modal translate", editor.m_HotkeyTranslateModal, "Starts Blender-style modal translate. Default: G.");
            drawHotkeyCombo("Translate gizmo", editor.m_HotkeyTranslateGizmo, "Switches the transform gizmo to translate mode. Default: T.");
            drawHotkeyCombo("Rotate gizmo", editor.m_HotkeyRotateGizmo, "Switches the transform gizmo to rotate mode. Default: R.");
            drawHotkeyCombo("Scale gizmo", editor.m_HotkeyScaleGizmo, "Switches the transform gizmo to scale mode. Default: S.");

            if (ImGui::Button("Reset input defaults"))
            {
                resetInputDefaults();
            }
        }

        if (ImGui::CollapsingHeader("Windows & Diagnostics", defaultOpenFlags))
        {
            if (ImGui::Checkbox("Show log / errors", &editor.m_ShowLogPanel))
            {
                settingsChanged = true;
            }
            if (ImGui::Checkbox("Show stats", &editor.m_ShowStatsPanel))
            {
                settingsChanged = true;
            }
            if (ImGui::Checkbox("Show viewport info", &editor.m_ShowViewportInfoPanel))
            {
                settingsChanged = true;
            }
            if (ImGui::Checkbox("Show shortcuts", &editor.m_ShowShortcutReferencePanel))
            {
                settingsChanged = true;
            }
            if (ImGui::Checkbox("Show ImGui Demo", &editor.m_ShowDemoWindow))
            {
                settingsChanged = true;
            }

            ImGui::SeparatorText("Profiler");
#ifdef TRACY_ENABLE
            ImGui::TextUnformatted("Tracy instrumentation: enabled in this build");
#else
            ImGui::TextUnformatted("Tracy instrumentation: disabled in this build");
#endif
            ImGui::SameLine();
            drawHelpMarker("Use the Stats panel for quick frame timing in-app. Tracy captures are available only in builds compiled with TRACY_ENABLE.");

            if (ImGui::Button("Open diagnostics windows"))
            {
                editor.m_ShowLogPanel = true;
                editor.m_ShowStatsPanel = true;
                editor.m_ShowViewportInfoPanel = true;
                editor.m_ShowShortcutReferencePanel = true;
                settingsChanged = true;
            }
        }

        if (ImGui::CollapsingHeader("Persistence", defaultOpenFlags))
        {
            if (ImGui::Button("Save UI settings"))
            {
                editor.SaveSettings();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("(auto-save on change enabled)");

            if (ImGui::Button("Save current ImGui style"))
            {
                ImGuiLayer::SaveCurrentStyle();
            }
            ImGui::SameLine();
            drawHelpMarker("Use after tweaking colors, rounding or spacing in the ImGui Demo style editor.");

            if (ImGui::Button("Load saved ImGui style"))
            {
                if (ImGuiLayer::LoadSavedStyle())
                {
                    settingsChanged = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset saved ImGui style"))
            {
                ImGuiLayer::ResetSavedStyle();
                editor.ApplyTheme(editor.m_CurrentTheme);
                settingsChanged = true;
            }

            ImGui::SeparatorText("Defaults");
            if (ImGui::Button("Reset viewport performance"))
            {
                resetViewportPerformanceDefaults();
            }
            ImGui::SameLine();
            drawHelpMarker("Restores native viewport rendering scale and the default allowed render-scale range.");

            if (ImGui::Button("Reset dock layout"))
            {
                resetDockLayoutDefaults();
            }
            ImGui::SameLine();
            drawHelpMarker("Restores the default Hazel-like docking arrangement and panel visibility.");
        }

        ImGui::End();
    }

} // namespace ds
