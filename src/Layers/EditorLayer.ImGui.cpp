#define IMVIEWGUIZMO_IMPLEMENTATION
#include "Layers/EditorLayerPrivate.h"

namespace ds
{
    void EditorLayer::OnImGuiRender()
    {
        DS_PROFILE_SCOPE_N("EditorLayer::OnImGuiRender");
        EnsureSceneDefaults();
        bool settingsChanged = false;
        const ImGuiIO &io = ImGui::GetIO();

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

                if (ImGui::MenuItem("Render Image", "F12"))
                {
                    SyncRenderAppearanceFromViewport();
                    m_ShowRenderImageDialog = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow))
                {
                    settingsChanged = true;
                }
                if (ImGui::MenuItem("Log / Errors", nullptr, &m_ShowLogPanel))
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

            ImGui::EndMenuBar();
        }

        ImGui::Begin("Viewport");
        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();
        m_BlockSelectionThisFrame = false;
        m_GizmoConsumedMouseThisFrame = false;

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
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Translate mode canceled.";
            AppendSelectionDebugLog("Translate mode canceled (Esc/RMB)");
        };

        auto commitTranslateMode = [&]()
        {
            if (!m_TranslateModeActive)
            {
                return;
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
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Translate mode applied.";
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
            AppendSelectionDebugLog("Rotate mode canceled (Esc/RMB)");
        };

        auto commitRotateMode = [&]()
        {
            if (!m_RotateModeActive)
            {
                return;
            }

            m_RotateModeActive = false;
            m_InteractionMode = InteractionMode::Select;
            m_RotateConstraintAxis = -1;
            m_RotateIndices.clear();
            m_RotateInitialCartesian.clear();
            m_RotateCurrentAngle = 0.0f;
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Rotate mode applied around 3D cursor.";
            AppendSelectionDebugLog("Rotate mode applied (LMB/Enter/R)");
        };

        auto deleteCurrentSelection = [&]()
        {
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

        if (m_ViewportFocused && !io.WantTextInput && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            m_AddMenuPopupPos = glm::vec2(io.MousePos.x, io.MousePos.y);
            ImGui::OpenPopup("AddSceneObjectPopup");
            m_BlockSelectionThisFrame = true;
        }

        if (m_ViewportFocused && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
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

        if (m_ViewportFocused && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_H, false))
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

        if (m_ViewportFocused && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_N, false))
        {
            m_ShowToolsPanel = !m_ShowToolsPanel;
            settingsChanged = true;
        }

        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F12, false))
        {
            SyncRenderAppearanceFromViewport();
            m_ShowRenderImageDialog = true;
            m_ShowRenderPreviewWindow = true;
        }

        const bool translateModalHotkey = ImGui::IsKeyPressed(ImGuiKey_G, false);
        const bool translateGizmoHotkey = (m_InteractionMode != InteractionMode::ViewSet && ImGui::IsKeyPressed(ImGuiKey_T, false));

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

        if (m_ViewportFocused && !io.WantTextInput && m_InteractionMode != InteractionMode::ViewSet && ImGui::IsKeyPressed(ImGuiKey_R, false))
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

        if (m_ViewportFocused && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_S, false))
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

                const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
                const glm::vec2 mouseDelta = mousePos - m_TranslateLastMousePos;
                m_TranslateLastMousePos = mousePos;

                const glm::vec3 forward = glm::normalize(glm::vec3(
                    std::cos(m_Camera->GetPitch()) * std::sin(m_Camera->GetYaw()),
                    std::cos(m_Camera->GetPitch()) * std::cos(m_Camera->GetYaw()),
                    std::sin(m_Camera->GetPitch())));
                const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
                const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
                const glm::vec3 up = glm::normalize(glm::cross(right, forward));

                const float panSpeed = 0.006f * m_Camera->GetDistance();
                glm::vec3 frameDelta = (-right * mouseDelta.x + up * mouseDelta.y) * panSpeed;

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

                m_TranslateCurrentOffset += frameDelta;

                glm::vec3 appliedOffset = m_TranslateCurrentOffset;
                if (io.KeyCtrl)
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

        if (m_ViewportFocused && m_InteractionMode == InteractionMode::Select && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_B, false))
        {
            m_BoxSelectArmed = true;
            m_BoxSelecting = false;
            m_CircleSelectArmed = false;
            m_CircleSelecting = false;
            AppendSelectionDebugLog("Box select armed (press LMB and drag)");
        }

        if (m_ViewportFocused && m_InteractionMode == InteractionMode::Select && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_C, false))
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

                        const bool manipulated = ImGuizmo::Manipulate(
                            glm::value_ptr(m_Camera->GetViewMatrix()),
                            glm::value_ptr(m_Camera->GetProjectionMatrix()),
                            operation,
                            mode,
                            glm::value_ptr(gizmoTransform),
                            glm::value_ptr(deltaTransform),
                            m_GizmoSnapEnabled ? snapValues : nullptr);
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

                        if (!gizmoOver && !gizmoUsing && pivotInViewport && !m_FallbackGizmoDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (hoveredFallbackAxis >= 0)
                            {
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

                                    if (m_GizmoSnapEnabled)
                                    {
                                        const float snapStep = glm::radians(std::max(0.1f, m_GizmoRotateSnapDeg));
                                        m_FallbackDragAccumulated += deltaAngle;
                                        const float snappedTarget = std::round(m_FallbackDragAccumulated / snapStep) * snapStep;
                                        deltaAngle = snappedTarget - m_FallbackDragApplied;
                                        m_FallbackDragApplied = snappedTarget;
                                    }

                                    if (std::abs(deltaAngle) > 1e-6f)
                                    {
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
                                    if (m_GizmoSnapEnabled)
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
                                AppendSelectionDebugLog("Fallback axis drag ended");
                            }
                        }

                        if (manipulated && ImGuizmo::IsUsing())
                        {
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
                    }
                    else if (s_LastValidPivot)
                    {
                        AppendSelectionDebugLog("Gizmo pivot unavailable: selected atoms are not valid indices.");
                        s_LastValidPivot = false;
                    }
                }
                else if (s_LastValidPivot)
                {
                    AppendSelectionDebugLog("Gizmo pivot reset: transform gizmo preconditions no longer met.");
                    s_LastValidPivot = false;
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

                    if (!m_SelectedAtomIndices.empty())
                    {
                        ImGui::SeparatorText("Quick change atom type");
                        ImGui::SetNextItemWidth(92.0f);
                        ImGui::InputText("##CtxQuickAtomType", m_ChangeAtomElementBuffer.data(), m_ChangeAtomElementBuffer.size());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Table"))
                        {
                            m_PeriodicTableTarget = PeriodicTableTarget::ChangeSelectedAtoms;
                            m_PeriodicTableOpenedFromContextMenu = true;
                            m_PeriodicTableOpen = true;
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
                        m_SelectedAtomIndices.clear();
                        m_SelectedBondKeys.clear();
                        m_SelectedBondLabelKey = 0;
                        m_SelectedTransformEmptyIndex = -1;
                        m_SelectedSpecialNode = SpecialNodeSelection::None;
                        m_SelectedAtomIndices.reserve(m_WorkingStructure.atoms.size());
                        for (std::size_t i = 0; i < m_WorkingStructure.atoms.size(); ++i)
                        {
                            if (!IsAtomHidden(i) && IsAtomCollectionVisible(i) && IsAtomCollectionSelectable(i))
                            {
                                m_SelectedAtomIndices.push_back(i);
                            }
                        }
                        AppendSelectionDebugLog("Context menu: Select All");
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
                            m_TransformEmpties[static_cast<std::size_t>(m_ActiveTransformEmptyIndex)].position = m_CursorPosition;
                            settingsChanged = true;
                            AppendSelectionDebugLog("Context menu: Move active empty -> 3D cursor");
                        }

                        if (ImGui::MenuItem("Move active empty to selection center", nullptr, false, HasActiveTransformEmpty() && !m_SelectedAtomIndices.empty()))
                        {
                            m_TransformEmpties[static_cast<std::size_t>(m_ActiveTransformEmptyIndex)].position = ComputeSelectionCenter();
                            settingsChanged = true;
                            AppendSelectionDebugLog("Context menu: Move active empty -> selection center");
                        }

                        if (ImGui::MenuItem("Align active empty axes to world", nullptr, false, HasActiveTransformEmpty()))
                        {
                            TransformEmpty &activeEmpty = m_TransformEmpties[static_cast<std::size_t>(m_ActiveTransformEmptyIndex)];
                            activeEmpty.axes = {
                                glm::vec3(1.0f, 0.0f, 0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f),
                                glm::vec3(0.0f, 0.0f, 1.0f)};
                            settingsChanged = true;
                            AppendSelectionDebugLog("Context menu: Align active empty axes -> world");
                        }

                        if (ImGui::MenuItem("Align active empty Z to selected atoms", nullptr, false, HasActiveTransformEmpty() && m_SelectedAtomIndices.size() >= 2))
                        {
                            if (AlignEmptyZAxisFromSelectedAtoms(m_ActiveTransformEmptyIndex))
                            {
                                settingsChanged = true;
                                AppendSelectionDebugLog("Context menu: Align active empty Z -> selected atoms");
                            }
                        }

                        if (ImGui::MenuItem("Delete active empty", nullptr, false, HasActiveTransformEmpty()))
                        {
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
                            settingsChanged |= SceneGroupingBackend::CreateGroupFromCurrentSelection(*this);
                        }

                        if (ImGui::MenuItem("Add selection to active group", nullptr, false, hasSelection && m_ActiveGroupIndex >= 0))
                        {
                            SceneGroupingBackend::AddCurrentSelectionToGroup(*this, m_ActiveGroupIndex);
                            settingsChanged = true;
                        }

                        if (ImGui::MenuItem("Remove selection from active group", nullptr, false, hasSelection && m_ActiveGroupIndex >= 0))
                        {
                            SceneGroupingBackend::RemoveCurrentSelectionFromGroup(*this, m_ActiveGroupIndex);
                            settingsChanged = true;
                        }

                        if (ImGui::MenuItem("Select active group", nullptr, false, m_ActiveGroupIndex >= 0))
                        {
                            SceneGroupingBackend::SelectGroup(*this, m_ActiveGroupIndex);
                        }

                        if (ImGui::MenuItem("Delete active group", nullptr, false, m_ActiveGroupIndex >= 0))
                        {
                            settingsChanged |= SceneGroupingBackend::DeleteGroup(*this, m_ActiveGroupIndex);
                        }

                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Selection Utilities"))
                    {
                        const bool hasSelectedEmpty = (m_SelectedTransformEmptyIndex >= 0 && m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size()));
                        const bool hasSelectedLight = (m_SelectedSpecialNode == SpecialNodeSelection::Light);
                        if (ImGui::BeginMenu("Change selected atom type", !m_SelectedAtomIndices.empty()))
                        {
                            ImGui::SetNextItemWidth(110.0f);
                            ImGui::InputText("Element", m_ChangeAtomElementBuffer.data(), m_ChangeAtomElementBuffer.size());

                            if (ImGui::MenuItem("Periodic table..."))
                            {
                                m_PeriodicTableTarget = PeriodicTableTarget::ChangeSelectedAtoms;
                                m_PeriodicTableOpenedFromContextMenu = true;
                                m_PeriodicTableOpen = true;
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
                        m_CircleSelectRadius = glm::clamp(m_CircleSelectRadius + io.MouseWheel * 4.0f, 8.0f, 260.0f);
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
                            const bool additiveSelection = io.KeyCtrl;
                            const bool includeAtoms =
                                (m_SelectionFilter == SelectionFilter::AtomsOnly) ||
                                (m_SelectionFilter == SelectionFilter::AtomsAndBonds);
                            const bool includeBonds =
                                (m_SelectionFilter == SelectionFilter::AtomsAndBonds) ||
                                (m_SelectionFilter == SelectionFilter::BondsOnly) ||
                                (m_SelectionFilter == SelectionFilter::BondLabelsOnly);
                            if (includeAtoms)
                            {
                                SelectAtomsInScreenRect(m_BoxSelectStart, m_BoxSelectEnd, additiveSelection);
                            }
                            else if (!additiveSelection)
                            {
                                m_SelectedAtomIndices.clear();
                                m_SelectedTransformEmptyIndex = -1;
                                m_SelectedSpecialNode = SpecialNodeSelection::None;
                            }
                            if (includeBonds)
                            {
                                SelectBondsInScreenRect(m_BoxSelectStart, m_BoxSelectEnd, additiveSelection);
                            }
                            else if (!additiveSelection)
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
                        const bool additiveSelection = io.KeyCtrl;
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
                            SelectAtomsInScreenCircle(mousePos, m_CircleSelectRadius, additiveSelection);
                        }
                        else if (!additiveSelection)
                        {
                            m_SelectedAtomIndices.clear();
                            m_SelectedTransformEmptyIndex = -1;
                            m_SelectedSpecialNode = SpecialNodeSelection::None;
                        }
                        if (includeBonds)
                        {
                            SelectBondsInScreenCircle(mousePos, m_CircleSelectRadius, additiveSelection);
                        }
                        else if (!additiveSelection)
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
                                if (includeAtoms)
                                {
                                    SelectAtomsInScreenCircle(mousePos, m_CircleSelectRadius, true);
                                }
                                if (includeBonds)
                                {
                                    SelectBondsInScreenCircle(mousePos, m_CircleSelectRadius, true);
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
                    drawList->AddCircleFilled(center, radius, IM_COL32(94, 185, 255, 36), 48);
                    drawList->AddCircle(center, radius, IM_COL32(124, 210, 255, 225), 48, 1.6f);
                    char radiusLabel[64] = {};
                    std::snprintf(radiusLabel, sizeof(radiusLabel), "C-select r=%.0f", radius);
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

        ImGui::Begin("Viewport Info");
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
                if (ImGui::DragFloatRange2("Render scale min/max", &m_RenderScaleMin, &m_RenderScaleMax, 0.01f, 0.1f, 2.0f, "min %.2f", "max %.2f"))
                {
                    ensureFloatRange(m_RenderScaleMin, m_RenderScaleMax, 0.1f, 2.0f);
                    settingsChanged = true;
                }
                if (ImGui::SliderFloat("Render scale", &m_ViewportRenderScale, m_RenderScaleMin, m_RenderScaleMax, "%.2fx"))
                {
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
            }

            if (ImGui::CollapsingHeader("Atoms", defaultOpenFlags))
            {
                ImGui::Indent(nestedSectionIndent);
                if (ImGui::CollapsingHeader("Rendering", defaultOpenFlags))
                {
                    ensureFloatRange(m_AtomSizeMin, m_AtomSizeMax, 0.01f, 4.0f);
                    ensureFloatRange(m_AtomBrightnessMin, m_AtomBrightnessMax, 0.05f, 6.0f);
                    ensureFloatRange(m_AtomGlowMin, m_AtomGlowMax, 0.0f, 2.0f);

                    if (ImGui::DragFloatRange2("Atom size min/max", &m_AtomSizeMin, &m_AtomSizeMax, 0.01f, 0.01f, 4.0f, "min %.2f", "max %.2f"))
                    {
                        ensureFloatRange(m_AtomSizeMin, m_AtomSizeMax, 0.01f, 4.0f);
                        settingsChanged = true;
                    }
                    if (ImGui::SliderFloat("Atom size", &m_SceneSettings.atomScale, m_AtomSizeMin, m_AtomSizeMax, "%.2f"))
                    {
                        settingsChanged = true;
                    }
                    if (ImGui::DragFloatRange2("Atom brightness min/max", &m_AtomBrightnessMin, &m_AtomBrightnessMax, 0.01f, 0.05f, 6.0f, "min %.2f", "max %.2f"))
                    {
                        ensureFloatRange(m_AtomBrightnessMin, m_AtomBrightnessMax, 0.05f, 6.0f);
                        settingsChanged = true;
                    }
                    if (ImGui::SliderFloat("Atom brightness", &m_SceneSettings.atomBrightness, m_AtomBrightnessMin, m_AtomBrightnessMax, "%.2f"))
                    {
                        settingsChanged = true;
                    }
                    if (ImGui::DragFloatRange2("Atom glow min/max", &m_AtomGlowMin, &m_AtomGlowMax, 0.005f, 0.0f, 2.0f, "min %.2f", "max %.2f"))
                    {
                        ensureFloatRange(m_AtomGlowMin, m_AtomGlowMax, 0.0f, 2.0f);
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

                if (ImGui::CollapsingHeader("Per-element colors", defaultOpenFlags))
                {
                    if (!m_HasStructureLoaded || m_WorkingStructure.species.empty())
                    {
                        ImGui::TextDisabled("Load structure to edit per-element colors.");
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

                            glm::vec3 color = ColorFromElement(element);
                            const auto overrideIt = m_ElementColorOverrides.find(element);
                            if (overrideIt != m_ElementColorOverrides.end())
                            {
                                color = overrideIt->second;
                            }

                            std::string label = element + "##ElementColor_" + element;
                            if (ImGui::ColorEdit3(label.c_str(), &color.x,
                                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
                            {
                                m_ElementColorOverrides[element] = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
                                settingsChanged = true;
                            }
                        }

                        if (ImGui::Button("Reset per-element colors"))
                        {
                            m_ElementColorOverrides.clear();
                            settingsChanged = true;
                        }
                    }
                }

                if (ImGui::CollapsingHeader("Atom editing", defaultOpenFlags))
                {
                    const char *inputCoordinateModes[] = {"Direct", "Cartesian"};

                    ImGui::InputText("Element", m_AddAtomElementBuffer.data(), m_AddAtomElementBuffer.size());
                    ImGui::SameLine();
                    if (ImGui::Button("Periodic table"))
                    {
                        m_PeriodicTableTarget = PeriodicTableTarget::AddAtomEntry;
                        m_PeriodicTableOpenedFromContextMenu = false;
                        m_PeriodicTableOpen = true;
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
                    "Dark",
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

                if (ImGui::Checkbox("Touchpad-friendly navigation", &m_TouchpadNavigationEnabled))
                {
                    settingsChanged = true;
                }

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

        if (!m_ShowToolsPanel)
        {
            const ImVec2 workPos = viewport->WorkPos;
            const ImVec2 workSize = viewport->WorkSize;
            ImGui::SetNextWindowPos(ImVec2(workPos.x + workSize.x - 28.0f, workPos.y + 120.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(24.0f, 120.0f), ImGuiCond_Always);
            ImGui::Begin("##ToolsCollapsedStrip", nullptr,
                         ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings);
            ImGui::Dummy(ImVec2(1.0f, 8.0f));
            if (ImGui::Button(">", ImVec2(20.0f, 36.0f)))
            {
                m_ShowToolsPanel = true;
                settingsChanged = true;
            }
            ImGui::Spacing();
            ImGui::TextUnformatted("N");
            ImGui::End();
        }

        if (m_ShowToolsPanel)
        {
            ImGui::SetNextWindowSize(ImVec2(460.0f, 780.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Tools", &m_ShowToolsPanel);

            const char *coordinateModes[] = {"Direct", "Cartesian"};
            const ImGuiTreeNodeFlags defaultOpenFlags = ImGuiTreeNodeFlags_DefaultOpen;
            const float nestedSectionIndent = ImGui::GetStyle().IndentSpacing * 0.65f;

            ImGui::TextUnformatted("Workflow");

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
                    const char *samplePath = "assets/samples/POSCAR";
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

            auto drawAtomsToolsSection = [&]()
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

                    if (ImGui::CollapsingHeader("Per-element colors", defaultOpenFlags))
                    {
                        if (!m_HasStructureLoaded || m_WorkingStructure.species.empty())
                        {
                            ImGui::TextDisabled("Load structure to edit per-element colors.");
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

                                glm::vec3 color = ColorFromElement(element);
                                const auto overrideIt = m_ElementColorOverrides.find(element);
                                if (overrideIt != m_ElementColorOverrides.end())
                                {
                                    color = overrideIt->second;
                                }

                                std::string label = element + "##ToolsElementColor_" + element;
                                if (ImGui::ColorEdit3(label.c_str(), &color.x,
                                                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
                                {
                                    m_ElementColorOverrides[element] = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
                                    settingsChanged = true;
                                }
                            }

                            if (ImGui::Button("Reset per-element colors"))
                            {
                                m_ElementColorOverrides.clear();
                                settingsChanged = true;
                            }
                        }
                    }

                    if (ImGui::CollapsingHeader("Atom editing", defaultOpenFlags))
                    {
                        const char *inputCoordinateModes[] = {"Direct", "Cartesian"};

                        ImGui::InputText("Element", m_AddAtomElementBuffer.data(), m_AddAtomElementBuffer.size());
                        ImGui::SameLine();
                        if (ImGui::Button("Periodic table"))
                        {
                            m_PeriodicTableTarget = PeriodicTableTarget::AddAtomEntry;
                            m_PeriodicTableOpenedFromContextMenu = false;
                            m_PeriodicTableOpen = true;
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

                if (ImGui::Button("Hide selected (H)"))
                {
                    if (!m_SelectedAtomIndices.empty())
                    {
                        for (std::size_t atomIndex : m_SelectedAtomIndices)
                        {
                            m_HiddenAtomIndices.insert(atomIndex);
                        }
                        m_SelectedAtomIndices.clear();
                    }
                    for (std::uint64_t key : m_SelectedBondKeys)
                    {
                        m_HiddenBondKeys.insert(key);
                        auto labelIt = m_BondLabelStates.find(key);
                        if (labelIt != m_BondLabelStates.end())
                        {
                            labelIt->second.hidden = true;
                        }
                    }
                    m_SelectedBondKeys.clear();
                    if (m_SelectedBondLabelKey != 0)
                    {
                        auto labelIt = m_BondLabelStates.find(m_SelectedBondLabelKey);
                        if (labelIt != m_BondLabelStates.end())
                        {
                            labelIt->second.hidden = true;
                        }
                        m_SelectedBondLabelKey = 0;
                    }
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Unhide all (Alt+H)"))
                {
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
                    settingsChanged = true;
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

            drawAtomsToolsSection();

            if (ImGui::CollapsingHeader("Bonds", defaultOpenFlags))
            {
                constexpr ImGuiColorEditFlags kPickerOnlyFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel;
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

                const char *selectionFilters[] = {"Atoms", "Atoms + Bonds", "Bonds", "Bonds labels"};
                int selectionFilterIndex = static_cast<int>(m_SelectionFilter);
                if (ImGui::Combo("Selection filter", &selectionFilterIndex, selectionFilters, IM_ARRAYSIZE(selectionFilters)))
                {
                    m_SelectionFilter = static_cast<SelectionFilter>(std::clamp(selectionFilterIndex, 0, 3));
                    settingsChanged = true;
                }

                const char *bondRenderStyles[] = {"Unicolor line", "Bi-color line", "Color gradient"};
                int bondRenderStyleIndex = static_cast<int>(m_BondRenderStyle);
                if (ImGui::Combo("Bond render style", &bondRenderStyleIndex, bondRenderStyles, IM_ARRAYSIZE(bondRenderStyles)))
                {
                    m_BondRenderStyle = static_cast<BondRenderStyle>(std::clamp(bondRenderStyleIndex, 0, 2));
                    settingsChanged = true;
                }
                if (ImGui::Checkbox("Delete affects labels only", &m_BondLabelDeleteOnlyMode))
                {
                    settingsChanged = true;
                }

                ImGui::TextDisabled("Select tools: B = box, C = circle (mouse wheel changes circle radius)");

                if (ImGui::Checkbox("Show bond length labels", &m_ShowBondLengthLabels))
                {
                    settingsChanged = true;
                }

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
                    m_AutoBondsDirty = true;
                    m_LastStructureOperationFailed = false;
                    m_LastStructureMessage = "Bond regeneration scheduled.";
                }
                if (!canRegenerateBonds)
                {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(m_AutoBondsDirty ? "Status: pending rebuild" : "Status: cached");

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
                if (ImGui::BeginTable("BondColorGrid", 2, ImGuiTableFlags_SizingStretchProp))
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

                if (ImGui::Button("Restore deleted bonds"))
                {
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

                if (ImGui::Checkbox("Show static angle labels", &m_ShowStaticAngleLabels))
                {
                    settingsChanged = true;
                }
                ImGui::SameLine();
                ImGui::Text("Angle labels: %zu", m_AngleLabelStates.size());
                const bool hasAngleLabels = !m_AngleLabelStates.empty();
                if (!hasAngleLabels)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Clear all angle labels") && hasAngleLabels)
                {
                    m_AngleLabelStates.clear();
                    settingsChanged = true;
                }
                if (!hasAngleLabels)
                {
                    ImGui::EndDisabled();
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
                        labelState.deleted = true;
                        labelState.hidden = true;
                        m_SelectedBondKeys.erase(m_SelectedBondLabelKey);
                        settingsChanged = true;
                    }
                }

                if (ImGui::Button("Restore all deleted labels"))
                {
                    for (auto &[key, state] : m_BondLabelStates)
                    {
                        (void)key;
                        state.deleted = false;
                        state.hidden = false;
                    }
                    settingsChanged = true;
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

        if (m_ShowSceneOutlinerPanel)
        {
            ImGui::Begin("Scene Outliner", &m_ShowSceneOutlinerPanel);

            if (ImGui::Button("Add collection"))
            {
                SceneCollection collection;
                collection.id = GenerateSceneUUID();
                char label[48] = {};
                std::snprintf(label, sizeof(label), "Collection %d", m_CollectionCounter++);
                collection.name = label;
                m_Collections.push_back(collection);
                m_ActiveCollectionIndex = static_cast<int>(m_Collections.size()) - 1;
                settingsChanged = true;
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
                const int deletedCollectionIndex = m_ActiveCollectionIndex;
                std::vector<std::size_t> atomIndicesToDelete;
                atomIndicesToDelete.reserve(m_AtomCollectionIndices.size());
                for (std::size_t atomIndex = 0; atomIndex < m_AtomCollectionIndices.size(); ++atomIndex)
                {
                    if (m_AtomCollectionIndices[atomIndex] == deletedCollectionIndex)
                    {
                        atomIndicesToDelete.push_back(atomIndex);
                    }
                }

                std::vector<int> emptyIndicesToDelete;
                emptyIndicesToDelete.reserve(m_TransformEmpties.size());
                for (int emptyIndex = 0; emptyIndex < static_cast<int>(m_TransformEmpties.size()); ++emptyIndex)
                {
                    if (m_TransformEmpties[static_cast<std::size_t>(emptyIndex)].collectionIndex == deletedCollectionIndex)
                    {
                        emptyIndicesToDelete.push_back(emptyIndex);
                    }
                }

                const std::size_t removedAtomCount = atomIndicesToDelete.size();
                if (!atomIndicesToDelete.empty())
                {
                    m_SelectedAtomIndices = atomIndicesToDelete;
                    m_SelectedTransformEmptyIndex = -1;
                    m_SelectedSpecialNode = SpecialNodeSelection::None;
                    m_SelectedBondKeys.clear();
                    m_SelectedBondLabelKey = 0;
                    deleteCurrentSelection();
                }

                const std::size_t removedEmptyCount = emptyIndicesToDelete.size();
                for (auto it = emptyIndicesToDelete.rbegin(); it != emptyIndicesToDelete.rend(); ++it)
                {
                    DeleteTransformEmptyAtIndex(*it);
                }

                m_Collections.erase(m_Collections.begin() + deletedCollectionIndex);

                for (int &collectionIndex : m_AtomCollectionIndices)
                {
                    if (collectionIndex > deletedCollectionIndex)
                    {
                        --collectionIndex;
                    }
                }
                EnsureAtomCollectionAssignments();

                for (TransformEmpty &empty : m_TransformEmpties)
                {
                    if (empty.collectionIndex > deletedCollectionIndex)
                    {
                        --empty.collectionIndex;
                    }

                    if (empty.collectionIndex < 0 || empty.collectionIndex >= static_cast<int>(m_Collections.size()))
                    {
                        empty.collectionIndex = 0;
                    }

                    if (!m_Collections.empty())
                    {
                        empty.collectionId = m_Collections[static_cast<std::size_t>(empty.collectionIndex)].id;
                    }
                }

                if (m_ActiveCollectionIndex >= static_cast<int>(m_Collections.size()))
                {
                    m_ActiveCollectionIndex = static_cast<int>(m_Collections.size()) - 1;
                }
                if (m_ActiveCollectionIndex < 0)
                {
                    m_ActiveCollectionIndex = 0;
                }

                settingsChanged = true;
                m_LastStructureOperationFailed = false;
                m_LastStructureMessage = "Deleted active collection with atoms=" + std::to_string(removedAtomCount) +
                                         ", empties=" + std::to_string(removedEmptyCount) + ".";
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
            const bool canCreateGroup = !m_SelectedAtomIndices.empty() || m_SelectedTransformEmptyIndex >= 0;
            if (!canCreateGroup)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Create group from selection") && canCreateGroup)
            {
                settingsChanged |= SceneGroupingBackend::CreateGroupFromCurrentSelection(*this);
            }
            if (!canCreateGroup)
            {
                ImGui::EndDisabled();
            }

            ImGui::SeparatorText("Collections");

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

            for (std::size_t collectionIndex = 0; collectionIndex < m_Collections.size(); ++collectionIndex)
            {
                SceneCollection &collection = m_Collections[collectionIndex];
                ImGui::PushID(static_cast<int>(collectionIndex));

                ImGui::Checkbox("##visible", &collection.visible);
                ImGui::SameLine();
                ImGui::Checkbox("##selectable", &collection.selectable);
                ImGui::SameLine();

                const bool isActiveCollection = (static_cast<int>(collectionIndex) == m_ActiveCollectionIndex);
                if (ImGui::Selectable(collection.name.c_str(), isActiveCollection))
                {
                    m_ActiveCollectionIndex = static_cast<int>(collectionIndex);
                }

                if (ImGui::BeginDragDropTarget())
                {
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

                if (ImGui::TreeNode("Objects"))
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
                            const std::string label = "Empty: " + empty.name + "##empty_" + std::to_string(emptyIndex);
                            const bool open = ImGui::TreeNodeEx(
                                label.c_str(),
                                (isSelected ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);

                            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                            {
                                m_SelectedTransformEmptyIndex = static_cast<int>(emptyIndex);
                                m_ActiveTransformEmptyIndex = static_cast<int>(emptyIndex);
                                m_SelectedAtomIndices.clear();
                                m_OutlinerAtomSelectionAnchor.reset();
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

                        const std::string atomsLabel = "Atoms (" + std::to_string(collectionAtomCount) + ")";
                        if (ImGui::TreeNode(atomsLabel.c_str()))
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

                                if (!selectableAtom)
                                {
                                    ImGui::EndDisabled();
                                }
                            }
                            ImGui::TreePop();
                        }
                    }

                    if (collectionIndex == 0)
                    {
                        if (ImGui::TreeNode("Groups"))
                        {
                            for (std::size_t groupIndex = 0; groupIndex < m_ObjectGroups.size(); ++groupIndex)
                            {
                                SceneGroupingBackend::SanitizeGroup(*this, static_cast<int>(groupIndex));
                                const SceneGroup &group = m_ObjectGroups[groupIndex];
                                const bool isActiveGroup = (static_cast<int>(groupIndex) == m_ActiveGroupIndex);
                                const std::string groupLabel = "Group: " + group.name;
                                if (ImGui::Selectable(groupLabel.c_str(), isActiveGroup))
                                {
                                    m_ActiveGroupIndex = static_cast<int>(groupIndex);
                                    SceneGroupingBackend::SelectGroup(*this, m_ActiveGroupIndex);
                                }
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                {
                                    ImGui::SetTooltip("Atoms: %zu | Empties: %zu", group.atomIndices.size(), group.emptyIndices.size());
                                }
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
                    SceneGroupingBackend::AddCurrentSelectionToGroup(*this, m_ActiveGroupIndex);
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove selection from active"))
                {
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
                if (ImGui::Selectable(group.name.c_str(), isActiveGroup))
                {
                    m_ActiveGroupIndex = static_cast<int>(groupIndex);
                    SceneGroupingBackend::SelectGroup(*this, m_ActiveGroupIndex);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    ImGui::SetTooltip("Atoms: %zu | Empties: %zu", group.atomIndices.size(), group.emptyIndices.size());
                }
            }

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

            const char *filterItems[] = {
                "All",
                "Trace only",
                "Info + above",
                "Warnings + Errors + Fatal",
                "Errors + Fatal",
                "Fatal only"};

            if (ImGui::Combo("Filter", &m_LogFilter, filterItems, IM_ARRAYSIZE(filterItems)))
            {
                settingsChanged = true;
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

            ImGui::Separator();

            ImGui::BeginChild("LogEntries", ImVec2(0.0f, 0.0f), true);
            const auto entries = Logger::Get().GetEntriesSnapshot();
            for (const auto &entry : entries)
            {
                if (m_LogFilter == 1 && entry.level != LogLevel::Trace)
                {
                    continue;
                }
                if (m_LogFilter == 2 && entry.level == LogLevel::Trace)
                {
                    continue;
                }
                if (m_LogFilter == 3 && (entry.level == LogLevel::Trace || entry.level == LogLevel::Info))
                {
                    continue;
                }
                if (m_LogFilter == 4 && !(entry.level == LogLevel::Error || entry.level == LogLevel::Fatal))
                {
                    continue;
                }
                if (m_LogFilter == 5 && entry.level != LogLevel::Fatal)
                {
                    continue;
                }

                ImVec4 color = ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
                const char *levelText = "TRACE";

                if (entry.level == LogLevel::Info)
                {
                    color = ImVec4(0.42f, 0.78f, 0.98f, 1.0f);
                    levelText = "INFO";
                }
                else if (entry.level == LogLevel::Warn)
                {
                    color = ImVec4(0.96f, 0.76f, 0.35f, 1.0f);
                    levelText = "WARN";
                }
                else if (entry.level == LogLevel::Error)
                {
                    color = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
                    levelText = "ERROR";
                }
                else if (entry.level == LogLevel::Fatal)
                {
                    color = ImVec4(1.0f, 0.08f, 0.08f, 1.0f);
                    levelText = "FATAL";
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(levelText);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::Text("[%s] %s", entry.timestamp.c_str(), entry.message.c_str());
            }

            if (m_LogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            {
                ImGui::SetScrollHereY(1.0f);
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
                    m_PeriodicTableTarget = PeriodicTableTarget::AddAtomEntry;
                    m_PeriodicTableOpenedFromContextMenu = false;
                    m_PeriodicTableOpen = true;
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


} // namespace ds
