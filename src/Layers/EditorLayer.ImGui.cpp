#define IMVIEWGUIZMO_IMPLEMENTATION
#include "Layers/EditorLayerPrivate.h"

#include <imgui_internal.h>
#include <GLFW/glfw3.h>

namespace ds
{
    void EditorLayer::ApplyDefaultDockLayout(unsigned int dockspaceId)
    {
        ImGuiViewport *mainViewport = ImGui::GetMainViewport();
        if (mainViewport == nullptr)
        {
            return;
        }

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, mainViewport->WorkSize);

        ImGuiID centerDockId = dockspaceId;
        const ImGuiID leftDockId = ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Left, 0.20f, nullptr, &centerDockId);
        ImGuiID rightDockId = ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Right, 0.27f, nullptr, &centerDockId);
        const ImGuiID bottomDockId = ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Down, 0.24f, nullptr, &centerDockId);
        const ImGuiID rightUtilityDockId = ImGui::DockBuilderSplitNode(rightDockId, ImGuiDir_Down, 0.42f, nullptr, &rightDockId);

        ImGui::DockBuilderDockWindow("Scene Outliner", leftDockId);
        ImGui::DockBuilderDockWindow("Viewport", centerDockId);
        ImGui::DockBuilderDockWindow("Properties", rightDockId);
        ImGui::DockBuilderDockWindow("Appearance", rightDockId);
        ImGui::DockBuilderDockWindow("Actions", rightDockId);
        ImGui::DockBuilderDockWindow("Element Catalog", rightDockId);
        ImGui::DockBuilderDockWindow("Volumetrics", rightDockId);
        ImGui::DockBuilderDockWindow("Periodic Table", rightDockId);
        ImGui::DockBuilderDockWindow("Viewport Settings", rightUtilityDockId);
        ImGui::DockBuilderDockWindow("Render Preview", rightUtilityDockId);
        ImGui::DockBuilderDockWindow("Settings", rightUtilityDockId);
        ImGui::DockBuilderDockWindow("Log / Errors", bottomDockId);
        ImGui::DockBuilderDockWindow("Stats", bottomDockId);
        ImGui::DockBuilderDockWindow("Viewport Info", bottomDockId);
        ImGui::DockBuilderDockWindow("Shortcuts", bottomDockId);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    void EditorLayer::OnImGuiRender()
    {
        DS_PROFILE_SCOPE_N("EditorLayer::OnImGuiRender");
        EnsureSceneDefaults();
        bool settingsChanged = false;
        const ImGuiIO &io = ImGui::GetIO();
        const auto isConfiguredKeyPressed = [](std::uint32_t key) -> bool
        {
            return key != 0u && ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false);
        };
        const auto keyDisplayName = [](std::uint32_t key) -> std::string
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
        bool openRecentProjectPopupRequested = false;
        auto createProjectViaDialog = [&]() -> bool
        {
            std::string projectFolder;
            if (!OpenNativeFolderDialog(projectFolder, "Create DefectsStudio project", m_LastProjectDialogPath))
            {
                return false;
            }

            return CreateProjectAt(projectFolder);
        };
        auto openProjectViaDialog = [&]() -> bool
        {
            std::string projectFolder;
            if (!OpenNativeFolderDialog(projectFolder, "Open DefectsStudio project", m_LastProjectDialogPath))
            {
                return false;
            }

            return OpenProjectAt(projectFolder);
        };
        const bool allowProjectShortcutKeys = !io.WantTextInput && !ImGui::IsAnyItemActive();
        if (allowProjectShortcutKeys)
        {
            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N, false))
            {
                settingsChanged |= createProjectViaDialog();
            }
            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false))
            {
                settingsChanged |= openProjectViaDialog();
            }
            if (io.KeyCtrl && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_O, false) && !m_RecentProjectPaths.empty())
            {
                openRecentProjectPopupRequested = true;
            }
        }

        {
            ImGuiStyle &style = ImGui::GetStyle();
            static bool spacingInit = false;
            static ImVec2 baseItemSpacing(8.0f, 4.0f);
            static ImVec2 baseItemInnerSpacing(4.0f, 4.0f);
            static ImVec2 baseFramePadding(4.0f, 3.0f);
            static float baseIndentSpacing = 21.0f;
            if (!spacingInit)
            {
                baseItemSpacing = style.ItemSpacing;
                baseItemInnerSpacing = style.ItemInnerSpacing;
                baseFramePadding = style.FramePadding;
                baseIndentSpacing = style.IndentSpacing;
                spacingInit = true;
            }

            const float s = glm::clamp(m_UiSpacingScale, 0.75f, 1.8f);
            style.ItemSpacing = ImVec2(baseItemSpacing.x * s, baseItemSpacing.y * s);
            style.ItemInnerSpacing = ImVec2(baseItemInnerSpacing.x * s, baseItemInnerSpacing.y * s);
            style.FramePadding = ImVec2(baseFramePadding.x * s, baseFramePadding.y * s);
            style.IndentSpacing = baseIndentSpacing * s;
        }

        static bool firstFrameLogged = false;
        if (!firstFrameLogged)
        {
            firstFrameLogged = true;
            LogInfo("OnImGuiRender: first GUI frame reached.");
        }

        static bool s_LastCanRenderTransformGizmo = false;
        static bool s_LastValidPivot = false;
        static bool s_LastGizmoOver = false;
        static bool s_LastGizmoUsing = false;
        static bool s_LastGizmoManipulated = false;
        static bool s_GizmoStateInitialized = false;
        static bool s_GizmoInteractionActive = false;
        static std::size_t s_LastSelectedCount = 0;

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        windowFlags |= ImGuiWindowFlags_NoTitleBar;
        windowFlags |= ImGuiWindowFlags_NoCollapse;
        windowFlags |= ImGuiWindowFlags_NoResize;
        windowFlags |= ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
        windowFlags |= ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("DockSpaceRoot", nullptr, windowFlags);
        ImGui::PopStyleVar(2);

        ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        if (m_ApplyDefaultDockLayoutOnNextFrame || m_RequestDockLayoutReset)
        {
            ApplyDefaultDockLayout(dockspaceId);
            m_ApplyDefaultDockLayoutOnNextFrame = false;
            m_RequestDockLayoutReset = false;
        }

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open POSCAR/CONTCAR", "Ctrl+O"))
                {
                    std::vector<std::string> selectedPaths;
                    if (OpenNativeFilesDialog(selectedPaths) && !selectedPaths.empty())
                    {
                        std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", selectedPaths.front().c_str());
                        for (const std::string &selectedPath : selectedPaths)
                        {
                            AppendStructureFromPathAsCollection(selectedPath);
                        }
                    }
                }

                if (ImGui::MenuItem("Open CHG/CHGCAR/PARCHG", nullptr))
                {
                    const std::filesystem::path initialDirectory = !GetProjectRootPath().empty()
                                                                      ? (GetProjectRootPath() / "project")
                                                                      : GetAppRootPath();
                    std::vector<std::string> selectedPaths;
                    if (OpenNativeVolumetricFilesDialog(selectedPaths, initialDirectory.string()) && !selectedPaths.empty())
                    {
                        for (const std::string &selectedPath : selectedPaths)
                        {
                            if (LoadVolumetricDatasetFromPath(selectedPath))
                            {
                                SaveProjectManifest();
                            }
                        }
                        settingsChanged = true;
                    }
                }

                if (ImGui::MenuItem("Export POSCAR/CONTCAR", "Ctrl+S", false, m_HasStructureLoaded))
                {
                    std::string selectedPath;
                    if (SaveNativeFileDialog(selectedPath))
                    {
                        std::snprintf(m_ExportPathBuffer.data(), m_ExportPathBuffer.size(), "%s", selectedPath.c_str());
                        const CoordinateMode exportMode = (m_ExportCoordinateModeIndex == 0)
                                                              ? CoordinateMode::Direct
                                                              : CoordinateMode::Cartesian;
                        ExportStructureToPath(selectedPath, exportMode, m_ExportPrecision);
                    }
                }
                if (ImGui::MenuItem("Export Active Collection to POSCAR", nullptr, false, m_HasStructureLoaded && m_ActiveCollectionIndex >= 0))
                {
                    std::string selectedPath;
                    if (SaveNativeFileDialog(selectedPath))
                    {
                        const CoordinateMode exportMode = (m_ExportCoordinateModeIndex == 0)
                                                              ? CoordinateMode::Direct
                                                              : CoordinateMode::Cartesian;
                        ExportCollectionToPath(m_ActiveCollectionIndex, selectedPath, exportMode, m_ExportPrecision);
                    }
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Create Project...", "Ctrl+Shift+N"))
                {
                    settingsChanged |= createProjectViaDialog();
                }

                if (ImGui::MenuItem("Open Project...", "Ctrl+Shift+O"))
                {
                    settingsChanged |= openProjectViaDialog();
                }

                if (ImGui::MenuItem("Open Recent Project...", "Ctrl+Alt+O", false, !m_RecentProjectPaths.empty()))
                {
                    openRecentProjectPopupRequested = true;
                }

                if (ImGui::BeginMenu("Recent Projects"))
                {
                    if (m_RecentProjectPaths.empty())
                    {
                        ImGui::MenuItem("(none)", nullptr, false, false);
                    }
                    else
                    {
                        for (const std::string &recentProjectPath : m_RecentProjectPaths)
                        {
                            const std::filesystem::path recentPath(recentProjectPath);
                            const std::string label = recentPath.filename().string().empty() ? recentProjectPath : recentPath.filename().string();
                            if (ImGui::MenuItem(label.c_str()))
                            {
                                settingsChanged |= OpenProjectAt(recentProjectPath);
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                            {
                                ImGui::SetTooltip("%s", recentProjectPath.c_str());
                            }
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Render Image", "F12"))
                {
                    SyncRenderAppearanceFromViewport();
                    m_ShowRenderImageDialog = true;
                }
                ImGui::Separator();
                ImGui::TextDisabled("Project: %s", m_ProjectName.c_str());
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                const bool canUndoSceneEdit = !m_UndoStack.empty();
                const bool canRedoSceneEdit = !m_RedoStack.empty();
                const bool canCopySelection =
                    !m_SelectedAtomIndices.empty() ||
                    (m_SelectedTransformEmptyIndex >= 0 && m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size()));
                const bool canPasteClipboard = HasClipboardPayload();
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndoSceneEdit))
                {
                    settingsChanged |= UndoSceneEdit();
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y / Ctrl+Shift+Z", false, canRedoSceneEdit))
                {
                    settingsChanged |= RedoSceneEdit();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Copy", "Ctrl+C", false, canCopySelection))
                {
                    CopyCurrentSelectionToClipboard();
                }
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPasteClipboard))
                {
                    settingsChanged |= PasteClipboard();
                }
                if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, canCopySelection))
                {
                    settingsChanged |= DuplicateCurrentSelection();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Select All", "Ctrl+A", false, m_HasStructureLoaded))
                {
                    settingsChanged |= SelectAllVisibleByCurrentFilter();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Log / Errors", nullptr, &m_ShowLogPanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Stats", nullptr, &m_ShowStatsPanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Viewport Info", nullptr, &m_ShowViewportInfoPanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Shortcuts", nullptr, &m_ShowShortcutReferencePanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Element Catalog", nullptr, &m_ShowElementCatalogPanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Volumetrics", nullptr, &m_ShowVolumetricsPanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Periodic Table", nullptr, &m_ShowPeriodicTablePanel))
                {
                    if (m_ShowPeriodicTablePanel)
                    {
                        m_RequestPeriodicTableFocus = true;
                    }
                    else
                    {
                        m_PeriodicTableOpen = false;
                    }
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Scene Outliner", nullptr, &m_ShowSceneOutlinerPanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Properties", nullptr, &m_ShowObjectPropertiesPanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Actions", nullptr, &m_ShowActionsPanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Appearance", nullptr, &m_ShowAppearancePanel))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Viewport Settings", nullptr, &m_ViewportSettingsOpen))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Render Preview", nullptr, &m_ShowRenderPreviewWindow))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Settings", nullptr, &m_ShowSettingsPanel))
                {
                    settingsChanged = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Reset Dock Layout"))
                {
                    m_ShowLogPanel = true;
                    m_ShowStatsPanel = true;
                    m_ShowViewportInfoPanel = true;
                    m_ShowShortcutReferencePanel = false;
                    m_ShowSceneOutlinerPanel = true;
                    m_ShowObjectPropertiesPanel = true;
                    m_ShowActionsPanel = true;
                    m_ShowAppearancePanel = true;
                    m_ShowVolumetricsPanel = true;
                    m_ShowPeriodicTablePanel = false;
                    m_PeriodicTableOpen = false;
                    m_ViewportSettingsOpen = true;
                    m_ShowRenderPreviewWindow = true;
                    m_ShowSettingsPanel = true;
                    m_RequestDockLayoutReset = true;
                    settingsChanged = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        if (openRecentProjectPopupRequested)
        {
            ImGui::OpenPopup("Open Recent Project");
        }

        ImGui::SetNextWindowSize(ImVec2(520.0f, 320.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Open Recent Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (m_RecentProjectPaths.empty())
            {
                ImGui::TextDisabled("No recent projects saved yet.");
            }
            else
            {
                ImGui::TextDisabled("Pick a recent project root:");
                ImGui::Separator();
                for (std::size_t recentIndex = 0; recentIndex < m_RecentProjectPaths.size(); ++recentIndex)
                {
                    const std::string &recentProjectPath = m_RecentProjectPaths[recentIndex];
                    const std::filesystem::path recentPath(recentProjectPath);
                    const std::string visibleLabel = recentPath.filename().string().empty() ? recentProjectPath : recentPath.filename().string();
                    const std::string popupLabel = visibleLabel + "##RecentProject_" + std::to_string(recentIndex);
                    if (ImGui::Selectable(popupLabel.c_str(), false, ImGuiSelectableFlags_SpanAvailWidth))
                    {
                        settingsChanged |= OpenProjectAt(recentProjectPath);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    {
                        ImGui::SetTooltip("%s", recentProjectPath.c_str());
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Begin("Viewport");
        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();
        m_BlockSelectionThisFrame = false;
        m_GizmoConsumedMouseThisFrame = false;

        auto beginPendingTransformUndo = [&](const char *label)
        {
            m_PendingTransformUndoSnapshot = CaptureSceneSnapshot();
            m_PendingTransformUndoLabel = (label != nullptr) ? label : "Transform selection";
            m_PendingTransformUndoValid = true;
            m_PendingTransformUndoDirty = false;
        };

        auto cancelPendingTransformUndo = [&]()
        {
            m_PendingTransformUndoValid = false;
            m_PendingTransformUndoDirty = false;
            m_PendingTransformUndoLabel.clear();
        };

        auto commitPendingTransformUndo = [&]()
        {
            if (!m_PendingTransformUndoValid)
            {
                return;
            }

            if (m_PendingTransformUndoDirty)
            {
                PushUndoSnapshot(m_PendingTransformUndoLabel.empty() ? "Transform selection" : m_PendingTransformUndoLabel, m_PendingTransformUndoSnapshot);
            }
            cancelPendingTransformUndo();
        };

        auto beginTranslateMode = [&]()
        {
            if (m_RotateModeActive)
            {
                AppendSelectionDebugLog("Translate mode start denied: rotate mode is active");
                return;
            }

            const bool hasSelectedEmpty =
                (m_SelectedTransformEmptyIndex >= 0 &&
                 m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size()) &&
                 m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].visible &&
                 m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].selectable &&
                 IsCollectionVisible(m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].collectionIndex) &&
                 IsCollectionSelectable(m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].collectionIndex));
            const bool hasSelectedSpecial = (m_SelectedSpecialNode != SpecialNodeSelection::None);

            if (!m_Camera || (m_SelectedAtomIndices.empty() && !hasSelectedEmpty && !hasSelectedSpecial))
            {
                AppendSelectionDebugLog("Translate mode start denied: missing camera/structure/atom-or-empty-selection");
                return;
            }

            m_TranslateIndices.clear();
            m_TranslateInitialCartesian.clear();
            m_TranslateEmptyIndex = -1;
            m_TranslateEmptyInitialPosition = glm::vec3(0.0f);
            m_TranslateCurrentOffset = glm::vec3(0.0f);
            m_TranslateConstraintAxis = -1;
            m_TranslatePlaneLockAxis = -1;
            m_TranslateSpecialNode = 0;
            m_TranslateTypedDistanceBuffer.clear();
            m_TranslateTypedDistanceActive = false;

            for (std::size_t atomIndex : m_SelectedAtomIndices)
            {
                if (atomIndex >= m_WorkingStructure.atoms.size())
                {
                    continue;
                }

                m_TranslateIndices.push_back(atomIndex);
                m_TranslateInitialCartesian.push_back(GetAtomCartesianPosition(atomIndex));
            }

            if (hasSelectedEmpty)
            {
                m_TranslateEmptyIndex = m_SelectedTransformEmptyIndex;
                m_TranslateEmptyInitialPosition = m_TransformEmpties[static_cast<std::size_t>(m_TranslateEmptyIndex)].position;
            }

            if (hasSelectedSpecial)
            {
                if (m_SelectedSpecialNode == SpecialNodeSelection::Light)
                {
                    m_TranslateSpecialNode = 1;
                    m_TranslateEmptyInitialPosition = m_LightPosition;
                }
            }

            if (m_TranslateIndices.empty() && m_TranslateEmptyIndex < 0 && m_TranslateSpecialNode == 0)
            {
                AppendSelectionDebugLog("Translate mode start denied: no valid selected atoms/empty");
                return;
            }

            beginPendingTransformUndo("Translate selection");
            m_TranslateModeActive = true;
            m_InteractionMode = InteractionMode::Translate;
            m_GizmoOperationIndex = 0;
            m_TranslateLastMousePos = glm::vec2(io.MousePos.x, io.MousePos.y);
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Translate mode active: move mouse, X/Y/Z constrain, Shift+X/Y/Z plane lock, Ctrl snap.";
            AppendSelectionDebugLog("Translate mode started (G)");
        };

        auto beginRotateMode = [&]()
        {
            if (m_TranslateModeActive)
            {
                AppendSelectionDebugLog("Rotate mode start denied: translate mode is active");
                return;
            }

            if (!m_Camera || !m_HasStructureLoaded || m_SelectedAtomIndices.empty())
            {
                AppendSelectionDebugLog("Rotate mode start denied: missing camera/structure/selection");
                return;
            }

            m_RotateIndices.clear();
            m_RotateInitialCartesian.clear();
            m_RotateConstraintAxis = -1;
            m_RotateCurrentAngle = 0.0f;

            for (std::size_t atomIndex : m_SelectedAtomIndices)
            {
                if (atomIndex >= m_WorkingStructure.atoms.size())
                {
                    continue;
                }

                m_RotateIndices.push_back(atomIndex);
                m_RotateInitialCartesian.push_back(GetAtomCartesianPosition(atomIndex));
            }

            if (m_RotateIndices.empty())
            {
                AppendSelectionDebugLog("Rotate mode start denied: no valid selected indices");
                return;
            }

            beginPendingTransformUndo("Rotate selection");
            m_RotateModeActive = true;
            m_InteractionMode = InteractionMode::Rotate;
            m_GizmoOperationIndex = 1;
            m_RotateLastMousePos = glm::vec2(io.MousePos.x, io.MousePos.y);
            m_RotatePivot = m_CursorPosition;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Rotate mode active: move mouse, X/Y/Z axis, Ctrl snap to step.";
            AppendSelectionDebugLog("Rotate mode started (R)");
        };

        auto cancelTranslateMode = [&]()
        {
            if (!m_TranslateModeActive)
            {
                return;
            }

            const std::size_t count = std::min(m_TranslateIndices.size(), m_TranslateInitialCartesian.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                SetAtomCartesianPosition(m_TranslateIndices[i], m_TranslateInitialCartesian[i]);
            }

            if (m_TranslateEmptyIndex >= 0 && m_TranslateEmptyIndex < static_cast<int>(m_TransformEmpties.size()))
            {
                m_TransformEmpties[static_cast<std::size_t>(m_TranslateEmptyIndex)].position = m_TranslateEmptyInitialPosition;
            }
            if (m_TranslateSpecialNode == 1)
            {
                m_LightPosition = m_TranslateEmptyInitialPosition;
            }

            m_TranslateModeActive = false;
            m_InteractionMode = InteractionMode::Select;
            m_TranslateIndices.clear();
            m_TranslateInitialCartesian.clear();
            m_TranslateEmptyIndex = -1;
            m_TranslateEmptyInitialPosition = glm::vec3(0.0f);
            m_TranslateCurrentOffset = glm::vec3(0.0f);
            m_TranslateConstraintAxis = -1;
            m_TranslatePlaneLockAxis = -1;
            m_TranslateSpecialNode = 0;
            m_TranslateTypedDistanceBuffer.clear();
            m_TranslateTypedDistanceActive = false;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Translate mode canceled.";
            cancelPendingTransformUndo();
            AppendSelectionDebugLog("Translate mode canceled (Esc/RMB)");
        };

        auto commitTranslateMode = [&]()
        {
            if (!m_TranslateModeActive)
            {
                return;
            }

            bool hasAppliedChange = glm::length2(m_TranslateCurrentOffset) > 1e-10f;
            if (!hasAppliedChange && m_TranslateEmptyIndex >= 0 && m_TranslateEmptyIndex < static_cast<int>(m_TransformEmpties.size()))
            {
                hasAppliedChange = glm::length2(m_TransformEmpties[static_cast<std::size_t>(m_TranslateEmptyIndex)].position - m_TranslateEmptyInitialPosition) > 1e-10f;
            }
            if (!hasAppliedChange && m_TranslateSpecialNode == 1)
            {
                hasAppliedChange = glm::length2(m_LightPosition - m_TranslateEmptyInitialPosition) > 1e-10f;
            }
            if (hasAppliedChange)
            {
                m_PendingTransformUndoDirty = true;
            }

            m_TranslateModeActive = false;
            m_InteractionMode = InteractionMode::Select;
            m_TranslateIndices.clear();
            m_TranslateInitialCartesian.clear();
            m_TranslateEmptyIndex = -1;
            m_TranslateEmptyInitialPosition = glm::vec3(0.0f);
            m_TranslateCurrentOffset = glm::vec3(0.0f);
            m_TranslateConstraintAxis = -1;
            m_TranslatePlaneLockAxis = -1;
            m_TranslateSpecialNode = 0;
            m_TranslateTypedDistanceBuffer.clear();
            m_TranslateTypedDistanceActive = false;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Translate mode applied.";
            commitPendingTransformUndo();
            AppendSelectionDebugLog("Translate mode applied (LMB/Enter)");
        };

        auto cancelRotateMode = [&]()
        {
            if (!m_RotateModeActive)
            {
                return;
            }

            const std::size_t count = std::min(m_RotateIndices.size(), m_RotateInitialCartesian.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                SetAtomCartesianPosition(m_RotateIndices[i], m_RotateInitialCartesian[i]);
            }

            m_RotateModeActive = false;
            m_InteractionMode = InteractionMode::Select;
            m_RotateConstraintAxis = -1;
            m_RotateIndices.clear();
            m_RotateInitialCartesian.clear();
            m_RotateCurrentAngle = 0.0f;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Rotate mode canceled.";
            cancelPendingTransformUndo();
            AppendSelectionDebugLog("Rotate mode canceled (Esc/RMB)");
        };

        auto commitRotateMode = [&]()
        {
            if (!m_RotateModeActive)
            {
                return;
            }

            if (std::abs(m_RotateCurrentAngle) > 1e-6f)
            {
                m_PendingTransformUndoDirty = true;
            }

            m_RotateModeActive = false;
            m_InteractionMode = InteractionMode::Select;
            m_RotateConstraintAxis = -1;
            m_RotateIndices.clear();
            m_RotateInitialCartesian.clear();
            m_RotateCurrentAngle = 0.0f;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Rotate mode applied around 3D cursor.";
            commitPendingTransformUndo();
            AppendSelectionDebugLog("Rotate mode applied (LMB/Enter/R)");
        };

        auto deleteCurrentSelection = [&]()
        {
            const bool hasDeletableSelection =
                !m_SelectedBondKeys.empty() ||
                (!m_SelectedAtomIndices.empty() && m_HasStructureLoaded) ||
                (m_SelectedTransformEmptyIndex >= 0 && m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size())) ||
                m_ActiveGroupIndex >= 0;
            if (hasDeletableSelection)
            {
                PushUndoSnapshot("Delete selection");
            }

            if (!m_SelectedBondKeys.empty())
            {
                const bool hasAtomLikeSelection =
                    !m_SelectedAtomIndices.empty() ||
                    (m_SelectedTransformEmptyIndex >= 0 && m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size())) ||
                    m_SelectedSpecialNode != SpecialNodeSelection::None;

                std::size_t affectedCount = 0;
                const bool deleteLabelsOnly = m_BondLabelDeleteOnlyMode || (m_SelectionFilter == SelectionFilter::BondLabelsOnly);
                if (deleteLabelsOnly)
                {
                    for (std::uint64_t key : m_SelectedBondKeys)
                    {
                        BondLabelState &state = m_BondLabelStates[key];
                        if (!state.deleted)
                        {
                            ++affectedCount;
                        }
                        state.deleted = true;
                        state.hidden = true;
                    }
                    if (affectedCount > 0)
                    {
                        settingsChanged = true;
                        m_LastStructureOperationFailed = false;
                        m_LastStructureMessage = "Deleted " + std::to_string(affectedCount) + " selected bond label(s).";
                        LogInfo(m_LastStructureMessage);
                    }
                }
                else
                {
                    for (std::uint64_t key : m_SelectedBondKeys)
                    {
                        const auto [it, inserted] = m_DeletedBondKeys.insert(key);
                        (void)it;
                        if (inserted)
                        {
                            ++affectedCount;
                        }

                        auto stateIt = m_BondLabelStates.find(key);
                        if (stateIt != m_BondLabelStates.end())
                        {
                            stateIt->second.hidden = true;
                        }
                    }
                    if (affectedCount > 0)
                    {
                        settingsChanged = true;
                        m_LastStructureOperationFailed = false;
                        m_LastStructureMessage = "Deleted " + std::to_string(affectedCount) + " selected bond(s).";
                        LogInfo(m_LastStructureMessage);
                    }
                }

                m_SelectedBondKeys.clear();
                m_SelectedBondLabelKey = 0;
                if (!hasAtomLikeSelection)
                {
                    return;
                }
            }

            if (m_SelectedTransformEmptyIndex >= 0 && m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size()))
            {
                DeleteTransformEmptyAtIndex(m_SelectedTransformEmptyIndex);
                settingsChanged = true;
                m_LastStructureOperationFailed = false;
                m_LastStructureMessage = "Deleted selected Empty.";
                LogInfo(m_LastStructureMessage);
                return;
            }

            auto deleteAtomsByIndices = [&](const std::vector<std::size_t> &atomIndices) -> std::size_t
            {
                if (!m_HasStructureLoaded)
                {
                    return 0;
                }

                std::vector<std::size_t> uniqueIndices = atomIndices;
                std::sort(uniqueIndices.begin(), uniqueIndices.end());
                uniqueIndices.erase(std::unique(uniqueIndices.begin(), uniqueIndices.end()), uniqueIndices.end());
                uniqueIndices.erase(
                    std::remove_if(uniqueIndices.begin(), uniqueIndices.end(), [&](std::size_t atomIndex)
                                   { return atomIndex >= m_WorkingStructure.atoms.size(); }),
                    uniqueIndices.end());

                if (uniqueIndices.empty())
                {
                    return 0;
                }

                for (auto it = uniqueIndices.rbegin(); it != uniqueIndices.rend(); ++it)
                {
                    const std::size_t atomIndex = *it;
                    m_WorkingStructure.atoms.erase(m_WorkingStructure.atoms.begin() + static_cast<std::ptrdiff_t>(atomIndex));
                    m_HiddenAtomIndices.erase(atomIndex);
                    if (atomIndex < m_AtomNodeIds.size())
                    {
                        m_AtomColorOverrides.erase(m_AtomNodeIds[atomIndex]);
                        m_AtomNodeIds.erase(m_AtomNodeIds.begin() + static_cast<std::ptrdiff_t>(atomIndex));
                    }
                    if (atomIndex < m_AtomCollectionIndices.size())
                    {
                        m_AtomCollectionIndices.erase(m_AtomCollectionIndices.begin() + static_cast<std::ptrdiff_t>(atomIndex));
                    }
                }

                if (!m_HiddenAtomIndices.empty())
                {
                    std::unordered_set<std::size_t> remappedHidden;
                    remappedHidden.reserve(m_HiddenAtomIndices.size());
                    for (std::size_t hiddenIndex : m_HiddenAtomIndices)
                    {
                        std::size_t shift = 0;
                        for (std::size_t removedIndex : uniqueIndices)
                        {
                            if (removedIndex < hiddenIndex)
                            {
                                ++shift;
                            }
                        }
                        if (shift <= hiddenIndex)
                        {
                            remappedHidden.insert(hiddenIndex - shift);
                        }
                    }
                    m_HiddenAtomIndices = std::move(remappedHidden);
                }

                if (!m_AngleLabelStates.empty())
                {
                    auto remapIndexAfterDelete = [&](std::size_t oldIndex, std::size_t &outIndex) -> bool
                    {
                        if (std::binary_search(uniqueIndices.begin(), uniqueIndices.end(), oldIndex))
                        {
                            return false;
                        }

                        const std::size_t shift = static_cast<std::size_t>(
                            std::lower_bound(uniqueIndices.begin(), uniqueIndices.end(), oldIndex) - uniqueIndices.begin());
                        outIndex = oldIndex - shift;
                        return true;
                    };

                    std::unordered_map<std::string, AngleLabelState> remappedAngleLabels;
                    remappedAngleLabels.reserve(m_AngleLabelStates.size());

                    for (const auto &[key, state] : m_AngleLabelStates)
                    {
                        (void)key;
                        std::size_t mappedA = 0;
                        std::size_t mappedB = 0;
                        std::size_t mappedC = 0;
                        if (!remapIndexAfterDelete(state.atomA, mappedA) ||
                            !remapIndexAfterDelete(state.atomB, mappedB) ||
                            !remapIndexAfterDelete(state.atomC, mappedC))
                        {
                            continue;
                        }
                        if (mappedA == mappedB || mappedB == mappedC || mappedA == mappedC)
                        {
                            continue;
                        }

                        AngleLabelState remappedState;
                        remappedState.atomA = mappedA;
                        remappedState.atomB = mappedB;
                        remappedState.atomC = mappedC;
                        remappedAngleLabels[MakeAngleTripletKey(mappedA, mappedB, mappedC)] = remappedState;
                    }

                    m_AngleLabelStates = std::move(remappedAngleLabels);
                }

                EnsureAtomCollectionAssignments();

                m_WorkingStructure.RebuildSpeciesFromAtoms();
                m_AutoBondsDirty = true;
                m_SelectedAtomIndices.clear();
                m_SelectedTransformEmptyIndex = -1;
                m_SelectedBondKeys.clear();
                m_SelectedBondLabelKey = 0;

                for (int groupIndex = 0; groupIndex < static_cast<int>(m_ObjectGroups.size()); ++groupIndex)
                {
                    SceneGroupingBackend::SanitizeGroup(*this, groupIndex);
                }

                settingsChanged = true;
                return uniqueIndices.size();
            };

            if (!m_SelectedAtomIndices.empty() && m_HasStructureLoaded)
            {
                const std::size_t removedAtomCount = deleteAtomsByIndices(m_SelectedAtomIndices);
                if (removedAtomCount == 0)
                {
                    return;
                }
                m_LastStructureOperationFailed = false;
                m_LastStructureMessage = "Deleted " + std::to_string(removedAtomCount) + " selected atom(s).";
                LogInfo(m_LastStructureMessage);
                return;
            }

            if (m_ActiveGroupIndex >= 0)
            {
                if (SceneGroupingBackend::DeleteGroup(*this, m_ActiveGroupIndex))
                {
                    settingsChanged = true;
                    m_LastStructureOperationFailed = false;
                    m_LastStructureMessage = "Deleted active group.";
                    LogInfo(m_LastStructureMessage);
                }
            }
        };

        auto hideCurrentSelection = [&]()
        {
            const bool hasHideableSelection =
                !m_SelectedAtomIndices.empty() ||
                !m_SelectedBondKeys.empty() ||
                m_SelectedBondLabelKey != 0;
            if (hasHideableSelection)
            {
                PushUndoSnapshot("Hide selection");
            }

            std::size_t hiddenAtoms = 0;
            std::size_t hiddenBonds = 0;
            std::size_t hiddenLabels = 0;

            for (std::size_t atomIndex : m_SelectedAtomIndices)
            {
                if (atomIndex >= m_WorkingStructure.atoms.size())
                {
                    continue;
                }
                const auto [it, inserted] = m_HiddenAtomIndices.insert(atomIndex);
                (void)it;
                if (inserted)
                {
                    ++hiddenAtoms;
                }
            }

            for (std::uint64_t key : m_SelectedBondKeys)
            {
                const auto [it, inserted] = m_HiddenBondKeys.insert(key);
                (void)it;
                if (inserted)
                {
                    ++hiddenBonds;
                }

                auto labelIt = m_BondLabelStates.find(key);
                if (labelIt != m_BondLabelStates.end() && !labelIt->second.hidden)
                {
                    labelIt->second.hidden = true;
                    ++hiddenLabels;
                }
            }

            if (m_SelectedBondLabelKey != 0)
            {
                auto labelIt = m_BondLabelStates.find(m_SelectedBondLabelKey);
                if (labelIt != m_BondLabelStates.end() && !labelIt->second.hidden)
                {
                    labelIt->second.hidden = true;
                    ++hiddenLabels;
                }
            }

            m_SelectedAtomIndices.clear();
            m_SelectedBondKeys.clear();
            m_SelectedBondLabelKey = 0;
            m_SelectedTransformEmptyIndex = -1;
            m_SelectedSpecialNode = SpecialNodeSelection::None;

            if (hiddenAtoms > 0 || hiddenBonds > 0 || hiddenLabels > 0)
            {
                m_LastStructureOperationFailed = false;
                m_LastStructureMessage = "Hidden atoms=" + std::to_string(hiddenAtoms) +
                                         ", bonds=" + std::to_string(hiddenBonds) +
                                         ", labels=" + std::to_string(hiddenLabels) + ".";
                settingsChanged = true;
            }
        };

        auto unhideAllSceneElements = [&]()
        {
            const bool hasHiddenSceneElements = !m_HiddenAtomIndices.empty() || !m_HiddenBondKeys.empty();
            bool hasHiddenLabels = false;
            if (!hasHiddenSceneElements)
            {
                for (const auto &[key, state] : m_BondLabelStates)
                {
                    (void)key;
                    if (!state.deleted && state.hidden)
                    {
                        hasHiddenLabels = true;
                        break;
                    }
                }
            }

            if (hasHiddenSceneElements || hasHiddenLabels)
            {
                PushUndoSnapshot("Unhide all");
            }

            m_HiddenAtomIndices.clear();
            m_HiddenBondKeys.clear();
            for (auto &[key, state] : m_BondLabelStates)
            {
                (void)key;
                if (!state.deleted)
                {
                    state.hidden = false;
                }
            }

            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Unhide All: atoms, bonds and labels restored.";
            settingsChanged = true;
        };

        auto applyPieSelection = [&](int slice)
        {
            cancelTranslateMode();
            cancelRotateMode();

            if (slice == 0)
            {
                m_InteractionMode = InteractionMode::Select;
                m_LastStructureMessage = "Mode: Select";
            }
            else if (slice == 1)
            {
                m_InteractionMode = InteractionMode::Navigate;
                m_LastStructureMessage = "Mode: Navigate";
            }
            else if (slice == 2)
            {
                m_InteractionMode = InteractionMode::ViewSet;
                m_LastStructureMessage = "Mode: ViewSet";
            }
            else if (slice == 3)
            {
                m_GizmoEnabled = true;
                m_GizmoOperationIndex = 0;
                m_InteractionMode = InteractionMode::Select;
                m_LastStructureMessage = "Transform mode: Translate gizmo (T).";
            }
            else if (slice == 4)
            {
                m_GizmoEnabled = true;
                m_GizmoOperationIndex = 1;
                m_InteractionMode = InteractionMode::Select;
                m_LastStructureMessage = "Transform mode: Rotate gizmo (R).";
            }
            else if (slice == 5)
            {
                m_GizmoEnabled = true;
                m_GizmoOperationIndex = 2;
                m_InteractionMode = InteractionMode::Select;
                m_LastStructureMessage = "Transform mode: Scale gizmo (S).";
            }
        };

        const bool pieHotkeyHeld = (m_ViewportFocused && !io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_Tab));
        if (pieHotkeyHeld && !m_ModePieActive)
        {
            m_ModePieActive = true;
            m_ModePiePopupPos = glm::vec2(io.MousePos.x, io.MousePos.y);
            m_ModePieHoveredSlice = -1;
        }

        if (m_ModePieActive)
        {
            m_BlockSelectionThisFrame = true;

            static const char *kPieLabels[] = {
                "Select",
                "Navigate",
                "ViewSet",
                "Move",
                "Rotate",
                "Scale"};

            const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
            const glm::vec2 center = m_ModePiePopupPos;
            const glm::vec2 delta = mousePos - center;
            const float distance = glm::length(delta);
            const float twoPi = glm::two_pi<float>();
            const float step = twoPi / 6.0f;

            m_ModePieHoveredSlice = -1;
            if (distance > 36.0f)
            {
                float angle = std::atan2(delta.y, delta.x);
                if (angle < 0.0f)
                {
                    angle += twoPi;
                }
                m_ModePieHoveredSlice = static_cast<int>(angle / step);
                if (m_ModePieHoveredSlice < 0)
                {
                    m_ModePieHoveredSlice = 0;
                }
                if (m_ModePieHoveredSlice > 5)
                {
                    m_ModePieHoveredSlice = 5;
                }
            }

            ImDrawList *drawList = ImGui::GetForegroundDrawList();
            const ImVec2 drawCenter(center.x, center.y);
            constexpr float innerRadius = 40.0f;
            constexpr float outerRadius = 126.0f;
            for (int i = 0; i < 6; ++i)
            {
                const float startAngle = step * static_cast<float>(i) - step * 0.5f;
                const float endAngle = startAngle + step;
                const bool hovered = (m_ModePieHoveredSlice == i);
                const ImU32 fillColor = hovered ? IM_COL32(90, 170, 250, 230) : IM_COL32(36, 43, 54, 220);
                const ImU32 borderColor = hovered ? IM_COL32(170, 220, 255, 255) : IM_COL32(110, 130, 150, 230);

                drawList->PathClear();
                drawList->PathArcTo(drawCenter, outerRadius, startAngle, endAngle, 20);
                drawList->PathArcTo(drawCenter, innerRadius, endAngle, startAngle, 12);
                drawList->PathFillConvex(fillColor);
                drawList->PathClear();
                drawList->PathArcTo(drawCenter, outerRadius, startAngle, endAngle, 20);
                drawList->PathArcTo(drawCenter, innerRadius, endAngle, startAngle, 12);
                drawList->PathStroke(borderColor, ImDrawFlags_Closed, 1.5f);

                const float midAngle = (startAngle + endAngle) * 0.5f;
                const ImVec2 labelPos(
                    drawCenter.x + std::cos(midAngle) * ((innerRadius + outerRadius) * 0.5f),
                    drawCenter.y + std::sin(midAngle) * ((innerRadius + outerRadius) * 0.5f));
                const ImVec2 textSize = ImGui::CalcTextSize(kPieLabels[i]);
                drawList->AddText(ImVec2(labelPos.x - textSize.x * 0.5f, labelPos.y - textSize.y * 0.5f), IM_COL32(240, 245, 255, 255), kPieLabels[i]);
            }

            drawList->AddCircleFilled(drawCenter, innerRadius - 2.0f, IM_COL32(20, 24, 30, 230), 32);
            drawList->AddCircle(drawCenter, innerRadius - 2.0f, IM_COL32(140, 160, 180, 255), 32, 1.5f);
            const ImVec2 tabTextSize = ImGui::CalcTextSize("TAB");
            drawList->AddText(ImVec2(drawCenter.x - tabTextSize.x * 0.5f, drawCenter.y - tabTextSize.y * 0.5f), IM_COL32(210, 220, 235, 255), "TAB");

            if (!pieHotkeyHeld)
            {
                if (m_ModePieHoveredSlice >= 0)
                {
                    applyPieSelection(m_ModePieHoveredSlice);
                }
                m_ModePieActive = false;
                m_ModePieHoveredSlice = -1;
            }
        }

        if (m_ViewportFocused && !io.WantTextInput && io.KeyShift && isConfiguredKeyPressed(m_HotkeyAddMenu))
        {
            m_AddMenuPopupPos = glm::vec2(io.MousePos.x, io.MousePos.y);
            ImGui::OpenPopup("AddSceneObjectPopup");
            m_BlockSelectionThisFrame = true;
        }

        if (!io.WantTextInput && io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            if (io.KeyShift)
            {
                settingsChanged |= RedoSceneEdit();
            }
            else
            {
                settingsChanged |= UndoSceneEdit();
            }
            m_BlockSelectionThisFrame = settingsChanged;
        }
        else if (!io.WantTextInput && io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Y, false))
        {
            settingsChanged |= RedoSceneEdit();
            m_BlockSelectionThisFrame = settingsChanged;
        }

        if (!io.WantTextInput && io.KeyCtrl && !io.KeyAlt && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_C, false))
        {
            const bool copied = CopyCurrentSelectionToClipboard();
            m_BlockSelectionThisFrame = copied;
        }
        else if (!io.WantTextInput && io.KeyCtrl && !io.KeyAlt && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_V, false))
        {
            settingsChanged |= PasteClipboard();
            m_BlockSelectionThisFrame = settingsChanged;
        }
        else if (!io.WantTextInput && io.KeyCtrl && !io.KeyAlt && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_D, false))
        {
            settingsChanged |= DuplicateCurrentSelection();
            m_BlockSelectionThisFrame = settingsChanged;
        }
        else if (!io.WantTextInput && io.KeyCtrl && !io.KeyAlt && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            settingsChanged |= SelectAllVisibleByCurrentFilter();
            m_BlockSelectionThisFrame = settingsChanged;
        }

        if (m_ViewportFocused &&
            !io.WantTextInput &&
            !ImGui::IsAnyItemActive() &&
            !m_TranslateModeActive &&
            !m_RotateModeActive &&
            (ImGui::IsKeyPressed(ImGuiKey_Period, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)))
        {
            float focusDistanceMultiplier = 1.0f;
            bool persistFocusAdjustment = false;
            if (io.KeyShift)
            {
                focusDistanceMultiplier = 0.80f;
                persistFocusAdjustment = true;
            }
            else if (io.KeyAlt)
            {
                focusDistanceMultiplier = 1.25f;
                persistFocusAdjustment = true;
            }

            settingsChanged |= FocusCameraOnCursor(focusDistanceMultiplier, persistFocusAdjustment);
            m_BlockSelectionThisFrame = settingsChanged;
        }

        if (m_ViewportFocused && !io.WantTextInput && isConfiguredKeyPressed(m_HotkeyDeleteSelection))
        {
            const auto selectedLabelIt = m_BondLabelStates.find(m_SelectedBondLabelKey);
            const bool hasAtomLikeSelection =
                !m_SelectedAtomIndices.empty() ||
                m_SelectedTransformEmptyIndex >= 0 ||
                m_SelectedSpecialNode != SpecialNodeSelection::None;

            if (!hasAtomLikeSelection && m_SelectedBondKeys.empty() && selectedLabelIt != m_BondLabelStates.end() && !selectedLabelIt->second.deleted)
            {
                m_SelectedBondKeys.insert(m_SelectedBondLabelKey);
            }

            deleteCurrentSelection();
            m_BlockSelectionThisFrame = true;
        }

        if (m_ViewportFocused && !io.WantTextInput && isConfiguredKeyPressed(m_HotkeyHideSelection))
        {
            if (io.KeyAlt)
            {
                unhideAllSceneElements();
                AppendSelectionDebugLog("Hotkey: Alt+H -> Unhide All");
            }
            else
            {
                hideCurrentSelection();
                AppendSelectionDebugLog("Hotkey: H -> Hide selection");
            }
            m_BlockSelectionThisFrame = true;
        }

        if (m_ViewportFocused && !io.WantTextInput && isConfiguredKeyPressed(m_HotkeyToggleSidePanels))
        {
            const bool anySidePanelVisible = m_ShowActionsPanel || m_ShowAppearancePanel;
            m_ShowActionsPanel = !anySidePanelVisible;
            m_ShowAppearancePanel = !anySidePanelVisible;
            settingsChanged = true;
        }

        if (!io.WantTextInput && isConfiguredKeyPressed(m_HotkeyOpenRender))
        {
            SyncRenderAppearanceFromViewport();
            m_ShowRenderImageDialog = true;
            m_ShowRenderPreviewWindow = true;
        }

        const bool translateModalHotkey = isConfiguredKeyPressed(m_HotkeyTranslateModal);
        const bool translateGizmoHotkey = (m_InteractionMode != InteractionMode::ViewSet && isConfiguredKeyPressed(m_HotkeyTranslateGizmo));

        if (m_ViewportFocused && !io.WantTextInput && translateModalHotkey)
        {
            cancelTranslateMode();
            cancelRotateMode();
            beginTranslateMode();
            m_LastStructureOperationFailed = false;
            if (m_SelectedAtomIndices.empty() && m_SelectedTransformEmptyIndex < 0 && m_SelectedSpecialNode == SpecialNodeSelection::None)
            {
                m_LastStructureMessage = "Translate mode unavailable: select atoms, empty, or Light first.";
            }
            AppendSelectionDebugLog("Hotkey: G -> modal Translate");
        }

        if (m_ViewportFocused && !io.WantTextInput && translateGizmoHotkey)
        {
            cancelTranslateMode();
            cancelRotateMode();
            m_InteractionMode = InteractionMode::Select;
            m_GizmoEnabled = true;
            m_GizmoOperationIndex = 0;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Transform mode: Translate gizmo (T).";
            if (!m_HasStructureLoaded || (m_SelectedAtomIndices.empty() && m_SelectedTransformEmptyIndex < 0))
            {
                m_LastStructureMessage += " Select atoms or empty to use gizmo.";
            }
            AppendSelectionDebugLog("Hotkey: T -> gizmo Translate");
        }

        if (m_ViewportFocused && !io.WantTextInput && m_InteractionMode != InteractionMode::ViewSet && isConfiguredKeyPressed(m_HotkeyRotateGizmo))
        {
            cancelTranslateMode();
            cancelRotateMode();
            m_InteractionMode = InteractionMode::Select;
            m_GizmoEnabled = true;
            m_GizmoOperationIndex = 1;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Transform mode: Rotate around 3D cursor (Blender-style R).";
            if (!m_HasStructureLoaded || (m_SelectedAtomIndices.empty() && m_SelectedTransformEmptyIndex < 0))
            {
                m_LastStructureMessage += " Select atoms or empty to use gizmo.";
            }
            AppendSelectionDebugLog("Hotkey: R -> gizmo Rotate");
        }

        if (m_ViewportFocused && !io.WantTextInput && isConfiguredKeyPressed(m_HotkeyScaleGizmo))
        {
            cancelTranslateMode();
            cancelRotateMode();
            m_InteractionMode = InteractionMode::Select;
            m_GizmoEnabled = true;
            m_GizmoOperationIndex = 2;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Transform mode: Scale (Blender-style S).";
            if (!m_HasStructureLoaded || (m_SelectedAtomIndices.empty() && m_SelectedTransformEmptyIndex < 0))
            {
                m_LastStructureMessage += " Select atoms or empty to use gizmo.";
            }
            AppendSelectionDebugLog("Hotkey: S -> gizmo Scale");
        }

        if (m_TranslateModeActive && m_Camera)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                cancelTranslateMode();
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                commitTranslateMode();
            }
            else
            {
                if (ImGui::IsKeyPressed(ImGuiKey_X, false))
                {
                    if (io.KeyShift)
                    {
                        m_TranslatePlaneLockAxis = 0;
                        m_TranslateConstraintAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: plane lock X (Shift+X)");
                    }
                    else
                    {
                        m_TranslateConstraintAxis = 0;
                        m_TranslatePlaneLockAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: axis X");
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                {
                    if (io.KeyShift)
                    {
                        m_TranslatePlaneLockAxis = 1;
                        m_TranslateConstraintAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: plane lock Y (Shift+Y)");
                    }
                    else
                    {
                        m_TranslateConstraintAxis = 1;
                        m_TranslatePlaneLockAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: axis Y");
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                {
                    if (io.KeyShift)
                    {
                        m_TranslatePlaneLockAxis = 2;
                        m_TranslateConstraintAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: plane lock Z (Shift+Z)");
                    }
                    else
                    {
                        m_TranslateConstraintAxis = 2;
                        m_TranslatePlaneLockAxis = -1;
                        AppendSelectionDebugLog("Translate constraint: axis Z");
                    }
                }

                auto appendTypedTranslateChar = [&](char character)
                {
                    if (m_TranslateConstraintAxis < 0 || m_TranslateConstraintAxis >= 3 || m_TranslatePlaneLockAxis >= 0)
                    {
                        m_LastStructureOperationFailed = true;
                        m_LastStructureMessage = "Typed translate distance needs a single X/Y/Z axis constraint.";
                        return;
                    }

                    if (character == '-' && !m_TranslateTypedDistanceBuffer.empty())
                    {
                        return;
                    }
                    if (character == '.' && m_TranslateTypedDistanceBuffer.find('.') != std::string::npos)
                    {
                        return;
                    }

                    m_TranslateTypedDistanceActive = true;
                    m_TranslateTypedDistanceBuffer.push_back(character);
                };

                if (!io.KeyCtrl && !io.KeyAlt)
                {
                    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && m_TranslateTypedDistanceActive)
                    {
                        if (!m_TranslateTypedDistanceBuffer.empty())
                        {
                            m_TranslateTypedDistanceBuffer.pop_back();
                        }
                        if (m_TranslateTypedDistanceBuffer.empty())
                        {
                            m_TranslateTypedDistanceActive = false;
                        }
                    }

                    const std::array<std::pair<ImGuiKey, char>, 12> numericKeys = {
                        std::pair{ImGuiKey_0, '0'},
                        std::pair{ImGuiKey_1, '1'},
                        std::pair{ImGuiKey_2, '2'},
                        std::pair{ImGuiKey_3, '3'},
                        std::pair{ImGuiKey_4, '4'},
                        std::pair{ImGuiKey_5, '5'},
                        std::pair{ImGuiKey_6, '6'},
                        std::pair{ImGuiKey_7, '7'},
                        std::pair{ImGuiKey_8, '8'},
                        std::pair{ImGuiKey_9, '9'},
                        std::pair{ImGuiKey_KeypadDecimal, '.'},
                        std::pair{ImGuiKey_Period, '.'}};

                    for (const auto &[key, character] : numericKeys)
                    {
                        if (ImGui::IsKeyPressed(key, false))
                        {
                            appendTypedTranslateChar(character);
                        }
                    }

                    if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))
                    {
                        appendTypedTranslateChar('-');
                    }
                }

                const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                const glm::vec2 mouseDelta = mousePos - m_TranslateLastMousePos;

                glm::vec2 nextMouseReference = mousePos;
                const float viewportWidthPixels = m_ViewportRectMax.x - m_ViewportRectMin.x;
                const float viewportHeightPixels = m_ViewportRectMax.y - m_ViewportRectMin.y;
                if (viewportWidthPixels > 64.0f && viewportHeightPixels > 64.0f)
                {
                    constexpr float kWrapTriggerMargin = 12.0f;
                    constexpr float kWrapInset = 20.0f;

                    glm::vec2 wrappedMousePos = mousePos;
                    bool shouldWrapCursor = false;

                    if (mousePos.x <= m_ViewportRectMin.x + kWrapTriggerMargin)
                    {
                        wrappedMousePos.x = m_ViewportRectMax.x - kWrapInset;
                        shouldWrapCursor = true;
                    }
                    else if (mousePos.x >= m_ViewportRectMax.x - kWrapTriggerMargin)
                    {
                        wrappedMousePos.x = m_ViewportRectMin.x + kWrapInset;
                        shouldWrapCursor = true;
                    }

                    if (mousePos.y <= m_ViewportRectMin.y + kWrapTriggerMargin)
                    {
                        wrappedMousePos.y = m_ViewportRectMax.y - kWrapInset;
                        shouldWrapCursor = true;
                    }
                    else if (mousePos.y >= m_ViewportRectMax.y - kWrapTriggerMargin)
                    {
                        wrappedMousePos.y = m_ViewportRectMin.y + kWrapInset;
                        shouldWrapCursor = true;
                    }

                    if (shouldWrapCursor)
                    {
                        if (GLFWwindow *window = ApplicationContext::Get().GetWindow())
                        {
                            int windowX = 0;
                            int windowY = 0;
                            glfwGetWindowPos(window, &windowX, &windowY);
                            glfwSetCursorPos(
                                window,
                                static_cast<double>(wrappedMousePos.x - static_cast<float>(windowX)),
                                static_cast<double>(wrappedMousePos.y - static_cast<float>(windowY)));
                            nextMouseReference = wrappedMousePos;
                        }
                    }
                }

                m_TranslateLastMousePos = nextMouseReference;

                const glm::vec3 forward = glm::normalize(glm::vec3(
                    std::cos(m_Camera->GetPitch()) * std::sin(m_Camera->GetYaw()),
                    std::cos(m_Camera->GetPitch()) * std::cos(m_Camera->GetYaw()),
                    std::sin(m_Camera->GetPitch())));
                const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
                const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
                const glm::vec3 up = glm::normalize(glm::cross(right, forward));

                glm::vec3 translatePivot(0.0f);
                if (!m_TranslateInitialCartesian.empty())
                {
                    for (const glm::vec3 &position : m_TranslateInitialCartesian)
                    {
                        translatePivot += position;
                    }
                    translatePivot /= static_cast<float>(m_TranslateInitialCartesian.size());
                }
                else if (m_TranslateSpecialNode == 1)
                {
                    translatePivot = m_TranslateEmptyInitialPosition;
                }

                const float viewportHeight = std::max(1.0f, m_ViewportRectMax.y - m_ViewportRectMin.y);
                float worldPerPixel = 0.0f;
                if (m_ProjectionModeIndex == 1)
                {
                    worldPerPixel = (2.0f * m_Camera->GetOrthographicSize()) / viewportHeight;
                }
                else
                {
                    const glm::vec3 cameraPosition = m_Camera->GetTarget() - forward * m_Camera->GetDistance();
                    const float pivotDepth = std::max(0.25f, glm::dot(translatePivot - cameraPosition, forward));
                    const float halfFovRadians = glm::radians(m_Camera->GetPerspectiveFovDegrees()) * 0.5f;
                    worldPerPixel = (2.0f * std::tan(halfFovRadians) * pivotDepth) / viewportHeight;
                }

                glm::vec3 frameDelta = (-right * mouseDelta.x + up * mouseDelta.y) * std::max(0.0005f, worldPerPixel);

                std::array<glm::vec3, 3> transformAxes = ResolveTransformAxes(translatePivot);
                if (m_TranslateEmptyIndex >= 0 && m_TranslateEmptyIndex < static_cast<int>(m_TransformEmpties.size()))
                {
                    const TransformEmpty &translateEmpty = m_TransformEmpties[static_cast<std::size_t>(m_TranslateEmptyIndex)];
                    transformAxes = translateEmpty.axes;

                    // Keep constraints stable even if stored axes become slightly non-orthonormal over edits.
                    for (glm::vec3 &axis : transformAxes)
                    {
                        if (glm::length2(axis) < 1e-8f)
                        {
                            axis = glm::vec3(0.0f);
                        }
                        else
                        {
                            axis = glm::normalize(axis);
                        }
                    }

                    if (glm::length2(transformAxes[0]) < 1e-8f)
                        transformAxes[0] = glm::vec3(1.0f, 0.0f, 0.0f);
                    if (glm::length2(transformAxes[1]) < 1e-8f)
                        transformAxes[1] = glm::vec3(0.0f, 1.0f, 0.0f);

                    transformAxes[2] = glm::cross(transformAxes[0], transformAxes[1]);
                    if (glm::length2(transformAxes[2]) < 1e-8f)
                        transformAxes[2] = glm::vec3(0.0f, 0.0f, 1.0f);
                    else
                        transformAxes[2] = glm::normalize(transformAxes[2]);

                    transformAxes[1] = glm::cross(transformAxes[2], transformAxes[0]);
                    if (glm::length2(transformAxes[1]) < 1e-8f)
                        transformAxes[1] = glm::vec3(0.0f, 1.0f, 0.0f);
                    else
                        transformAxes[1] = glm::normalize(transformAxes[1]);
                }

                const glm::vec3 axis[3] = {
                    transformAxes[0],
                    transformAxes[1],
                    transformAxes[2]};

                if (m_TranslateConstraintAxis >= 0 && m_TranslateConstraintAxis < 3)
                {
                    glm::vec3 axisWorld = axis[m_TranslateConstraintAxis];
                    if (glm::length2(axisWorld) > 1e-8f)
                    {
                        axisWorld = glm::normalize(axisWorld);

                        auto projectToScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen) -> bool
                        {
                            const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(world, 1.0f);
                            if (clip.w <= 1e-6f)
                            {
                                return false;
                            }

                            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                            const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                            const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                            outScreen = glm::vec2(
                                m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                                m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
                            return true;
                        };

                        const float axisProbe = std::max(0.25f, m_Camera->GetDistance() * 0.2f);
                        glm::vec2 axisA(0.0f);
                        glm::vec2 axisB(0.0f);
                        if (projectToScreen(translatePivot - axisWorld * axisProbe, axisA) && projectToScreen(translatePivot + axisWorld * axisProbe, axisB))
                        {
                            const glm::vec2 axisScreen = axisB - axisA;
                            const float axisScreenLen = glm::length(axisScreen);
                            if (axisScreenLen > 1.0f)
                            {
                                const glm::vec2 axisDirScreen = axisScreen / axisScreenLen;
                                const float worldPerPixel = (2.0f * axisProbe) / axisScreenLen;
                                const float mouseSigned = glm::dot(mouseDelta, axisDirScreen);
                                frameDelta = axisWorld * (mouseSigned * worldPerPixel);
                            }
                            else
                            {
                                frameDelta = axisWorld * glm::dot(frameDelta, axisWorld);
                            }
                        }
                        else
                        {
                            frameDelta = axisWorld * glm::dot(frameDelta, axisWorld);
                        }
                    }
                }
                else if (m_TranslatePlaneLockAxis >= 0 && m_TranslatePlaneLockAxis < 3)
                {
                    frameDelta -= axis[m_TranslatePlaneLockAxis] * glm::dot(frameDelta, axis[m_TranslatePlaneLockAxis]);
                }

                std::optional<float> typedDistance;
                if (m_TranslateTypedDistanceActive &&
                    m_TranslateConstraintAxis >= 0 &&
                    m_TranslateConstraintAxis < 3 &&
                    !m_TranslateTypedDistanceBuffer.empty() &&
                    m_TranslateTypedDistanceBuffer != "-" &&
                    m_TranslateTypedDistanceBuffer != "." &&
                    m_TranslateTypedDistanceBuffer != "-.")
                {
                    try
                    {
                        typedDistance = std::stof(m_TranslateTypedDistanceBuffer);
                    }
                    catch (...)
                    {
                    }
                }

                if (!typedDistance.has_value())
                {
                    m_TranslateCurrentOffset += frameDelta;
                }

                glm::vec3 appliedOffset = m_TranslateCurrentOffset;
                if (typedDistance.has_value())
                {
                    appliedOffset = axis[m_TranslateConstraintAxis] * *typedDistance;
                    m_TranslateCurrentOffset = appliedOffset;
                }
                else if (io.KeyCtrl)
                {
                    const float snapStep = std::max(0.001f, m_GizmoTranslateSnap);

                    if (m_TranslateConstraintAxis >= 0 && m_TranslateConstraintAxis < 3)
                    {
                        const float scalar = glm::dot(m_TranslateCurrentOffset, axis[m_TranslateConstraintAxis]);
                        const float snapped = std::round(scalar / snapStep) * snapStep;
                        appliedOffset = axis[m_TranslateConstraintAxis] * snapped;
                    }
                    else
                    {
                        appliedOffset.x = std::round(appliedOffset.x / snapStep) * snapStep;
                        appliedOffset.y = std::round(appliedOffset.y / snapStep) * snapStep;
                        appliedOffset.z = std::round(appliedOffset.z / snapStep) * snapStep;

                        if (m_TranslatePlaneLockAxis >= 0 && m_TranslatePlaneLockAxis < 3)
                        {
                            if (m_TranslatePlaneLockAxis == 0)
                                appliedOffset.x = 0.0f;
                            if (m_TranslatePlaneLockAxis == 1)
                                appliedOffset.y = 0.0f;
                            if (m_TranslatePlaneLockAxis == 2)
                                appliedOffset.z = 0.0f;
                        }
                    }
                }

                const std::size_t count = std::min(m_TranslateIndices.size(), m_TranslateInitialCartesian.size());
                for (std::size_t i = 0; i < count; ++i)
                {
                    SetAtomCartesianPosition(m_TranslateIndices[i], m_TranslateInitialCartesian[i] + appliedOffset);
                }

                if (m_TranslateEmptyIndex >= 0 && m_TranslateEmptyIndex < static_cast<int>(m_TransformEmpties.size()))
                {
                    m_TransformEmpties[static_cast<std::size_t>(m_TranslateEmptyIndex)].position = m_TranslateEmptyInitialPosition + appliedOffset;
                }
                if (m_TranslateSpecialNode == 1)
                {
                    m_LightPosition = m_TranslateEmptyInitialPosition + appliedOffset;
                }

                m_BlockSelectionThisFrame = true;
                m_GizmoConsumedMouseThisFrame = true;
            }
        }

        if (m_RotateModeActive && m_Camera)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                cancelRotateMode();
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            {
                commitRotateMode();
            }
            else if (!m_GizmoEnabled)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_X, false))
                {
                    m_RotateConstraintAxis = 0;
                    AppendSelectionDebugLog("Rotate axis: X");
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                {
                    m_RotateConstraintAxis = 1;
                    AppendSelectionDebugLog("Rotate axis: Y");
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                {
                    m_RotateConstraintAxis = 2;
                    AppendSelectionDebugLog("Rotate axis: Z");
                }

                const std::array<glm::vec3, 3> transformAxes = ResolveTransformAxes(m_RotatePivot);

                glm::vec3 rotationAxis(0.0f, 0.0f, 1.0f);
                if (m_RotateConstraintAxis == 0)
                    rotationAxis = transformAxes[0];
                else if (m_RotateConstraintAxis == 1)
                    rotationAxis = transformAxes[1];
                else if (m_RotateConstraintAxis == 2)
                    rotationAxis = transformAxes[2];
                else
                    rotationAxis = glm::normalize(glm::vec3(
                        std::cos(m_Camera->GetPitch()) * std::sin(m_Camera->GetYaw()),
                        std::cos(m_Camera->GetPitch()) * std::cos(m_Camera->GetYaw()),
                        std::sin(m_Camera->GetPitch())));

                const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                const glm::vec2 mouseDelta = mousePos - m_RotateLastMousePos;
                m_RotateLastMousePos = mousePos;

                const float rotateSpeed = 0.008f;
                m_RotateCurrentAngle += (mouseDelta.x - mouseDelta.y) * rotateSpeed;

                float appliedAngle = m_RotateCurrentAngle;
                if (io.KeyCtrl)
                {
                    const float snapRadians = glm::radians(std::max(0.1f, m_GizmoRotateSnapDeg));
                    appliedAngle = std::round(appliedAngle / snapRadians) * snapRadians;
                }

                const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), appliedAngle, glm::normalize(rotationAxis));
                const std::size_t count = std::min(m_RotateIndices.size(), m_RotateInitialCartesian.size());
                for (std::size_t i = 0; i < count; ++i)
                {
                    const glm::vec3 relative = m_RotateInitialCartesian[i] - m_RotatePivot;
                    const glm::vec3 rotated = glm::vec3(rotation * glm::vec4(relative, 0.0f));
                    SetAtomCartesianPosition(m_RotateIndices[i], m_RotatePivot + rotated);
                }

                m_BlockSelectionThisFrame = true;
                m_GizmoConsumedMouseThisFrame = true;
            }
            else
            {
                m_BlockSelectionThisFrame = true;
            }
        }

        if (m_ViewportFocused && m_InteractionMode == InteractionMode::ViewSet && !io.WantTextInput && m_Camera)
        {
            const glm::vec3 target = m_Camera->GetTarget();
            const float distance = m_Camera->GetDistance();
            bool viewKeyUsed = false;
            float yaw = m_Camera->GetYaw();
            float pitch = m_Camera->GetPitch();

            if (ImGui::IsKeyPressed(ImGuiKey_T, false))
            {
                pitch = glm::half_pi<float>() - 0.01f;
                yaw = 0.0f;
                viewKeyUsed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_B, false))
            {
                pitch = -glm::half_pi<float>() + 0.01f;
                yaw = 0.0f;
                viewKeyUsed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_L, false))
            {
                yaw = -glm::half_pi<float>();
                pitch = 0.0f;
                viewKeyUsed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            {
                yaw = glm::half_pi<float>();
                pitch = 0.0f;
                viewKeyUsed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_P, false))
            {
                yaw = 0.0f;
                pitch = 0.0f;
                viewKeyUsed = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_K, false))
            {
                yaw = glm::pi<float>();
                pitch = 0.0f;
                viewKeyUsed = true;
            }

            if (viewKeyUsed)
            {
                StartCameraOrbitTransition(target, distance, yaw, pitch);
                m_LastStructureOperationFailed = false;
                m_LastStructureMessage = "ViewSet: smooth camera transition started from keyboard.";
            }
        }

        if (m_ViewportFocused && m_InteractionMode == InteractionMode::Select && !io.WantTextInput && isConfiguredKeyPressed(m_HotkeyBoxSelect))
        {
            m_BoxSelectArmed = true;
            m_BoxSelecting = false;
            m_CircleSelectArmed = false;
            m_CircleSelecting = false;
            AppendSelectionDebugLog("Box select armed (press LMB and drag)");
        }

        if (m_ViewportFocused && m_InteractionMode == InteractionMode::Select && !io.WantTextInput && isConfiguredKeyPressed(m_HotkeyCircleSelect))
        {
            m_CircleSelectArmed = true;
            m_CircleSelecting = false;
            m_BoxSelectArmed = false;
            m_BoxSelecting = false;
            AppendSelectionDebugLog("Circle select armed (LMB to apply, wheel changes radius)");
        }

        if ((m_BoxSelectArmed || m_CircleSelectArmed) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            m_BoxSelectArmed = false;
            m_BoxSelecting = false;
            m_CircleSelectArmed = false;
            m_CircleSelecting = false;
            AppendSelectionDebugLog("Box/circle select canceled with Escape");
        }

        ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x < 1.0f)
            size.x = 1.0f;
        if (size.y < 1.0f)
            size.y = 1.0f;
        m_ViewportSize = glm::vec2(size.x, size.y);

        if (m_RenderBackend)
        {
            const std::uint32_t texture = m_RenderBackend->GetColorAttachmentRendererID();
            if (texture != 0)
            {
                ImTextureID textureID = (ImTextureID)(std::uintptr_t)texture;
                ImGui::Image(textureID, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                const ImVec2 rectMin = ImGui::GetItemRectMin();
                const ImVec2 rectMax = ImGui::GetItemRectMax();
                m_ViewportRectMin = glm::vec2(rectMin.x, rectMin.y);
                m_ViewportRectMax = glm::vec2(rectMax.x, rectMax.y);

                if (m_Camera)
                {
                    ImGui::SetNextWindowPos(ImVec2(rectMin.x + 14.0f, rectMin.y + 10.0f), ImGuiCond_FirstUseEver);
                    ImGuiWindowFlags rotateToolbarFlags =
                        ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse |
                        ImGuiWindowFlags_NoNav;

                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
                    if (ImGui::Begin("Viewport Controls", nullptr, rotateToolbarFlags))
                    {
                        const float stepRad = glm::radians(glm::clamp(m_ViewportRotateStepDeg, 0.1f, 180.0f));
                        const float pitchLimit = glm::half_pi<float>() - 0.01f;
                        const ImGuiStyle &style = ImGui::GetStyle();
                        const float frameHeight = ImGui::GetFrameHeight();

                        auto calcButtonWidth = [&](const char *label)
                        {
                            return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
                        };

                        auto centerRow = [&](float rowWidth)
                        {
                            const float avail = ImGui::GetContentRegionAvail().x;
                            if (avail > rowWidth)
                            {
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - rowWidth) * 0.5f);
                            }
                        };

                        auto applyView = [&](float yaw, float pitch)
                        {
                            StartCameraOrbitTransition(
                                m_Camera->GetTarget(),
                                m_Camera->GetDistance(),
                                yaw,
                                glm::clamp(pitch, -pitchLimit, pitchLimit));
                            m_LastStructureOperationFailed = false;
                        };

                        const float row1Width =
                            ImGui::CalcTextSize("View").x + style.ItemSpacing.x +
                            frameHeight + style.ItemSpacing.x +
                            frameHeight + style.ItemSpacing.x +
                            frameHeight + style.ItemSpacing.x +
                            frameHeight + style.ItemSpacing.x +
                            calcButtonWidth("Roll-") + style.ItemSpacing.x +
                            calcButtonWidth("Roll+") + style.ItemSpacing.x +
                            calcButtonWidth("Front");
                        centerRow(row1Width);

                        ImGui::TextUnformatted("View");
                        ImGui::SameLine();
                        if (ImGui::ArrowButton("##ViewLeft", ImGuiDir_Left))
                        {
                            applyView(m_Camera->GetYaw() + stepRad, m_Camera->GetPitch());
                            m_LastStructureMessage = "Viewport: rotate left.";
                        }
                        ImGui::SameLine();
                        if (ImGui::ArrowButton("##ViewRight", ImGuiDir_Right))
                        {
                            applyView(m_Camera->GetYaw() - stepRad, m_Camera->GetPitch());
                            m_LastStructureMessage = "Viewport: rotate right.";
                        }
                        ImGui::SameLine();
                        if (ImGui::ArrowButton("##ViewUp", ImGuiDir_Up))
                        {
                            applyView(m_Camera->GetYaw(), m_Camera->GetPitch() + stepRad);
                            m_LastStructureMessage = "Viewport: tilt up.";
                        }
                        ImGui::SameLine();
                        if (ImGui::ArrowButton("##ViewDown", ImGuiDir_Down))
                        {
                            applyView(m_Camera->GetYaw(), m_Camera->GetPitch() - stepRad);
                            m_LastStructureMessage = "Viewport: tilt down.";
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Roll-"))
                        {
                            StartCameraOrbitTransition(
                                m_Camera->GetTarget(),
                                m_Camera->GetDistance(),
                                m_Camera->GetYaw(),
                                m_Camera->GetPitch(),
                                m_Camera->GetRoll() - stepRad);
                            m_LastStructureOperationFailed = false;
                            m_LastStructureMessage = "Viewport: roll left.";
                            settingsChanged = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Roll+"))
                        {
                            StartCameraOrbitTransition(
                                m_Camera->GetTarget(),
                                m_Camera->GetDistance(),
                                m_Camera->GetYaw(),
                                m_Camera->GetPitch(),
                                m_Camera->GetRoll() + stepRad);
                            m_LastStructureOperationFailed = false;
                            m_LastStructureMessage = "Viewport: roll right.";
                            settingsChanged = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Front"))
                        {
                            applyView(0.0f, 0.0f);
                            m_LastStructureMessage = "Viewport: front view.";
                        }
                        ImGui::SameLine();

                        const float stepInputWidth = 150.0f;
                        const float row2Width = ImGui::CalcTextSize("Step (deg):").x + style.ItemSpacing.x + stepInputWidth;
                        // centerRow(row2Width);
                        ImGui::TextUnformatted("Step (deg):");
                        ImGui::SameLine();
                        ImGui::PushItemWidth(stepInputWidth);
                        if (ImGui::InputFloat("##ViewportRotateStep", &m_ViewportRotateStepDeg, 1.0f, 5.0f, "%.1f"))
                        {
                            m_ViewportRotateStepDeg = glm::clamp(m_ViewportRotateStepDeg, 0.1f, 180.0f);
                            settingsChanged = true;
                        }
                        ImGui::PopItemWidth();

                        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
                            ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive())
                        {
                            m_BlockSelectionThisFrame = true;
                            m_GizmoConsumedMouseThisFrame = true;
                        }
                    }
                    ImGui::End();
                    ImGui::PopStyleVar(3);
                }

                if (m_ShowGlobalAxesOverlay && m_Camera)
                {
                    const glm::vec3 origin = m_SceneOriginPosition;
                    const float sceneExtent = std::max(3.0f, m_SceneSettings.gridSpacing * static_cast<float>(std::max(2, m_SceneSettings.gridHalfExtent)));
                    const float axisLen = std::max(sceneExtent * 2.0f, m_Camera->GetDistance() * 2.4f);
                    auto projectWorldToScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen) -> bool
                    {
                        const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(world, 1.0f);
                        if (clip.w <= 0.0001f)
                        {
                            return false;
                        }

                        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                        outScreen = glm::vec2(
                            m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                            m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
                        return true;
                    };

                    ImDrawList *overlay = ImGui::GetWindowDrawList();
                    overlay->PushClipRect(rectMin, rectMax, true);
                    const ImU32 axisColor[3] = {
                        ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[0].r, m_AxisColors[0].g, m_AxisColors[0].b, 0.95f)),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[1].r, m_AxisColors[1].g, m_AxisColors[1].b, 0.95f)),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[2].r, m_AxisColors[2].g, m_AxisColors[2].b, 0.95f))};
                    const glm::vec3 axisDir[3] = {
                        glm::vec3(1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f)};
                    const char *axisLabel[3] = {"X", "Y", "Z"};
                    glm::vec2 originScreen(0.0f);
                    const bool hasOrigin = projectWorldToScreen(origin, originScreen);

                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (!m_ShowGlobalAxis[axis])
                        {
                            continue;
                        }

                        glm::vec2 startScreen(0.0f);
                        glm::vec2 endScreen(0.0f);
                        const bool hasStart = projectWorldToScreen(origin - axisDir[axis] * axisLen, startScreen);
                        const bool hasEnd = projectWorldToScreen(origin + axisDir[axis] * axisLen, endScreen);
                        if (!hasOrigin || (!hasStart && !hasEnd))
                        {
                            continue;
                        }

                        if (hasStart)
                        {
                            overlay->AddLine(ImVec2(startScreen.x, startScreen.y), ImVec2(originScreen.x, originScreen.y), axisColor[axis], 2.0f);
                        }

                        if (hasEnd)
                        {
                            overlay->AddLine(ImVec2(originScreen.x, originScreen.y), ImVec2(endScreen.x, endScreen.y), axisColor[axis], 2.0f);
                            overlay->AddCircleFilled(ImVec2(endScreen.x, endScreen.y), 3.2f, axisColor[axis]);
                            overlay->AddText(ImVec2(endScreen.x + 6.0f, endScreen.y - 7.0f), axisColor[axis], axisLabel[axis]);
                        }
                    }
                    overlay->PopClipRect();
                }

                if (m_TranslateModeActive && m_Camera && m_TranslateConstraintAxis >= 0 && m_TranslateConstraintAxis < 3 &&
                    (!m_TranslateInitialCartesian.empty() || (m_TranslateEmptyIndex >= 0 && m_TranslateEmptyIndex < static_cast<int>(m_TransformEmpties.size()))))
                {
                    glm::vec3 pivot(0.0f);
                    if (!m_TranslateInitialCartesian.empty())
                    {
                        for (const glm::vec3 &position : m_TranslateInitialCartesian)
                        {
                            pivot += position;
                        }
                        pivot /= static_cast<float>(m_TranslateInitialCartesian.size());
                    }

                    std::array<glm::vec3, 3> axes = ResolveTransformAxes(pivot);
                    if (m_TranslateEmptyIndex >= 0 && m_TranslateEmptyIndex < static_cast<int>(m_TransformEmpties.size()))
                    {
                        const TransformEmpty &translateEmpty = m_TransformEmpties[static_cast<std::size_t>(m_TranslateEmptyIndex)];
                        pivot = translateEmpty.position;
                        axes = translateEmpty.axes;

                        for (glm::vec3 &axis : axes)
                        {
                            if (glm::length2(axis) > 1e-8f)
                            {
                                axis = glm::normalize(axis);
                            }
                        }
                    }

                    const glm::vec3 activeAxis = glm::normalize(axes[m_TranslateConstraintAxis]);
                    const glm::vec3 currentPivot = pivot + m_TranslateCurrentOffset;
                    const float sceneExtent = std::max(3.0f, m_SceneSettings.gridSpacing * static_cast<float>(std::max(2, m_SceneSettings.gridHalfExtent)));
                    const float lineLen = std::max(sceneExtent * 2.0f, m_Camera->GetDistance() * 2.0f);

                    auto projectWorldToScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen) -> bool
                    {
                        const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(world, 1.0f);
                        if (clip.w <= 0.0001f)
                        {
                            return false;
                        }

                        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                        outScreen = glm::vec2(
                            m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                            m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
                        return true;
                    };

                    glm::vec2 a(0.0f);
                    glm::vec2 b(0.0f);
                    if (projectWorldToScreen(currentPivot - activeAxis * lineLen, a) && projectWorldToScreen(currentPivot + activeAxis * lineLen, b))
                    {
                        ImDrawList *overlay = ImGui::GetWindowDrawList();
                        overlay->PushClipRect(rectMin, rectMax, true);
                        const ImU32 guideColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
                            m_AxisColors[m_TranslateConstraintAxis].r,
                            m_AxisColors[m_TranslateConstraintAxis].g,
                            m_AxisColors[m_TranslateConstraintAxis].b,
                            0.96f));
                        overlay->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), guideColor, 2.6f);
                        overlay->PopClipRect();
                    }
                }

                if (m_ShowTransformEmpties && m_Camera && !m_TransformEmpties.empty())
                {
                    const float axisLen = glm::max(0.08f, m_TransformEmptyVisualScale * m_Camera->GetDistance() * 0.28f);
                    const glm::vec3 axisDir[3] = {
                        glm::vec3(1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f)};

                    auto projectWorldToScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen) -> bool
                    {
                        const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(world, 1.0f);
                        if (clip.w <= 0.0001f)
                        {
                            return false;
                        }

                        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                        outScreen = glm::vec2(
                            m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                            m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
                        return true;
                    };

                    ImDrawList *overlay = ImGui::GetWindowDrawList();
                    overlay->PushClipRect(rectMin, rectMax, true);
                    const ImU32 axisColor[3] = {
                        ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[0].r, m_AxisColors[0].g, m_AxisColors[0].b, 0.96f)),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[1].r, m_AxisColors[1].g, m_AxisColors[1].b, 0.96f)),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[2].r, m_AxisColors[2].g, m_AxisColors[2].b, 0.96f))};

                    for (std::size_t i = 0; i < m_TransformEmpties.size(); ++i)
                    {
                        const TransformEmpty &empty = m_TransformEmpties[i];
                        if (!empty.visible || !IsCollectionVisible(empty.collectionIndex))
                        {
                            continue;
                        }
                        glm::vec2 centerScreen(0.0f);
                        if (!projectWorldToScreen(empty.position, centerScreen))
                        {
                            continue;
                        }

                        for (int axis = 0; axis < 3; ++axis)
                        {
                            glm::vec2 endScreen(0.0f);
                            glm::vec3 axisWorld = empty.axes[axis];
                            if (glm::length2(axisWorld) < 1e-8f)
                            {
                                axisWorld = (axis == 0)   ? glm::vec3(1.0f, 0.0f, 0.0f)
                                            : (axis == 1) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                          : glm::vec3(0.0f, 0.0f, 1.0f);
                            }
                            else
                            {
                                axisWorld = glm::normalize(axisWorld);
                            }

                            if (!projectWorldToScreen(empty.position + axisWorld * axisLen, endScreen))
                            {
                                continue;
                            }

                            overlay->AddLine(
                                ImVec2(centerScreen.x, centerScreen.y),
                                ImVec2(endScreen.x, endScreen.y),
                                axisColor[axis],
                                1.8f);
                        }

                        const bool isSelectedEmpty = (static_cast<int>(i) == m_SelectedTransformEmptyIndex);
                        const ImU32 centerColor = isSelectedEmpty ? IM_COL32(255, 255, 255, 245) : IM_COL32(230, 230, 230, 210);
                        overlay->AddCircleFilled(ImVec2(centerScreen.x, centerScreen.y), isSelectedEmpty ? 4.0f : 3.0f, centerColor);
                    }

                    overlay->PopClipRect();
                }

                const bool hasSelectedEmpty =
                    (m_SelectedTransformEmptyIndex >= 0 && m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size()) &&
                     m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].visible &&
                     m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].selectable &&
                     IsCollectionVisible(m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].collectionIndex) &&
                     IsCollectionSelectable(m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].collectionIndex));
                const bool hasSelectedLight = (m_SelectedSpecialNode == SpecialNodeSelection::Light);
                const bool canRenderTransformGizmo = m_GizmoEnabled && m_Camera && ((m_HasStructureLoaded && !m_SelectedAtomIndices.empty()) || hasSelectedEmpty || hasSelectedLight);
                if (canRenderTransformGizmo != s_LastCanRenderTransformGizmo || m_SelectedAtomIndices.size() != s_LastSelectedCount)
                {
                    std::ostringstream gizmoPreconditionsLog;
                    gizmoPreconditionsLog << "Gizmo preconditions: canRender=" << (canRenderTransformGizmo ? "1" : "0")
                                          << " gizmoEnabled=" << (m_GizmoEnabled ? "1" : "0")
                                          << " hasCamera=" << (m_Camera ? "1" : "0")
                                          << " hasStructure=" << (m_HasStructureLoaded ? "1" : "0")
                                          << " selectedCount=" << m_SelectedAtomIndices.size()
                                          << " selectedEmpty=" << (hasSelectedEmpty ? "1" : "0")
                                          << " selectedLight=" << (hasSelectedLight ? "1" : "0");
                    AppendSelectionDebugLog(gizmoPreconditionsLog.str());
                    s_LastCanRenderTransformGizmo = canRenderTransformGizmo;
                    s_LastSelectedCount = m_SelectedAtomIndices.size();
                }

                if (canRenderTransformGizmo)
                {
                    glm::vec3 pivot(0.0f);
                    std::size_t validCount = 0;

                    if (hasSelectedEmpty)
                    {
                        pivot = m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].position;
                        validCount = 1;
                    }
                    else if (hasSelectedLight)
                    {
                        pivot = m_LightPosition;
                        validCount = 1;
                    }
                    else if (m_RotateModeActive || m_GizmoOperationIndex == 1)
                    {
                        pivot = m_CursorPosition;
                        validCount = 1;
                    }
                    else
                    {
                        for (std::size_t atomIndex : m_SelectedAtomIndices)
                        {
                            if (atomIndex >= m_WorkingStructure.atoms.size())
                            {
                                continue;
                            }

                            pivot += GetAtomCartesianPosition(atomIndex);
                            ++validCount;
                        }
                    }

                    if (validCount > 0)
                    {
                        if (!(m_RotateModeActive || m_GizmoOperationIndex == 1))
                        {
                            pivot /= static_cast<float>(validCount);
                        }
                        glm::vec2 pivotScreen(0.0f, 0.0f);
                        bool pivotInViewport = false;

                        if (!s_LastValidPivot)
                        {
                            std::ostringstream pivotLog;
                            pivotLog << "Gizmo pivot ready: validCount=" << validCount
                                     << " pivot=(" << pivot.x << "," << pivot.y << "," << pivot.z << ")";
                            AppendSelectionDebugLog(pivotLog.str());

                            const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(pivot, 1.0f);
                            if (clip.w > 0.0001f)
                            {
                                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                                const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                                const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                                const glm::vec2 pivotScreen(
                                    m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                                    m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
                                const bool inViewport =
                                    pivotScreen.x >= m_ViewportRectMin.x && pivotScreen.x <= m_ViewportRectMax.x &&
                                    pivotScreen.y >= m_ViewportRectMin.y && pivotScreen.y <= m_ViewportRectMax.y;

                                std::ostringstream projectionLog;
                                projectionLog << "Gizmo pivot projection: ndc=(" << ndc.x << "," << ndc.y << "," << ndc.z << ")"
                                              << " screen=(" << pivotScreen.x << "," << pivotScreen.y << ")"
                                              << " inViewport=" << (inViewport ? "1" : "0");
                                AppendSelectionDebugLog(projectionLog.str());
                            }
                            else
                            {
                                AppendSelectionDebugLog("Gizmo pivot projection: clip.w <= 0 (behind camera).");
                            }
                        }
                        s_LastValidPivot = true;

                        {
                            const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(pivot, 1.0f);
                            if (clip.w > 0.0001f)
                            {
                                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                                const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                                const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                                pivotScreen = glm::vec2(
                                    m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                                    m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
                                pivotInViewport =
                                    pivotScreen.x >= m_ViewportRectMin.x && pivotScreen.x <= m_ViewportRectMax.x &&
                                    pivotScreen.y >= m_ViewportRectMin.y && pivotScreen.y <= m_ViewportRectMax.y;
                                m_FallbackPivotScreen = pivotScreen;
                            }
                        }

                        const int activeOperation = m_RotateModeActive ? 1 : m_GizmoOperationIndex;
                        std::array<glm::vec3, 3> transformAxes = ResolveTransformAxes(pivot);
                        if (hasSelectedEmpty && m_GizmoModeIndex != 1)
                        {
                            transformAxes = m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].axes;
                        }

                        glm::vec2 fallbackAxisScreenDir[3] = {
                            glm::vec2(1.0f, 0.0f),
                            glm::vec2(0.0f, 1.0f),
                            glm::vec2(0.0f, -1.0f)};
                        glm::vec2 fallbackAxisEnd[3] = {
                            pivotScreen,
                            pivotScreen,
                            pivotScreen};
                        float fallbackPixelsPerWorld[3] = {1.0f, 1.0f, 1.0f};
                        bool fallbackAxisVisible[3] = {false, false, false};
                        int hoveredFallbackAxis = -1;

                        if (pivotInViewport)
                        {
                            const ImU32 axisColor[3] = {
                                ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[0].r, m_AxisColors[0].g, m_AxisColors[0].b, 0.96f)),
                                ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[1].r, m_AxisColors[1].g, m_AxisColors[1].b, 0.96f)),
                                ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[2].r, m_AxisColors[2].g, m_AxisColors[2].b, 0.96f))};

                            const float markerScale = glm::clamp(m_FallbackGizmoVisualScale, 0.5f, 6.0f);
                            const float centerRadius = (m_FallbackGizmoDragging ? 9.0f : 7.5f) * markerScale;
                            const float thickness = (m_FallbackGizmoDragging ? 3.2f : 2.6f) * markerScale;
                            const float headLength = 12.0f * markerScale;
                            const float headWidth = 8.0f * markerScale;
                            const float axisWorldLen = glm::max(0.4f, 0.18f * m_Camera->GetDistance());

                            ImDrawList *overlay = ImGui::GetWindowDrawList();
                            overlay->PushClipRect(rectMin, rectMax, true);
                            const ImVec2 p(pivotScreen.x, pivotScreen.y);

                            for (int axis = 0; axis < 3; ++axis)
                            {
                                const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                                const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                                auto projectScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen) -> bool
                                {
                                    const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(world, 1.0f);
                                    if (clip.w <= 0.0001f)
                                    {
                                        return false;
                                    }

                                    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                                    outScreen = glm::vec2(
                                        m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                                        m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
                                    return true;
                                };

                                glm::vec2 posScreen(0.0f);
                                glm::vec2 negScreen(0.0f);
                                const bool hasPos = projectScreen(pivot + transformAxes[axis] * axisWorldLen, posScreen);
                                const bool hasNeg = projectScreen(pivot - transformAxes[axis] * axisWorldLen, negScreen);

                                glm::vec2 axisVec(0.0f);
                                if (hasPos && hasNeg)
                                {
                                    axisVec = posScreen - negScreen;
                                }
                                else if (hasPos)
                                {
                                    axisVec = posScreen - pivotScreen;
                                }
                                else if (hasNeg)
                                {
                                    axisVec = pivotScreen - negScreen;
                                }

                                float axisPixels = glm::length(axisVec);
                                if (axisPixels < 1.0f)
                                {
                                    axisVec = (axis == 0)   ? glm::vec2(1.0f, 0.0f)
                                              : (axis == 1) ? glm::vec2(0.0f, -1.0f)
                                                            : glm::vec2(-0.7071f, -0.7071f);
                                    axisPixels = 1.0f;
                                }

                                axisVec /= axisPixels;
                                const float visualAxisLen = glm::clamp(axisPixels, 55.0f * markerScale, 140.0f * markerScale);
                                const glm::vec2 endScreen = pivotScreen + axisVec * visualAxisLen;
                                fallbackAxisVisible[axis] = true;
                                fallbackAxisScreenDir[axis] = axisVec;
                                fallbackAxisEnd[axis] = endScreen;
                                fallbackPixelsPerWorld[axis] = glm::max(30.0f, axisPixels / axisWorldLen);

                                if (activeOperation == 0 || activeOperation == 2)
                                {
                                    const ImVec2 a(p.x, p.y);
                                    const ImVec2 b(endScreen.x, endScreen.y);
                                    overlay->AddLine(a, b, axisColor[axis], thickness);

                                    if (activeOperation == 0)
                                    {
                                        const glm::vec2 perp(-axisVec.y, axisVec.x);
                                        const ImVec2 tip(endScreen.x, endScreen.y);
                                        const ImVec2 left(endScreen.x - axisVec.x * headLength + perp.x * headWidth,
                                                          endScreen.y - axisVec.y * headLength + perp.y * headWidth);
                                        const ImVec2 right(endScreen.x - axisVec.x * headLength - perp.x * headWidth,
                                                           endScreen.y - axisVec.y * headLength - perp.y * headWidth);
                                        overlay->AddTriangleFilled(tip, left, right, axisColor[axis]);
                                    }
                                    else
                                    {
                                        const float handleHalf = 5.0f * markerScale;
                                        overlay->AddRectFilled(
                                            ImVec2(endScreen.x - handleHalf, endScreen.y - handleHalf),
                                            ImVec2(endScreen.x + handleHalf, endScreen.y + handleHalf),
                                            axisColor[axis],
                                            1.5f * markerScale);
                                    }
                                }
                            }

                            if (activeOperation == 1)
                            {
                                const float ringRadius[3] = {
                                    22.0f * markerScale,
                                    30.0f * markerScale,
                                    38.0f * markerScale};
                                const ImU32 rotateColor[3] = {
                                    IM_COL32(230, 70, 70, 245),
                                    IM_COL32(70, 230, 70, 245),
                                    IM_COL32(70, 120, 245, 245)};

                                for (int axis = 0; axis < 3; ++axis)
                                {
                                    overlay->AddCircle(p, ringRadius[axis], rotateColor[axis], 64, 2.0f * markerScale);
                                }

                                if (!m_FallbackGizmoDragging)
                                {
                                    const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                                    const float dist = glm::length(mousePos - pivotScreen);
                                    float bestDist = std::numeric_limits<float>::max();
                                    for (int axis = 0; axis < 3; ++axis)
                                    {
                                        const float ringDist = std::abs(dist - ringRadius[axis]);
                                        if (ringDist < 8.0f * markerScale && ringDist < bestDist)
                                        {
                                            bestDist = ringDist;
                                            hoveredFallbackAxis = axis;
                                        }
                                    }
                                }
                            }
                            else if (!m_FallbackGizmoDragging)
                            {
                                const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                                const float pickRadius = 10.0f * markerScale;
                                float bestDist = std::numeric_limits<float>::max();

                                for (int axis = 0; axis < 3; ++axis)
                                {
                                    if (!fallbackAxisVisible[axis])
                                    {
                                        continue;
                                    }

                                    const glm::vec2 a = pivotScreen;
                                    const glm::vec2 b = fallbackAxisEnd[axis];
                                    const glm::vec2 ab = b - a;
                                    const float abLen2 = glm::dot(ab, ab);
                                    if (abLen2 < 1.0f)
                                    {
                                        continue;
                                    }

                                    const float t = glm::clamp(glm::dot(mousePos - a, ab) / abLen2, 0.0f, 1.0f);
                                    const glm::vec2 closest = a + ab * t;
                                    const float dist = glm::length(mousePos - closest);
                                    if (dist < pickRadius && dist < bestDist)
                                    {
                                        if (activeOperation == 2 && t < 0.72f)
                                        {
                                            continue;
                                        }
                                        bestDist = dist;
                                        hoveredFallbackAxis = axis;
                                    }
                                }
                            }

                            if (m_TranslateModeActive && m_TranslateConstraintAxis >= 0 && m_TranslateConstraintAxis < 3)
                            {
                                const int axis = m_TranslateConstraintAxis;
                                const glm::vec2 axisDir = glm::normalize(fallbackAxisScreenDir[axis]);
                                const float farLen = std::max(rectMax.x - rectMin.x, rectMax.y - rectMin.y) * 1.35f;
                                const glm::vec2 guideA = pivotScreen - axisDir * farLen;
                                const glm::vec2 guideB = pivotScreen + axisDir * farLen;

                                overlay->AddLine(
                                    ImVec2(guideA.x, guideA.y),
                                    ImVec2(guideB.x, guideB.y),
                                    axisColor[axis],
                                    1.8f * markerScale);
                            }

                            if (hoveredFallbackAxis >= 0 || m_FallbackGizmoDragging)
                            {
                                const int axisToHighlight = m_FallbackGizmoDragging ? m_FallbackGizmoAxis : hoveredFallbackAxis;
                                if (axisToHighlight >= 0 && axisToHighlight < 3 && fallbackAxisVisible[axisToHighlight])
                                {
                                    const ImVec2 tip(fallbackAxisEnd[axisToHighlight].x, fallbackAxisEnd[axisToHighlight].y);
                                    overlay->AddCircle(tip, 8.0f * markerScale, IM_COL32(255, 255, 255, 235), 0, 2.0f * markerScale);
                                }
                            }

                            overlay->AddCircleFilled(p, centerRadius, IM_COL32(245, 245, 245, 235));
                            overlay->AddCircle(p, centerRadius + 2.5f * markerScale, IM_COL32(20, 20, 20, 230), 0, 1.6f * markerScale);
                            overlay->PopClipRect();
                        }

                        glm::mat4 gizmoTransform(1.0f);
                        if (m_GizmoModeIndex != 1)
                        {
                            gizmoTransform[0] = glm::vec4(transformAxes[0], 0.0f);
                            gizmoTransform[1] = glm::vec4(transformAxes[1], 0.0f);
                            gizmoTransform[2] = glm::vec4(transformAxes[2], 0.0f);
                        }
                        gizmoTransform[3] = glm::vec4(pivot, 1.0f);
                        glm::mat4 deltaTransform(1.0f);

                        // Use the current window draw list so ImGuizmo is rendered in the viewport context.
                        ImGuizmo::SetDrawlist();
                        ImGuizmo::BeginFrame();
                        ImGuizmo::Enable(true);
                        ImGuizmo::PushID(0);
                        ImGuizmo::SetOrthographic(m_ProjectionModeIndex == 1);
                        const float gizmoRectWidth = rectMax.x - rectMin.x;
                        const float gizmoRectHeight = rectMax.y - rectMin.y;
                        ImGuizmo::SetRect(rectMin.x, rectMin.y, gizmoRectWidth, gizmoRectHeight);
                        const glm::vec3 cameraDirection = glm::normalize(glm::vec3(
                            std::cos(m_Camera->GetPitch()) * std::sin(m_Camera->GetYaw()),
                            std::sin(m_Camera->GetPitch()),
                            std::cos(m_Camera->GetPitch()) * std::cos(m_Camera->GetYaw())));
                        const glm::vec3 cameraPosition = m_Camera->GetTarget() - cameraDirection * m_Camera->GetDistance();
                        const float distanceToPivot = glm::max(0.25f, glm::length(cameraPosition - pivot));
                        const float distanceCompensation = glm::clamp(6.0f / distanceToPivot, 0.55f, 1.85f);
                        const float gizmoSize = glm::clamp(m_TransformGizmoSize * distanceCompensation, 0.07f, 0.40f);
                        ImGuizmo::SetGizmoSizeClipSpace(gizmoSize);

                        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
                        if (m_RotateModeActive)
                        {
                            operation = ImGuizmo::ROTATE;
                        }
                        else if (m_GizmoOperationIndex == 1)
                        {
                            operation = ImGuizmo::ROTATE;
                        }
                        else if (m_GizmoOperationIndex == 2)
                        {
                            operation = ImGuizmo::SCALE;
                        }

                        ImGuizmo::MODE mode = (m_GizmoModeIndex == 1) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

                        float snapValues[3] = {m_GizmoTranslateSnap, m_GizmoTranslateSnap, m_GizmoTranslateSnap};
                        if (operation == ImGuizmo::ROTATE)
                        {
                            snapValues[0] = m_GizmoRotateSnapDeg;
                            snapValues[1] = m_GizmoRotateSnapDeg;
                            snapValues[2] = m_GizmoRotateSnapDeg;
                        }
                        else if (operation == ImGuizmo::SCALE)
                        {
                            snapValues[0] = m_GizmoScaleSnap;
                            snapValues[1] = m_GizmoScaleSnap;
                            snapValues[2] = m_GizmoScaleSnap;
                        }
                        const bool gizmoSnapActive = m_GizmoSnapEnabled || io.KeyCtrl;

                        const bool manipulated = ImGuizmo::Manipulate(
                            glm::value_ptr(m_Camera->GetViewMatrix()),
                            glm::value_ptr(m_Camera->GetProjectionMatrix()),
                            operation,
                            mode,
                            glm::value_ptr(gizmoTransform),
                            glm::value_ptr(deltaTransform),
                            gizmoSnapActive ? snapValues : nullptr);
                        ImGuizmo::PopID();

                        const bool gizmoOver = ImGuizmo::IsOver();
                        const bool gizmoUsing = ImGuizmo::IsUsing();
                        if (!s_GizmoStateInitialized || gizmoOver != s_LastGizmoOver || gizmoUsing != s_LastGizmoUsing || manipulated != s_LastGizmoManipulated)
                        {
                            std::ostringstream gizmoStateLog;
                            gizmoStateLog << "Gizmo state: over=" << (gizmoOver ? "1" : "0")
                                          << " using=" << (gizmoUsing ? "1" : "0")
                                          << " manipulated=" << (manipulated ? "1" : "0")
                                          << " size=" << gizmoSize
                                          << " distPivot=" << distanceToPivot
                                          << " op=" << m_GizmoOperationIndex
                                          << " mode=" << m_GizmoModeIndex;
                            AppendSelectionDebugLog(gizmoStateLog.str());
                            s_LastGizmoOver = gizmoOver;
                            s_LastGizmoUsing = gizmoUsing;
                            s_LastGizmoManipulated = manipulated;
                            s_GizmoStateInitialized = true;
                        }

                        if (gizmoOver || gizmoUsing)
                        {
                            m_GizmoConsumedMouseThisFrame = true;
                        }

                        if (gizmoUsing && !s_GizmoInteractionActive)
                        {
                            beginPendingTransformUndo(
                                (activeOperation == 1) ? "Rotate selection"
                                                       : ((activeOperation == 2) ? "Scale selection"
                                                                                 : "Translate selection"));
                            s_GizmoInteractionActive = true;
                        }

                        if (!gizmoOver && !gizmoUsing && pivotInViewport && !m_FallbackGizmoDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (hoveredFallbackAxis >= 0)
                            {
                                beginPendingTransformUndo(
                                    (activeOperation == 1) ? "Rotate selection"
                                                           : ((activeOperation == 2) ? "Scale selection"
                                                                                     : "Translate selection"));
                                m_FallbackGizmoDragging = true;
                                m_FallbackGizmoOperation = activeOperation;
                                m_FallbackGizmoAxis = hoveredFallbackAxis;
                                m_FallbackLastMousePos = glm::vec2(io.MousePos.x, io.MousePos.y);
                                m_FallbackDragAxisScreenDir = fallbackAxisScreenDir[hoveredFallbackAxis];
                                m_FallbackDragAxisWorldDir = transformAxes[hoveredFallbackAxis];
                                m_FallbackDragPixelsPerWorld = std::max(1.0f, fallbackPixelsPerWorld[hoveredFallbackAxis]);
                                m_FallbackDragAccumulated = 0.0f;
                                m_FallbackDragApplied = 0.0f;
                                m_FallbackRotateLastAngle = std::atan2(io.MousePos.y - pivotScreen.y, io.MousePos.x - pivotScreen.x);
                                m_GizmoConsumedMouseThisFrame = true;

                                std::ostringstream dragStartLog;
                                dragStartLog << "Fallback gizmo drag started: op=" << m_FallbackGizmoOperation
                                             << " axis=" << m_FallbackGizmoAxis
                                             << " pxPerWorld=" << m_FallbackDragPixelsPerWorld;
                                AppendSelectionDebugLog(dragStartLog.str());
                            }
                        }

                        if (m_FallbackGizmoDragging)
                        {
                            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                            {
                                const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                                const glm::vec2 delta = mousePos - m_FallbackLastMousePos;
                                m_FallbackLastMousePos = mousePos;

                                if (m_FallbackGizmoOperation == 1)
                                {
                                    const float currentAngle = std::atan2(mousePos.y - m_FallbackPivotScreen.y, mousePos.x - m_FallbackPivotScreen.x);
                                    float deltaAngle = NormalizeAngleRadians(currentAngle - m_FallbackRotateLastAngle);
                                    m_FallbackRotateLastAngle = currentAngle;

                                    if (m_GizmoSnapEnabled || io.KeyCtrl)
                                    {
                                        const float snapStep = glm::radians(std::max(0.1f, m_GizmoRotateSnapDeg));
                                        m_FallbackDragAccumulated += deltaAngle;
                                        const float snappedTarget = std::round(m_FallbackDragAccumulated / snapStep) * snapStep;
                                        deltaAngle = snappedTarget - m_FallbackDragApplied;
                                        m_FallbackDragApplied = snappedTarget;
                                    }

                                    if (std::abs(deltaAngle) > 1e-6f)
                                    {
                                        m_PendingTransformUndoDirty = true;
                                        const glm::vec3 axisWorld = glm::normalize(m_FallbackDragAxisWorldDir);
                                        const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), deltaAngle, axisWorld);
                                        if (hasSelectedEmpty)
                                        {
                                            TransformEmpty &selectedEmpty = m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)];
                                            selectedEmpty.axes[0] = glm::normalize(glm::vec3(rotation * glm::vec4(selectedEmpty.axes[0], 0.0f)));
                                            selectedEmpty.axes[1] = glm::normalize(glm::vec3(rotation * glm::vec4(selectedEmpty.axes[1], 0.0f)));
                                            selectedEmpty.axes[2] = glm::normalize(glm::cross(selectedEmpty.axes[0], selectedEmpty.axes[1]));
                                            selectedEmpty.axes[1] = glm::normalize(glm::cross(selectedEmpty.axes[2], selectedEmpty.axes[0]));
                                            m_LastStructureOperationFailed = false;
                                            m_LastStructureMessage = "Fallback rotate applied to selected empty.";
                                        }
                                        else if (hasSelectedLight)
                                        {
                                            m_LastStructureOperationFailed = false;
                                            m_LastStructureMessage = "Fallback rotate skipped for light (position-only helper).";
                                        }
                                        else
                                            for (std::size_t atomIndex : m_SelectedAtomIndices)
                                            {
                                                if (atomIndex >= m_WorkingStructure.atoms.size())
                                                {
                                                    continue;
                                                }

                                                const glm::vec3 atomCartesian = GetAtomCartesianPosition(atomIndex);
                                                const glm::vec3 relative = atomCartesian - pivot;
                                                const glm::vec3 rotated = glm::vec3(rotation * glm::vec4(relative, 0.0f));
                                                SetAtomCartesianPosition(atomIndex, pivot + rotated);
                                            }

                                        if (!hasSelectedEmpty && !hasSelectedLight)
                                        {
                                            m_LastStructureOperationFailed = false;
                                            m_LastStructureMessage = "Fallback rotate applied to selection (" + std::to_string(m_SelectedAtomIndices.size()) + " atoms).";
                                        }
                                    }
                                }
                                else
                                {
                                    const float deltaOnAxisPixels = glm::dot(delta, m_FallbackDragAxisScreenDir);
                                    const float deltaOnAxisWorld = deltaOnAxisPixels / std::max(1.0f, m_FallbackDragPixelsPerWorld);

                                    float worldToApply = deltaOnAxisWorld;
                                    if (m_GizmoSnapEnabled || io.KeyCtrl)
                                    {
                                        const float snapStep = (m_FallbackGizmoOperation == 2)
                                                                   ? std::max(0.001f, m_GizmoScaleSnap)
                                                                   : std::max(0.001f, m_GizmoTranslateSnap);
                                        m_FallbackDragAccumulated += deltaOnAxisWorld;
                                        const float snappedTarget = std::round(m_FallbackDragAccumulated / snapStep) * snapStep;
                                        worldToApply = snappedTarget - m_FallbackDragApplied;
                                        m_FallbackDragApplied = snappedTarget;
                                    }

                                    if (m_FallbackGizmoOperation == 2)
                                    {
                                        const float factor = glm::clamp(1.0f + worldToApply, 0.05f, 20.0f);
                                        if (std::abs(factor - 1.0f) > 1e-6f)
                                        {
                                            m_PendingTransformUndoDirty = true;
                                            if (hasSelectedEmpty)
                                            {
                                                TransformEmpty &selectedEmpty = m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)];
                                                selectedEmpty.position = pivot + (selectedEmpty.position - pivot) * factor;
                                                m_LastStructureOperationFailed = false;
                                                m_LastStructureMessage = "Fallback scale applied to selected empty.";
                                            }
                                            else if (hasSelectedLight)
                                            {
                                                m_LightPosition = pivot + (m_LightPosition - pivot) * factor;
                                                m_LastStructureOperationFailed = false;
                                                m_LastStructureMessage = "Fallback scale applied to light helper.";
                                            }
                                            else
                                                for (std::size_t atomIndex : m_SelectedAtomIndices)
                                                {
                                                    if (atomIndex >= m_WorkingStructure.atoms.size())
                                                    {
                                                        continue;
                                                    }

                                                    const glm::vec3 scaleAxis = glm::normalize(m_FallbackDragAxisWorldDir);
                                                    const glm::vec3 relative = GetAtomCartesianPosition(atomIndex) - pivot;
                                                    const float along = glm::dot(relative, scaleAxis);
                                                    const glm::vec3 perpendicular = relative - scaleAxis * along;
                                                    const glm::vec3 scaled = perpendicular + scaleAxis * (along * factor);
                                                    SetAtomCartesianPosition(atomIndex, pivot + scaled);
                                                }

                                            if (!hasSelectedEmpty && !hasSelectedLight)
                                            {
                                                m_LastStructureOperationFailed = false;
                                                m_LastStructureMessage = "Fallback scale applied to selection (" + std::to_string(m_SelectedAtomIndices.size()) + " atoms).";
                                            }
                                        }
                                    }
                                    else
                                    {
                                        const glm::vec3 worldDelta = m_FallbackDragAxisWorldDir * worldToApply;
                                        if (glm::length(worldDelta) > 0.000001f)
                                        {
                                            m_PendingTransformUndoDirty = true;
                                            if (hasSelectedEmpty)
                                            {
                                                TransformEmpty &selectedEmpty = m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)];
                                                selectedEmpty.position += worldDelta;
                                                m_LastStructureOperationFailed = false;
                                                m_LastStructureMessage = "Fallback translate applied to selected empty.";
                                            }
                                            else if (hasSelectedLight)
                                            {
                                                m_LightPosition += worldDelta;
                                                m_LastStructureOperationFailed = false;
                                                m_LastStructureMessage = "Fallback translate applied to light helper.";
                                            }
                                            else
                                                for (std::size_t atomIndex : m_SelectedAtomIndices)
                                                {
                                                    if (atomIndex >= m_WorkingStructure.atoms.size())
                                                    {
                                                        continue;
                                                    }

                                                    const glm::vec3 atomCartesian = GetAtomCartesianPosition(atomIndex);
                                                    SetAtomCartesianPosition(atomIndex, atomCartesian + worldDelta);
                                                }

                                            if (!hasSelectedEmpty && !hasSelectedLight)
                                            {
                                                m_LastStructureOperationFailed = false;
                                                m_LastStructureMessage = "Fallback gizmo drag applied to selection (" + std::to_string(m_SelectedAtomIndices.size()) + " atoms).";
                                            }
                                        }
                                    }
                                }

                                m_GizmoConsumedMouseThisFrame = true;
                            }
                            else
                            {
                                m_FallbackGizmoDragging = false;
                                m_FallbackGizmoOperation = -1;
                                m_FallbackGizmoAxis = -1;
                                m_FallbackDragAccumulated = 0.0f;
                                m_FallbackDragApplied = 0.0f;
                                m_FallbackRotateLastAngle = 0.0f;
                                if (m_PendingTransformUndoDirty)
                                {
                                    commitPendingTransformUndo();
                                }
                                else
                                {
                                    cancelPendingTransformUndo();
                                }
                                AppendSelectionDebugLog("Fallback axis drag ended");
                            }
                        }

                        if (manipulated && ImGuizmo::IsUsing())
                        {
                            m_PendingTransformUndoDirty = true;
                            if (hasSelectedEmpty)
                            {
                                TransformEmpty &selectedEmpty = m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)];
                                selectedEmpty.position = glm::vec3(gizmoTransform[3]);

                                if (operation == ImGuizmo::ROTATE)
                                {
                                    selectedEmpty.axes[0] = glm::normalize(glm::vec3(gizmoTransform[0]));
                                    selectedEmpty.axes[1] = glm::normalize(glm::vec3(gizmoTransform[1]));
                                    selectedEmpty.axes[2] = glm::normalize(glm::vec3(gizmoTransform[2]));
                                }

                                m_LastStructureOperationFailed = false;
                                m_LastStructureMessage = "Gizmo transform applied to selected empty.";
                            }
                            else if (hasSelectedLight)
                            {
                                m_LightPosition = glm::vec3(gizmoTransform[3]);
                                m_LastStructureOperationFailed = false;
                                m_LastStructureMessage = "Gizmo transform applied to light helper.";
                            }
                            else
                                for (std::size_t atomIndex : m_SelectedAtomIndices)
                                {
                                    if (atomIndex >= m_WorkingStructure.atoms.size())
                                    {
                                        continue;
                                    }

                                    const glm::vec3 atomCartesian = GetAtomCartesianPosition(atomIndex);
                                    const glm::vec3 transformed = glm::vec3(deltaTransform * glm::vec4(atomCartesian, 1.0f));
                                    SetAtomCartesianPosition(atomIndex, transformed);
                                }

                            if (!hasSelectedEmpty && !hasSelectedLight)
                            {
                                m_LastStructureOperationFailed = false;
                                m_LastStructureMessage = "Gizmo transform applied to selection (" + std::to_string(m_SelectedAtomIndices.size()) + " atoms).";
                            }
                        }

                        if (s_GizmoInteractionActive && !gizmoUsing)
                        {
                            if (m_PendingTransformUndoDirty)
                            {
                                commitPendingTransformUndo();
                            }
                            else
                            {
                                cancelPendingTransformUndo();
                            }
                            s_GizmoInteractionActive = false;
                        }
                    }
                    else if (s_LastValidPivot)
                    {
                        AppendSelectionDebugLog("Gizmo pivot unavailable: selected atoms are not valid indices.");
                        s_LastValidPivot = false;
                        if (s_GizmoInteractionActive)
                        {
                            cancelPendingTransformUndo();
                            s_GizmoInteractionActive = false;
                        }
                    }
                }
                else if (s_LastValidPivot)
                {
                    AppendSelectionDebugLog("Gizmo pivot reset: transform gizmo preconditions no longer met.");
                    s_LastValidPivot = false;
                    if (s_GizmoInteractionActive)
                    {
                        cancelPendingTransformUndo();
                        s_GizmoInteractionActive = false;
                    }
                    m_FallbackGizmoDragging = false;
                    m_FallbackGizmoOperation = -1;
                    m_FallbackGizmoAxis = -1;
                }

                if ((m_GizmoEnabled && (ImGuizmo::IsOver() || ImGuizmo::IsUsing())) ||
                    (m_ViewGuizmoEnabled && (ImViewGuizmo::IsOver() || ImViewGuizmo::IsUsing())))
                {
                    m_BlockSelectionThisFrame = true;
                    m_GizmoConsumedMouseThisFrame = true;
                }

                if (m_ViewGuizmoEnabled && m_Camera)
                {
                    ImViewGuizmo::BeginFrame();
                    ImViewGuizmo::GetStyle().scale = m_ViewGizmoScale;
                    ImViewGuizmo::GetStyle().axisColors[0] = ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[0].r, m_AxisColors[0].g, m_AxisColors[0].b, 1.0f));
                    ImViewGuizmo::GetStyle().axisColors[1] = ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[1].r, m_AxisColors[1].g, m_AxisColors[1].b, 1.0f));
                    ImViewGuizmo::GetStyle().axisColors[2] = ImGui::ColorConvertFloat4ToU32(ImVec4(m_AxisColors[2].r, m_AxisColors[2].g, m_AxisColors[2].b, 1.0f));

                    glm::vec3 target = m_Camera->GetTarget();
                    const float yaw = m_Camera->GetYaw();
                    const float pitch = m_Camera->GetPitch();
                    const float distance = m_Camera->GetDistance();

                    const glm::vec3 cameraDirection = glm::normalize(glm::vec3(
                        std::cos(pitch) * std::sin(yaw),
                        std::sin(pitch),
                        std::cos(pitch) * std::cos(yaw)));

                    glm::vec3 cameraPosition = target - cameraDirection * distance;
                    glm::quat cameraRotation = glm::quatLookAt(cameraDirection, glm::vec3(0.0f, 0.0f, 1.0f));

                    const float rotateWidgetWidth = 256.0f * m_ViewGizmoScale;
                    const ImVec2 rotatePos(rectMax.x - rotateWidgetWidth - m_ViewGizmoOffsetRight, rectMin.y + m_ViewGizmoOffsetTop);

                    if (m_ViewGizmoDragMode)
                    {
                        const ImVec2 dragMin = rotatePos;
                        const ImVec2 dragMax(rotatePos.x + rotateWidgetWidth, rotatePos.y + rotateWidgetWidth);
                        const bool mouseInDragZone =
                            io.MousePos.x >= dragMin.x && io.MousePos.x <= dragMax.x &&
                            io.MousePos.y >= dragMin.y && io.MousePos.y <= dragMax.y;

                        if (!m_ViewGizmoDragging && mouseInDragZone && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            m_ViewGizmoDragging = true;
                            m_ViewGizmoDragAnchor = glm::vec2(io.MousePos.x, io.MousePos.y);
                            m_ViewGizmoStartOffsetRight = m_ViewGizmoOffsetRight;
                            m_ViewGizmoStartOffsetTop = m_ViewGizmoOffsetTop;
                            m_BlockSelectionThisFrame = true;
                        }

                        if (m_ViewGizmoDragging)
                        {
                            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                            {
                                const glm::vec2 delta = glm::vec2(io.MousePos.x, io.MousePos.y) - m_ViewGizmoDragAnchor;
                                m_ViewGizmoOffsetRight = glm::max(0.0f, m_ViewGizmoStartOffsetRight - delta.x);
                                m_ViewGizmoOffsetTop = glm::max(0.0f, m_ViewGizmoStartOffsetTop + delta.y);
                                m_BlockSelectionThisFrame = true;
                                settingsChanged = true;
                            }
                            else
                            {
                                m_ViewGizmoDragging = false;
                            }
                        }
                    }

                    bool viewChanged = false;
                    viewChanged |= ImViewGuizmo::Rotate(cameraPosition, cameraRotation, target, rotatePos, 0.0125f);

                    if (viewChanged)
                    {
                        const glm::vec3 viewDir = glm::normalize(target - cameraPosition);
                        const float nextDistance = glm::max(0.5f, glm::length(target - cameraPosition));
                        const float nextYaw = std::atan2(viewDir.x, viewDir.y);
                        const float nextPitch = std::asin(glm::clamp(viewDir.z, -1.0f, 1.0f));
                        m_Camera->SetOrbitState(target, nextDistance, nextYaw, nextPitch);
                    }

                    if (ImViewGuizmo::IsOver() || ImViewGuizmo::IsUsing())
                    {
                        m_BlockSelectionThisFrame = true;
                        m_GizmoConsumedMouseThisFrame = true;
                    }
                }

                {
                    ImDrawList *overlayDraw = ImGui::GetWindowDrawList();

                    if (m_RenderCropEnabled)
                    {
                        const float xNorm = glm::clamp(m_RenderCropRectNormalized[0], 0.0f, 1.0f);
                        const float yNorm = glm::clamp(m_RenderCropRectNormalized[1], 0.0f, 1.0f);
                        const float wNorm = glm::clamp(m_RenderCropRectNormalized[2], 0.01f, 1.0f);
                        const float hNorm = glm::clamp(m_RenderCropRectNormalized[3], 0.01f, 1.0f);
                        const float x1Norm = glm::clamp(xNorm + wNorm, 0.0f, 1.0f);
                        const float y1Norm = glm::clamp(yNorm + hNorm, 0.0f, 1.0f);

                        const float viewportWidth = m_ViewportRectMax.x - m_ViewportRectMin.x;
                        const float viewportHeight = m_ViewportRectMax.y - m_ViewportRectMin.y;

                        const ImVec2 cropMin(
                            m_ViewportRectMin.x + viewportWidth * xNorm,
                            m_ViewportRectMin.y + viewportHeight * yNorm);
                        const ImVec2 cropMax(
                            m_ViewportRectMin.x + viewportWidth * x1Norm,
                            m_ViewportRectMin.y + viewportHeight * y1Norm);

                        overlayDraw->AddRectFilled(cropMin, cropMax, IM_COL32(125, 230, 255, 18), 2.0f);
                        overlayDraw->AddRect(cropMin, cropMax, IM_COL32(125, 230, 255, 230), 2.0f, 0, 2.0f);
                        overlayDraw->AddText(ImVec2(cropMin.x + 6.0f, cropMin.y + 6.0f), IM_COL32(200, 245, 255, 255), "Render area");
                    }

                    auto projectWorldToScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen) -> bool
                    {
                        const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(world, 1.0f);
                        if (clip.w <= 0.0001f)
                        {
                            return false;
                        }

                        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                        if (ndc.x < -1.05f || ndc.x > 1.05f || ndc.y < -1.05f || ndc.y > 1.05f || ndc.z < -1.0f || ndc.z > 1.0f)
                        {
                            return false;
                        }

                        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                        outScreen = glm::vec2(
                            m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                            m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
                        return true;
                    };

                    glm::vec2 originScreen(0.0f);
                    if (projectWorldToScreen(m_SceneOriginPosition, originScreen))
                    {
                        const ImU32 originColor = IM_COL32(235, 200, 65, 235);
                        overlayDraw->AddCircleFilled(ImVec2(originScreen.x, originScreen.y), 5.0f, originColor, 20);
                        overlayDraw->AddText(ImVec2(originScreen.x + 8.0f, originScreen.y - 8.0f), IM_COL32(242, 235, 205, 230), "Origin");
                    }

                    glm::vec2 lightScreen(0.0f);
                    if (projectWorldToScreen(m_LightPosition, lightScreen))
                    {
                        const ImU32 sunColor = ImGui::ColorConvertFloat4ToU32(
                            ImVec4(m_SceneSettings.lightColor.r, m_SceneSettings.lightColor.g, m_SceneSettings.lightColor.b, 0.95f));
                        overlayDraw->AddCircleFilled(ImVec2(lightScreen.x, lightScreen.y), 7.0f, sunColor, 20);
                        overlayDraw->AddCircle(ImVec2(lightScreen.x, lightScreen.y), 9.0f, IM_COL32(255, 255, 255, 220), 20, 1.2f);
                        overlayDraw->AddText(ImVec2(lightScreen.x + 10.0f, lightScreen.y - 8.0f), IM_COL32(235, 240, 248, 230), "Light");
                    }

                    if (m_ShowBondLengthLabels && m_Camera && !m_GeneratedBonds.empty())
                    {
                        struct LabelHitRegion
                        {
                            std::uint64_t key = 0;
                            ImVec2 min;
                            ImVec2 max;
                            float depthToCamera = 0.0f;
                        };

                        std::unordered_map<std::uint64_t, const BondSegment *> visibleBondByKey;
                        visibleBondByKey.reserve(m_GeneratedBonds.size());
                        for (const BondSegment &bond : m_GeneratedBonds)
                        {
                            const std::uint64_t key = MakeBondPairKey(bond.atomA, bond.atomB);
                            if (m_DeletedBondKeys.find(key) != m_DeletedBondKeys.end() ||
                                m_HiddenBondKeys.find(key) != m_HiddenBondKeys.end() ||
                                IsAtomHidden(bond.atomA) || IsAtomHidden(bond.atomB) ||
                                !IsAtomCollectionVisible(bond.atomA) || !IsAtomCollectionVisible(bond.atomB))
                            {
                                continue;
                            }

                            visibleBondByKey[key] = &bond;
                        }

                        const auto labelItems = BuildBondLabelLayout(
                            *m_Camera,
                            static_cast<std::uint32_t>(std::max(1.0f, m_ViewportSize.x)),
                            static_cast<std::uint32_t>(std::max(1.0f, m_ViewportSize.y)),
                            m_ViewportRectMin,
                            m_ViewportRectMax,
                            m_ShowBondLengthLabels,
                            1.0f,
                            m_BondLabelPrecision,
                            false,
                            {0.0f, 0.0f, 1.0f, 1.0f});

                        std::vector<LabelHitRegion> hitRegions;
                        hitRegions.reserve(labelItems.size());
                        ImFont *font = ImGuiLayer::GetBondLabelFont();
                        if (font == nullptr)
                        {
                            font = ImGui::GetFont();
                        }

                        const glm::vec3 finalTextColor = glm::clamp(m_BondLabelTextColor, glm::vec3(0.0f), glm::vec3(1.0f));
                        const glm::vec3 finalBgColor = glm::clamp(m_BondLabelBackgroundColor, glm::vec3(0.0f), glm::vec3(1.0f));
                        const glm::vec3 finalBorderColor = glm::clamp(m_BondLabelBorderColor, glm::vec3(0.0f), glm::vec3(1.0f));
                        const glm::vec3 selectedBgColor = glm::clamp(finalBgColor + glm::vec3(0.18f, 0.20f, 0.24f), glm::vec3(0.0f), glm::vec3(1.0f));
                        const ImU32 textColorNormal = ImGui::ColorConvertFloat4ToU32(ImVec4(finalTextColor.r, finalTextColor.g, finalTextColor.b, 0.96f));
                        const ImU32 textColorSelected = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.93f, 0.70f, 0.98f));
                        const ImU32 bgColorNormal = ImGui::ColorConvertFloat4ToU32(ImVec4(finalBgColor.r, finalBgColor.g, finalBgColor.b, 0.74f));
                        const ImU32 bgColorSelected = ImGui::ColorConvertFloat4ToU32(ImVec4(selectedBgColor.r, selectedBgColor.g, selectedBgColor.b, 0.86f));
                        const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(finalBorderColor.r, finalBorderColor.g, finalBorderColor.b, 0.92f));

                        for (const BondLabelLayoutItem &item : labelItems)
                        {
                            overlayDraw->AddRectFilled(
                                ImVec2(item.boxMin.x, item.boxMin.y),
                                ImVec2(item.boxMax.x, item.boxMax.y),
                                item.selected ? bgColorSelected : bgColorNormal,
                                2.0f);
                            overlayDraw->AddRect(
                                ImVec2(item.boxMin.x, item.boxMin.y),
                                ImVec2(item.boxMax.x, item.boxMax.y),
                                borderColor,
                                2.0f,
                                0,
                                item.selected ? 1.6f : 1.0f);
                            overlayDraw->AddText(
                                font,
                                item.fontSize,
                                ImVec2(item.textPos.x, item.textPos.y),
                                item.selected ? textColorSelected : textColorNormal,
                                item.text.c_str());

                            LabelHitRegion region;
                            region.key = item.key;
                            region.min = ImVec2(item.boxMin.x, item.boxMin.y);
                            region.max = ImVec2(item.boxMax.x, item.boxMax.y);
                            region.depthToCamera = item.depthToCamera;
                            hitRegions.push_back(region);
                        }

                        const bool canPickLabel =
                            (m_InteractionMode == InteractionMode::Select) && m_ViewportFocused && m_ViewportHovered &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImViewGuizmo::IsOver();
                        if (canPickLabel && !hitRegions.empty())
                        {
                            const ImVec2 mousePos = ImGui::GetIO().MousePos;
                            std::uint64_t pickedKey = 0;
                            float pickedDepth = std::numeric_limits<float>::max();
                            for (const LabelHitRegion &region : hitRegions)
                            {
                                const bool inside = mousePos.x >= region.min.x && mousePos.x <= region.max.x && mousePos.y >= region.min.y && mousePos.y <= region.max.y;
                                if (!inside)
                                {
                                    continue;
                                }

                                if (region.depthToCamera <= pickedDepth)
                                {
                                    pickedDepth = region.depthToCamera;
                                    pickedKey = region.key;
                                }
                            }

                            if (pickedKey != 0)
                            {
                                if (!io.KeyCtrl)
                                {
                                    m_SelectedBondKeys.clear();
                                }
                                if (io.KeyCtrl && m_SelectedBondKeys.find(pickedKey) != m_SelectedBondKeys.end())
                                {
                                    m_SelectedBondKeys.erase(pickedKey);
                                }
                                else
                                {
                                    m_SelectedBondKeys.insert(pickedKey);
                                }
                                m_SelectedBondLabelKey = pickedKey;
                                m_GizmoConsumedMouseThisFrame = true;
                                m_BlockSelectionThisFrame = true;
                            }
                        }

                        const bool hasActiveBondLabelSelection = (m_SelectedBondLabelKey != 0) || !m_SelectedBondKeys.empty();
                        if (m_BondLabelGizmoEnabled && hasActiveBondLabelSelection)
                        {
                            std::vector<std::uint64_t> requestedKeys;
                            requestedKeys.reserve(m_SelectedBondKeys.size() + 1);
                            if (m_SelectedBondLabelKey != 0)
                            {
                                requestedKeys.push_back(m_SelectedBondLabelKey);
                            }
                            for (std::uint64_t key : m_SelectedBondKeys)
                            {
                                if (key != m_SelectedBondLabelKey)
                                {
                                    requestedKeys.push_back(key);
                                }
                            }

                            std::vector<std::uint64_t> activeKeys;
                            activeKeys.reserve(requestedKeys.size());
                            for (std::uint64_t key : requestedKeys)
                            {
                                auto bondIt = visibleBondByKey.find(key);
                                if (bondIt == visibleBondByKey.end())
                                {
                                    continue;
                                }

                                BondLabelState &state = m_BondLabelStates[key];
                                if (state.hidden || state.deleted)
                                {
                                    continue;
                                }

                                activeKeys.push_back(key);
                            }

                            if (!activeKeys.empty())
                            {
                                glm::vec3 pivot(0.0f);
                                for (std::uint64_t key : activeKeys)
                                {
                                    const BondSegment &bond = *visibleBondByKey[key];
                                    const BondLabelState &state = m_BondLabelStates[key];
                                    pivot += bond.midpoint + state.worldOffset;
                                }
                                pivot /= static_cast<float>(activeKeys.size());

                                glm::mat4 labelTransform(1.0f);
                                labelTransform[3] = glm::vec4(pivot, 1.0f);
                                glm::mat4 deltaTransform(1.0f);

                                ImGuizmo::SetDrawlist();
                                ImGuizmo::BeginFrame();
                                ImGuizmo::PushID(9341);
                                ImGuizmo::SetOrthographic(m_ProjectionModeIndex == 1);
                                ImGuizmo::SetRect(rectMin.x, rectMin.y, rectMax.x - rectMin.x, rectMax.y - rectMin.y);
                                ImGuizmo::SetGizmoSizeClipSpace(glm::clamp(m_TransformGizmoSize * 0.92f, 0.07f, 0.40f));

                                const bool labelManipulated = ImGuizmo::Manipulate(
                                    glm::value_ptr(m_Camera->GetViewMatrix()),
                                    glm::value_ptr(m_Camera->GetProjectionMatrix()),
                                    ImGuizmo::TRANSLATE,
                                    ImGuizmo::WORLD,
                                    glm::value_ptr(labelTransform),
                                    glm::value_ptr(deltaTransform),
                                    nullptr);
                                ImGuizmo::PopID();

                                if (ImGuizmo::IsOver() || ImGuizmo::IsUsing())
                                {
                                    m_GizmoConsumedMouseThisFrame = true;
                                    m_BlockSelectionThisFrame = true;
                                }

                                if (labelManipulated)
                                {
                                    const glm::vec3 transformedPivot = glm::vec3(labelTransform[3]);
                                    const glm::vec3 translationDelta = transformedPivot - pivot;
                                    for (std::uint64_t key : activeKeys)
                                    {
                                        BondLabelState &state = m_BondLabelStates[key];
                                        state.worldOffset += translationDelta;
                                    }
                                    settingsChanged = true;
                                }
                            }
                        }
                    }

                    if (m_Camera)
                    {
                        const int precision = std::clamp(m_MeasurementPrecision, 0, 6);
                        const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
                            glm::clamp(m_MeasurementTextColor.r, 0.0f, 1.0f),
                            glm::clamp(m_MeasurementTextColor.g, 0.0f, 1.0f),
                            glm::clamp(m_MeasurementTextColor.b, 0.0f, 1.0f),
                            0.97f));
                        const ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
                            glm::clamp(m_MeasurementBackgroundColor.r, 0.0f, 1.0f),
                            glm::clamp(m_MeasurementBackgroundColor.g, 0.0f, 1.0f),
                            glm::clamp(m_MeasurementBackgroundColor.b, 0.0f, 1.0f),
                            0.82f));
                        const ImU32 angleGuideColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
                            glm::clamp(m_BondSelectedColor.r, 0.0f, 1.0f),
                            glm::clamp(m_BondSelectedColor.g, 0.0f, 1.0f),
                            glm::clamp(m_BondSelectedColor.b, 0.0f, 1.0f),
                            0.90f));

                        auto drawMeasurementLabelAtScreen = [&](const glm::vec2 &screenPos, const std::string &text)
                        {
                            const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
                            const ImVec2 pos(screenPos.x - textSize.x * 0.5f, screenPos.y - 12.0f - textSize.y);
                            overlayDraw->AddRectFilled(
                                ImVec2(pos.x - 4.0f, pos.y - 3.0f),
                                ImVec2(pos.x + textSize.x + 4.0f, pos.y + textSize.y + 3.0f),
                                bgColor,
                                2.0f);
                            overlayDraw->AddText(pos, textColor, text.c_str());
                        };

                        auto drawMeasurementLabelAtWorld = [&](const glm::vec3 &worldPos, const std::string &text)
                        {
                            glm::vec2 screen(0.0f);
                            if (!projectWorldToScreen(worldPos, screen))
                            {
                                return;
                            }
                            drawMeasurementLabelAtScreen(screen, text);
                        };

                        if (m_ShowStaticAngleLabels && !m_AngleLabelStates.empty())
                        {
                            for (auto it = m_AngleLabelStates.begin(); it != m_AngleLabelStates.end();)
                            {
                                const AngleLabelState &state = it->second;
                                if (state.atomA >= m_WorkingStructure.atoms.size() ||
                                    state.atomB >= m_WorkingStructure.atoms.size() ||
                                    state.atomC >= m_WorkingStructure.atoms.size() ||
                                    state.atomA == state.atomB ||
                                    state.atomB == state.atomC ||
                                    state.atomA == state.atomC)
                                {
                                    it = m_AngleLabelStates.erase(it);
                                    continue;
                                }

                                if (IsAtomHidden(state.atomA) || IsAtomHidden(state.atomB) || IsAtomHidden(state.atomC) ||
                                    !IsAtomCollectionVisible(state.atomA) || !IsAtomCollectionVisible(state.atomB) || !IsAtomCollectionVisible(state.atomC))
                                {
                                    ++it;
                                    continue;
                                }

                                const glm::vec3 posA = GetAtomCartesianPosition(state.atomA);
                                const glm::vec3 posB = GetAtomCartesianPosition(state.atomB);
                                const glm::vec3 posC = GetAtomCartesianPosition(state.atomC);
                                const glm::vec3 ba = posA - posB;
                                const glm::vec3 bc = posC - posB;
                                if (glm::length2(ba) <= 1e-10f || glm::length2(bc) <= 1e-10f)
                                {
                                    ++it;
                                    continue;
                                }

                                glm::vec2 screenA(0.0f);
                                glm::vec2 screenB(0.0f);
                                glm::vec2 screenC(0.0f);
                                if (!projectWorldToScreen(posA, screenA) ||
                                    !projectWorldToScreen(posB, screenB) ||
                                    !projectWorldToScreen(posC, screenC))
                                {
                                    ++it;
                                    continue;
                                }

                                const glm::vec2 dirA = screenA - screenB;
                                const glm::vec2 dirC = screenC - screenB;
                                const float lenA = glm::length(dirA);
                                const float lenC = glm::length(dirC);
                                if (lenA <= 1e-4f || lenC <= 1e-4f)
                                {
                                    ++it;
                                    continue;
                                }

                                const glm::vec2 normA = dirA / lenA;
                                const glm::vec2 normC = dirC / lenC;
                                const float rayLength = std::min(std::min(lenA, lenC) * 0.55f, 80.0f);
                                const glm::vec2 rayAEnd = screenB + normA * rayLength;
                                const glm::vec2 rayCEnd = screenB + normC * rayLength;
                                overlayDraw->AddLine(ImVec2(screenB.x, screenB.y), ImVec2(rayAEnd.x, rayAEnd.y), angleGuideColor, 1.5f);
                                overlayDraw->AddLine(ImVec2(screenB.x, screenB.y), ImVec2(rayCEnd.x, rayCEnd.y), angleGuideColor, 1.5f);

                                const float arcRadius = glm::clamp(rayLength * 0.55f, 12.0f, 56.0f);
                                const float startAngle = std::atan2(normA.y, normA.x);
                                const float deltaAngle = NormalizeAngleRadians(std::atan2(normC.y, normC.x) - startAngle);
                                const int arcSegments = 24;
                                std::vector<ImVec2> arcPoints;
                                arcPoints.reserve(static_cast<std::size_t>(arcSegments) + 1);
                                for (int segment = 0; segment <= arcSegments; ++segment)
                                {
                                    const float t = static_cast<float>(segment) / static_cast<float>(arcSegments);
                                    const float a = startAngle + deltaAngle * t;
                                    arcPoints.emplace_back(screenB.x + std::cos(a) * arcRadius, screenB.y + std::sin(a) * arcRadius);
                                }
                                if (!arcPoints.empty())
                                {
                                    overlayDraw->AddPolyline(arcPoints.data(), static_cast<int>(arcPoints.size()), angleGuideColor, false, 1.5f);
                                }

                                const float cosTheta = glm::clamp(glm::dot(glm::normalize(ba), glm::normalize(bc)), -1.0f, 1.0f);
                                const float angleDeg = glm::degrees(std::acos(cosTheta));
                                std::ostringstream label;
                                label << "angle = " << std::fixed << std::setprecision(precision) << angleDeg << " deg";

                                glm::vec2 bisector = normA + normC;
                                if (glm::length2(bisector) <= 1e-6f)
                                {
                                    bisector = normA;
                                }
                                else
                                {
                                    bisector = glm::normalize(bisector);
                                }
                                drawMeasurementLabelAtScreen(screenB + bisector * (arcRadius + 14.0f), label.str());

                                ++it;
                            }
                        }

                        if (m_ShowSelectionMeasurements && !m_SelectedAtomIndices.empty())
                        {
                            if (m_ShowSelectionDistanceMeasurement && m_SelectedAtomIndices.size() >= 2)
                            {
                                const std::size_t atomA = m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 2];
                                const std::size_t atomB = m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 1];
                                if (atomA < m_WorkingStructure.atoms.size() && atomB < m_WorkingStructure.atoms.size() && atomA != atomB)
                                {
                                    const glm::vec3 posA = GetAtomCartesianPosition(atomA);
                                    const glm::vec3 posB = GetAtomCartesianPosition(atomB);
                                    const float distance = glm::length(posB - posA);
                                    std::ostringstream label;
                                    label << "d = " << std::fixed << std::setprecision(precision) << distance << " A";
                                    drawMeasurementLabelAtWorld((posA + posB) * 0.5f, label.str());
                                }
                            }

                            if (m_ShowSelectionAngleMeasurement && m_SelectedAtomIndices.size() >= 3)
                            {
                                const std::size_t atomA = m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 3];
                                const std::size_t atomB = m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 2];
                                const std::size_t atomC = m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 1];
                                if (atomA < m_WorkingStructure.atoms.size() && atomB < m_WorkingStructure.atoms.size() && atomC < m_WorkingStructure.atoms.size() &&
                                    atomA != atomB && atomB != atomC && atomA != atomC)
                                {
                                    const glm::vec3 posA = GetAtomCartesianPosition(atomA);
                                    const glm::vec3 posB = GetAtomCartesianPosition(atomB);
                                    const glm::vec3 posC = GetAtomCartesianPosition(atomC);
                                    const glm::vec3 ba = posA - posB;
                                    const glm::vec3 bc = posC - posB;
                                    if (glm::length2(ba) > 1e-10f && glm::length2(bc) > 1e-10f)
                                    {
                                        const float cosTheta = glm::clamp(glm::dot(glm::normalize(ba), glm::normalize(bc)), -1.0f, 1.0f);
                                        const float angleDeg = glm::degrees(std::acos(cosTheta));
                                        std::ostringstream label;
                                        label << "angle = " << std::fixed << std::setprecision(precision) << angleDeg << " deg";
                                        drawMeasurementLabelAtWorld(posB, label.str());
                                    }
                                }
                            }
                        }
                    }
                }

                if (m_ReopenViewportSelectionContextMenu)
                {
                    ImGui::OpenPopup("ViewportSelectionContext");
                    m_ReopenViewportSelectionContextMenu = false;
                }

                if (ImGui::BeginPopupContextItem("ViewportSelectionContext", ImGuiPopupFlags_MouseButtonRight))
                {
                    const bool hasLoadedAtoms = m_HasStructureLoaded && !m_WorkingStructure.atoms.empty();
                    const glm::vec2 contextMouse(io.MousePos.x, io.MousePos.y);
                    const bool hasSelectedEmpty =
                        (m_SelectedTransformEmptyIndex >= 0 && m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size()));
                    const bool hasSelectedLight = (m_SelectedSpecialNode == SpecialNodeSelection::Light);
                    const bool canCopySelection = !m_SelectedAtomIndices.empty() || hasSelectedEmpty;
                    const bool canPasteSelection = HasClipboardPayload();
                    const bool canDuplicateSelection = canCopySelection;

                    if (ImGui::MenuItem("Copy", "Ctrl+C", false, canCopySelection))
                    {
                        CopyCurrentSelectionToClipboard();
                    }
                    if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPasteSelection))
                    {
                        settingsChanged |= PasteClipboard();
                    }
                    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, canDuplicateSelection))
                    {
                        settingsChanged |= DuplicateCurrentSelection();
                    }
                    if (ImGui::MenuItem("Extract to New Collection", nullptr, false, canCopySelection))
                    {
                        settingsChanged |= ExtractSelectionToNewCollection();
                    }
                    if (ImGui::MenuItem("Delete selection", nullptr, false, canDuplicateSelection || !m_SelectedBondKeys.empty()))
                    {
                        deleteCurrentSelection();
                    }
                    if (ImGui::MenuItem("Hide selection", nullptr, false, !m_SelectedAtomIndices.empty() || !m_SelectedBondKeys.empty() || m_SelectedBondLabelKey != 0))
                    {
                        hideCurrentSelection();
                    }
                    ImGui::Separator();

                    if (!m_SelectedAtomIndices.empty())
                    {
                        ImGui::SeparatorText("Quick change atom type");
                        ImGui::SetNextItemWidth(92.0f);
                        ImGui::InputText("##CtxQuickAtomType", m_ChangeAtomElementBuffer.data(), m_ChangeAtomElementBuffer.size());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Table"))
                        {
                            OpenPeriodicTable(PeriodicTableTarget::ChangeSelectedAtoms, true);
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Apply"))
                        {
                            if (ApplyElementToSelectedAtoms(std::string(m_ChangeAtomElementBuffer.data())))
                            {
                                settingsChanged = true;
                                AppendSelectionDebugLog("Context menu: Quick changed selected atom type");
                            }
                        }
                        ImGui::Separator();
                    }

                    if (ImGui::MenuItem("Select All", nullptr, false, hasLoadedAtoms))
                    {
                        settingsChanged |= SelectAllVisibleByCurrentFilter();
                    }

                    if (ImGui::MenuItem("Clear Selection", nullptr, false, !m_SelectedAtomIndices.empty() || m_SelectedTransformEmptyIndex >= 0))
                    {
                        m_SelectedAtomIndices.clear();
                        m_SelectedBondKeys.clear();
                        m_SelectedBondLabelKey = 0;
                        m_SelectedTransformEmptyIndex = -1;
                        m_SelectedSpecialNode = SpecialNodeSelection::None;
                        AppendSelectionDebugLog("Context menu: Clear Selection");
                    }

                    if (ImGui::MenuItem("Invert Selection", nullptr, false, hasLoadedAtoms))
                    {
                        std::vector<std::size_t> inverted;
                        inverted.reserve(m_WorkingStructure.atoms.size());
                        for (std::size_t i = 0; i < m_WorkingStructure.atoms.size(); ++i)
                        {
                            if (!IsAtomHidden(i) && IsAtomCollectionVisible(i) && IsAtomCollectionSelectable(i) && !IsAtomSelected(i))
                            {
                                inverted.push_back(i);
                            }
                        }
                        m_SelectedAtomIndices = std::move(inverted);
                        m_SelectedBondKeys.clear();
                        m_SelectedBondLabelKey = 0;
                        m_SelectedTransformEmptyIndex = -1;
                        m_SelectedSpecialNode = SpecialNodeSelection::None;
                        AppendSelectionDebugLog("Context menu: Invert Selection");
                    }

                    if (ImGui::MenuItem("Frame Selected", nullptr, false, !m_SelectedAtomIndices.empty() && m_Camera))
                    {
                        glm::vec3 boundsMin(std::numeric_limits<float>::max());
                        glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

                        for (std::size_t atomIndex : m_SelectedAtomIndices)
                        {
                            if (atomIndex >= m_WorkingStructure.atoms.size())
                            {
                                continue;
                            }

                            glm::vec3 position = m_WorkingStructure.atoms[atomIndex].position;
                            if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
                            {
                                position = m_WorkingStructure.DirectToCartesian(position);
                            }

                            boundsMin = glm::min(boundsMin, position);
                            boundsMax = glm::max(boundsMax, position);
                        }

                        m_Camera->FrameBounds(boundsMin, boundsMax);
                        AppendSelectionDebugLog("Context menu: Frame Selected");
                    }

                    if (ImGui::MenuItem("Set 3D Cursor Here (grid plane)", nullptr, false, m_Camera != nullptr))
                    {
                        Set3DCursorFromScreenPoint(contextMouse);
                        AppendSelectionDebugLog("Context menu: Set 3D Cursor Here");
                        settingsChanged = true;
                    }

                    if (ImGui::BeginMenu("Move 3D Cursor To"))
                    {
                        if (ImGui::MenuItem("Selection Center of Mass", nullptr, false, !m_SelectedAtomIndices.empty()))
                        {
                            if (Set3DCursorToSelectionCenterOfMass())
                            {
                                AppendSelectionDebugLog("Context menu: Cursor -> Selection COM");
                                settingsChanged = true;
                            }
                        }

                        if (ImGui::MenuItem("First Selected", nullptr, false, !m_SelectedAtomIndices.empty()))
                        {
                            if (Set3DCursorToSelectedAtom(false))
                            {
                                AppendSelectionDebugLog("Context menu: Cursor -> First Selected");
                                settingsChanged = true;
                            }
                        }

                        if (ImGui::MenuItem("Last Selected", nullptr, false, !m_SelectedAtomIndices.empty()))
                        {
                            if (Set3DCursorToSelectedAtom(true))
                            {
                                AppendSelectionDebugLog("Context menu: Cursor -> Last Selected");
                                settingsChanged = true;
                            }
                        }

                        if (ImGui::MenuItem("Origin", nullptr, false, true))
                        {
                            m_CursorPosition = m_SceneOriginPosition;
                            AppendSelectionDebugLog("Context menu: Cursor -> Origin");
                            settingsChanged = true;
                        }

                        if (ImGui::MenuItem("Active Empty", nullptr, false, HasActiveTransformEmpty()))
                        {
                            m_CursorPosition = m_TransformEmpties[static_cast<std::size_t>(m_ActiveTransformEmptyIndex)].position;
                            AppendSelectionDebugLog("Context menu: Cursor -> Active Empty");
                            settingsChanged = true;
                        }

                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Transform Empty"))
                    {
                        if (ImGui::MenuItem("Add at 3D cursor", nullptr, false, m_HasStructureLoaded))
                        {
                            PushUndoSnapshot("Add empty at 3D cursor");
                            TransformEmpty empty;
                            empty.id = GenerateSceneUUID();
                            empty.position = m_CursorPosition;
                            empty.collectionIndex = m_ActiveCollectionIndex;
                            empty.collectionId = m_Collections[static_cast<std::size_t>(m_ActiveCollectionIndex)].id;
                            char label[32] = {};
                            std::snprintf(label, sizeof(label), "Empty %d", m_TransformEmptyCounter++);
                            empty.name = label;
                            m_TransformEmpties.push_back(empty);
                            m_ActiveTransformEmptyIndex = static_cast<int>(m_TransformEmpties.size()) - 1;
                            m_SelectedTransformEmptyIndex = m_ActiveTransformEmptyIndex;
                            AppendSelectionDebugLog("Context menu: Add Empty at 3D cursor");
                        }

                        if (ImGui::MenuItem("Add at selection center", nullptr, false, m_HasStructureLoaded && !m_SelectedAtomIndices.empty()))
                        {
                            PushUndoSnapshot("Add empty at selection center");
                            TransformEmpty empty;
                            empty.id = GenerateSceneUUID();
                            empty.position = ComputeSelectionCenter();
                            ComputeSelectionAxesAround(empty.position, empty.axes);
                            empty.collectionIndex = m_ActiveCollectionIndex;
                            empty.collectionId = m_Collections[static_cast<std::size_t>(m_ActiveCollectionIndex)].id;
                            char label[32] = {};
                            std::snprintf(label, sizeof(label), "Empty %d", m_TransformEmptyCounter++);
                            empty.name = label;
                            m_TransformEmpties.push_back(empty);
                            m_ActiveTransformEmptyIndex = static_cast<int>(m_TransformEmpties.size()) - 1;
                            m_SelectedTransformEmptyIndex = m_ActiveTransformEmptyIndex;
                            AppendSelectionDebugLog("Context menu: Add Empty at selection center");
                        }

                        if (ImGui::MenuItem("Use active empty as tmp transform", nullptr, false, HasActiveTransformEmpty()))
                        {
                            m_UseTemporaryLocalAxes = true;
                            m_TemporaryAxesSource = TemporaryAxesSource::ActiveEmpty;
                            m_GizmoModeIndex = 0;
                            m_CursorPosition = m_TransformEmpties[static_cast<std::size_t>(m_ActiveTransformEmptyIndex)].position;
                            settingsChanged = true;
                            AppendSelectionDebugLog("Context menu: Active Empty -> temporary transform");
                        }

                        if (ImGui::MenuItem("Select active empty", nullptr, false, HasActiveTransformEmpty()))
                        {
                            m_SelectedTransformEmptyIndex = m_ActiveTransformEmptyIndex;
                            m_SelectedAtomIndices.clear();
                            AppendSelectionDebugLog("Context menu: Select active empty");
                        }

                        if (ImGui::MenuItem("Move active empty to 3D cursor", nullptr, false, HasActiveTransformEmpty()))
                        {
                            PushUndoSnapshot("Move active empty to 3D cursor");
                            m_TransformEmpties[static_cast<std::size_t>(m_ActiveTransformEmptyIndex)].position = m_CursorPosition;
                            settingsChanged = true;
                            AppendSelectionDebugLog("Context menu: Move active empty -> 3D cursor");
                        }

                        if (ImGui::MenuItem("Move active empty to selection center", nullptr, false, HasActiveTransformEmpty() && !m_SelectedAtomIndices.empty()))
                        {
                            PushUndoSnapshot("Move active empty to selection center");
                            m_TransformEmpties[static_cast<std::size_t>(m_ActiveTransformEmptyIndex)].position = ComputeSelectionCenter();
                            settingsChanged = true;
                            AppendSelectionDebugLog("Context menu: Move active empty -> selection center");
                        }

                        if (ImGui::MenuItem("Align active empty axes to world", nullptr, false, HasActiveTransformEmpty()))
                        {
                            PushUndoSnapshot("Align active empty axes to world");
                            TransformEmpty &activeEmpty = m_TransformEmpties[static_cast<std::size_t>(m_ActiveTransformEmptyIndex)];
                            activeEmpty.axes = {
                                glm::vec3(1.0f, 0.0f, 0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f),
                                glm::vec3(0.0f, 0.0f, 1.0f)};
                            settingsChanged = true;
                            AppendSelectionDebugLog("Context menu: Align active empty axes -> world");
                        }

                        if (ImGui::MenuItem("Align active empty axes to camera view", nullptr, false, HasActiveTransformEmpty()))
                        {
                            PushUndoSnapshot("Align active empty axes to camera view");
                            if (AlignEmptyAxesToCameraView(m_ActiveTransformEmptyIndex))
                            {
                                settingsChanged = true;
                                AppendSelectionDebugLog("Context menu: Align active empty axes -> camera view");
                            }
                        }

                        if (ImGui::MenuItem("Align active empty Z to selected atoms", nullptr, false, HasActiveTransformEmpty() && m_SelectedAtomIndices.size() >= 2))
                        {
                            PushUndoSnapshot("Align active empty Z from selected atoms");
                            if (AlignEmptyZAxisFromSelectedAtoms(m_ActiveTransformEmptyIndex))
                            {
                                settingsChanged = true;
                                AppendSelectionDebugLog("Context menu: Align active empty Z -> selected atoms");
                            }
                        }

                        if (ImGui::MenuItem("Delete active empty", nullptr, false, HasActiveTransformEmpty()))
                        {
                            PushUndoSnapshot("Delete active empty");
                            DeleteTransformEmptyAtIndex(m_ActiveTransformEmptyIndex);
                            AppendSelectionDebugLog("Context menu: Delete active empty");
                        }

                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Groups"))
                    {
                        const bool hasSelection = !m_SelectedAtomIndices.empty() || m_SelectedTransformEmptyIndex >= 0;
                        if (ImGui::MenuItem("Create group from selection", nullptr, false, hasSelection))
                        {
                            PushUndoSnapshot("Create group from selection");
                            settingsChanged |= SceneGroupingBackend::CreateGroupFromCurrentSelection(*this);
                        }

                        if (ImGui::MenuItem("Add selection to active group", nullptr, false, hasSelection && m_ActiveGroupIndex >= 0))
                        {
                            PushUndoSnapshot("Add selection to group");
                            SceneGroupingBackend::AddCurrentSelectionToGroup(*this, m_ActiveGroupIndex);
                            settingsChanged = true;
                        }

                        if (ImGui::MenuItem("Remove selection from active group", nullptr, false, hasSelection && m_ActiveGroupIndex >= 0))
                        {
                            PushUndoSnapshot("Remove selection from group");
                            SceneGroupingBackend::RemoveCurrentSelectionFromGroup(*this, m_ActiveGroupIndex);
                            settingsChanged = true;
                        }

                        if (ImGui::MenuItem("Select active group", nullptr, false, m_ActiveGroupIndex >= 0))
                        {
                            SceneGroupingBackend::SelectGroup(*this, m_ActiveGroupIndex);
                        }

                        if (ImGui::MenuItem("Delete active group", nullptr, false, m_ActiveGroupIndex >= 0))
                        {
                            PushUndoSnapshot("Delete group");
                            settingsChanged |= SceneGroupingBackend::DeleteGroup(*this, m_ActiveGroupIndex);
                        }

                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Selection Utilities"))
                    {
                        if (ImGui::BeginMenu("Change selected atom type", !m_SelectedAtomIndices.empty()))
                        {
                            ImGui::SetNextItemWidth(110.0f);
                            ImGui::InputText("Element", m_ChangeAtomElementBuffer.data(), m_ChangeAtomElementBuffer.size());

                            if (ImGui::MenuItem("Periodic table..."))
                            {
                                OpenPeriodicTable(PeriodicTableTarget::ChangeSelectedAtoms, true);
                            }

                            if (ImGui::MenuItem("Apply"))
                            {
                                if (ApplyElementToSelectedAtoms(std::string(m_ChangeAtomElementBuffer.data())))
                                {
                                    settingsChanged = true;
                                    AppendSelectionDebugLog("Context menu: Changed selected atom type");
                                }
                            }

                            ImGui::EndMenu();
                        }

                        if (ImGui::MenuItem("Move selected objects to 3D cursor", nullptr, false, !m_SelectedAtomIndices.empty() || hasSelectedEmpty || hasSelectedLight))
                        {
                            PushUndoSnapshot("Move selection to 3D cursor");
                            if (hasSelectedEmpty && m_SelectedAtomIndices.empty())
                            {
                                m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].position = m_CursorPosition;
                            }
                            else if (hasSelectedLight && m_SelectedAtomIndices.empty())
                            {
                                m_LightPosition = m_CursorPosition;
                            }
                            else
                            {
                                const glm::vec3 selectionCenter = ComputeSelectionCenter();
                                const glm::vec3 delta = m_CursorPosition - selectionCenter;
                                for (const std::size_t atomIndex : m_SelectedAtomIndices)
                                {
                                    if (atomIndex >= m_WorkingStructure.atoms.size())
                                    {
                                        continue;
                                    }
                                    SetAtomCartesianPosition(atomIndex, GetAtomCartesianPosition(atomIndex) + delta);
                                }
                            }
                            settingsChanged = true;
                            AppendSelectionDebugLog("Context menu: Selection center -> 3D cursor");
                        }

                        if (ImGui::MenuItem("Select active group", nullptr, false, m_ActiveGroupIndex >= 0))
                        {
                            SceneGroupingBackend::SelectGroup(*this, m_ActiveGroupIndex);
                            AppendSelectionDebugLog("Context menu: Select active group");
                        }

                        if (ImGui::MenuItem("Move selected to Origin", nullptr, false, !m_SelectedAtomIndices.empty() || hasSelectedEmpty || hasSelectedLight))
                        {
                            PushUndoSnapshot("Move selection to origin");
                            if (hasSelectedEmpty && m_SelectedAtomIndices.empty())
                            {
                                m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].position = m_SceneOriginPosition;
                            }
                            else if (hasSelectedLight && m_SelectedAtomIndices.empty())
                            {
                                m_LightPosition = m_SceneOriginPosition;
                            }
                            else
                            {
                                const glm::vec3 selectionCenter = ComputeSelectionCenter();
                                const glm::vec3 delta = m_SceneOriginPosition - selectionCenter;
                                for (const std::size_t atomIndex : m_SelectedAtomIndices)
                                {
                                    if (atomIndex >= m_WorkingStructure.atoms.size())
                                    {
                                        continue;
                                    }
                                    SetAtomCartesianPosition(atomIndex, GetAtomCartesianPosition(atomIndex) + delta);
                                }
                            }
                            settingsChanged = true;
                            AppendSelectionDebugLog("Context menu: Selection center -> Origin");
                        }

                        ImGui::EndMenu();
                    }

                    ImGui::EndPopup();
                }

                ImGui::SetNextWindowPos(ImVec2(m_AddMenuPopupPos.x, m_AddMenuPopupPos.y), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
                if (ImGui::BeginPopup("AddSceneObjectPopup"))
                {
                    if (ImGui::MenuItem("Atom...", "Shift+A"))
                    {
                        m_AddAtomPosition = m_CursorPosition;
                        m_AddAtomCoordinateModeIndex = 1;
                        m_ShowAddAtomDialog = true;
                    }

                    if (ImGui::MenuItem("Empty at 3D cursor", "Shift+A"))
                    {
                        TransformEmpty empty;
                        empty.id = GenerateSceneUUID();
                        empty.position = m_CursorPosition;
                        empty.collectionIndex = m_ActiveCollectionIndex;
                        empty.collectionId = m_Collections[static_cast<std::size_t>(m_ActiveCollectionIndex)].id;
                        char label[32] = {};
                        std::snprintf(label, sizeof(label), "Empty %d", m_TransformEmptyCounter++);
                        empty.name = label;
                        m_TransformEmpties.push_back(empty);
                        m_ActiveTransformEmptyIndex = static_cast<int>(m_TransformEmpties.size()) - 1;
                        m_SelectedTransformEmptyIndex = m_ActiveTransformEmptyIndex;
                        settingsChanged = true;
                    }

                    const bool canAddFromSelection = m_HasStructureLoaded && !m_SelectedAtomIndices.empty();
                    if (ImGui::MenuItem("Empty at selection center", "Shift+A", false, canAddFromSelection))
                    {
                        TransformEmpty empty;
                        empty.id = GenerateSceneUUID();
                        empty.position = ComputeSelectionCenter();
                        ComputeSelectionAxesAround(empty.position, empty.axes);
                        empty.collectionIndex = m_ActiveCollectionIndex;
                        empty.collectionId = m_Collections[static_cast<std::size_t>(m_ActiveCollectionIndex)].id;
                        char label[32] = {};
                        std::snprintf(label, sizeof(label), "Empty %d", m_TransformEmptyCounter++);
                        empty.name = label;
                        m_TransformEmpties.push_back(empty);
                        m_ActiveTransformEmptyIndex = static_cast<int>(m_TransformEmpties.size()) - 1;
                        m_SelectedTransformEmptyIndex = m_ActiveTransformEmptyIndex;
                        settingsChanged = true;
                    }

                    ImGui::EndPopup();
                }

                if ((m_BoxSelectArmed || m_CircleSelectArmed) && m_InteractionMode == InteractionMode::Select && m_ViewportFocused)
                {
                    const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                    const bool insideViewport =
                        mousePos.x >= m_ViewportRectMin.x && mousePos.x <= m_ViewportRectMax.x &&
                        mousePos.y >= m_ViewportRectMin.y && mousePos.y <= m_ViewportRectMax.y;

                    if (m_CircleSelectArmed && insideViewport && std::abs(io.MouseWheel) > 0.0f)
                    {
                        const float wheelDelta = m_InvertCircleSelectWheel ? -io.MouseWheel : io.MouseWheel;
                        m_CircleSelectRadius = glm::clamp(m_CircleSelectRadius + wheelDelta * m_CircleSelectWheelStep, 8.0f, 260.0f);
                    }

                    if (m_BoxSelectArmed && !m_BoxSelecting && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && insideViewport)
                    {
                        m_BoxSelecting = true;
                        m_BoxSelectStart = mousePos;
                        m_BoxSelectEnd = mousePos;
                        AppendSelectionDebugLog("Box select drag started");
                    }

                    if (m_BoxSelectArmed && m_BoxSelecting)
                    {
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        {
                            m_BoxSelectEnd = mousePos;
                        }
                        else
                        {
                            m_BoxSelectEnd = mousePos;
                            const SelectionStrokeMode strokeMode = ResolveSelectionStrokeMode(io.KeyCtrl);
                            const bool includeAtoms =
                                (m_SelectionFilter == SelectionFilter::AtomsOnly) ||
                                (m_SelectionFilter == SelectionFilter::AtomsAndBonds);
                            const bool includeBonds =
                                (m_SelectionFilter == SelectionFilter::AtomsAndBonds) ||
                                (m_SelectionFilter == SelectionFilter::BondsOnly) ||
                                (m_SelectionFilter == SelectionFilter::BondLabelsOnly);
                            if (includeAtoms)
                            {
                                SelectAtomsInScreenRect(m_BoxSelectStart, m_BoxSelectEnd, strokeMode);
                            }
                            else if (strokeMode == SelectionStrokeMode::Replace)
                            {
                                m_SelectedAtomIndices.clear();
                                m_SelectedTransformEmptyIndex = -1;
                                m_SelectedSpecialNode = SpecialNodeSelection::None;
                            }
                            if (includeBonds)
                            {
                                SelectBondsInScreenRect(m_BoxSelectStart, m_BoxSelectEnd, strokeMode);
                            }
                            else if (strokeMode == SelectionStrokeMode::Replace)
                            {
                                m_SelectedBondKeys.clear();
                                m_SelectedBondLabelKey = 0;
                            }
                            m_BoxSelecting = false;
                            m_BoxSelectArmed = false;
                            AppendSelectionDebugLog("Box select drag finished");
                        }
                    }

                    if (m_CircleSelectArmed && !m_CircleSelecting && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && insideViewport)
                    {
                        const SelectionStrokeMode strokeMode = ResolveSelectionStrokeMode(io.KeyCtrl);
                        m_CircleSelecting = true;
                        const bool includeAtoms =
                            (m_SelectionFilter == SelectionFilter::AtomsOnly) ||
                            (m_SelectionFilter == SelectionFilter::AtomsAndBonds);
                        const bool includeBonds =
                            (m_SelectionFilter == SelectionFilter::AtomsAndBonds) ||
                            (m_SelectionFilter == SelectionFilter::BondsOnly) ||
                                (m_SelectionFilter == SelectionFilter::BondLabelsOnly);
                        if (includeAtoms)
                        {
                            SelectAtomsInScreenCircle(mousePos, m_CircleSelectRadius, strokeMode);
                        }
                        else if (strokeMode == SelectionStrokeMode::Replace)
                        {
                            m_SelectedAtomIndices.clear();
                            m_SelectedTransformEmptyIndex = -1;
                            m_SelectedSpecialNode = SpecialNodeSelection::None;
                        }
                        if (includeBonds)
                        {
                            SelectBondsInScreenCircle(mousePos, m_CircleSelectRadius, strokeMode);
                        }
                        else if (strokeMode == SelectionStrokeMode::Replace)
                        {
                            m_SelectedBondKeys.clear();
                            m_SelectedBondLabelKey = 0;
                        }
                        m_BlockSelectionThisFrame = true;
                    }

                    if (m_CircleSelectArmed && m_CircleSelecting)
                    {
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        {
                            if (insideViewport)
                            {
                                const bool includeAtoms =
                                    (m_SelectionFilter == SelectionFilter::AtomsOnly) ||
                                    (m_SelectionFilter == SelectionFilter::AtomsAndBonds);
                                const bool includeBonds =
                                    (m_SelectionFilter == SelectionFilter::AtomsAndBonds) ||
                                    (m_SelectionFilter == SelectionFilter::BondsOnly) ||
                                    (m_SelectionFilter == SelectionFilter::BondLabelsOnly);
                                const SelectionStrokeMode strokeMode = io.KeyShift ? SelectionStrokeMode::Subtract : SelectionStrokeMode::Add;
                                if (includeAtoms)
                                {
                                    SelectAtomsInScreenCircle(mousePos, m_CircleSelectRadius, strokeMode);
                                }
                                if (includeBonds)
                                {
                                    SelectBondsInScreenCircle(mousePos, m_CircleSelectRadius, strokeMode);
                                }
                            }
                            m_BlockSelectionThisFrame = true;
                        }
                        else
                        {
                            m_CircleSelecting = false;
                            AppendSelectionDebugLog("Circle select stroke finished");
                        }
                    }

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        m_BoxSelecting = false;
                        m_BoxSelectArmed = false;
                        m_CircleSelecting = false;
                        m_CircleSelectArmed = false;
                        AppendSelectionDebugLog("Box/circle select canceled with RMB");
                    }
                }

                if (m_BoxSelecting)
                {
                    ImDrawList *drawList = ImGui::GetWindowDrawList();
                    const ImVec2 rectA(
                        std::min(m_BoxSelectStart.x, m_BoxSelectEnd.x),
                        std::min(m_BoxSelectStart.y, m_BoxSelectEnd.y));
                    const ImVec2 rectB(
                        std::max(m_BoxSelectStart.x, m_BoxSelectEnd.x),
                        std::max(m_BoxSelectStart.y, m_BoxSelectEnd.y));
                    drawList->AddRectFilled(rectA, rectB, IM_COL32(85, 160, 255, 40));
                    drawList->AddRect(rectA, rectB, IM_COL32(95, 185, 255, 220), 0.0f, 0, 1.5f);
                }

                if (m_CircleSelectArmed)
                {
                    ImDrawList *drawList = ImGui::GetWindowDrawList();
                    const ImVec2 center(io.MousePos.x, io.MousePos.y);
                    const float radius = glm::clamp(m_CircleSelectRadius, 8.0f, 260.0f);
                    const SelectionStrokeMode previewStrokeMode = ResolveSelectionStrokeMode(io.KeyCtrl);
                    const ImU32 fillColor = (previewStrokeMode == SelectionStrokeMode::Subtract)
                                                ? IM_COL32(255, 118, 118, 42)
                                                : IM_COL32(94, 185, 255, 36);
                    const ImU32 outlineColor = (previewStrokeMode == SelectionStrokeMode::Subtract)
                                                   ? IM_COL32(255, 148, 148, 235)
                                                   : IM_COL32(124, 210, 255, 225);
                    drawList->AddCircleFilled(center, radius, fillColor, 48);
                    drawList->AddCircle(center, radius, outlineColor, 48, 1.6f);
                    char radiusLabel[64] = {};
                    const char *modeLabel = (previewStrokeMode == SelectionStrokeMode::Subtract) ? "subtract" : ((previewStrokeMode == SelectionStrokeMode::Add) ? "add" : "replace");
                    std::snprintf(radiusLabel, sizeof(radiusLabel), "C-select %s r=%.0f", modeLabel, radius);
                    drawList->AddText(ImVec2(center.x + 10.0f, center.y - radius - 18.0f), IM_COL32(210, 232, 248, 235), radiusLabel);
                }
            }
            else
            {
                ImGui::TextUnformatted("Viewport render target not ready.");
                m_ViewportRectMin = glm::vec2(0.0f, 0.0f);
                m_ViewportRectMax = glm::vec2(0.0f, 0.0f);
            }
        }
        else
        {
            ImGui::TextUnformatted("OpenGL backend unavailable.");
            m_ViewportRectMin = glm::vec2(0.0f, 0.0f);
            m_ViewportRectMax = glm::vec2(0.0f, 0.0f);
        }

        HandleViewportSelection();

        ImGui::End();

        if (m_ShowViewportInfoPanel)
        {
            ImGui::Begin("Viewport Info", &m_ShowViewportInfoPanel);
            ImGui::Text("Size: %.0f x %.0f", size.x, size.y);
            ImGui::Text("Render: %u x %u (%.0f%%)",
                        static_cast<unsigned int>(std::max(1.0f, m_ViewportSize.x * m_ViewportRenderScale)),
                        static_cast<unsigned int>(std::max(1.0f, m_ViewportSize.y * m_ViewportRenderScale)),
                        m_ViewportRenderScale * 100.0f);
            ImGui::Text("Focused: %s | Hovered: %s", m_ViewportFocused ? "yes" : "no", m_ViewportHovered ? "yes" : "no");
            ImGui::Text("ImGui capture: mouse=%s keyboard=%s", io.WantCaptureMouse ? "yes" : "no", io.WantCaptureKeyboard ? "yes" : "no");
            ImGui::Text("3D Cursor: (%.3f, %.3f, %.3f)", m_CursorPosition.x, m_CursorPosition.y, m_CursorPosition.z);
            ImGui::Separator();
            ImGui::TextUnformatted("Navigation:");
            ImGui::BulletText("MMB orbit");
            ImGui::BulletText("Shift + MMB pan");
            ImGui::BulletText("Mouse Wheel zoom");
            ImGui::BulletText("Touchpad mode: Alt+LMB orbit, Alt+Shift+LMB pan, Alt+RMB zoom");
            ImGui::BulletText("RMB on viewport: selection/cursor context menu");
            ImGui::BulletText("Top-right axis gizmo: rotate view");
            ImGui::BulletText("ViewSet mode keys: T top, B bottom, L left, R right, P front, K back");
            ImGui::End();
        }

        if (m_ViewportSettingsOpen)
        {
            ImGui::Begin("Viewport Settings", &m_ViewportSettingsOpen);
            ImGui::PushItemWidth(210.0f);
            const ImGuiTreeNodeFlags defaultOpenFlags = ImGuiTreeNodeFlags_DefaultOpen;
            const float nestedSectionIndent = ImGui::GetStyle().IndentSpacing * 0.65f;

            auto ensureFloatRange = [](float &minValue, float &maxValue, float hardMin, float hardMax)
            {
                minValue = glm::clamp(minValue, hardMin, hardMax);
                maxValue = glm::clamp(maxValue, hardMin, hardMax);
                if (minValue > maxValue)
                {
                    std::swap(minValue, maxValue);
                }
            };
            auto ensureIntRange = [](int &minValue, int &maxValue, int hardMin, int hardMax)
            {
                minValue = std::clamp(minValue, hardMin, hardMax);
                maxValue = std::clamp(maxValue, hardMin, hardMax);
                if (minValue > maxValue)
                {
                    std::swap(minValue, maxValue);
                }
            };

            if (ImGui::CollapsingHeader("Grid & Background", defaultOpenFlags))
            {
                ensureIntRange(m_GridHalfExtentMin, m_GridHalfExtentMax, 1, 128);
                ensureFloatRange(m_GridSpacingMin, m_GridSpacingMax, 0.01f, 20.0f);
                ensureFloatRange(m_GridLineWidthMin, m_GridLineWidthMax, 0.5f, 10.0f);
                ensureFloatRange(m_GridOpacityMin, m_GridOpacityMax, 0.01f, 1.0f);

                if (ImGui::ColorEdit3("Background", &m_SceneSettings.clearColor.x))
                {
                    settingsChanged = true;
                }
                if (ImGui::Checkbox("Draw grid", &m_SceneSettings.drawGrid))
                {
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Draw cell edges", &m_ShowCellEdges))
                {
                    m_SceneSettings.drawCellEdges = m_ShowCellEdges;
                    settingsChanged = true;
                }
                if (ImGui::Button(m_CleanViewMode ? "Disable clean view" : "Enable clean view"))
                {
                    m_CleanViewMode = !m_CleanViewMode;
                    if (m_CleanViewMode)
                    {
                        m_SceneSettings.drawGrid = false;
                        m_ShowCellEdges = false;
                        m_ShowGlobalAxesOverlay = false;
                        m_Show3DCursor = false;
                        m_ShowTransformEmpties = false;
                        m_ShowBondLengthLabels = false;
                    }
                    settingsChanged = true;
                }
                if (ImGui::InputInt2("Grid half extent min/max", &m_GridHalfExtentMin))
                {
                    ensureIntRange(m_GridHalfExtentMin, m_GridHalfExtentMax, 1, 128);
                    settingsChanged = true;
                }
                if (ImGui::SliderInt("Grid half extent", &m_SceneSettings.gridHalfExtent, m_GridHalfExtentMin, m_GridHalfExtentMax))
                {
                    settingsChanged = true;
                }
                if (ImGui::DragFloatRange2("Grid spacing min/max", &m_GridSpacingMin, &m_GridSpacingMax, 0.01f, 0.01f, 20.0f, "min %.2f", "max %.2f"))
                {
                    ensureFloatRange(m_GridSpacingMin, m_GridSpacingMax, 0.01f, 20.0f);
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Grid spacing", &m_SceneSettings.gridSpacing, m_GridSpacingMin, m_GridSpacingMax, "%.2f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::DragFloatRange2("Grid line width min/max", &m_GridLineWidthMin, &m_GridLineWidthMax, 0.01f, 0.5f, 10.0f, "min %.2f", "max %.2f"))
                {
                    ensureFloatRange(m_GridLineWidthMin, m_GridLineWidthMax, 0.5f, 10.0f);
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Grid line width", &m_SceneSettings.gridLineWidth, m_GridLineWidthMin, m_GridLineWidthMax, "%.1f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Grid color", &m_SceneSettings.gridColor.x))
                {
                    settingsChanged = true;
                }
                if (ImGui::DragFloatRange2("Grid opacity min/max", &m_GridOpacityMin, &m_GridOpacityMax, 0.005f, 0.01f, 1.0f, "min %.2f", "max %.2f"))
                {
                    ensureFloatRange(m_GridOpacityMin, m_GridOpacityMax, 0.01f, 1.0f);
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Grid opacity", &m_SceneSettings.gridOpacity, m_GridOpacityMin, m_GridOpacityMax, "%.2f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Cell edge color", &m_CellEdgeColor.x))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Cell edge width", &m_CellEdgeLineWidth, 0.5f, 10.0f, "%.1f"))
                {
                    settingsChanged = true;
                }
            }

            if (ImGui::CollapsingHeader("Lighting", defaultOpenFlags))
            {
                ensureFloatRange(m_AmbientMin, m_AmbientMax, 0.0f, 4.0f);
                ensureFloatRange(m_DiffuseMin, m_DiffuseMax, 0.0f, 4.0f);

                if (ImGui::SliderFloat3("Light direction", &m_SceneSettings.lightDirection.x, -1.0f, 1.0f, "%.2f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::DragFloatRange2("Ambient min/max", &m_AmbientMin, &m_AmbientMax, 0.01f, 0.0f, 4.0f, "min %.2f", "max %.2f"))
                {
                    ensureFloatRange(m_AmbientMin, m_AmbientMax, 0.0f, 4.0f);
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Ambient", &m_SceneSettings.ambientStrength, m_AmbientMin, m_AmbientMax, "%.2f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::DragFloatRange2("Diffuse min/max", &m_DiffuseMin, &m_DiffuseMax, 0.01f, 0.0f, 4.0f, "min %.2f", "max %.2f"))
                {
                    ensureFloatRange(m_DiffuseMin, m_DiffuseMax, 0.0f, 4.0f);
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Diffuse", &m_SceneSettings.diffuseStrength, m_DiffuseMin, m_DiffuseMax, "%.2f"))
                {
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Light color", &m_SceneSettings.lightColor.x))
                {
                    settingsChanged = true;
                }
            }

            if (ImGui::CollapsingHeader("Projection", defaultOpenFlags))
            {
                ensureFloatRange(m_RenderScaleMin, m_RenderScaleMax, 0.1f, 2.0f);
                ImGui::TextWrapped("Internal viewport scale lowers the render target resolution to reduce GPU cost on slower hardware.");
                const std::uint32_t scaledWidth = static_cast<std::uint32_t>(std::max(1.0f, m_ViewportSize.x * m_ViewportRenderScale));
                const std::uint32_t scaledHeight = static_cast<std::uint32_t>(std::max(1.0f, m_ViewportSize.y * m_ViewportRenderScale));
                ImGui::Text("Internal target: %u x %u", scaledWidth, scaledHeight);

                auto setViewportRenderScale = [&](float scale)
                {
                    m_ViewportRenderScale = glm::clamp(scale, m_RenderScaleMin, m_RenderScaleMax);
                    settingsChanged = true;
                };

                if (ImGui::Button("25%"))
                {
                    setViewportRenderScale(0.25f);
                }
                ImGui::SameLine();
                if (ImGui::Button("50%"))
                {
                    setViewportRenderScale(0.50f);
                }
                ImGui::SameLine();
                if (ImGui::Button("75%"))
                {
                    setViewportRenderScale(0.75f);
                }
                ImGui::SameLine();
                if (ImGui::Button("100%"))
                {
                    setViewportRenderScale(1.0f);
                }

                if (ImGui::SliderFloat("Internal render scale", &m_ViewportRenderScale, m_RenderScaleMin, m_RenderScaleMax, "%.2fx"))
                {
                    settingsChanged = true;
                }
                if (ImGui::DragFloatRange2("Allowed scale range", &m_RenderScaleMin, &m_RenderScaleMax, 0.01f, 0.1f, 2.0f, "min %.2f", "max %.2f"))
                {
                    ensureFloatRange(m_RenderScaleMin, m_RenderScaleMax, 0.1f, 2.0f);
                    m_ViewportRenderScale = glm::clamp(m_ViewportRenderScale, m_RenderScaleMin, m_RenderScaleMax);
                    settingsChanged = true;
                }

                const char *projectionModes[] = {"Perspective", "Orthographic"};
                if (ImGui::Combo("Mode", &m_ProjectionModeIndex, projectionModes, IM_ARRAYSIZE(projectionModes)))
                {
                    if (m_Camera)
                    {
                        m_Camera->SetProjectionMode(m_ProjectionModeIndex == 0 ? OrbitCamera::ProjectionMode::Perspective : OrbitCamera::ProjectionMode::Orthographic);
                    }
                    settingsChanged = true;
                }

                if (m_Camera && m_ProjectionModeIndex == 0)
                {
                    float fov = m_Camera->GetPerspectiveFovDegrees();
                    if (ImGui::SliderFloat("FOV", &fov, 10.0f, 100.0f, "%.1f deg"))
                    {
                        m_Camera->SetPerspectiveFovDegrees(fov);
                    }
                }

                if (m_Camera && m_ProjectionModeIndex == 1)
                {
                    float orthoSize = m_Camera->GetOrthographicSize();
                    if (ImGui::SliderFloat("Ortho size", &orthoSize, 0.1f, 40.0f, "%.2f"))
                    {
                        m_Camera->SetOrthographicSize(orthoSize);
                    }
                }

                ImGui::TextDisabled("Camera focus tuning lives in Settings > Input & Navigation.");
                DrawInlineHelpMarker("Use Settings > Input & Navigation to control the . focus distance, minimum distance, selection padding and scene clip safety.");
            }

            if (ImGui::CollapsingHeader("Selection highlight", defaultOpenFlags))
            {
                ensureFloatRange(m_SelectionOutlineMin, m_SelectionOutlineMax, 0.5f, 20.0f);
                if (ImGui::ColorEdit3("Selection color", &m_SelectionColor.x))
                {
                    settingsChanged = true;
                }
                if (ImGui::DragFloatRange2("Selection outline min/max", &m_SelectionOutlineMin, &m_SelectionOutlineMax, 0.05f, 0.5f, 20.0f, "min %.2f", "max %.2f"))
                {
                    ensureFloatRange(m_SelectionOutlineMin, m_SelectionOutlineMax, 0.5f, 20.0f);
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Selection outline", &m_SelectionOutlineThickness, m_SelectionOutlineMin, m_SelectionOutlineMax, "%.1f"))
                {
                    settingsChanged = true;
                }
            }

            if (ImGui::CollapsingHeader("Measurements", defaultOpenFlags))
            {
                if (ImGui::Checkbox("Show selection measurements", &m_ShowSelectionMeasurements))
                {
                    settingsChanged = true;
                }
                if (ImGui::Checkbox("Distance (last 2 selected atoms)", &m_ShowSelectionDistanceMeasurement))
                {
                    settingsChanged = true;
                }
                if (ImGui::Checkbox("Angle (last 3 selected atoms)", &m_ShowSelectionAngleMeasurement))
                {
                    settingsChanged = true;
                }
                if (ImGui::Checkbox("Static angle labels", &m_ShowStaticAngleLabels))
                {
                    settingsChanged = true;
                }
                if (ImGui::SliderInt("Measurement precision", &m_MeasurementPrecision, 0, 6))
                {
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Measurement text color", &m_MeasurementTextColor.x))
                {
                    settingsChanged = true;
                }
                if (ImGui::ColorEdit3("Measurement background", &m_MeasurementBackgroundColor.x))
                {
                    settingsChanged = true;
                }
            }

            if (ImGui::CollapsingHeader("Axis & Cursor Colors", defaultOpenFlags))
            {
                constexpr ImGuiColorEditFlags kPickerOnlyFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel;
                auto drawVisibilityColorRow = [&](const char *checkboxLabel, bool *visible, const char *colorLabel, glm::vec3 &color)
                {
                    if (ImGui::Checkbox(checkboxLabel, visible))
                    {
                        settingsChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::ColorEdit3(colorLabel, &color.x, kPickerOnlyFlags))
                    {
                        settingsChanged = true;
                    }
                };

                if (ImGui::BeginTable("AxisCursorGrid", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    drawVisibilityColorRow("X", &m_ShowGlobalAxis[0], "##AxisXColor", m_AxisColors[0]);
                    ImGui::TableNextColumn();
                    drawVisibilityColorRow("Y", &m_ShowGlobalAxis[1], "##AxisYColor", m_AxisColors[1]);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    drawVisibilityColorRow("Z", &m_ShowGlobalAxis[2], "##AxisZColor", m_AxisColors[2]);
                    ImGui::TableNextColumn();
                    drawVisibilityColorRow("3D cursor", &m_Show3DCursor, "##CursorColor", m_CursorColor);
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("Camera & UI", defaultOpenFlags))
            {
                int currentTheme = static_cast<int>(m_CurrentTheme);
                const char *items[] = {
                    "Hazel Dark",
                    "Light",
                    "Classic",
                    "PhotoshopStyle",
                    "WarmSlate"};

                if (ImGui::Combo("Style preset", &currentTheme, items, IM_ARRAYSIZE(items)))
                {
                    m_CurrentTheme = static_cast<ThemePreset>(currentTheme);
                    ApplyTheme(m_CurrentTheme);
                    settingsChanged = true;
                }

                float uiScale = m_FontScale;
                if (ImGui::SliderFloat("Font size scale", &uiScale, 0.7f, 2.0f, "%.2f"))
                {
                    ApplyFontScale(uiScale);
                    settingsChanged = true;
                }

                float orbitSensitivity = m_CameraOrbitSensitivity;
                if (ImGui::SliderFloat("Orbit sensitivity", &orbitSensitivity, 0.05f, 4.0f, "%.2fx"))
                {
                    m_CameraOrbitSensitivity = orbitSensitivity;
                    ApplyCameraSensitivity();
                    settingsChanged = true;
                }

                float panSensitivity = m_CameraPanSensitivity;
                if (ImGui::SliderFloat("Pan sensitivity", &panSensitivity, 0.05f, 4.0f, "%.2fx"))
                {
                    m_CameraPanSensitivity = panSensitivity;
                    ApplyCameraSensitivity();
                    settingsChanged = true;
                }

                float zoomSensitivity = m_CameraZoomSensitivity;
                if (ImGui::SliderFloat("Zoom sensitivity", &zoomSensitivity, 0.05f, 4.0f, "%.2fx"))
                {
                    m_CameraZoomSensitivity = zoomSensitivity;
                    ApplyCameraSensitivity();
                    settingsChanged = true;
                }

                ImGui::TextDisabled("Input + mouse behaviour live in Settings > Input & Navigation.");

                ImGui::SeparatorText("Camera presets");

                const auto trimPresetName = [](const std::string &raw) -> std::string
                {
                    const std::size_t first = raw.find_first_not_of(" \t\r\n");
                    if (first == std::string::npos)
                    {
                        return std::string();
                    }
                    const std::size_t last = raw.find_last_not_of(" \t\r\n");
                    return raw.substr(first, last - first + 1);
                };

                auto captureCurrentCamera = [&](CameraPreset &preset)
                {
                    if (m_Camera)
                    {
                        preset.target = m_Camera->GetTarget();
                        preset.distance = m_Camera->GetDistance();
                        preset.yaw = m_Camera->GetYaw();
                        preset.pitch = m_Camera->GetPitch();
                        preset.roll = m_Camera->GetRoll();
                    }
                    else
                    {
                        preset.target = m_CameraTargetPersisted;
                        preset.distance = m_CameraDistancePersisted;
                        preset.yaw = m_CameraYawPersisted;
                        preset.pitch = m_CameraPitchPersisted;
                        preset.roll = m_CameraRollPersisted;
                    }
                    preset.distance = std::max(0.05f, preset.distance);
                };

                auto applyPresetCamera = [&](const CameraPreset &preset)
                {
                    if (m_Camera)
                    {
                        m_Camera->SetOrbitState(preset.target, preset.distance, preset.yaw, preset.pitch);
                        m_Camera->SetRoll(preset.roll);
                    }
                    m_CameraTargetPersisted = preset.target;
                    m_CameraDistancePersisted = preset.distance;
                    m_CameraYawPersisted = preset.yaw;
                    m_CameraPitchPersisted = preset.pitch;
                    m_CameraRollPersisted = preset.roll;
                    m_HasPersistedCameraState = true;
                };

                const bool hasPresetSelection =
                    m_SelectedCameraPresetIndex >= 0 &&
                    m_SelectedCameraPresetIndex < static_cast<int>(m_CameraPresets.size());

                std::string selectedPresetLabel = "(none)";
                if (hasPresetSelection)
                {
                    selectedPresetLabel = m_CameraPresets[static_cast<std::size_t>(m_SelectedCameraPresetIndex)].name;
                }

                if (ImGui::BeginCombo("Preset list", selectedPresetLabel.c_str()))
                {
                    for (std::size_t i = 0; i < m_CameraPresets.size(); ++i)
                    {
                        const bool selected = (static_cast<int>(i) == m_SelectedCameraPresetIndex);
                        if (ImGui::Selectable(m_CameraPresets[i].name.c_str(), selected))
                        {
                            m_SelectedCameraPresetIndex = static_cast<int>(i);
                            std::snprintf(m_CameraPresetNameBuffer.data(), m_CameraPresetNameBuffer.size(), "%s", m_CameraPresets[i].name.c_str());
                            settingsChanged = true;
                        }
                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::InputText("Preset name", m_CameraPresetNameBuffer.data(), m_CameraPresetNameBuffer.size());

                if (ImGui::Button("Save current as new"))
                {
                    CameraPreset preset;
                    preset.name = trimPresetName(std::string(m_CameraPresetNameBuffer.data()));
                    if (preset.name.empty())
                    {
                        preset.name = "Preset " + std::to_string(m_CameraPresets.size() + 1);
                    }
                    captureCurrentCamera(preset);
                    m_CameraPresets.push_back(preset);
                    m_SelectedCameraPresetIndex = static_cast<int>(m_CameraPresets.size()) - 1;
                    std::snprintf(m_CameraPresetNameBuffer.data(), m_CameraPresetNameBuffer.size(), "%s", preset.name.c_str());
                    settingsChanged = true;
                }

                ImGui::SameLine();
                if (!hasPresetSelection)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Apply selected") && hasPresetSelection)
                {
                    applyPresetCamera(m_CameraPresets[static_cast<std::size_t>(m_SelectedCameraPresetIndex)]);
                    settingsChanged = true;
                }
                if (!hasPresetSelection)
                {
                    ImGui::EndDisabled();
                }

                if (!hasPresetSelection)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Update selected") && hasPresetSelection)
                {
                    CameraPreset &preset = m_CameraPresets[static_cast<std::size_t>(m_SelectedCameraPresetIndex)];
                    const std::string typedName = trimPresetName(std::string(m_CameraPresetNameBuffer.data()));
                    if (!typedName.empty())
                    {
                        preset.name = typedName;
                    }
                    captureCurrentCamera(preset);
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete selected") && hasPresetSelection)
                {
                    m_CameraPresets.erase(m_CameraPresets.begin() + m_SelectedCameraPresetIndex);
                    if (m_CameraPresets.empty())
                    {
                        m_SelectedCameraPresetIndex = -1;
                    }
                    else
                    {
                        m_SelectedCameraPresetIndex = std::min(m_SelectedCameraPresetIndex, static_cast<int>(m_CameraPresets.size()) - 1);
                        std::snprintf(m_CameraPresetNameBuffer.data(), m_CameraPresetNameBuffer.size(), "%s",
                                      m_CameraPresets[static_cast<std::size_t>(m_SelectedCameraPresetIndex)].name.c_str());
                    }
                    settingsChanged = true;
                }
                if (!hasPresetSelection)
                {
                    ImGui::EndDisabled();
                }
            }

            ImGui::PopItemWidth();

            ImGui::End();
        }

        if (!m_ShowActionsPanel && !m_ShowAppearancePanel)
        {
            const ImVec2 workPos = viewport->WorkPos;
            const ImVec2 workSize = viewport->WorkSize;
            ImGui::SetNextWindowPos(ImVec2(workPos.x + workSize.x - 28.0f, workPos.y + 120.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(24.0f, 120.0f), ImGuiCond_Always);
            ImGui::Begin("##SidePanelsCollapsedStrip", nullptr,
                         ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings);
            ImGui::Dummy(ImVec2(1.0f, 8.0f));
            if (ImGui::Button(">", ImVec2(20.0f, 36.0f)))
            {
                m_ShowActionsPanel = true;
                m_ShowAppearancePanel = true;
                settingsChanged = true;
            }
            ImGui::Spacing();
            ImGui::TextUnformatted("N");
            ImGui::End();
        }

        if (m_ShowActionsPanel || m_ShowAppearancePanel)
        {
            const char *coordinateModes[] = {"Direct", "Cartesian"};
            const ImGuiTreeNodeFlags defaultOpenFlags = ImGuiTreeNodeFlags_DefaultOpen;
            const float nestedSectionIndent = ImGui::GetStyle().IndentSpacing * 0.65f;

            auto drawStructureIoSection = [&]()
            {
                if (ImGui::CollapsingHeader("Structure I/O", defaultOpenFlags))
                {
                    ImGui::InputText("Import path", m_ImportPathBuffer.data(), m_ImportPathBuffer.size());
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##Import"))
                    {
                        std::vector<std::string> selectedPaths;
                        if (OpenNativeFilesDialog(selectedPaths) && !selectedPaths.empty())
                        {
                            std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", selectedPaths.front().c_str());
                            for (const std::string &selectedPath : selectedPaths)
                            {
                                AppendStructureFromPathAsCollection(selectedPath);
                            }
                        }
                    }

                    if (ImGui::Button("Load multiple files"))
                    {
                        std::vector<std::string> selectedPaths;
                        if (OpenNativeFilesDialog(selectedPaths) && !selectedPaths.empty())
                        {
                            std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", selectedPaths.front().c_str());
                            for (const std::string &selectedPath : selectedPaths)
                            {
                                AppendStructureFromPathAsCollection(selectedPath);
                            }
                        }
                    }

                    if (ImGui::Button("Load POSCAR/CONTCAR"))
                    {
                        AppendStructureFromPathAsCollection(std::string(m_ImportPathBuffer.data()));
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Load sample"))
                    {
                        const char *samplePath = EditorLayer::kFallbackStartupImportPath;
                        std::snprintf(m_ImportPathBuffer.data(), m_ImportPathBuffer.size(), "%s", samplePath);
                        AppendStructureFromPathAsCollection(samplePath);
                    }

                    ImGui::InputText("Export path", m_ExportPathBuffer.data(), m_ExportPathBuffer.size());
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##Export"))
                    {
                        std::string selectedPath;
                        if (SaveNativeFileDialog(selectedPath))
                        {
                            std::snprintf(m_ExportPathBuffer.data(), m_ExportPathBuffer.size(), "%s", selectedPath.c_str());
                        }
                    }

                    ImGui::Combo("Export coordinates", &m_ExportCoordinateModeIndex, coordinateModes, IM_ARRAYSIZE(coordinateModes));
                    ImGui::SliderInt("Export precision", &m_ExportPrecision, 1, 16);

                    if (ImGui::Button("Export POSCAR/CONTCAR"))
                    {
                        const CoordinateMode exportMode = (m_ExportCoordinateModeIndex == 0)
                                                              ? CoordinateMode::Direct
                                                              : CoordinateMode::Cartesian;
                        ExportStructureToPath(std::string(m_ExportPathBuffer.data()), exportMode, m_ExportPrecision);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Export active collection") && m_HasStructureLoaded && m_ActiveCollectionIndex >= 0)
                    {
                        const CoordinateMode exportMode = (m_ExportCoordinateModeIndex == 0)
                                                              ? CoordinateMode::Direct
                                                              : CoordinateMode::Cartesian;
                        ExportCollectionToPath(m_ActiveCollectionIndex, std::string(m_ExportPathBuffer.data()), exportMode, m_ExportPrecision);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Restore original state"))
                    {
                        if (m_OriginalStructure.has_value())
                        {
                            m_WorkingStructure = *m_OriginalStructure;
                            m_HasStructureLoaded = true;
                            m_AutoBondsDirty = true;
                            m_LastStructureOperationFailed = false;
                            m_LastStructureMessage = "Original file state restored.";
                            LogInfo(m_LastStructureMessage);
                        }
                        else
                        {
                            m_LastStructureOperationFailed = true;
                            m_LastStructureMessage = "Restore failed: no original structure captured yet.";
                            LogWarn(m_LastStructureMessage);
                        }
                    }
                }
            };

            auto drawAtomAppearanceSection = [&]()
            {
                if (ImGui::CollapsingHeader("Atoms", defaultOpenFlags))
                {
                    ImGui::Indent(nestedSectionIndent);
                    if (ImGui::CollapsingHeader("Rendering", defaultOpenFlags))
                    {
                        m_AtomSizeMin = glm::clamp(m_AtomSizeMin, 0.01f, 4.0f);
                        m_AtomSizeMax = glm::clamp(m_AtomSizeMax, 0.01f, 4.0f);
                        if (m_AtomSizeMin > m_AtomSizeMax)
                        {
                            std::swap(m_AtomSizeMin, m_AtomSizeMax);
                        }
                        m_AtomBrightnessMin = glm::clamp(m_AtomBrightnessMin, 0.05f, 6.0f);
                        m_AtomBrightnessMax = glm::clamp(m_AtomBrightnessMax, 0.05f, 6.0f);
                        if (m_AtomBrightnessMin > m_AtomBrightnessMax)
                        {
                            std::swap(m_AtomBrightnessMin, m_AtomBrightnessMax);
                        }
                        m_AtomGlowMin = glm::clamp(m_AtomGlowMin, 0.0f, 2.0f);
                        m_AtomGlowMax = glm::clamp(m_AtomGlowMax, 0.0f, 2.0f);
                        if (m_AtomGlowMin > m_AtomGlowMax)
                        {
                            std::swap(m_AtomGlowMin, m_AtomGlowMax);
                        }

                        if (ImGui::DragFloatRange2("Atom size min/max", &m_AtomSizeMin, &m_AtomSizeMax, 0.01f, 0.01f, 4.0f, "min %.2f", "max %.2f"))
                        {
                            settingsChanged = true;
                        }
                        if (ImGui::SliderFloat("Atom size", &m_SceneSettings.atomScale, m_AtomSizeMin, m_AtomSizeMax, "%.2f"))
                        {
                            settingsChanged = true;
                        }
                        if (ImGui::DragFloatRange2("Atom brightness min/max", &m_AtomBrightnessMin, &m_AtomBrightnessMax, 0.01f, 0.05f, 6.0f, "min %.2f", "max %.2f"))
                        {
                            settingsChanged = true;
                        }
                        if (ImGui::SliderFloat("Atom brightness", &m_SceneSettings.atomBrightness, m_AtomBrightnessMin, m_AtomBrightnessMax, "%.2f"))
                        {
                            settingsChanged = true;
                        }
                        if (ImGui::DragFloatRange2("Atom glow min/max", &m_AtomGlowMin, &m_AtomGlowMax, 0.005f, 0.0f, 2.0f, "min %.2f", "max %.2f"))
                        {
                            settingsChanged = true;
                        }
                        if (ImGui::SliderFloat("Atom glow", &m_SceneSettings.atomGlowStrength, m_AtomGlowMin, m_AtomGlowMax, "%.2f"))
                        {
                            settingsChanged = true;
                        }
                        if (ImGui::Checkbox("Override atom color", &m_SceneSettings.overrideAtomColor))
                        {
                            settingsChanged = true;
                        }
                        if (m_SceneSettings.overrideAtomColor &&
                            ImGui::ColorEdit3("Atom color", &m_SceneSettings.atomOverrideColor.x,
                                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
                        {
                            settingsChanged = true;
                        }
                    }

                    if (ImGui::CollapsingHeader("Per-element project overrides", defaultOpenFlags))
                    {
                        if (ImGui::Button("Open Element Catalog"))
                        {
                            EnsureElementAppearanceSelection();
                            m_ShowElementCatalogPanel = true;
                            settingsChanged = true;
                        }

                        if (!m_HasStructureLoaded || m_WorkingStructure.species.empty())
                        {
                            ImGui::TextDisabled("Load structure to edit project overrides for element colors and sizes.");
                        }
                        else
                        {
                            std::vector<std::string> species = m_WorkingStructure.species;
                            std::sort(species.begin(), species.end());
                            species.erase(std::unique(species.begin(), species.end()), species.end());

                            for (const std::string &rawElement : species)
                            {
                                const std::string element = NormalizeElementSymbol(rawElement);
                                if (element.empty())
                                {
                                    continue;
                                }

                                ImGui::PushID(element.c_str());

                                bool hasColorOverride = (m_ElementColorOverrides.find(element) != m_ElementColorOverrides.end());
                                glm::vec3 color = ResolveElementColor(element);
                                if (ImGui::Checkbox("##OverrideColor", &hasColorOverride))
                                {
                                    if (hasColorOverride)
                                    {
                                        m_ElementColorOverrides[element] = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
                                    }
                                    else
                                    {
                                        m_ElementColorOverrides.erase(element);
                                    }
                                    settingsChanged = true;
                                }

                                ImGui::SameLine();
                                ImGui::TextUnformatted(element.c_str());
                                ImGui::SameLine(0.0f, 12.0f);
                                std::string label = "Color##AppearanceElementColor_" + element;
                                if (ImGui::ColorEdit3(label.c_str(), &color.x,
                                                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
                                {
                                    m_ElementColorOverrides[element] = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
                                    settingsChanged = true;
                                }

                                ImGui::SameLine();
                                bool hasScaleOverride = (m_ElementScaleOverrides.find(element) != m_ElementScaleOverrides.end());
                                if (ImGui::Checkbox("Project size##OverrideScale", &hasScaleOverride))
                                {
                                    if (hasScaleOverride)
                                    {
                                        m_ElementScaleOverrides[element] = ResolveElementVisualScale(element);
                                    }
                                    else
                                    {
                                        m_ElementScaleOverrides.erase(element);
                                    }
                                    settingsChanged = true;
                                }

                                float scale = ResolveElementVisualScale(element);
                                if (ImGui::SliderFloat(("Scale##AppearanceElementScale_" + element).c_str(), &scale, 0.1f, 4.0f, "%.2fx"))
                                {
                                    m_ElementScaleOverrides[element] = std::clamp(scale, 0.1f, 4.0f);
                                    settingsChanged = true;
                                }

                                ImGui::PopID();
                            }

                            if (ImGui::Button("Clear project element overrides"))
                            {
                                m_ElementColorOverrides.clear();
                                m_ElementScaleOverrides.clear();
                                settingsChanged = true;
                            }
                        }
                    }
                    ImGui::Unindent(nestedSectionIndent);
                }
            };

            auto drawAtomActionsSection = [&]()
            {
                if (ImGui::CollapsingHeader("Atoms", defaultOpenFlags))
                {
                    ImGui::Indent(nestedSectionIndent);

                    if (ImGui::CollapsingHeader("Atom editing", defaultOpenFlags))
                    {
                        const char *inputCoordinateModes[] = {"Direct", "Cartesian"};

                        ImGui::InputText("Element", m_AddAtomElementBuffer.data(), m_AddAtomElementBuffer.size());
                        ImGui::SameLine();
                        if (ImGui::Button("Periodic table"))
                        {
                            OpenPeriodicTable(PeriodicTableTarget::AddAtomEntry);
                        }
                        ImGui::DragFloat3("Position", &m_AddAtomPosition.x, 0.01f, -1000.0f, 1000.0f, "%.5f");
                        ImGui::Combo("Input coordinates", &m_AddAtomCoordinateModeIndex, inputCoordinateModes, IM_ARRAYSIZE(inputCoordinateModes));

                        if (ImGui::Button("Use camera target") && m_Camera)
                        {
                            m_AddAtomPosition = m_Camera->GetTarget();
                            m_AddAtomCoordinateModeIndex = 1;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Use 3D cursor"))
                        {
                            m_AddAtomPosition = m_CursorPosition;
                            m_AddAtomCoordinateModeIndex = 1;
                        }
                        ImGui::SameLine();
                        const bool canAddAtom = m_HasStructureLoaded;
                        if (!canAddAtom)
                        {
                            ImGui::BeginDisabled();
                        }
                        if (ImGui::Button("Add atom"))
                        {
                            m_ShowAddAtomDialog = true;
                        }
                        if (!canAddAtom)
                        {
                            ImGui::EndDisabled();
                        }

                        ImGui::SeparatorText("Change selected atom type");
                        ImGui::InputText("New element", m_ChangeAtomElementBuffer.data(), m_ChangeAtomElementBuffer.size());
                        ImGui::SameLine();
                        if (ImGui::Button("Apply type to selection"))
                        {
                            if (ApplyElementToSelectedAtoms(std::string(m_ChangeAtomElementBuffer.data())))
                            {
                                settingsChanged = true;
                            }
                        }

                        const bool canDeleteSelectedAtoms = !m_SelectedAtomIndices.empty() && m_HasStructureLoaded;
                        if (!canDeleteSelectedAtoms)
                        {
                            ImGui::BeginDisabled();
                        }
                        if (ImGui::Button("Remove selected atoms"))
                        {
                            deleteCurrentSelection();
                        }
                        if (!canDeleteSelectedAtoms)
                        {
                            ImGui::EndDisabled();
                        }
                    }

                    ImGui::Unindent(nestedSectionIndent);
                }
            };

            auto drawBondAppearanceSection = [&]()
            {
                if (ImGui::CollapsingHeader("Bonds & Labels", defaultOpenFlags))
                {
                    constexpr ImGuiColorEditFlags kPickerOnlyFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel;

                    const char *bondRenderStyles[] = {"Unicolor line", "Bi-color line", "Color gradient"};
                    int bondRenderStyleIndex = static_cast<int>(m_BondRenderStyle);
                    if (ImGui::Combo("Bond render style", &bondRenderStyleIndex, bondRenderStyles, IM_ARRAYSIZE(bondRenderStyles)))
                    {
                        m_BondRenderStyle = static_cast<BondRenderStyle>(std::clamp(bondRenderStyleIndex, 0, 2));
                        settingsChanged = true;
                    }
                    ImGui::SameLine();
                    DrawInlineHelpMarker("Unicolor uses one bond color. Bi-color splits the bond by atom colors. Color gradient interpolates colors across the full bond.");

                    if (ImGui::Checkbox("Show bond length labels", &m_ShowBondLengthLabels))
                    {
                        settingsChanged = true;
                    }
                    if (ImGui::Checkbox("Show static angle labels", &m_ShowStaticAngleLabels))
                    {
                        settingsChanged = true;
                    }

                    if (ImGui::DragFloatRange2("Bond line width min/max", &m_BondLineWidthMin, &m_BondLineWidthMax, 0.05f, 0.2f, 20.0f, "min %.2f", "max %.2f"))
                    {
                        m_BondLineWidthMin = glm::clamp(m_BondLineWidthMin, 0.2f, 50.0f);
                        m_BondLineWidthMax = glm::clamp(m_BondLineWidthMax, 0.2f, 100.0f);
                        if (m_BondLineWidthMin > m_BondLineWidthMax)
                        {
                            std::swap(m_BondLineWidthMin, m_BondLineWidthMax);
                        }
                        m_BondLineWidth = glm::clamp(m_BondLineWidth, m_BondLineWidthMin, m_BondLineWidthMax);
                        settingsChanged = true;
                    }

                    if (ImGui::SliderFloat("Bond line width", &m_BondLineWidth, m_BondLineWidthMin, m_BondLineWidthMax, "%.1f"))
                    {
                        settingsChanged = true;
                    }

                    auto drawColorPicker = [&](const char *label, glm::vec3 &color)
                    {
                        if (ImGui::ColorEdit3(label, &color.x, kPickerOnlyFlags))
                        {
                            settingsChanged = true;
                        }
                    };

                    if (ImGui::BeginTable("AppearanceBondColorGrid", 2, ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        drawColorPicker("Bond color", m_BondColor);
                        ImGui::TableNextColumn();
                        drawColorPicker("Selected bond color", m_BondSelectedColor);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        drawColorPicker("Label text color", m_BondLabelTextColor);
                        ImGui::TableNextColumn();
                        drawColorPicker("Label background color", m_BondLabelBackgroundColor);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        drawColorPicker("Label border color", m_BondLabelBorderColor);
                        ImGui::TableNextColumn();
                        ImGui::Dummy(ImVec2(1.0f, 1.0f));
                        ImGui::EndTable();
                    }

                    if (ImGui::SliderInt("Label precision", &m_BondLabelPrecision, 0, 6))
                    {
                        settingsChanged = true;
                    }

                    if (ImGui::Checkbox("Label gizmo enabled", &m_BondLabelGizmoEnabled))
                    {
                        settingsChanged = true;
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("Translate labels directly in viewport");
                    ImGui::SameLine();
                    DrawInlineHelpMarker("Enable this to drag selected bond labels in the viewport. Scale still lives in the panel because labels are screen-facing.");

                    if (ImGui::SliderFloat("Multi label scale", &m_BondLabelMultiScaleValue, 0.25f, 4.0f, "%.2f"))
                    {
                        if (!m_SelectedBondKeys.empty())
                        {
                            const float targetScale = glm::clamp(m_BondLabelMultiScaleValue, 0.25f, 4.0f);
                            for (std::uint64_t key : m_SelectedBondKeys)
                            {
                                BondLabelState &state = m_BondLabelStates[key];
                                state.scale = targetScale;
                                state.hidden = false;
                            }
                        }
                        settingsChanged = true;
                    }

                    auto selectedLabelIt = m_BondLabelStates.find(m_SelectedBondLabelKey);
                    const bool hasSelectedLabel =
                        selectedLabelIt != m_BondLabelStates.end() &&
                        !selectedLabelIt->second.deleted;

                    ImGui::Separator();
                    ImGui::TextUnformatted("Bond label objects");
                    if (!hasSelectedLabel)
                    {
                        ImGui::TextUnformatted("Select a bond label in viewport to edit it.");
                    }
                    else
                    {
                        BondLabelState &labelState = selectedLabelIt->second;
                        ImGui::Text("Selected label: atom %zu <-> atom %zu", labelState.atomA, labelState.atomB);
                        if (ImGui::Checkbox("Hidden", &labelState.hidden))
                        {
                            settingsChanged = true;
                        }
                        if (ImGui::DragFloat3("Label world offset", &labelState.worldOffset.x, 0.01f, -100.0f, 100.0f, "%.3f"))
                        {
                            settingsChanged = true;
                        }
                        if (ImGui::SliderFloat("Label scale", &labelState.scale, 0.25f, 4.0f, "%.2f"))
                        {
                            settingsChanged = true;
                        }
                        if (ImGui::Button("Reset label transform"))
                        {
                            labelState.worldOffset = glm::vec3(0.0f);
                            labelState.scale = 1.0f;
                            settingsChanged = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Delete label"))
                        {
                            PushUndoSnapshot("Delete bond label");
                            labelState.deleted = true;
                            labelState.hidden = true;
                            m_SelectedBondKeys.erase(m_SelectedBondLabelKey);
                            settingsChanged = true;
                        }
                    }

                    if (ImGui::Button("Restore all deleted labels"))
                    {
                        PushUndoSnapshot("Restore deleted bond labels");
                        for (auto &[key, state] : m_BondLabelStates)
                        {
                            (void)key;
                            state.deleted = false;
                            state.hidden = false;
                        }
                        settingsChanged = true;
                    }
                }
            };

            if (m_ShowActionsPanel)
            {
                ImGui::SetNextWindowSize(ImVec2(460.0f, 780.0f), ImGuiCond_FirstUseEver);
                ImGui::Begin("Actions", &m_ShowActionsPanel);

                ImGui::TextUnformatted("Workflow");
                drawStructureIoSection();

            if (ImGui::CollapsingHeader("Selection & Transform", defaultOpenFlags))
            {
                const char *interactionModeLabel = "Navigate";
                if (m_InteractionMode == InteractionMode::Select)
                    interactionModeLabel = "Select";
                else if (m_InteractionMode == InteractionMode::ViewSet)
                    interactionModeLabel = "ViewSet";
                else if (m_InteractionMode == InteractionMode::Translate)
                    interactionModeLabel = "Translate";
                else if (m_InteractionMode == InteractionMode::Rotate)
                    interactionModeLabel = "Rotate";

                ImGui::Text("Mode: %s", interactionModeLabel);
                ImGui::Text("Selection: %zu atoms", m_SelectedAtomIndices.size());
                if (m_SelectedTransformEmptyIndex >= 0 && m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size()))
                {
                    ImGui::Text("Selected empty: %s", m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].name.c_str());
                }
                if (ImGui::Button("Cycle mode"))
                {
                    ToggleInteractionMode();
                }
                ImGui::SameLine();
                DrawInlineHelpMarker("Selection: LMB select, Ctrl+LMB add/remove, B then drag for box select.\nHold Tab to open radial PieMenu for mode switching.\nViewSet: T/B/L/R/P/K to snap camera.");

                if (ImGui::Button("Clear selection"))
                {
                    m_SelectedAtomIndices.clear();
                    m_SelectedTransformEmptyIndex = -1;
                    m_SelectedSpecialNode = SpecialNodeSelection::None;
                }
                ImGui::SameLine();
                DrawInlineHelpMarker("Transform shortcuts in viewport: G translate, R rotate, S scale.\nIn ViewSet, R works as Right View.");

                const bool canUndoSceneEdit = !m_UndoStack.empty();
                const bool canRedoSceneEdit = !m_RedoStack.empty();
                if (!canUndoSceneEdit)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Undo (Ctrl+Z)") && canUndoSceneEdit)
                {
                    settingsChanged |= UndoSceneEdit();
                }
                if (!canUndoSceneEdit)
                {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                if (!canRedoSceneEdit)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Redo (Ctrl+Y)") && canRedoSceneEdit)
                {
                    settingsChanged |= RedoSceneEdit();
                }
                if (!canRedoSceneEdit)
                {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("History: %zu / %zu", m_UndoStack.size(), m_RedoStack.size());

                if (ImGui::Button("Hide selected (H)"))
                {
                    hideCurrentSelection();
                }
                ImGui::SameLine();
                if (ImGui::Button("Unhide all (Alt+H)"))
                {
                    unhideAllSceneElements();
                }

                const bool hasSelectedEmpty = (m_SelectedTransformEmptyIndex >= 0 && m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size()));
                const bool hasSelectedLight = (m_SelectedSpecialNode == SpecialNodeSelection::Light);
                const bool canMoveSelectionToCursor = !m_SelectedAtomIndices.empty() || hasSelectedEmpty || hasSelectedLight;
                if (!canMoveSelectionToCursor)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Move selected objects to 3D cursor") && canMoveSelectionToCursor)
                {
                    PushUndoSnapshot("Move selection to 3D cursor");
                    if (hasSelectedEmpty && m_SelectedAtomIndices.empty())
                    {
                        m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].position = m_CursorPosition;
                    }
                    else if (hasSelectedLight && m_SelectedAtomIndices.empty())
                    {
                        m_LightPosition = m_CursorPosition;
                    }
                    else
                    {
                        const glm::vec3 selectionCenter = ComputeSelectionCenter();
                        const glm::vec3 delta = m_CursorPosition - selectionCenter;
                        for (const std::size_t atomIndex : m_SelectedAtomIndices)
                        {
                            if (atomIndex >= m_WorkingStructure.atoms.size())
                            {
                                continue;
                            }
                            SetAtomCartesianPosition(atomIndex, GetAtomCartesianPosition(atomIndex) + delta);
                        }
                    }
                    settingsChanged = true;
                }
                if (!canMoveSelectionToCursor)
                {
                    ImGui::EndDisabled();
                }

                ImGui::SameLine();
                if (ImGui::Button("Move selected to Origin") && canMoveSelectionToCursor)
                {
                    PushUndoSnapshot("Move selection to origin");
                    if (hasSelectedEmpty && m_SelectedAtomIndices.empty())
                    {
                        m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].position = m_SceneOriginPosition;
                    }
                    else if (hasSelectedLight && m_SelectedAtomIndices.empty())
                    {
                        m_LightPosition = m_SceneOriginPosition;
                    }
                    else
                    {
                        const glm::vec3 selectionCenter = ComputeSelectionCenter();
                        const glm::vec3 delta = m_SceneOriginPosition - selectionCenter;
                        for (const std::size_t atomIndex : m_SelectedAtomIndices)
                        {
                            if (atomIndex >= m_WorkingStructure.atoms.size())
                            {
                                continue;
                            }
                            SetAtomCartesianPosition(atomIndex, GetAtomCartesianPosition(atomIndex) + delta);
                        }
                    }
                    settingsChanged = true;
                }
            }

            if (ImGui::CollapsingHeader("3D Cursor", defaultOpenFlags))
            {
                if (ImGui::Checkbox("Snap cursor to grid", &m_CursorSnapToGrid))
                {
                    settingsChanged = true;
                }
                if (DrawColoredVec3Control("Cursor position", m_CursorPosition, 0.01f, -1000.0f, 1000.0f, "%.5f"))
                {
                    settingsChanged = true;
                }

                static float s_CursorUniformXYZ = 0.0f;
                ImGui::SetNextItemWidth(120.0f);
                ImGui::InputFloat("Cursor XYZ", &s_CursorUniformXYZ, 0.1f, 1.0f, "%.4f");
                ImGui::SameLine();
                if (ImGui::Button("Apply cursor XYZ"))
                {
                    m_CursorPosition = glm::vec3(s_CursorUniformXYZ);
                    settingsChanged = true;
                }

                if (ImGui::SliderFloat("Cursor size", &m_CursorVisualScale, 0.05f, 1.00f, "%.2f"))
                {
                    settingsChanged = true;
                }
            }

            if (ImGui::CollapsingHeader("Origin & Light", defaultOpenFlags))
            {
                if (DrawColoredVec3Control("Light position", m_LightPosition, 0.01f, -1000.0f, 1000.0f, "%.5f"))
                {
                    settingsChanged = true;
                }

                if (ImGui::Button("Select Light"))
                {
                    m_SelectedSpecialNode = SpecialNodeSelection::Light;
                    m_SelectedAtomIndices.clear();
                    m_SelectedTransformEmptyIndex = -1;
                }

                if (ImGui::Button("3D cursor to Origin"))
                {
                    m_CursorPosition = m_SceneOriginPosition;
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cursor <- camera target") && m_Camera)
                {
                    m_CursorPosition = m_Camera->GetTarget();
                    settingsChanged = true;
                }
            }

            drawAtomActionsSection();

            if (ImGui::CollapsingHeader("Bond Workflow", defaultOpenFlags))
            {
                if (ImGui::Checkbox("Auto-generate bonds", &m_AutoBondGenerationEnabled))
                {
                    m_AutoBondsDirty = m_AutoBondGenerationEnabled;
                    if (!m_AutoBondGenerationEnabled)
                    {
                        m_GeneratedBonds.clear();
                        m_SelectedBondLabelKey = 0;
                        m_SelectedBondKeys.clear();
                    }
                    settingsChanged = true;
                }
                if (ImGui::Checkbox("Auto-recalculate after edits", &m_AutoRecalculateBondsOnEdit))
                {
                    if (m_AutoRecalculateBondsOnEdit && m_AutoBondGenerationEnabled)
                    {
                        m_AutoBondsDirty = true;
                    }
                    settingsChanged = true;
                }
                ImGui::SameLine();
                DrawInlineHelpMarker("When disabled, atom edits mark bonds dirty but do not rebuild them until you press Regenerate bonds.");

                const char *selectionFilters[] = {"Atoms", "Atoms + Bonds", "Bonds", "Bonds labels"};
                int selectionFilterIndex = static_cast<int>(m_SelectionFilter);
                if (ImGui::Combo("Selection filter", &selectionFilterIndex, selectionFilters, IM_ARRAYSIZE(selectionFilters)))
                {
                    m_SelectionFilter = static_cast<SelectionFilter>(std::clamp(selectionFilterIndex, 0, 3));
                    settingsChanged = true;
                }
                ImGui::SameLine();
                DrawInlineHelpMarker("Choose whether clicks and marquee tools target atoms, bonds, or only bond labels.");

                if (ImGui::Checkbox("Delete affects labels only", &m_BondLabelDeleteOnlyMode))
                {
                    settingsChanged = true;
                }
                ImGui::SameLine();
                DrawInlineHelpMarker("When enabled, Delete removes only the measurement label object, not the underlying bond.");

                ImGui::TextDisabled("Select tools: B = box, C = circle (mouse wheel changes circle radius)");

                if (ImGui::SliderFloat("Bond threshold scale", &m_BondThresholdScale, 0.80f, 1.80f, "%.2f"))
                {
                    m_AutoBondsDirty = true;
                    settingsChanged = true;
                }

                if (ImGui::Checkbox("Enable per-element-pair cutoff overrides", &m_BondUsePairThresholdOverrides))
                {
                    m_AutoBondsDirty = true;
                    settingsChanged = true;
                }
                ImGui::SameLine();
                DrawInlineHelpMarker("Overrides let you tune bond detection per element pair after the global threshold scale is applied.");

                if (m_BondUsePairThresholdOverrides && m_HasStructureLoaded && !m_WorkingStructure.species.empty())
                {
                    if (ImGui::TreeNode("Pair cutoff overrides"))
                    {
                        for (std::size_t i = 0; i < m_WorkingStructure.species.size(); ++i)
                        {
                            for (std::size_t j = i; j < m_WorkingStructure.species.size(); ++j)
                            {
                                const std::string key = BuildElementPairScaleKey(m_WorkingStructure.species[i], m_WorkingStructure.species[j]);
                                if (key.empty())
                                {
                                    continue;
                                }

                                float pairScale = ResolveBondThresholdScaleForPair(m_WorkingStructure.species[i], m_WorkingStructure.species[j]);
                                if (ImGui::SliderFloat(key.c_str(), &pairScale, 0.40f, 3.00f, "%.2f"))
                                {
                                    m_BondPairThresholdScaleOverrides[key] = pairScale;
                                    m_AutoBondsDirty = true;
                                    settingsChanged = true;
                                }
                            }
                        }

                        if (ImGui::Button("Clear pair overrides"))
                        {
                            m_BondPairThresholdScaleOverrides.clear();
                            m_AutoBondsDirty = true;
                            settingsChanged = true;
                        }

                        ImGui::TreePop();
                    }
                }

                const bool canRegenerateBonds = m_HasStructureLoaded && !m_WorkingStructure.atoms.empty();
                if (!canRegenerateBonds)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Regenerate bonds"))
                {
                    PushUndoSnapshot("Regenerate bonds");
                    m_AutoBondsDirty = true;
                    m_DeletedBondKeys.clear();
                    m_LastStructureOperationFailed = false;
                    m_LastStructureMessage = "Bond regeneration scheduled.";
                    settingsChanged = true;
                }
                ImGui::SameLine();
                DrawInlineHelpMarker("Rebuild bond connectivity from the current atom positions and cutoff settings. Useful after manual cleanup or threshold edits.");
                if (!canRegenerateBonds)
                {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(m_AutoBondsDirty ? "Status: pending rebuild" : "Status: cached");

                if (ImGui::Button("Restore deleted bonds"))
                {
                    PushUndoSnapshot("Restore deleted bonds");
                    m_DeletedBondKeys.clear();
                    settingsChanged = true;
                }
                ImGui::SameLine();
                const bool canCreateManualBond =
                    m_SelectedAtomIndices.size() == 2 &&
                    m_SelectedAtomIndices[0] < m_WorkingStructure.atoms.size() &&
                    m_SelectedAtomIndices[1] < m_WorkingStructure.atoms.size() &&
                    m_SelectedAtomIndices[0] != m_SelectedAtomIndices[1];
                if (!canCreateManualBond)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Create bond from selected atoms") && canCreateManualBond)
                {
                    PushUndoSnapshot("Create manual bond");
                    const std::size_t atomA = m_SelectedAtomIndices[0];
                    const std::size_t atomB = m_SelectedAtomIndices[1];
                    const std::uint64_t bondKey = MakeBondPairKey(atomA, atomB);

                    m_ManualBondKeys.insert(bondKey);
                    m_DeletedBondKeys.erase(bondKey);
                    m_HiddenBondKeys.erase(bondKey);

                    BondLabelState &labelState = m_BondLabelStates[bondKey];
                    labelState.atomA = std::min(atomA, atomB);
                    labelState.atomB = std::max(atomA, atomB);
                    labelState.scale = glm::clamp(labelState.scale, 0.25f, 4.0f);
                    labelState.deleted = false;
                    labelState.hidden = false;

                    m_AutoBondsDirty = true;
                    m_LastStructureOperationFailed = false;
                    m_LastStructureMessage = "Manual bond created between atom " +
                                             std::to_string(labelState.atomA) + " and atom " +
                                             std::to_string(labelState.atomB) + ".";
                    settingsChanged = true;
                }
                if (!canCreateManualBond)
                {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                const bool canCreateAngleLabel =
                    m_SelectedAtomIndices.size() >= 3 &&
                    m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 3] < m_WorkingStructure.atoms.size() &&
                    m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 2] < m_WorkingStructure.atoms.size() &&
                    m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 1] < m_WorkingStructure.atoms.size() &&
                    m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 3] != m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 2] &&
                    m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 2] != m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 1] &&
                    m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 3] != m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 1];
                if (!canCreateAngleLabel)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Create angle label (last 3 selected)") && canCreateAngleLabel)
                {
                    PushUndoSnapshot("Create angle label");
                    const std::size_t atomA = m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 3];
                    const std::size_t atomB = m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 2];
                    const std::size_t atomC = m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 1];

                    AngleLabelState angleState;
                    angleState.atomA = atomA;
                    angleState.atomB = atomB;
                    angleState.atomC = atomC;
                    m_AngleLabelStates[MakeAngleTripletKey(atomA, atomB, atomC)] = angleState;

                    m_LastStructureOperationFailed = false;
                    m_LastStructureMessage = "Added static angle label for atoms " +
                                             std::to_string(atomA) + "-" +
                                             std::to_string(atomB) + "-" +
                                             std::to_string(atomC) + ".";
                    settingsChanged = true;
                }
                if (!canCreateAngleLabel)
                {
                    ImGui::EndDisabled();
                }
                ImGui::TextDisabled("Angle order: last 3 selected atoms = A-B-C (B is the vertex).");
                if (ImGui::Button("Hide selected bonds/labels"))
                {
                    PushUndoSnapshot("Hide selected bonds and labels");
                    for (std::uint64_t key : m_SelectedBondKeys)
                    {
                        m_HiddenBondKeys.insert(key);
                        auto stateIt = m_BondLabelStates.find(key);
                        if (stateIt != m_BondLabelStates.end())
                        {
                            stateIt->second.hidden = true;
                        }
                    }
                    if (m_SelectedBondLabelKey != 0)
                    {
                        auto stateIt = m_BondLabelStates.find(m_SelectedBondLabelKey);
                        if (stateIt != m_BondLabelStates.end())
                        {
                            stateIt->second.hidden = true;
                        }
                    }
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Restore hidden bonds"))
                {
                    PushUndoSnapshot("Restore hidden bonds");
                    m_HiddenBondKeys.clear();
                    for (auto &[key, state] : m_BondLabelStates)
                    {
                        (void)key;
                        if (!state.deleted)
                        {
                            state.hidden = false;
                        }
                    }
                    settingsChanged = true;
                }
                ImGui::SameLine();
                ImGui::Text("Selected bonds: %zu", m_SelectedBondKeys.size());

                ImGui::Text("Angle labels: %zu", m_AngleLabelStates.size());
                const bool hasAngleLabels = !m_AngleLabelStates.empty();
                if (!hasAngleLabels)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Clear all angle labels") && hasAngleLabels)
                {
                    PushUndoSnapshot("Clear angle labels");
                    m_AngleLabelStates.clear();
                    settingsChanged = true;
                }
                if (!hasAngleLabels)
                {
                    ImGui::EndDisabled();
                }
            }

            if (ImGui::CollapsingHeader("Status", defaultOpenFlags))
            {
                if (m_HasStructureLoaded)
                {
                    const char *modeLabel = m_WorkingStructure.coordinateMode == CoordinateMode::Direct ? "Direct" : "Cartesian";
                    ImGui::Separator();
                    ImGui::Text("Loaded: %d atoms | %zu species | mode: %s",
                                m_WorkingStructure.GetAtomCount(),
                                m_WorkingStructure.species.size(),
                                modeLabel);
                    ImGui::Text("Title: %s", m_WorkingStructure.title.empty() ? "(empty)" : m_WorkingStructure.title.c_str());
                    ImGui::Text("Bonds: %zu (auto=%s, threshold x%.2f)",
                                m_GeneratedBonds.size(),
                                m_AutoBondGenerationEnabled ? "on" : "off",
                                m_BondThresholdScale);
                }
                else
                {
                    ImGui::Separator();
                    ImGui::TextUnformatted("Loaded: none");
                }
            }

            if (!m_LastStructureMessage.empty())
            {
                const ImVec4 color = m_LastStructureOperationFailed
                                         ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                                         : ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextWrapped("%s", m_LastStructureMessage.c_str());
                ImGui::PopStyleColor();
            }

            std::size_t errorCount = Logger::Get().GetErrorCount();
            ImGui::Separator();
            ImGui::Text("Errors captured: %zu", errorCount);
            ImGui::End();
            }

            if (m_ShowAppearancePanel)
            {
                ImGui::SetNextWindowSize(ImVec2(420.0f, 720.0f), ImGuiCond_FirstUseEver);
                ImGui::Begin("Appearance", &m_ShowAppearancePanel);
                ImGui::TextUnformatted("Scene look");
                drawAtomAppearanceSection();
                drawBondAppearanceSection();
                ImGui::End();
            }
        }

        if (m_ShowSceneOutlinerPanel)
        {
            ImGui::Begin("Scene Outliner", &m_ShowSceneOutlinerPanel);

            enum class OutlinerIconKind
            {
                Collection,
                Empty,
                Atom,
                Group
            };

            auto drawOutlinerIcon = [&](OutlinerIconKind kind, ImU32 color)
            {
                (void)kind;
                (void)color;
                ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
            };

            if (ImGui::Button("Add collection"))
            {
                PushUndoSnapshot("Add collection");
                SceneCollection collection;
                collection.id = GenerateSceneUUID();
                char label[48] = {};
                std::snprintf(label, sizeof(label), "Collection %d", m_CollectionCounter++);
                collection.name = label;
                m_Collections.push_back(collection);
                m_ActiveCollectionIndex = static_cast<int>(m_Collections.size()) - 1;
                m_SelectedCollectionIndices.clear();
                m_SelectedCollectionIndices.insert(m_ActiveCollectionIndex);
                m_OutlinerCollectionSelectionAnchor = static_cast<std::size_t>(m_ActiveCollectionIndex);
                settingsChanged = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Add a new collection.");
            }

            ImGui::SameLine();
            const bool canDuplicateCollection =
                m_ActiveCollectionIndex >= 0 &&
                m_ActiveCollectionIndex < static_cast<int>(m_Collections.size());
            if (!canDuplicateCollection)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Duplicate active collection") && canDuplicateCollection)
            {
                settingsChanged |= DuplicateCollection(m_ActiveCollectionIndex);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Duplicate the active collection.\nShortcut: Ctrl+D while Scene Outliner is focused.");
            }
            if (!canDuplicateCollection)
            {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            const bool canDeleteCollection =
                m_Collections.size() > 1 &&
                m_ActiveCollectionIndex >= 0 &&
                m_ActiveCollectionIndex < static_cast<int>(m_Collections.size());
            if (!canDeleteCollection)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Delete active collection") && canDeleteCollection)
            {
                PushUndoSnapshot("Delete collection");
                settingsChanged |= DeleteCollectionAtIndex(m_ActiveCollectionIndex);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Delete the active collection.\nShortcut: Delete while Scene Outliner is focused.");
            }
            if (!canDeleteCollection)
            {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            const bool canAssignAtomsToCollection = !m_SelectedAtomIndices.empty() &&
                                                    m_ActiveCollectionIndex >= 0 &&
                                                    m_ActiveCollectionIndex < static_cast<int>(m_Collections.size());
            if (!canAssignAtomsToCollection)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Assign selected atoms -> active collection") && canAssignAtomsToCollection)
            {
                PushUndoSnapshot("Assign atoms to collection");
                for (std::size_t atomIndex : m_SelectedAtomIndices)
                {
                    if (atomIndex < m_AtomCollectionIndices.size())
                    {
                        m_AtomCollectionIndices[atomIndex] = m_ActiveCollectionIndex;
                    }
                }
                settingsChanged = true;
            }
            if (!canAssignAtomsToCollection)
            {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            const bool canExtractSelectionToCollection = !m_SelectedAtomIndices.empty() || m_SelectedTransformEmptyIndex >= 0;
            if (!canExtractSelectionToCollection)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Extract selection -> new collection") && canExtractSelectionToCollection)
            {
                settingsChanged |= ExtractSelectionToNewCollection();
            }
            if (!canExtractSelectionToCollection)
            {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            const bool canCreateGroup = !m_SelectedAtomIndices.empty() || m_SelectedTransformEmptyIndex >= 0;
            if (!canCreateGroup)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Create group from selection") && canCreateGroup)
            {
                PushUndoSnapshot("Create group from selection");
                settingsChanged |= SceneGroupingBackend::CreateGroupFromCurrentSelection(*this);
            }
            if (!canCreateGroup)
            {
                ImGui::EndDisabled();
            }

            const bool canRenameCollection =
                m_ActiveCollectionIndex >= 0 &&
                m_ActiveCollectionIndex < static_cast<int>(m_Collections.size());
            const bool outlinerWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (canRenameCollection &&
                outlinerWindowFocused &&
                ImGui::IsKeyPressed(ImGuiKey_F2, false) &&
                !ImGui::IsAnyItemActive())
            {
                BeginCollectionRename(m_ActiveCollectionIndex);
            }
            if (canRenameCollection &&
                outlinerWindowFocused &&
                !ImGui::IsAnyItemActive() &&
                io.KeyCtrl &&
                ImGui::IsKeyPressed(ImGuiKey_D, false))
            {
                settingsChanged |= DuplicateCollection(m_ActiveCollectionIndex);
            }
            if (canRenameCollection &&
                outlinerWindowFocused &&
                !ImGui::IsAnyItemActive() &&
                isConfiguredKeyPressed(m_HotkeyDeleteSelection) &&
                m_Collections.size() > 1)
            {
                PushUndoSnapshot("Delete collection");
                settingsChanged |= DeleteCollectionAtIndex(m_ActiveCollectionIndex);
            }

            if (m_RenameCollectionDialogOpen)
            {
                ImGui::OpenPopup("Rename Collection");
                m_RenameCollectionDialogOpen = false;
            }

            if (ImGui::BeginPopupModal("Rename Collection", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (ImGui::IsWindowAppearing())
                {
                    ImGui::SetKeyboardFocusHere();
                }

                bool confirmRename = ImGui::InputText(
                    "Name",
                    m_RenameCollectionBuffer.data(),
                    m_RenameCollectionBuffer.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

                auto trimCollectionName = [](std::string value) -> std::string
                {
                    const std::string whitespace = " \t\r\n";
                    const std::size_t first = value.find_first_not_of(whitespace);
                    if (first == std::string::npos)
                    {
                        return std::string();
                    }

                    const std::size_t last = value.find_last_not_of(whitespace);
                    return value.substr(first, last - first + 1);
                };

                if (ImGui::Button("Rename"))
                {
                    confirmRename = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    m_RenameCollectionTargetIndex = -1;
                    ImGui::CloseCurrentPopup();
                }

                const bool canConfirmRename =
                    m_RenameCollectionTargetIndex >= 0 &&
                    m_RenameCollectionTargetIndex < static_cast<int>(m_Collections.size());
                if (confirmRename && canConfirmRename)
                {
                    const std::string trimmedName = trimCollectionName(std::string(m_RenameCollectionBuffer.data()));
                    if (!trimmedName.empty())
                    {
                        SceneCollection &collection = m_Collections[static_cast<std::size_t>(m_RenameCollectionTargetIndex)];
                        if (trimmedName != collection.name)
                        {
                            PushUndoSnapshot("Rename collection");
                            collection.name = trimmedName;
                            m_ActiveCollectionIndex = m_RenameCollectionTargetIndex;
                            settingsChanged = true;
                        }
                        m_RenameCollectionTargetIndex = -1;
                        ImGui::CloseCurrentPopup();
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::SeparatorText("Collections");
            ImGui::TextDisabled("Tip: F2 rename, Ctrl+D duplicate, Delete remove active collection.");

            auto selectAtomRangeInCollection = [&](std::size_t collectionIndex, std::size_t anchorIndex, std::size_t clickedIndex, bool additive)
            {
                const std::size_t rangeStart = std::min(anchorIndex, clickedIndex);
                const std::size_t rangeEnd = std::max(anchorIndex, clickedIndex);

                if (!additive)
                {
                    m_SelectedAtomIndices.clear();
                }

                for (std::size_t atomIndex = 0; atomIndex < m_WorkingStructure.atoms.size(); ++atomIndex)
                {
                    if (ResolveAtomCollectionIndex(atomIndex) != static_cast<int>(collectionIndex))
                    {
                        continue;
                    }

                    if (atomIndex < rangeStart || atomIndex > rangeEnd)
                    {
                        continue;
                    }

                    if (IsAtomHidden(atomIndex) || !IsAtomCollectionSelectable(atomIndex))
                    {
                        continue;
                    }

                    if (!IsAtomSelected(atomIndex))
                    {
                        m_SelectedAtomIndices.push_back(atomIndex);
                    }
                }
            };

            auto setSingleSelectedCollection = [&](std::size_t collectionIndex)
            {
                m_SelectedCollectionIndices.clear();
                m_SelectedCollectionIndices.insert(static_cast<int>(collectionIndex));
            };

            auto toggleSelectedCollection = [&](std::size_t collectionIndex)
            {
                const int collectionIndexInt = static_cast<int>(collectionIndex);
                const auto selectedIt = m_SelectedCollectionIndices.find(collectionIndexInt);
                if (selectedIt != m_SelectedCollectionIndices.end())
                {
                    m_SelectedCollectionIndices.erase(selectedIt);
                }
                else
                {
                    m_SelectedCollectionIndices.insert(collectionIndexInt);
                }
            };

            auto selectCollectionRange = [&](std::size_t anchorCollectionIndex, std::size_t clickedCollectionIndex, bool additive)
            {
                const std::size_t rangeStart = std::min(anchorCollectionIndex, clickedCollectionIndex);
                const std::size_t rangeEnd = std::max(anchorCollectionIndex, clickedCollectionIndex);

                if (!additive)
                {
                    m_SelectedCollectionIndices.clear();
                }

                for (std::size_t currentCollectionIndex = rangeStart; currentCollectionIndex <= rangeEnd; ++currentCollectionIndex)
                {
                    m_SelectedCollectionIndices.insert(static_cast<int>(currentCollectionIndex));
                }
            };

            auto resolveCollectionRangeAnchor = [&](std::size_t clickedCollectionIndex) -> std::size_t
            {
                if (m_OutlinerCollectionSelectionAnchor.has_value())
                {
                    return *m_OutlinerCollectionSelectionAnchor;
                }
                if (m_ActiveCollectionIndex >= 0 && m_ActiveCollectionIndex < static_cast<int>(m_Collections.size()))
                {
                    return static_cast<std::size_t>(m_ActiveCollectionIndex);
                }
                return clickedCollectionIndex;
            };

            auto trimOutlinerName = [](std::string value) -> std::string
            {
                const std::string whitespace = " \t\r\n";
                const std::size_t first = value.find_first_not_of(whitespace);
                if (first == std::string::npos)
                {
                    return std::string();
                }

                const std::size_t last = value.find_last_not_of(whitespace);
                return value.substr(first, last - first + 1);
            };

            static std::array<char, 128> s_EmptyRenameBuffer = {};
            static int s_EmptyRenameIndex = -1;
            auto applyOutlinerTreeOpenState = [&](const std::string &key, bool defaultOpen)
            {
                const auto it = m_OutlinerTreeOpenStates.find(key);
                if (it != m_OutlinerTreeOpenStates.end())
                {
                    ImGui::SetNextItemOpen(it->second, ImGuiCond_Always);
                }
                else
                {
                    ImGui::SetNextItemOpen(defaultOpen, ImGuiCond_Once);
                }
            };
            auto rememberOutlinerTreeOpenState = [&](const std::string &key, bool isOpen)
            {
                m_OutlinerTreeOpenStates[key] = isOpen;
            };

            for (std::size_t collectionIndex = 0; collectionIndex < m_Collections.size(); ++collectionIndex)
            {
                SceneCollection &collection = m_Collections[collectionIndex];
                ImGui::PushID(static_cast<int>(collectionIndex));

                if (ImGui::Checkbox("##visible", &collection.visible))
                {
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("##selectable", &collection.selectable))
                {
                    settingsChanged = true;
                }
                ImGui::SameLine();
                drawOutlinerIcon(OutlinerIconKind::Collection, IM_COL32(255, 174, 43, 220));
                ImGui::SameLine(0.0f, 6.0f);

                const int collectionIndexInt = static_cast<int>(collectionIndex);
                const bool isActiveCollection = (collectionIndexInt == m_ActiveCollectionIndex);
                const bool isCollectionSelected = m_SelectedCollectionIndices.find(collectionIndexInt) != m_SelectedCollectionIndices.end();
                const std::string collectionLabel = collection.name + "##collection_" + std::to_string(collection.id);
                if (ImGui::Selectable(collectionLabel.c_str(), isCollectionSelected || isActiveCollection, ImGuiSelectableFlags_SpanAvailWidth))
                {
                    m_ActiveCollectionIndex = collectionIndexInt;
                    const bool additiveCollectionSelection = io.KeyCtrl;
                    const bool rangeCollectionSelection = io.KeyShift;
                    if (rangeCollectionSelection)
                    {
                        selectCollectionRange(resolveCollectionRangeAnchor(collectionIndex), collectionIndex, additiveCollectionSelection);
                    }
                    else if (additiveCollectionSelection)
                    {
                        toggleSelectedCollection(collectionIndex);
                    }
                    else
                    {
                        setSingleSelectedCollection(collectionIndex);
                    }
                    m_OutlinerCollectionSelectionAnchor = collectionIndex;
                }
                const ImVec2 collectionRowRectMin = ImGui::GetItemRectMin();
                ImVec2 collectionRowRectMax = ImGui::GetItemRectMax();
                const ImGuiID collectionRowId = ImGui::GetItemID();
                if (ImGui::BeginPopupContextItem("##CollectionContextMenu"))
                {
                    m_ActiveCollectionIndex = collectionIndexInt;
                    if (m_SelectedCollectionIndices.find(collectionIndexInt) == m_SelectedCollectionIndices.end())
                    {
                        setSingleSelectedCollection(collectionIndex);
                        m_OutlinerCollectionSelectionAnchor = collectionIndex;
                    }
                    if (ImGui::MenuItem("Rename", "F2"))
                    {
                        BeginCollectionRename(collectionIndexInt);
                    }
                    if (ImGui::MenuItem("Copy", "Ctrl+C"))
                    {
                        CopyCollectionToClipboard(collectionIndexInt);
                    }
                    if (ImGui::MenuItem("Paste", "Ctrl+V", false, HasClipboardPayload()))
                    {
                        settingsChanged |= PasteClipboard();
                    }
                    if (ImGui::MenuItem("Export POSCAR"))
                    {
                        std::string selectedPath;
                        if (SaveNativeFileDialog(selectedPath))
                        {
                            const CoordinateMode exportMode = (m_ExportCoordinateModeIndex == 0)
                                                                  ? CoordinateMode::Direct
                                                                  : CoordinateMode::Cartesian;
                            settingsChanged |= ExportCollectionToPath(static_cast<int>(collectionIndex), selectedPath, exportMode, m_ExportPrecision);
                        }
                    }
                    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                    {
                        settingsChanged |= DuplicateCollection(collectionIndexInt);
                    }
                    if (ImGui::MenuItem("Delete", "Delete", false, m_Collections.size() > 1))
                    {
                        PushUndoSnapshot("Delete collection");
                        settingsChanged |= DeleteCollectionAtIndex(collectionIndexInt);
                    }
                    ImGui::EndPopup();
                }
                if (isActiveCollection)
                {
                    ImGui::SameLine(0.0f, 6.0f);
                    ImGui::TextDisabled("[active]");
                    const ImVec2 activeRectMax = ImGui::GetItemRectMax();
                    collectionRowRectMax.x = std::max(collectionRowRectMax.x, activeRectMax.x);
                    collectionRowRectMax.y = std::max(collectionRowRectMax.y, activeRectMax.y);
                }

                ImRect collectionDropRect(collectionRowRectMin, collectionRowRectMax);
                if (ImGui::BeginDragDropTargetCustom(collectionDropRect, collectionRowId))
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DS_ATOM_INDEX_LIST"))
                    {
                        const std::size_t atomCount = payload->DataSize / sizeof(std::uint64_t);
                        if (atomCount > 0 && payload->DataSize == atomCount * sizeof(std::uint64_t))
                        {
                            const auto *atomIndices = static_cast<const std::uint64_t *>(payload->Data);
                            bool anyCollectionChange = false;
                            for (std::size_t i = 0; i < atomCount; ++i)
                            {
                                const std::size_t atomIndex = static_cast<std::size_t>(atomIndices[i]);
                                if (atomIndex < m_AtomCollectionIndices.size() &&
                                    m_AtomCollectionIndices[atomIndex] != collectionIndexInt)
                                {
                                    anyCollectionChange = true;
                                    break;
                                }
                            }

                            if (anyCollectionChange)
                            {
                                PushUndoSnapshot("Move atoms to collection");
                                for (std::size_t i = 0; i < atomCount; ++i)
                                {
                                    const std::size_t atomIndex = static_cast<std::size_t>(atomIndices[i]);
                                    if (atomIndex < m_AtomCollectionIndices.size())
                                    {
                                        m_AtomCollectionIndices[atomIndex] = collectionIndexInt;
                                    }
                                }
                                settingsChanged = true;
                            }
                        }
                    }
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DS_EMPTY_INDEX"))
                    {
                        if (payload->DataSize == sizeof(int))
                        {
                            const int emptyIndex = *static_cast<const int *>(payload->Data);
                            if (emptyIndex >= 0 && emptyIndex < static_cast<int>(m_TransformEmpties.size()))
                            {
                                m_TransformEmpties[static_cast<std::size_t>(emptyIndex)].collectionIndex = static_cast<int>(collectionIndex);
                                m_TransformEmpties[static_cast<std::size_t>(emptyIndex)].collectionId = m_Collections[collectionIndex].id;
                                settingsChanged = true;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                const std::string objectsTreeKey = "collection_objects_" + std::to_string(collection.id);
                applyOutlinerTreeOpenState(objectsTreeKey, false);
                const bool objectsTreeOpen = ImGui::TreeNode("Objects##CollectionObjectsTree");
                rememberOutlinerTreeOpenState(objectsTreeKey, objectsTreeOpen);
                if (objectsTreeOpen)
                {
                    std::function<void(SceneUUID)> drawEmptyHierarchy = [&](SceneUUID parentId)
                    {
                        for (std::size_t emptyIndex = 0; emptyIndex < m_TransformEmpties.size(); ++emptyIndex)
                        {
                            TransformEmpty &empty = m_TransformEmpties[emptyIndex];
                            if (empty.collectionIndex != static_cast<int>(collectionIndex) || empty.parentEmptyId != parentId)
                            {
                                continue;
                            }

                            const bool isSelected = (m_SelectedTransformEmptyIndex == static_cast<int>(emptyIndex));
                            const std::string label = empty.name + "##empty_" + std::to_string(empty.id);
                            ImGui::PushID(static_cast<int>(emptyIndex));
                            drawOutlinerIcon(OutlinerIconKind::Empty, IM_COL32(172, 186, 205, 220));
                            ImGui::SameLine(0.0f, 6.0f);
                            const std::string emptyTreeKey = "empty_" + std::to_string(empty.id);
                            applyOutlinerTreeOpenState(emptyTreeKey, isSelected);
                            const bool open = ImGui::TreeNodeEx(
                                label.c_str(),
                                (isSelected ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
                            rememberOutlinerTreeOpenState(emptyTreeKey, open);

                            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                            {
                                m_SelectedTransformEmptyIndex = static_cast<int>(emptyIndex);
                                m_ActiveTransformEmptyIndex = static_cast<int>(emptyIndex);
                                m_SelectedAtomIndices.clear();
                                m_OutlinerAtomSelectionAnchor.reset();
                            }
                            if (ImGui::BeginPopupContextItem())
                            {
                                m_SelectedTransformEmptyIndex = static_cast<int>(emptyIndex);
                                m_ActiveTransformEmptyIndex = static_cast<int>(emptyIndex);
                                m_SelectedAtomIndices.clear();
                                m_OutlinerAtomSelectionAnchor.reset();

                                if (s_EmptyRenameIndex != static_cast<int>(emptyIndex) || ImGui::IsWindowAppearing())
                                {
                                    std::snprintf(s_EmptyRenameBuffer.data(), s_EmptyRenameBuffer.size(), "%s", empty.name.c_str());
                                    s_EmptyRenameIndex = static_cast<int>(emptyIndex);
                                }

                                ImGui::TextUnformatted("Rename");
                                ImGui::SetNextItemWidth(180.0f);
                                const bool renameSubmitted = ImGui::InputText("##RenameEmptyInline", s_EmptyRenameBuffer.data(), s_EmptyRenameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                                if (renameSubmitted || ImGui::SmallButton("Apply name"))
                                {
                                    const std::string trimmedName = trimOutlinerName(std::string(s_EmptyRenameBuffer.data()));
                                    if (!trimmedName.empty() && trimmedName != empty.name)
                                    {
                                        PushUndoSnapshot("Rename empty");
                                        empty.name = trimmedName;
                                        settingsChanged = true;
                                    }
                                }

                                if (ImGui::MenuItem("Copy", "Ctrl+C"))
                                {
                                    m_SelectedTransformEmptyIndex = static_cast<int>(emptyIndex);
                                    m_SelectedAtomIndices.clear();
                                    CopyCurrentSelectionToClipboard();
                                }
                                if (ImGui::MenuItem("Paste", "Ctrl+V", false, HasClipboardPayload()))
                                {
                                    settingsChanged |= PasteClipboard();
                                }
                                if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                                {
                                    m_SelectedTransformEmptyIndex = static_cast<int>(emptyIndex);
                                    m_SelectedAtomIndices.clear();
                                    settingsChanged |= DuplicateCurrentSelection();
                                }
                                if (ImGui::MenuItem("Delete"))
                                {
                                    PushUndoSnapshot("Delete empty");
                                    DeleteTransformEmptyAtIndex(static_cast<int>(emptyIndex));
                                    settingsChanged = true;
                                }
                                ImGui::EndPopup();
                            }

                            const int dragIndex = static_cast<int>(emptyIndex);
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                ImGui::SetDragDropPayload("DS_EMPTY_INDEX", &dragIndex, sizeof(int));
                                ImGui::Text("Move Empty: %s", empty.name.c_str());
                                ImGui::EndDragDropSource();
                            }

                            if (ImGui::BeginDragDropTarget())
                            {
                                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DS_EMPTY_INDEX"))
                                {
                                    if (payload->DataSize == sizeof(int))
                                    {
                                        const int childIndex = *static_cast<const int *>(payload->Data);
                                        if (childIndex >= 0 && childIndex < static_cast<int>(m_TransformEmpties.size()) && childIndex != static_cast<int>(emptyIndex))
                                        {
                                            m_TransformEmpties[static_cast<std::size_t>(childIndex)].parentEmptyId = empty.id;
                                            m_TransformEmpties[static_cast<std::size_t>(childIndex)].collectionIndex = static_cast<int>(collectionIndex);
                                            m_TransformEmpties[static_cast<std::size_t>(childIndex)].collectionId = m_Collections[collectionIndex].id;
                                            settingsChanged = true;
                                        }
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            if (open)
                            {
                                drawEmptyHierarchy(empty.id);
                                ImGui::TreePop();
                            }

                            ImGui::PopID();
                        }
                    };

                    drawEmptyHierarchy(0);

                    {
                        std::size_t collectionAtomCount = 0;
                        for (std::size_t atomIndex = 0; atomIndex < m_WorkingStructure.atoms.size(); ++atomIndex)
                        {
                            if (ResolveAtomCollectionIndex(atomIndex) == static_cast<int>(collectionIndex))
                            {
                                ++collectionAtomCount;
                            }
                        }

                        const std::string atomsLabel = "Atoms (" + std::to_string(collectionAtomCount) + ")##CollectionAtomsTree";
                        drawOutlinerIcon(OutlinerIconKind::Atom, IM_COL32(212, 220, 228, 220));
                        ImGui::SameLine(0.0f, 6.0f);
                        const std::string atomsTreeKey = "collection_atoms_" + std::to_string(collection.id);
                        applyOutlinerTreeOpenState(atomsTreeKey, false);
                        const bool atomsTreeOpen = ImGui::TreeNode(atomsLabel.c_str());
                        rememberOutlinerTreeOpenState(atomsTreeKey, atomsTreeOpen);
                        if (ImGui::BeginPopupContextItem("##CollectionAtomsContextMenu"))
                        {
                            if (ImGui::MenuItem("Select all"))
                            {
                                m_SelectedAtomIndices.clear();
                                for (std::size_t atomIndex = 0; atomIndex < m_WorkingStructure.atoms.size(); ++atomIndex)
                                {
                                    if (ResolveAtomCollectionIndex(atomIndex) != static_cast<int>(collectionIndex))
                                    {
                                        continue;
                                    }
                                    if (IsAtomHidden(atomIndex) || !IsAtomCollectionSelectable(atomIndex))
                                    {
                                        continue;
                                    }

                                    m_SelectedAtomIndices.push_back(atomIndex);
                                }

                                m_SelectedTransformEmptyIndex = -1;
                                m_SelectedBondKeys.clear();
                                m_SelectedBondLabelKey = 0;
                                m_SelectedSpecialNode = SpecialNodeSelection::None;
                                if (!m_SelectedAtomIndices.empty())
                                {
                                    m_OutlinerAtomSelectionAnchor = m_SelectedAtomIndices.back();
                                }
                                else
                                {
                                    m_OutlinerAtomSelectionAnchor.reset();
                                }
                                settingsChanged = true;
                            }
                            ImGui::EndPopup();
                        }
                        if (atomsTreeOpen)
                        {
                            for (std::size_t atomIndex = 0; atomIndex < m_WorkingStructure.atoms.size(); ++atomIndex)
                            {
                                if (ResolveAtomCollectionIndex(atomIndex) != static_cast<int>(collectionIndex))
                                {
                                    continue;
                                }

                                const bool hidden = IsAtomHidden(atomIndex);
                                const bool selectableAtom = IsAtomCollectionSelectable(atomIndex);
                                const bool isSelected = IsAtomSelected(atomIndex);
                                std::string atomLabel = m_WorkingStructure.atoms[atomIndex].element + " [" + std::to_string(atomIndex) + "]";
                                if (hidden)
                                {
                                    atomLabel += " (hidden)";
                                }

                                if (!selectableAtom)
                                {
                                    ImGui::BeginDisabled();
                                }

                                ImGui::PushID(static_cast<int>(atomIndex));
                                const glm::vec3 atomColor = ColorFromElement(m_WorkingStructure.atoms[atomIndex].element);
                                drawOutlinerIcon(
                                    OutlinerIconKind::Atom,
                                    IM_COL32(
                                        static_cast<int>(glm::clamp(atomColor.r, 0.0f, 1.0f) * 255.0f),
                                        static_cast<int>(glm::clamp(atomColor.g, 0.0f, 1.0f) * 255.0f),
                                        static_cast<int>(glm::clamp(atomColor.b, 0.0f, 1.0f) * 255.0f),
                                        220));
                                ImGui::SameLine(0.0f, 6.0f);
                                if (ImGui::Selectable(atomLabel.c_str(), isSelected))
                                {
                                    const bool shiftSelection = io.KeyShift && m_OutlinerAtomSelectionAnchor.has_value();
                                    if (shiftSelection)
                                    {
                                        selectAtomRangeInCollection(collectionIndex, *m_OutlinerAtomSelectionAnchor, atomIndex, io.KeyCtrl);
                                    }
                                    else if (io.KeyCtrl)
                                    {
                                        if (isSelected)
                                        {
                                            m_SelectedAtomIndices.erase(
                                                std::remove(m_SelectedAtomIndices.begin(), m_SelectedAtomIndices.end(), atomIndex),
                                                m_SelectedAtomIndices.end());
                                        }
                                        else if (!hidden)
                                        {
                                            m_SelectedAtomIndices.push_back(atomIndex);
                                        }
                                    }
                                    else
                                    {
                                        m_SelectedAtomIndices.clear();
                                        if (!hidden)
                                        {
                                            m_SelectedAtomIndices.push_back(atomIndex);
                                        }
                                    }
                                    m_SelectedTransformEmptyIndex = -1;
                                    if (!hidden && selectableAtom)
                                    {
                                        m_OutlinerAtomSelectionAnchor = atomIndex;
                                    }
                                }
                                if (!hidden && selectableAtom && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                                {
                                    std::vector<std::uint64_t> draggedAtomIndices;
                                    if (isSelected && !m_SelectedAtomIndices.empty())
                                    {
                                        draggedAtomIndices.reserve(m_SelectedAtomIndices.size());
                                        for (std::size_t selectedAtomIndex : m_SelectedAtomIndices)
                                        {
                                            draggedAtomIndices.push_back(static_cast<std::uint64_t>(selectedAtomIndex));
                                        }
                                    }
                                    else
                                    {
                                        draggedAtomIndices.push_back(static_cast<std::uint64_t>(atomIndex));
                                    }

                                    ImGui::SetDragDropPayload(
                                        "DS_ATOM_INDEX_LIST",
                                        draggedAtomIndices.data(),
                                        draggedAtomIndices.size() * sizeof(std::uint64_t));
                                    ImGui::Text("Move %zu atom(s)", draggedAtomIndices.size());
                                    ImGui::EndDragDropSource();
                                }
                                if (ImGui::BeginPopupContextItem())
                                {
                                    if (!hidden)
                                    {
                                        m_SelectedAtomIndices.clear();
                                        m_SelectedAtomIndices.push_back(atomIndex);
                                        m_OutlinerAtomSelectionAnchor = atomIndex;
                                    }
                                    m_SelectedTransformEmptyIndex = -1;

                                    if (ImGui::MenuItem("Copy", "Ctrl+C", false, !hidden && selectableAtom))
                                    {
                                        CopyCurrentSelectionToClipboard();
                                    }
                                    if (ImGui::MenuItem("Paste", "Ctrl+V", false, HasClipboardPayload()))
                                    {
                                        settingsChanged |= PasteClipboard();
                                    }
                                    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !hidden && selectableAtom))
                                    {
                                        settingsChanged |= DuplicateCurrentSelection();
                                    }
                                    if (ImGui::MenuItem("Delete", nullptr, false, !hidden && selectableAtom))
                                    {
                                        deleteCurrentSelection();
                                    }

                                    ImGui::SeparatorText("Change type");
                                    ImGui::SetNextItemWidth(96.0f);
                                    ImGui::InputText("##OutlinerAtomType", m_ChangeAtomElementBuffer.data(), m_ChangeAtomElementBuffer.size());
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Table"))
                                    {
                                        OpenPeriodicTable(PeriodicTableTarget::ChangeSelectedAtoms, true);
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Apply"))
                                    {
                                        if (ApplyElementToSelectedAtoms(std::string(m_ChangeAtomElementBuffer.data())))
                                        {
                                            settingsChanged = true;
                                        }
                                    }
                                    ImGui::EndPopup();
                                }

                                if (!selectableAtom)
                                {
                                    ImGui::EndDisabled();
                                }

                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                    }

                    if (collectionIndex == 0)
                    {
                        drawOutlinerIcon(OutlinerIconKind::Group, IM_COL32(255, 128, 0, 220));
                        ImGui::SameLine(0.0f, 6.0f);
                        const std::string groupsTreeKey = "collection_groups_" + std::to_string(collection.id);
                        applyOutlinerTreeOpenState(groupsTreeKey, false);
                        const bool groupsTreeOpen = ImGui::TreeNode("Groups##CollectionGroupsTree");
                        rememberOutlinerTreeOpenState(groupsTreeKey, groupsTreeOpen);
                        if (groupsTreeOpen)
                        {
                            for (std::size_t groupIndex = 0; groupIndex < m_ObjectGroups.size(); ++groupIndex)
                            {
                                SceneGroupingBackend::SanitizeGroup(*this, static_cast<int>(groupIndex));
                                const SceneGroup &group = m_ObjectGroups[groupIndex];
                                const bool isActiveGroup = (static_cast<int>(groupIndex) == m_ActiveGroupIndex);
                                const std::string groupLabel = group.name;
                                ImGui::PushID(static_cast<int>(groupIndex));
                                drawOutlinerIcon(OutlinerIconKind::Group, IM_COL32(255, 128, 0, 220));
                                ImGui::SameLine(0.0f, 6.0f);
                                if (ImGui::Selectable(groupLabel.c_str(), isActiveGroup))
                                {
                                    m_ActiveGroupIndex = static_cast<int>(groupIndex);
                                    SceneGroupingBackend::SelectGroup(*this, m_ActiveGroupIndex);
                                }
                                if (ImGui::BeginPopupContextItem())
                                {
                                    m_ActiveGroupIndex = static_cast<int>(groupIndex);
                                    if (ImGui::MenuItem("Select"))
                                    {
                                        SceneGroupingBackend::SelectGroup(*this, m_ActiveGroupIndex);
                                    }
                                    if (ImGui::MenuItem("Add current selection", nullptr, false, !m_SelectedAtomIndices.empty() || m_SelectedTransformEmptyIndex >= 0))
                                    {
                                        PushUndoSnapshot("Add selection to group");
                                        SceneGroupingBackend::AddCurrentSelectionToGroup(*this, m_ActiveGroupIndex);
                                        settingsChanged = true;
                                    }
                                    if (ImGui::MenuItem("Remove current selection", nullptr, false, !m_SelectedAtomIndices.empty() || m_SelectedTransformEmptyIndex >= 0))
                                    {
                                        PushUndoSnapshot("Remove selection from group");
                                        SceneGroupingBackend::RemoveCurrentSelectionFromGroup(*this, m_ActiveGroupIndex);
                                        settingsChanged = true;
                                    }
                                    if (ImGui::MenuItem("Delete"))
                                    {
                                        PushUndoSnapshot("Delete group");
                                        settingsChanged |= SceneGroupingBackend::DeleteGroup(*this, m_ActiveGroupIndex);
                                    }
                                    ImGui::EndPopup();
                                }
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                {
                                    ImGui::SetTooltip("Atoms: %zu | Empties: %zu", group.atomIndices.size(), group.emptyIndices.size());
                                }
                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DS_EMPTY_INDEX"))
                        {
                            if (payload->DataSize == sizeof(int))
                            {
                                const int droppedIndex = *static_cast<const int *>(payload->Data);
                                if (droppedIndex >= 0 && droppedIndex < static_cast<int>(m_TransformEmpties.size()))
                                {
                                    TransformEmpty &dropped = m_TransformEmpties[static_cast<std::size_t>(droppedIndex)];
                                    dropped.parentEmptyId = 0;
                                    dropped.collectionIndex = static_cast<int>(collectionIndex);
                                    dropped.collectionId = m_Collections[collectionIndex].id;
                                    settingsChanged = true;
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            ImGui::SeparatorText("Groups");
            if (m_ActiveGroupIndex >= 0 && m_ActiveGroupIndex < static_cast<int>(m_ObjectGroups.size()))
            {
                if (ImGui::Button("Select active group"))
                {
                    SceneGroupingBackend::SelectGroup(*this, m_ActiveGroupIndex);
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete active group"))
                {
                    PushUndoSnapshot("Delete group");
                    settingsChanged |= SceneGroupingBackend::DeleteGroup(*this, m_ActiveGroupIndex);
                }
                ImGui::SameLine();
                const bool hasSelection = !m_SelectedAtomIndices.empty() || m_SelectedTransformEmptyIndex >= 0;
                if (!hasSelection)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Add selection to active"))
                {
                    PushUndoSnapshot("Add selection to group");
                    SceneGroupingBackend::AddCurrentSelectionToGroup(*this, m_ActiveGroupIndex);
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove selection from active"))
                {
                    PushUndoSnapshot("Remove selection from group");
                    SceneGroupingBackend::RemoveCurrentSelectionFromGroup(*this, m_ActiveGroupIndex);
                    settingsChanged = true;
                }
                if (!hasSelection)
                {
                    ImGui::EndDisabled();
                }
            }

            for (std::size_t groupIndex = 0; groupIndex < m_ObjectGroups.size(); ++groupIndex)
            {
                SceneGroup &group = m_ObjectGroups[groupIndex];
                SceneGroupingBackend::SanitizeGroup(*this, static_cast<int>(groupIndex));
                const bool isActiveGroup = (static_cast<int>(groupIndex) == m_ActiveGroupIndex);
                ImGui::PushID(static_cast<int>(groupIndex));
                drawOutlinerIcon(OutlinerIconKind::Group, IM_COL32(255, 128, 0, 220));
                ImGui::SameLine(0.0f, 6.0f);
                if (ImGui::Selectable(group.name.c_str(), isActiveGroup))
                {
                    m_ActiveGroupIndex = static_cast<int>(groupIndex);
                    SceneGroupingBackend::SelectGroup(*this, m_ActiveGroupIndex);
                }
                if (ImGui::BeginPopupContextItem())
                {
                    m_ActiveGroupIndex = static_cast<int>(groupIndex);
                    if (ImGui::MenuItem("Select"))
                    {
                        SceneGroupingBackend::SelectGroup(*this, m_ActiveGroupIndex);
                    }
                    if (ImGui::MenuItem("Add current selection", nullptr, false, !m_SelectedAtomIndices.empty() || m_SelectedTransformEmptyIndex >= 0))
                    {
                        PushUndoSnapshot("Add selection to group");
                        SceneGroupingBackend::AddCurrentSelectionToGroup(*this, m_ActiveGroupIndex);
                        settingsChanged = true;
                    }
                    if (ImGui::MenuItem("Remove current selection", nullptr, false, !m_SelectedAtomIndices.empty() || m_SelectedTransformEmptyIndex >= 0))
                    {
                        PushUndoSnapshot("Remove selection from group");
                        SceneGroupingBackend::RemoveCurrentSelectionFromGroup(*this, m_ActiveGroupIndex);
                        settingsChanged = true;
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        PushUndoSnapshot("Delete group");
                        settingsChanged |= SceneGroupingBackend::DeleteGroup(*this, m_ActiveGroupIndex);
                    }
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    ImGui::SetTooltip("Atoms: %zu | Empties: %zu", group.atomIndices.size(), group.emptyIndices.size());
                }
                ImGui::PopID();
            }

            ImGui::End();
        }

        if (m_ShowShortcutReferencePanel)
        {
            ImGui::SetNextWindowSize(ImVec2(560.0f, 460.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Shortcuts", &m_ShowShortcutReferencePanel);

            struct ShortcutRow
            {
                const char *keys;
                const char *action;
            };

            auto drawShortcutSection = [&](const char *title, const std::initializer_list<ShortcutRow> &rows)
            {
                ImGui::SeparatorText(title);
                if (ImGui::BeginTable(title, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
                {
                    ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, 136.0f);
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                    for (const ShortcutRow &row : rows)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        if (ImGuiLayer::GetMonospaceFont() != nullptr)
                        {
                            ImGui::PushFont(ImGuiLayer::GetMonospaceFont());
                        }
                        ImGui::TextDisabled("%s", row.keys);
                        if (ImGuiLayer::GetMonospaceFont() != nullptr)
                        {
                            ImGui::PopFont();
                        }
                        ImGui::TableNextColumn();
                        ImGui::TextWrapped("%s", row.action);
                    }
                    ImGui::EndTable();
                }
            };

            const std::string renderShortcut = keyDisplayName(m_HotkeyOpenRender);
            const std::string panelsShortcut = keyDisplayName(m_HotkeyToggleSidePanels);
            const std::string boxShortcut = keyDisplayName(m_HotkeyBoxSelect);
            const std::string circleShortcut = keyDisplayName(m_HotkeyCircleSelect);
            const std::string hideShortcut = keyDisplayName(m_HotkeyHideSelection) + " / Alt+" + keyDisplayName(m_HotkeyHideSelection);
            const std::string translateModalShortcut = keyDisplayName(m_HotkeyTranslateModal);
            const std::string translateGizmoShortcut = keyDisplayName(m_HotkeyTranslateGizmo);
            const std::string rotateGizmoShortcut = keyDisplayName(m_HotkeyRotateGizmo);
            const std::string scaleGizmoShortcut = keyDisplayName(m_HotkeyScaleGizmo);
            const std::string addMenuShortcut = "Shift+" + keyDisplayName(m_HotkeyAddMenu);
            const std::string deleteShortcut = keyDisplayName(m_HotkeyDeleteSelection);

            drawShortcutSection("File & Layout", {
                                                  {"Ctrl+O", "Open POSCAR / CONTCAR"},
                                                  {"Ctrl+S", "Export POSCAR / CONTCAR"},
                                                  {"Ctrl+Shift+N", "Create a project by choosing a project root folder"},
                                                  {"Ctrl+Shift+O", "Open a project by choosing its project root folder"},
                                                  {"Ctrl+Alt+O", "Open the recent-project popup"},
                                                  {renderShortcut.c_str(), "Open Render Image and Render Preview"},
                                                  {panelsShortcut.c_str(), "Toggle side panels"},
                                                  {"Ctrl+Z", "Undo last core scene edit"},
                                                  {"Ctrl+Y", "Redo last core scene edit"},
                                                  {"Ctrl+Shift+Z", "Redo last core scene edit"}});

            drawShortcutSection("Selection & View", {
                                                       {"LMB", "Select atom, bond or helper"},
                                                       {"Ctrl+LMB", "Add or remove from selection"},
                                                       {"Ctrl+A", "Select all visible items for the current selection filter"},
                                                       {"Shift + C-drag", "Subtract from circle selection"},
                                                       {boxShortcut.c_str(), "Arm box selection"},
                                                       {circleShortcut.c_str(), "Arm circle selection"},
                                                       {"Tab", "Hold radial mode pie menu"},
                                                       {"T / B / L / R / P / K", "Snap camera in ViewSet mode"},
                                                       {hideShortcut.c_str(), "Hide selection / unhide all"}});

            drawShortcutSection("Transform", {
                                                 {translateModalShortcut.c_str(), "Modal translate"},
                                                 {translateGizmoShortcut.c_str(), "Translate gizmo"},
                                                 {rotateGizmoShortcut.c_str(), "Rotate gizmo"},
                                                 {scaleGizmoShortcut.c_str(), "Scale gizmo"},
                                                 {"X / Y / Z", "Constrain active modal transform"},
                                                 {"Enter / LMB", "Apply modal transform"},
                                                 {"Esc / RMB", "Cancel modal transform"}});

            drawShortcutSection("Scene Tools", {
                                                  {addMenuShortcut.c_str(), "Open Add Scene Object popup"},
                                                  {deleteShortcut.c_str(), "Delete current selection"},
                                                  {"Ctrl+C / Ctrl+V", "Copy and paste selection with internal editor clipboard"},
                                                  {"Ctrl+D", "Duplicate current selection"},
                                                  {"Ctrl+D / Delete", "Duplicate or delete the active collection while Scene Outliner is focused"},
                                                  {". / Shift+. / Alt+.", "Focus camera on current selection, or the 3D cursor if nothing is selected; Shift stores a closer distance, Alt a farther one"},
                                                  {"F2", "Rename active collection in Scene Outliner"},
                                                  {"Mouse Wheel", "Zoom viewport / adjust circle radius while C-select"},
                                                  {"MMB / Shift+MMB", "Orbit / pan viewport"},
                                                  {"Alt+LMB / Alt+Shift+LMB", "Touchpad orbit / pan mode"}});

            ImGui::End();
        }

        if (m_ShowStatsPanel)
        {
            ImGui::SetNextWindowSize(ImVec2(360.0f, 260.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Stats", &m_ShowStatsPanel);

            const float frameTimeMs = (io.Framerate > 0.0f) ? (1000.0f / io.Framerate) : 0.0f;
            const std::uint32_t renderWidth = static_cast<std::uint32_t>(std::max(1.0f, m_ViewportSize.x * m_ViewportRenderScale));
            const std::uint32_t renderHeight = static_cast<std::uint32_t>(std::max(1.0f, m_ViewportSize.y * m_ViewportRenderScale));
            std::size_t visibleAtomCount = 0;
            for (std::size_t atomIndex = 0; atomIndex < m_WorkingStructure.atoms.size(); ++atomIndex)
            {
                if (!IsAtomHidden(atomIndex) && IsAtomCollectionVisible(atomIndex))
                {
                    ++visibleAtomCount;
                }
            }

            std::size_t visibleBondCount = 0;
            for (const BondSegment &bond : m_GeneratedBonds)
            {
                const std::uint64_t bondKey = MakeBondPairKey(bond.atomA, bond.atomB);
                if (m_DeletedBondKeys.find(bondKey) == m_DeletedBondKeys.end() &&
                    m_HiddenBondKeys.find(bondKey) == m_HiddenBondKeys.end() &&
                    !IsAtomHidden(bond.atomA) &&
                    !IsAtomHidden(bond.atomB) &&
                    IsAtomCollectionVisible(bond.atomA) &&
                    IsAtomCollectionVisible(bond.atomB))
                {
                    ++visibleBondCount;
                }
            }

            ImGui::Text("Frame: %.2f ms", frameTimeMs);
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Text("Viewport: %.0f x %.0f", m_ViewportSize.x, m_ViewportSize.y);
            ImGui::Text("Render target: %u x %u (%.0f%%)", renderWidth, renderHeight, m_ViewportRenderScale * 100.0f);
            ImGui::Text("Atoms: %zu visible / %zu total", visibleAtomCount, m_WorkingStructure.atoms.size());
            ImGui::Text("Bonds: %zu visible / %zu generated", visibleBondCount, m_GeneratedBonds.size());
            ImGui::Text("Selection: %zu atoms | %zu bonds", m_SelectedAtomIndices.size(), m_SelectedBondKeys.size());
            ImGui::Text("Undo / Redo: %zu / %zu", m_UndoStack.size(), m_RedoStack.size());
            ImGui::Separator();
#ifdef TRACY_ENABLE
            ImGui::TextUnformatted("Profiler: Tracy CPU/GPU instrumentation enabled");
#else
            ImGui::TextUnformatted("Profiler: Tracy disabled in this build");
#endif
            ImGui::TextDisabled("Use render scale and input defaults in Settings to tune weaker GPUs.");
            ImGui::End();
        }

        if (m_ShowObjectPropertiesPanel)
        {
            PropertiesPanel::Draw(*this, settingsChanged);
        }

        if (m_ShowSettingsPanel)
        {
            SettingsPanel::Draw(*this, settingsChanged);
        }

        if (m_ShowLogPanel)
        {
            ImGui::Begin("Log / Errors", &m_ShowLogPanel);

            const auto entries = Logger::Get().GetEntriesSnapshot();
            std::size_t levelCounts[5] = {};
            for (const auto &entry : entries)
            {
                const int levelIndex = std::clamp(static_cast<int>(entry.level), 0, 4);
                ++levelCounts[levelIndex];
            }

            const char *filterItems[] = {
                "All",
                "Trace only",
                "Info + above",
                "Warnings + Errors + Fatal",
                "Errors + Fatal",
                "Fatal only"};

            struct LogVisual
            {
                const char *label;
                ImVec4 color;
            };

            auto getLogVisual = [](LogLevel level) -> LogVisual
            {
                switch (level)
                {
                case LogLevel::Info:
                    return {"INFO", ImVec4(0.42f, 0.78f, 0.98f, 1.0f)};
                case LogLevel::Warn:
                    return {"WARN", ImVec4(0.96f, 0.76f, 0.35f, 1.0f)};
                case LogLevel::Error:
                    return {"ERROR", ImVec4(0.95f, 0.35f, 0.35f, 1.0f)};
                case LogLevel::Fatal:
                    return {"FATAL", ImVec4(1.0f, 0.08f, 0.08f, 1.0f)};
                case LogLevel::Trace:
                default:
                    return {"TRACE", ImVec4(0.60f, 0.60f, 0.60f, 1.0f)};
                }
            };

            auto drawLogLevelIcon = [&](LogLevel level, const ImVec2 &screenPos, float size)
            {
                ImDrawList *drawList = ImGui::GetWindowDrawList();
                const LogVisual visual = getLogVisual(level);
                const ImU32 color = ImGui::ColorConvertFloat4ToU32(visual.color);
                const ImVec2 center(screenPos.x + size * 0.5f, screenPos.y + size * 0.5f);
                const float radius = size * 0.34f;

                switch (level)
                {
                case LogLevel::Info:
                    drawList->AddCircleFilled(center, radius, color, 18);
                    drawList->AddCircleFilled(center, size * 0.10f, IM_COL32(255, 255, 255, 255), 12);
                    break;
                case LogLevel::Warn:
                    drawList->PathClear();
                    drawList->PathLineTo(ImVec2(center.x, screenPos.y + size * 0.12f));
                    drawList->PathLineTo(ImVec2(screenPos.x + size * 0.18f, screenPos.y + size * 0.82f));
                    drawList->PathLineTo(ImVec2(screenPos.x + size * 0.82f, screenPos.y + size * 0.82f));
                    drawList->PathFillConvex(color);
                    break;
                case LogLevel::Error:
                    drawList->AddRectFilled(ImVec2(screenPos.x + size * 0.18f, screenPos.y + size * 0.18f),
                                            ImVec2(screenPos.x + size * 0.82f, screenPos.y + size * 0.82f),
                                            color, 2.0f);
                    break;
                case LogLevel::Fatal:
                    drawList->AddNgonFilled(center, radius, color, 8);
                    break;
                case LogLevel::Trace:
                default:
                    drawList->AddCircle(center, radius, color, 18, 1.6f);
                    break;
                }
            };

            auto passesLogFilter = [&](LogLevel level) -> bool
            {
                if (m_LogFilter == 1)
                {
                    return level == LogLevel::Trace;
                }
                if (m_LogFilter == 2)
                {
                    return level != LogLevel::Trace;
                }
                if (m_LogFilter == 3)
                {
                    return level != LogLevel::Trace && level != LogLevel::Info;
                }
                if (m_LogFilter == 4)
                {
                    return level == LogLevel::Error || level == LogLevel::Fatal;
                }
                if (m_LogFilter == 5)
                {
                    return level == LogLevel::Fatal;
                }
                return true;
            };

            auto drawFilterButton = [&](const char *label,
                                        int filterValue,
                                        const ImVec4 &accentColor,
                                        std::optional<LogLevel> iconLevel = std::nullopt)
            {
                const bool isSelected = (m_LogFilter == filterValue);
                if (isSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, accentColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.95f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.85f));
                }

                std::string buttonLabel = label;
                if (iconLevel.has_value())
                {
                    buttonLabel = "   " + buttonLabel;
                }

                if (ImGui::Button(buttonLabel.c_str()))
                {
                    m_LogFilter = filterValue;
                    settingsChanged = true;
                }

                if (iconLevel.has_value())
                {
                    const ImVec2 rectMin = ImGui::GetItemRectMin();
                    const ImVec2 rectSize = ImGui::GetItemRectSize();
                    const float iconSize = std::min(13.0f, rectSize.y - 6.0f);
                    drawLogLevelIcon(*iconLevel,
                                     ImVec2(rectMin.x + 6.0f, rectMin.y + (rectSize.y - iconSize) * 0.5f),
                                     iconSize);
                }

                if (isSelected)
                {
                    ImGui::PopStyleColor(3);
                }
            };

            if (ImGui::BeginTable("LogSummary", 3, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Entries: %zu", entries.size());
                ImGui::TableNextColumn();
                ImGui::Text("Warnings: %zu", levelCounts[static_cast<int>(LogLevel::Warn)]);
                ImGui::TableNextColumn();
                ImGui::Text("Errors/Fatal: %zu", levelCounts[static_cast<int>(LogLevel::Error)] + levelCounts[static_cast<int>(LogLevel::Fatal)]);
                ImGui::EndTable();
            }

            if (ImGui::BeginCombo("Filter", filterItems[m_LogFilter]))
            {
                for (int filterIndex = 0; filterIndex < static_cast<int>(IM_ARRAYSIZE(filterItems)); ++filterIndex)
                {
                    const bool selected = (m_LogFilter == filterIndex);
                    if (ImGui::Selectable(filterItems[filterIndex], selected))
                    {
                        m_LogFilter = filterIndex;
                        settingsChanged = true;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Checkbox("Auto-scroll", &m_LogAutoScroll))
            {
                settingsChanged = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Clear log"))
            {
                Logger::Get().Clear();
            }

            ImGui::SeparatorText("Quick Filters");
            drawFilterButton("All", 0, ImVec4(0.35f, 0.35f, 0.35f, 0.75f));
            ImGui::SameLine();
            drawFilterButton("Trace", 1, getLogVisual(LogLevel::Trace).color, LogLevel::Trace);
            ImGui::SameLine();
            drawFilterButton("Info+", 2, getLogVisual(LogLevel::Info).color, LogLevel::Info);
            ImGui::SameLine();
            drawFilterButton("Warn+", 3, getLogVisual(LogLevel::Warn).color, LogLevel::Warn);
            ImGui::SameLine();
            drawFilterButton("Error+", 4, getLogVisual(LogLevel::Error).color, LogLevel::Error);
            ImGui::SameLine();
            drawFilterButton("Fatal", 5, getLogVisual(LogLevel::Fatal).color, LogLevel::Fatal);

            ImGui::Separator();

            ImGui::BeginChild("LogEntries", ImVec2(0.0f, 0.0f), true);
            const bool shouldStickToBottom = m_LogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
            const ImGuiTableFlags tableFlags =
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY;

            ImFont *monoFont = ImGuiLayer::GetMonospaceFont();
            if (monoFont != nullptr)
            {
                ImGui::PushFont(monoFont);
            }

            if (ImGui::BeginTable("LogEntriesTable", 4, tableFlags, ImVec2(0.0f, 0.0f)))
            {
                ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 86.0f);
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 220.0f);
                ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto &entry : entries)
                {
                    if (!passesLogFilter(entry.level))
                    {
                        continue;
                    }

                    const LogVisual visual = getLogVisual(entry.level);
                    ImGui::TableNextRow();
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                           ImGui::ColorConvertFloat4ToU32(ImVec4(visual.color.x, visual.color.y, visual.color.z, 0.08f)));

                    ImGui::TableNextColumn();
                    {
                        const ImVec2 iconPos = ImGui::GetCursorScreenPos();
                        drawLogLevelIcon(entry.level, iconPos, 12.0f);
                        ImGui::Dummy(ImVec2(14.0f, 12.0f));
                        ImGui::SameLine(0.0f, 6.0f);
                        ImGui::PushStyleColor(ImGuiCol_Text, visual.color);
                        ImGui::TextUnformatted(visual.label);
                        ImGui::PopStyleColor();
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(entry.timestamp.c_str());

                    ImGui::TableNextColumn();
                    ImGui::Text("%s:%u", entry.file.c_str(), static_cast<unsigned int>(entry.line));
                    if (!entry.function.empty())
                    {
                        ImGui::TextDisabled("%s", entry.function.c_str());
                    }
                    if (ImGui::IsItemHovered() && !entry.function.empty())
                    {
                        ImGui::SetTooltip("%s\n%s:%u",
                                          entry.function.c_str(),
                                          entry.file.c_str(),
                                          static_cast<unsigned int>(entry.line));
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", entry.message.c_str());
                }

                if (shouldStickToBottom)
                {
                    ImGui::SetScrollHereY(1.0f);
                }

                ImGui::EndTable();
            }

            if (monoFont != nullptr)
            {
                ImGui::PopFont();
            }

            ImGui::EndChild();
            ImGui::End();
        }

        ImGui::End();

        if (m_ShowDemoWindow)
        {
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
        }

        DrawPeriodicTableWindow();
        DrawChangeAtomTypeConfirmDialog();
        DrawRenderImageDialog(settingsChanged);
        DrawRenderPreviewWindow(settingsChanged);
        DrawElementAppearanceWindow(settingsChanged);
        DrawVolumetricsWindow(settingsChanged);

        if (m_ShowAddAtomDialog)
        {
            const char *dialogCoordinateModes[] = {"Direct", "Cartesian"};
            ImGui::SetNextWindowSize(ImVec2(470.0f, 0.0f), ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            bool dialogOpen = m_ShowAddAtomDialog;
            if (ImGui::Begin("Add Atom", &dialogOpen, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::InputText("Element", m_AddAtomElementBuffer.data(), m_AddAtomElementBuffer.size());
                ImGui::SameLine();
                if (ImGui::Button("Periodic table"))
                {
                    OpenPeriodicTable(PeriodicTableTarget::AddAtomEntry);
                }

                DrawColoredVec3Control("Position", m_AddAtomPosition, 0.01f, -1000.0f, 1000.0f, "%.5f");
                ImGui::SetNextItemWidth(120.0f);
                ImGui::InputFloat("Set XYZ", &m_AddAtomUniformPositionValue, 0.1f, 1.0f, "%.4f");
                ImGui::SameLine();
                if (ImGui::Button("Apply to X/Y/Z"))
                {
                    m_AddAtomPosition = glm::vec3(m_AddAtomUniformPositionValue);
                }

                ImGui::Combo("Input coordinates", &m_AddAtomCoordinateModeIndex, dialogCoordinateModes, IM_ARRAYSIZE(dialogCoordinateModes));

                if (ImGui::Button("Use 3D cursor"))
                {
                    m_AddAtomPosition = m_CursorPosition;
                    m_AddAtomCoordinateModeIndex = 1;
                }
                ImGui::SameLine();
                if (ImGui::Button("Use camera target") && m_Camera)
                {
                    m_AddAtomPosition = m_Camera->GetTarget();
                    m_AddAtomCoordinateModeIndex = 1;
                }

                ImGui::Separator();
                const CoordinateMode inputMode = (m_AddAtomCoordinateModeIndex == 0)
                                                     ? CoordinateMode::Direct
                                                     : CoordinateMode::Cartesian;

                const bool canAddAtomNow = m_HasStructureLoaded;
                if (!canAddAtomNow)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Add atom"))
                {
                    if (AddAtomToStructure(std::string(m_AddAtomElementBuffer.data()), m_AddAtomPosition, inputMode))
                    {
                        settingsChanged = true;
                        dialogOpen = false;
                    }
                }
                if (!canAddAtomNow)
                {
                    ImGui::EndDisabled();
                }

                ImGui::SameLine();
                if (ImGui::Button("Close"))
                {
                    dialogOpen = false;
                }
            }
            ImGui::End();
            m_ShowAddAtomDialog = dialogOpen;
        }

        if (settingsChanged)
        {
            SaveSettings();
        }
    }

    void EditorLayer::DrawVolumetricsWindow(bool &settingsChanged)
    {
        if (!m_ShowVolumetricsPanel)
        {
            return;
        }

        EnsureVolumetricSelection();

        ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Volumetrics", &m_ShowVolumetricsPanel))
        {
            ImGui::End();
            return;
        }

        auto formatBytes = [](std::size_t bytes)
        {
            constexpr double kKiB = 1024.0;
            constexpr double kMiB = 1024.0 * 1024.0;
            constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;

            std::ostringstream out;
            out << std::fixed << std::setprecision(2);
            if (bytes >= static_cast<std::size_t>(kGiB))
            {
                out << (static_cast<double>(bytes) / kGiB) << " GiB";
            }
            else if (bytes >= static_cast<std::size_t>(kMiB))
            {
                out << (static_cast<double>(bytes) / kMiB) << " MiB";
            }
            else if (bytes >= static_cast<std::size_t>(kKiB))
            {
                out << (static_cast<double>(bytes) / kKiB) << " KiB";
            }
            else
            {
                out << bytes << " B";
            }
            return out.str();
        };

        ImGui::TextWrapped("Volumetrics MVP for CHG / CHGCAR / PARCHG. This panel now loads scalar fields, preserves multi-block files, shows dataset statistics, and renders preview isosurfaces on a decimated grid.");

        if (ImGui::Button("Load volumetric files..."))
        {
            const std::filesystem::path initialDirectory = !GetProjectRootPath().empty()
                                                              ? (GetProjectRootPath() / "project")
                                                              : GetAppRootPath();
            std::vector<std::string> selectedPaths;
            if (OpenNativeVolumetricFilesDialog(selectedPaths, initialDirectory.string()) && !selectedPaths.empty())
            {
                bool anyLoaded = false;
                for (const std::string &selectedPath : selectedPaths)
                {
                    anyLoaded |= LoadVolumetricDatasetFromPath(selectedPath);
                }
                if (anyLoaded)
                {
                    SaveProjectManifest();
                }
            }
        }

        ImGui::SameLine();
        const bool hasDataset = !m_VolumetricDatasets.empty();
        if (!hasDataset)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Unload selected"))
        {
            if (RemoveVolumetricDatasetAtIndex(m_ActiveVolumetricDatasetIndex))
            {
                SaveProjectManifest();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear all"))
        {
            ClearVolumetricDatasets();
            SaveProjectManifest();
        }
        if (!hasDataset)
        {
            ImGui::EndDisabled();
        }

        if (!m_LastVolumetricMessage.empty())
        {
            ImVec4 color = m_LastVolumetricOperationFailed ? ImVec4(0.95f, 0.42f, 0.42f, 1.0f) : ImVec4(0.64f, 0.88f, 0.68f, 1.0f);
            ImGui::TextColored(color, "%s", m_LastVolumetricMessage.c_str());
        }

        if (m_VolumetricDatasets.empty())
        {
            ImGui::SeparatorText("Expected structure");
            ImGui::BulletText("Each current sample file contains the same BN structure header and two volumetric blocks.");
            ImGui::BulletText("Typical workflow: load one or more PARCHG datasets, inspect blocks, then choose iso settings for rendering.");
            ImGui::BulletText("These files are large, so preview decimation will matter for interactive rendering.");
            ImGui::End();
            return;
        }

        std::size_t totalMemoryBytes = 0;
        for (const VolumetricDataset &dataset : m_VolumetricDatasets)
        {
            totalMemoryBytes += dataset.TotalMemoryBytes();
        }

        ImGui::SeparatorText("Loaded datasets");
        ImGui::Text("Count: %d", static_cast<int>(m_VolumetricDatasets.size()));
        ImGui::SameLine();
        ImGui::TextDisabled("| Total memory: %s", formatBytes(totalMemoryBytes).c_str());

        if (ImGui::BeginChild("VolumetricDatasetList", ImVec2(0.0f, 140.0f), true))
        {
            for (std::size_t datasetIndex = 0; datasetIndex < m_VolumetricDatasets.size(); ++datasetIndex)
            {
                const VolumetricDataset &dataset = m_VolumetricDatasets[datasetIndex];
                const bool isSelected = (static_cast<int>(datasetIndex) == m_ActiveVolumetricDatasetIndex);

                std::ostringstream label;
                label << dataset.sourceLabel << "##VolumetricDataset" << datasetIndex;
                if (ImGui::Selectable(label.str().c_str(), isSelected))
                {
                    m_ActiveVolumetricDatasetIndex = static_cast<int>(datasetIndex);
                    m_ActiveVolumetricBlockIndex = 0;
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(dataset.sourcePath.c_str());
                    ImGui::Text("Blocks: %d", static_cast<int>(dataset.blocks.size()));
                    ImGui::Text("Memory: %s", formatBytes(dataset.TotalMemoryBytes()).c_str());
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndChild();

        EnsureVolumetricSelection();
        if (m_ActiveVolumetricDatasetIndex < 0 || m_ActiveVolumetricDatasetIndex >= static_cast<int>(m_VolumetricDatasets.size()))
        {
            ImGui::End();
            return;
        }

        const VolumetricDataset &dataset = m_VolumetricDatasets[static_cast<std::size_t>(m_ActiveVolumetricDatasetIndex)];
        ImGui::SeparatorText("Dataset details");
        ImGui::Text("File kind: %s", VolumetricFileKindName(dataset.kind));
        ImGui::Text("Structure title: %s", dataset.title.empty() ? "(empty)" : dataset.title.c_str());
        ImGui::Text("Atoms in header: %d", dataset.structure.GetAtomCount());
        if (m_HasStructureLoaded)
        {
            const bool matchesCurrentScene = (dataset.structure.GetAtomCount() == m_WorkingStructure.GetAtomCount());
            ImGui::TextColored(matchesCurrentScene ? ImVec4(0.64f, 0.88f, 0.68f, 1.0f) : ImVec4(0.95f, 0.72f, 0.38f, 1.0f),
                               "Matches current scene atom count: %s",
                               matchesCurrentScene ? "yes" : "no");
        }

        if (!dataset.blocks.empty())
        {
            std::vector<const char *> blockLabels;
            blockLabels.reserve(dataset.blocks.size());
            for (const ScalarFieldBlock &block : dataset.blocks)
            {
                blockLabels.push_back(block.label.c_str());
            }

            ImGui::SeparatorText("Block");
            ImGui::Combo("Active block", &m_ActiveVolumetricBlockIndex, blockLabels.data(), static_cast<int>(blockLabels.size()));
            m_ActiveVolumetricBlockIndex = std::clamp(m_ActiveVolumetricBlockIndex, 0, static_cast<int>(dataset.blocks.size()) - 1);

            const ScalarFieldBlock &block = dataset.blocks[static_cast<std::size_t>(m_ActiveVolumetricBlockIndex)];
            ImGui::Text("Grid: %d x %d x %d", block.dimensions.x, block.dimensions.y, block.dimensions.z);
            ImGui::Text("Samples: %zu", block.SampleCount());
            ImGui::Text("Block memory: %s", formatBytes(block.MemoryBytes()).c_str());

            ImGui::SeparatorText("Statistics");
            ImGui::BulletText("Min: %.9g", block.statistics.minValue);
            ImGui::BulletText("Max: %.9g", block.statistics.maxValue);
            ImGui::BulletText("Mean: %.9g", block.statistics.meanValue);
            ImGui::BulletText("Abs max: %.9g", block.statistics.absMaxValue);

            ImGui::SeparatorText("Preview / reduction planning");
            if (ImGui::SliderInt("Preview max axis", &m_VolumetricPreviewMaxDimension, 32, 512))
            {
                settingsChanged = true;
                MarkVolumetricMeshesDirty();
            }

            const int decimationStep = block.SuggestedDecimationStep(m_VolumetricPreviewMaxDimension);
            const glm::ivec3 downsampledDimensions = block.DownsampledDimensions(m_VolumetricPreviewMaxDimension);
            const std::size_t downsampledSamples = block.DownsampledSampleCount(m_VolumetricPreviewMaxDimension);
            const std::size_t downsampledBytes = downsampledSamples * sizeof(float);
            ImGui::BulletText("Suggested uniform step: %d", decimationStep);
            ImGui::BulletText("Downsampled grid: %d x %d x %d", downsampledDimensions.x, downsampledDimensions.y, downsampledDimensions.z);
            ImGui::BulletText("Downsampled samples: %zu", downsampledSamples);
            ImGui::BulletText("Approx preview memory: %s", formatBytes(downsampledBytes).c_str());
            ImGui::TextWrapped("For these sample PARCHG files, a decimated preview is the safest way to keep interactivity while preserving the full-resolution data for final isosurface extraction.");

            if (dataset.blocks.size() == 2)
            {
                ImGui::SeparatorText("Block interpretation");
                ImGui::TextWrapped("This file currently looks like a two-block volumetric dataset. For MVP we will treat them as neutral Block 1 / Block 2 and avoid naming them spin-up / spin-down until the generation convention is confirmed.");
            }

            auto drawSurfaceControls = [&](const char *label, const char *idSuffix, VolumetricSurfaceState &surfaceState, float defaultIsoFactor)
            {
                ImGui::SeparatorText(label);
                if (ImGui::Checkbox((std::string("Enabled##") + idSuffix).c_str(), &surfaceState.enabled))
                {
                    surfaceState.dirty = true;
                    settingsChanged = true;
                }

                if (!surfaceState.enabled)
                {
                    ImGui::BeginDisabled();
                }

                if (ImGui::Combo((std::string("Block##") + idSuffix).c_str(), &surfaceState.blockIndex, blockLabels.data(), static_cast<int>(blockLabels.size())))
                {
                    surfaceState.dirty = true;
                    settingsChanged = true;
                }
                surfaceState.blockIndex = std::clamp(surfaceState.blockIndex, 0, static_cast<int>(dataset.blocks.size()) - 1);

                const ScalarFieldBlock &surfaceBlock = dataset.blocks[static_cast<std::size_t>(surfaceState.blockIndex)];
                const float dragSpeed = std::max(surfaceBlock.statistics.absMaxValue * 0.0025f, 1e-6f);
                const float dragMin = std::min(surfaceBlock.statistics.minValue, surfaceBlock.statistics.maxValue);
                const float dragMax = std::max(surfaceBlock.statistics.minValue, surfaceBlock.statistics.maxValue);
                ImGui::DragFloat((std::string("Iso value##") + idSuffix).c_str(), &surfaceState.isoValue, dragSpeed, dragMin, dragMax, "%.6g");
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    surfaceState.dirty = true;
                    settingsChanged = true;
                }

                ImGui::ColorEdit3((std::string("Color##") + idSuffix).c_str(), &surfaceState.color.x);
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    surfaceState.dirty = true;
                    settingsChanged = true;
                }

                ImGui::SliderFloat((std::string("Opacity##") + idSuffix).c_str(), &surfaceState.opacity, 0.05f, 0.95f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    surfaceState.dirty = true;
                    settingsChanged = true;
                }

                if (ImGui::Button((std::string("Rebuild now##") + idSuffix).c_str()))
                {
                    surfaceState.dirty = true;
                }

                ImGui::SameLine();
                if (ImGui::Button((std::string("Default iso##") + idSuffix).c_str()))
                {
                    surfaceState.isoValue =
                        std::max(std::max(std::abs(surfaceBlock.statistics.meanValue) * 6.0f, surfaceBlock.statistics.absMaxValue * 0.05f),
                                 surfaceBlock.statistics.absMaxValue * defaultIsoFactor);
                    surfaceState.dirty = true;
                    settingsChanged = true;
                }

                if (surfaceState.enabled)
                {
                    ImGui::Text("Triangles: %zu", surfaceState.mesh.TriangleCount());
                    ImGui::Text("Preview grid: %d x %d x %d",
                                surfaceState.sampledDimensions.x,
                                surfaceState.sampledDimensions.y,
                                surfaceState.sampledDimensions.z);
                    ImGui::Text("Decimation step: %d", surfaceState.decimationStep);
                    ImGui::Text("Preview mesh memory: %s", formatBytes(surfaceState.mesh.MemoryBytes()).c_str());
                    ImGui::Text("Last build: %.2f ms", surfaceState.lastBuildMilliseconds);
                    if (!surfaceState.lastStatus.empty())
                    {
                        const bool surfaceOkay = surfaceState.hasMesh;
                        ImGui::TextColored(surfaceOkay ? ImVec4(0.64f, 0.88f, 0.68f, 1.0f) : ImVec4(0.95f, 0.72f, 0.38f, 1.0f), "%s", surfaceState.lastStatus.c_str());
                    }
                }

                if (!surfaceState.enabled)
                {
                    ImGui::EndDisabled();
                }
            };

            ImGui::SeparatorText("Surface preview");
            ImGui::TextWrapped("Current MVP renders preview isosurfaces from the active dataset only. The mesh is rebuilt only when the active file, selected block, preview resolution, or surface parameters change.");
            drawSurfaceControls("Surface A", "PrimarySurface", m_PrimaryVolumetricSurface, 0.26f);
            drawSurfaceControls("Surface B", "SecondarySurface", m_SecondaryVolumetricSurface, 0.42f);
        }

        ImGui::End();
    }

    void EditorLayer::DrawElementAppearanceWindow(bool &settingsChanged)
    {
        if (!m_ShowElementCatalogPanel)
        {
            return;
        }

        EnsureElementAppearanceSelection();

        std::unordered_set<std::string> symbolSet;
        auto addSymbol = [&](const std::string &rawSymbol)
        {
            const std::string normalized = NormalizeElementSymbol(rawSymbol);
            if (!normalized.empty())
            {
                symbolSet.insert(normalized);
            }
        };

        for (const auto &[element, color] : m_AtomDefaults.elementColors)
        {
            (void)color;
            addSymbol(element);
        }
        for (const auto &[element, scale] : m_AtomDefaults.elementScales)
        {
            (void)scale;
            addSymbol(element);
        }
        for (const auto &[element, color] : m_ElementColorOverrides)
        {
            (void)color;
            addSymbol(element);
        }
        for (const auto &[element, scale] : m_ElementScaleOverrides)
        {
            (void)scale;
            addSymbol(element);
        }
        addSymbol(m_ElementCatalogSelectedSymbol);
        if (m_HasStructureLoaded)
        {
            for (const std::string &element : m_WorkingStructure.species)
            {
                addSymbol(element);
            }
            for (const Atom &atom : m_WorkingStructure.atoms)
            {
                addSymbol(atom.element);
            }
        }

        std::vector<std::string> symbols(symbolSet.begin(), symbolSet.end());
        std::sort(symbols.begin(), symbols.end());
        if (symbols.empty())
        {
            symbols.push_back("C");
        }

        if (std::find(symbols.begin(), symbols.end(), m_ElementCatalogSelectedSymbol) == symbols.end())
        {
            m_ElementCatalogSelectedSymbol = symbols.front();
        }

        std::string selectedAtomElement;
        if (m_HasStructureLoaded && !m_SelectedAtomIndices.empty())
        {
            const std::size_t atomIndex = m_SelectedAtomIndices.back();
            if (atomIndex < m_WorkingStructure.atoms.size())
            {
                selectedAtomElement = NormalizeElementSymbol(m_WorkingStructure.atoms[atomIndex].element);
            }
        }

        if (m_ElementCatalogFollowViewportSelection && !selectedAtomElement.empty())
        {
            m_ElementCatalogSelectedSymbol = selectedAtomElement;
        }

        ImGui::SetNextWindowSize(ImVec2(520.0f, 640.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Element Catalog", &m_ShowElementCatalogPanel))
        {
            ImGui::End();
            return;
        }

        ImGui::TextWrapped("Global element defaults come from atom_settings.yaml. Project-specific color and size overrides are stored in config/project/project_appearance.yaml.");

        ImGui::SeparatorText("Selection source");
        if (ImGui::RadioButton("Periodic table", !m_ElementCatalogFollowViewportSelection))
        {
            m_ElementCatalogFollowViewportSelection = false;
            settingsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Viewport selection", m_ElementCatalogFollowViewportSelection))
        {
            m_ElementCatalogFollowViewportSelection = true;
            if (!selectedAtomElement.empty())
            {
                m_ElementCatalogSelectedSymbol = selectedAtomElement;
            }
            if (!m_ShowPeriodicTablePanel)
            {
                m_PeriodicTableOpen = false;
            }
            settingsChanged = true;
        }

        if (m_ElementCatalogFollowViewportSelection)
        {
            if (!selectedAtomElement.empty())
            {
                ImGui::TextDisabled("Following selected viewport atom: %s", selectedAtomElement.c_str());
            }
            else
            {
                ImGui::TextDisabled("Select an atom in the viewport to drive the catalog selection.");
            }
        }

        if (m_ElementCatalogFollowViewportSelection)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::BeginCombo("Element", m_ElementCatalogSelectedSymbol.c_str()))
        {
            for (const std::string &symbol : symbols)
            {
                const bool isSelected = (symbol == m_ElementCatalogSelectedSymbol);
                if (ImGui::Selectable(symbol.c_str(), isSelected))
                {
                    m_ElementCatalogSelectedSymbol = symbol;
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (m_ElementCatalogFollowViewportSelection)
        {
            ImGui::EndDisabled();
        }

        if (!m_ElementCatalogFollowViewportSelection && !selectedAtomElement.empty())
        {
            if (ImGui::Button("Use selected atom element"))
            {
                m_ElementCatalogSelectedSymbol = selectedAtomElement;
            }
        }

        if (!m_ElementCatalogFollowViewportSelection)
        {
            ImGui::SeparatorText("Periodic table");
            ImGui::BeginChild("ElementCatalogPeriodicTable", ImVec2(0.0f, 320.0f), true);
            DrawPeriodicTableSelector("ElementCatalogInline", PeriodicTableTarget::ElementAppearanceEditor, false);
            ImGui::EndChild();
        }

        const std::string element = m_ElementCatalogSelectedSymbol;
        const glm::vec3 proceduralColor = glm::clamp(ColorFromElement(element), glm::vec3(0.0f), glm::vec3(1.0f));
        glm::vec3 catalogColor = proceduralColor;
        if (const auto it = m_AtomDefaults.elementColors.find(element); it != m_AtomDefaults.elementColors.end())
        {
            catalogColor = glm::clamp(it->second, glm::vec3(0.0f), glm::vec3(1.0f));
        }

        float catalogScale = 1.0f;
        if (const auto it = m_AtomDefaults.elementScales.find(element); it != m_AtomDefaults.elementScales.end())
        {
            catalogScale = std::clamp(it->second, 0.1f, 4.0f);
        }

        glm::vec3 resolvedColor = ResolveElementColor(element);
        float resolvedScale = ResolveElementVisualScale(element);
        int usageCount = 0;
        if (m_HasStructureLoaded)
        {
            for (const Atom &atom : m_WorkingStructure.atoms)
            {
                if (NormalizeElementSymbol(atom.element) == element)
                {
                    ++usageCount;
                }
            }
        }

        ImGui::SeparatorText("Resolved appearance");
        ImGui::ColorButton(
            "##ResolvedElementColor",
            ImVec4(resolvedColor.x, resolvedColor.y, resolvedColor.z, 1.0f),
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
            ImVec2(28.0f, 28.0f));
        ImGui::SameLine();
        ImGui::Text("%s", element.c_str());
        ImGui::TextDisabled("Effective size multiplier: %.2fx", resolvedScale);
        if (usageCount > 0)
        {
            ImGui::TextDisabled("Atoms in current structure: %d", usageCount);
        }
        else
        {
            ImGui::TextDisabled("This element is not used in the currently loaded structure.");
        }

        ImGui::SeparatorText("Catalog defaults (global app)");
        if (ImGui::ColorEdit3("Default color", &catalogColor.x,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
        {
            m_AtomDefaults.elementColors[element] = glm::clamp(catalogColor, glm::vec3(0.0f), glm::vec3(1.0f));
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("Default size", &catalogScale, 0.1f, 4.0f, "%.2fx"))
        {
            m_AtomDefaults.elementScales[element] = std::clamp(catalogScale, 0.1f, 4.0f);
            settingsChanged = true;
        }
        if (ImGui::Button("Reset catalog defaults for element"))
        {
            m_AtomDefaults.elementColors.erase(element);
            m_AtomDefaults.elementScales.erase(element);
            settingsChanged = true;
        }
        ImGui::TextDisabled("Fallback after reset: procedural palette + 1.00x size.");

        ImGui::SeparatorText("Project overrides");
        if (ImGui::Button("Import overrides..."))
        {
            std::string importPath;
            if (OpenNativeYamlDialog(importPath, GetProjectAppearanceFilePath().string()))
            {
                PushUndoSnapshot("Import project appearance overrides");
                if (ImportProjectAppearanceOverrides(importPath))
                {
                    settingsChanged = true;
                    m_LastStructureOperationFailed = false;
                    m_LastStructureMessage = "Imported project appearance overrides: " + importPath;
                    LogInfo(m_LastStructureMessage);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Export overrides..."))
        {
            std::string exportPath;
            if (SaveNativeYamlDialog(exportPath, GetProjectAppearanceFilePath().string()))
            {
                if (ExportProjectAppearanceOverrides(exportPath))
                {
                    m_LastStructureOperationFailed = false;
                    m_LastStructureMessage = "Exported project appearance overrides: " + exportPath;
                    LogInfo(m_LastStructureMessage);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset project overrides"))
        {
            PushUndoSnapshot("Reset project appearance overrides");
            ResetProjectAppearanceOverrides();
            settingsChanged = true;
        }

        bool hasProjectColorOverride = (m_ElementColorOverrides.find(element) != m_ElementColorOverrides.end());
        if (ImGui::Checkbox("Override color in current project", &hasProjectColorOverride))
        {
            if (hasProjectColorOverride)
            {
                m_ElementColorOverrides[element] = resolvedColor;
            }
            else
            {
                m_ElementColorOverrides.erase(element);
            }
            settingsChanged = true;
        }
        if (hasProjectColorOverride)
        {
            glm::vec3 projectColor = m_ElementColorOverrides[element];
            if (ImGui::ColorEdit3("Project color", &projectColor.x,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
            {
                m_ElementColorOverrides[element] = glm::clamp(projectColor, glm::vec3(0.0f), glm::vec3(1.0f));
                settingsChanged = true;
            }
        }

        bool hasProjectScaleOverride = (m_ElementScaleOverrides.find(element) != m_ElementScaleOverrides.end());
        if (ImGui::Checkbox("Override size in current project", &hasProjectScaleOverride))
        {
            if (hasProjectScaleOverride)
            {
                m_ElementScaleOverrides[element] = resolvedScale;
            }
            else
            {
                m_ElementScaleOverrides.erase(element);
            }
            settingsChanged = true;
        }
        if (hasProjectScaleOverride)
        {
            float projectScale = m_ElementScaleOverrides[element];
            if (ImGui::SliderFloat("Project size", &projectScale, 0.1f, 4.0f, "%.2fx"))
            {
                m_ElementScaleOverrides[element] = std::clamp(projectScale, 0.1f, 4.0f);
                settingsChanged = true;
            }
        }

        if (ImGui::Button("Clear project overrides"))
        {
            m_ElementColorOverrides.erase(element);
            m_ElementScaleOverrides.erase(element);
            settingsChanged = true;
        }

        ImGui::End();
    }


} // namespace ds
