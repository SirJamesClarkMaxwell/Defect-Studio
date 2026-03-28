#include "Layers/EditorLayerPrivate.h"

namespace ds
{
    void EditorLayer::SelectAtomsInScreenRect(const glm::vec2 &screenStart, const glm::vec2 &screenEnd, bool additiveSelection)
    {
        if (!m_Camera || !m_HasStructureLoaded || m_WorkingStructure.atoms.empty())
        {
            return;
        }

        const float left = std::min(screenStart.x, screenEnd.x);
        const float right = std::max(screenStart.x, screenEnd.x);
        const float top = std::min(screenStart.y, screenEnd.y);
        const float bottom = std::max(screenStart.y, screenEnd.y);

        if (!additiveSelection)
        {
            m_SelectedAtomIndices.clear();
        }

        const glm::mat4 viewProjection = m_Camera->GetViewProjectionMatrix();
        std::size_t addedCount = 0;

        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return;
        }

        for (std::size_t i = 0; i < m_WorkingStructure.atoms.size(); ++i)
        {
            if (IsAtomHidden(i) || !IsAtomCollectionVisible(i) || !IsAtomCollectionSelectable(i))
            {
                continue;
            }

            glm::vec3 center = m_WorkingStructure.atoms[i].position;
            if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
            {
                center = m_WorkingStructure.DirectToCartesian(center);
            }

            const glm::vec4 clip = viewProjection * glm::vec4(center, 1.0f);
            if (clip.w <= 1e-6f)
            {
                continue;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < -1.0f || ndc.z > 1.0f)
            {
                continue;
            }

            const float screenX = m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width;
            const float screenY = m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height;

            if (screenX >= left && screenX <= right && screenY >= top && screenY <= bottom)
            {
                if (!IsAtomSelected(i))
                {
                    m_SelectedAtomIndices.push_back(i);
                    ++addedCount;
                }
            }
        }

        AppendSelectionDebugLog(
            "Box selection: rect=(" + std::to_string(left) + "," + std::to_string(top) + ")-" +
            "(" + std::to_string(right) + "," + std::to_string(bottom) + ")" +
            " additive=" + (additiveSelection ? std::string("1") : std::string("0")) +
            " selected=" + std::to_string(m_SelectedAtomIndices.size()) +
            " added=" + std::to_string(addedCount));
    }

    void EditorLayer::SelectBondsInScreenRect(const glm::vec2 &screenStart, const glm::vec2 &screenEnd, bool additiveSelection)
    {
        if (!m_Camera || m_GeneratedBonds.empty())
        {
            return;
        }

        const float left = std::min(screenStart.x, screenEnd.x);
        const float right = std::max(screenStart.x, screenEnd.x);
        const float top = std::min(screenStart.y, screenEnd.y);
        const float bottom = std::max(screenStart.y, screenEnd.y);

        if (!additiveSelection)
        {
            m_SelectedBondKeys.clear();
        }

        const glm::mat4 viewProjection = m_Camera->GetViewProjectionMatrix();
        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return;
        }

        std::size_t addedCount = 0;
        for (const BondSegment &bond : m_GeneratedBonds)
        {
            const std::uint64_t key = MakeBondPairKey(bond.atomA, bond.atomB);
            if (m_DeletedBondKeys.find(key) != m_DeletedBondKeys.end() ||
                m_HiddenBondKeys.find(key) != m_HiddenBondKeys.end() ||
                IsAtomHidden(bond.atomA) || IsAtomHidden(bond.atomB) ||
                !IsAtomCollectionVisible(bond.atomA) || !IsAtomCollectionVisible(bond.atomB) ||
                !IsAtomCollectionSelectable(bond.atomA) || !IsAtomCollectionSelectable(bond.atomB))
            {
                continue;
            }

            const glm::vec4 clipA = viewProjection * glm::vec4(bond.start, 1.0f);
            const glm::vec4 clipB = viewProjection * glm::vec4(bond.end, 1.0f);
            if (clipA.w <= 1e-6f || clipB.w <= 1e-6f)
            {
                continue;
            }

            const glm::vec3 ndcA = glm::vec3(clipA) / clipA.w;
            const glm::vec3 ndcB = glm::vec3(clipB) / clipB.w;
            if (ndcA.z < -1.0f || ndcA.z > 1.0f || ndcB.z < -1.0f || ndcB.z > 1.0f)
            {
                continue;
            }

            const glm::vec2 screenA(
                m_ViewportRectMin.x + (ndcA.x * 0.5f + 0.5f) * width,
                m_ViewportRectMin.y + (1.0f - (ndcA.y * 0.5f + 0.5f)) * height);
            const glm::vec2 screenB(
                m_ViewportRectMin.x + (ndcB.x * 0.5f + 0.5f) * width,
                m_ViewportRectMin.y + (1.0f - (ndcB.y * 0.5f + 0.5f)) * height);

            const bool endpointInside =
                ((screenA.x >= left && screenA.x <= right && screenA.y >= top && screenA.y <= bottom) ||
                 (screenB.x >= left && screenB.x <= right && screenB.y >= top && screenB.y <= bottom));
            if (!endpointInside)
            {
                const glm::vec2 mid = (screenA + screenB) * 0.5f;
                if (!(mid.x >= left && mid.x <= right && mid.y >= top && mid.y <= bottom))
                {
                    continue;
                }
            }

            const auto [it, inserted] = m_SelectedBondKeys.insert(key);
            (void)it;
            if (inserted)
            {
                ++addedCount;
            }
        }

        AppendSelectionDebugLog(
            "Bond box selection: additive=" + std::string(additiveSelection ? "1" : "0") +
            " selected=" + std::to_string(m_SelectedBondKeys.size()) +
            " added=" + std::to_string(addedCount));
    }

    void EditorLayer::SelectAtomsInScreenCircle(const glm::vec2 &screenCenter, float screenRadius, bool additiveSelection)
    {
        if (!m_Camera || !m_HasStructureLoaded || m_WorkingStructure.atoms.empty())
        {
            return;
        }

        const float radius = std::max(1.0f, screenRadius);
        const float radiusSq = radius * radius;
        if (!additiveSelection)
        {
            m_SelectedAtomIndices.clear();
        }

        const glm::mat4 viewProjection = m_Camera->GetViewProjectionMatrix();
        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return;
        }

        std::size_t addedCount = 0;
        for (std::size_t i = 0; i < m_WorkingStructure.atoms.size(); ++i)
        {
            glm::vec3 center = m_WorkingStructure.atoms[i].position;
            if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
            {
                center = m_WorkingStructure.DirectToCartesian(center);
            }

            const glm::vec4 clip = viewProjection * glm::vec4(center, 1.0f);
            if (clip.w <= 1e-6f)
            {
                continue;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < -1.0f || ndc.z > 1.0f)
            {
                continue;
            }

            const glm::vec2 screenPos(
                m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);

            if (glm::length2(screenPos - screenCenter) <= radiusSq)
            {
                if (!IsAtomSelected(i))
                {
                    m_SelectedAtomIndices.push_back(i);
                    ++addedCount;
                }
            }
        }

        AppendSelectionDebugLog(
            "Circle atom selection: center=(" + std::to_string(screenCenter.x) + "," + std::to_string(screenCenter.y) + ")" +
            " radius=" + std::to_string(radius) +
            " selected=" + std::to_string(m_SelectedAtomIndices.size()) +
            " added=" + std::to_string(addedCount));
    }

    void EditorLayer::SelectBondsInScreenCircle(const glm::vec2 &screenCenter, float screenRadius, bool additiveSelection)
    {
        if (!m_Camera || m_GeneratedBonds.empty())
        {
            return;
        }

        const float radius = std::max(1.0f, screenRadius);
        const float radiusSq = radius * radius;
        if (!additiveSelection)
        {
            m_SelectedBondKeys.clear();
        }

        const glm::mat4 viewProjection = m_Camera->GetViewProjectionMatrix();
        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return;
        }

        std::size_t addedCount = 0;
        for (const BondSegment &bond : m_GeneratedBonds)
        {
            const std::uint64_t key = MakeBondPairKey(bond.atomA, bond.atomB);
            if (m_DeletedBondKeys.find(key) != m_DeletedBondKeys.end())
            {
                continue;
            }

            const glm::vec4 clip = viewProjection * glm::vec4(bond.midpoint, 1.0f);
            if (clip.w <= 1e-6f)
            {
                continue;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < -1.0f || ndc.z > 1.0f)
            {
                continue;
            }

            const glm::vec2 screenPos(
                m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
            if (glm::length2(screenPos - screenCenter) > radiusSq)
            {
                continue;
            }

            const auto [it, inserted] = m_SelectedBondKeys.insert(key);
            (void)it;
            if (inserted)
            {
                ++addedCount;
            }
        }

        AppendSelectionDebugLog(
            "Circle bond selection: center=(" + std::to_string(screenCenter.x) + "," + std::to_string(screenCenter.y) + ")" +
            " radius=" + std::to_string(radius) +
            " selected=" + std::to_string(m_SelectedBondKeys.size()) +
            " added=" + std::to_string(addedCount));
    }

    void EditorLayer::HandleViewportSelection()
    {
        if (m_TranslateModeActive || m_RotateModeActive)
        {
            return;
        }

        if (m_BlockSelectionThisFrame)
        {
            return;
        }

        if (m_GizmoConsumedMouseThisFrame)
        {
            return;
        }

        if (m_BoxSelectArmed || m_CircleSelectArmed)
        {
            return;
        }

        const ImGuiIO &io = ImGui::GetIO();
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            return;
        }

        const bool gizmoConsumesInput = (m_GizmoEnabled && (ImGuizmo::IsOver() || ImGuizmo::IsUsing())) ||
                                        (m_ViewGuizmoEnabled && (ImViewGuizmo::IsOver() || ImViewGuizmo::IsUsing()));
        if (gizmoConsumesInput)
        {
            AppendSelectionDebugLog("Ignored click: gizmo consumed input");
            return;
        }

        std::ostringstream clickLog;
        clickLog << "LMB click: mode=" << (m_InteractionMode == InteractionMode::Select ? "Select" : "Navigate")
                 << " focused=" << (m_ViewportFocused ? "1" : "0")
                 << " hovered=" << (m_ViewportHovered ? "1" : "0")
                 << " hasStructure=" << (m_HasStructureLoaded ? "1" : "0")
                 << " atomCount=" << m_WorkingStructure.atoms.size()
                 << " mouse=(" << io.MousePos.x << "," << io.MousePos.y << ")"
                 << " viewportMin=(" << m_ViewportRectMin.x << "," << m_ViewportRectMin.y << ")"
                 << " viewportMax=(" << m_ViewportRectMax.x << "," << m_ViewportRectMax.y << ")";
        AppendSelectionDebugLog(clickLog.str());

        if (m_InteractionMode != InteractionMode::Select)
        {
            AppendSelectionDebugLog("Ignored click: not in Select mode");
            return;
        }

        if (!m_ViewportFocused || !m_ViewportHovered)
        {
            AppendSelectionDebugLog("Ignored click: viewport not focused/hovered");
            return;
        }

        if ((!m_HasStructureLoaded || m_WorkingStructure.atoms.empty()) && m_TransformEmpties.empty() && m_GeneratedBonds.empty())
        {
            AppendSelectionDebugLog("Ignored click: no structure/atoms and no empties");
            return;
        }

        const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
        const bool insideViewport =
            mousePos.x >= m_ViewportRectMin.x && mousePos.x <= m_ViewportRectMax.x &&
            mousePos.y >= m_ViewportRectMin.y && mousePos.y <= m_ViewportRectMax.y;
        if (!insideViewport)
        {
            AppendSelectionDebugLog("Ignored click: outside viewport rect");
            return;
        }

        const bool multiSelect = io.KeyCtrl;
        const bool atomsOnlySelection = (m_SelectionFilter == SelectionFilter::AtomsOnly);
        const bool atomsAndBondsSelection = (m_SelectionFilter == SelectionFilter::AtomsAndBonds);
        const bool bondsOnlySelection = (m_SelectionFilter == SelectionFilter::BondsOnly);
        const bool labelsOnlySelection = (m_SelectionFilter == SelectionFilter::BondLabelsOnly);
        const bool allowAtomsSelection = atomsOnlySelection || atomsAndBondsSelection;
        const bool allowBondsSelection = atomsAndBondsSelection || bondsOnlySelection;

        auto projectWorldToScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen, float &outDepth) -> bool
        {
            const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(world, 1.0f);
            if (clip.w <= 1e-6f)
            {
                return false;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f || ndc.z < -1.0f || ndc.z > 1.0f)
            {
                return false;
            }

            const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
            const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
            outScreen = glm::vec2(
                m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);
            outDepth = ndc.z;
            return true;
        };

        auto pickSpecialNode = [&](SpecialNodeSelection &outSelection) -> bool
        {
            outSelection = SpecialNodeSelection::None;
            bool found = false;
            float bestDepth = std::numeric_limits<float>::max();
            float bestDist = std::numeric_limits<float>::max();
            constexpr float pickRadius = 22.0f;

            auto testNode = [&](SpecialNodeSelection selection, const glm::vec3 &position)
            {
                glm::vec2 screen(0.0f);
                float depth = 0.0f;
                if (!projectWorldToScreen(position, screen, depth))
                {
                    return;
                }

                const float dx = mousePos.x - screen.x;
                const float dy = mousePos.y - screen.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > pickRadius)
                {
                    return;
                }

                if (!found || depth < bestDepth - 1e-4f || (std::abs(depth - bestDepth) <= 1e-4f && dist < bestDist))
                {
                    found = true;
                    bestDepth = depth;
                    bestDist = dist;
                    outSelection = selection;
                }
            };

            testNode(SpecialNodeSelection::Light, m_LightPosition);
            return found;
        };

        if (labelsOnlySelection)
        {
            if (!multiSelect)
            {
                m_SelectedAtomIndices.clear();
                m_SelectedTransformEmptyIndex = -1;
                m_SelectedSpecialNode = SpecialNodeSelection::None;
                m_SelectedBondKeys.clear();
                m_SelectedBondLabelKey = 0;
                AppendSelectionDebugLog("Selection cleared: label-only mode (use bond label hitboxes)");
            }
            return;
        }

        if (allowAtomsSelection)
        {
            SpecialNodeSelection pickedSpecial = SpecialNodeSelection::None;
            if (pickSpecialNode(pickedSpecial))
            {
                if (!multiSelect)
                {
                    m_SelectedAtomIndices.clear();
                    m_SelectedTransformEmptyIndex = -1;
                    m_SelectedBondKeys.clear();
                }
                m_SelectedSpecialNode = pickedSpecial;
                AppendSelectionDebugLog("Selection set to special node: Light");
                return;
            }

            std::size_t pickedEmptyIndex = 0;
            const bool hasEmptyHit = PickTransformEmptyAtScreenPoint(mousePos, pickedEmptyIndex);
            if (hasEmptyHit)
            {
                if (!multiSelect)
                {
                    m_SelectedAtomIndices.clear();
                    m_SelectedBondKeys.clear();
                }

                m_SelectedTransformEmptyIndex = static_cast<int>(pickedEmptyIndex);
                m_ActiveTransformEmptyIndex = m_SelectedTransformEmptyIndex;
                m_SelectedSpecialNode = SpecialNodeSelection::None;
                AppendSelectionDebugLog("Selection set to transform empty index=" + std::to_string(pickedEmptyIndex));
                return;
            }
        }

        std::uint64_t pickedBondKey = 0;
        const bool hasBondHit = allowBondsSelection && PickBondAtScreenPoint(mousePos, pickedBondKey);
        if (hasBondHit && bondsOnlySelection)
        {
            if (!multiSelect)
            {
                m_SelectedBondKeys.clear();
                m_SelectedAtomIndices.clear();
                m_SelectedTransformEmptyIndex = -1;
                m_SelectedSpecialNode = SpecialNodeSelection::None;
            }

            if (multiSelect)
            {
                auto selectedIt = m_SelectedBondKeys.find(pickedBondKey);
                if (selectedIt != m_SelectedBondKeys.end())
                {
                    m_SelectedBondKeys.erase(selectedIt);
                }
                else
                {
                    m_SelectedBondKeys.insert(pickedBondKey);
                }
            }
            else
            {
                m_SelectedBondKeys.insert(pickedBondKey);
            }

            m_SelectedBondLabelKey = pickedBondKey;
            AppendSelectionDebugLog("Selection set to bond key=" + std::to_string(pickedBondKey));
            return;
        }

        if (bondsOnlySelection)
        {
            if (!multiSelect)
            {
                m_SelectedBondKeys.clear();
                m_SelectedBondLabelKey = 0;
                AppendSelectionDebugLog("Selection cleared: bond-only mode and no bond hit");
            }
            return;
        }

        std::size_t pickedAtomIndex = 0;
        const bool hasHit = allowAtomsSelection && PickAtomAtScreenPoint(mousePos, pickedAtomIndex);
        {
            std::ostringstream pickLog;
            pickLog << "Pick result: hasHit=" << (hasHit ? "1" : "0")
                    << " pickedIndex=" << pickedAtomIndex
                    << " ctrl=" << (multiSelect ? "1" : "0");
            AppendSelectionDebugLog(pickLog.str());
        }

        if (!hasHit)
        {
            if (hasBondHit && allowBondsSelection)
            {
                if (!multiSelect)
                {
                    m_SelectedBondKeys.clear();
                    m_SelectedAtomIndices.clear();
                    m_SelectedTransformEmptyIndex = -1;
                    m_SelectedSpecialNode = SpecialNodeSelection::None;
                }

                if (multiSelect)
                {
                    auto selectedIt = m_SelectedBondKeys.find(pickedBondKey);
                    if (selectedIt != m_SelectedBondKeys.end())
                    {
                        m_SelectedBondKeys.erase(selectedIt);
                    }
                    else
                    {
                        m_SelectedBondKeys.insert(pickedBondKey);
                    }
                }
                else
                {
                    m_SelectedBondKeys.insert(pickedBondKey);
                }

                m_SelectedBondLabelKey = pickedBondKey;
                AppendSelectionDebugLog("Selection set to bond key=" + std::to_string(pickedBondKey));
                return;
            }

            if (!multiSelect)
            {
                bool preserveSelectionForGizmo = false;
                if (m_GizmoEnabled && !m_SelectedAtomIndices.empty() && m_Camera)
                {
                    glm::vec3 pivot(0.0f);
                    std::size_t validCount = 0;
                    if (m_GizmoOperationIndex == 1)
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
                        if (m_GizmoOperationIndex != 1)
                        {
                            pivot /= static_cast<float>(validCount);
                        }

                        const glm::vec4 clip = m_Camera->GetViewProjectionMatrix() * glm::vec4(pivot, 1.0f);
                        if (clip.w > 0.0001f)
                        {
                            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                            const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
                            const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
                            const glm::vec2 pivotScreen(
                                m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width,
                                m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height);

                            const float distanceToPivot = glm::length(mousePos - pivotScreen);
                            float preserveRadius = 48.0f;
                            if (m_GizmoOperationIndex == 1)
                            {
                                preserveRadius = 140.0f;
                            }
                            else if (m_GizmoOperationIndex == 2)
                            {
                                preserveRadius = 96.0f;
                            }
                            preserveSelectionForGizmo = distanceToPivot <= preserveRadius;
                        }
                    }
                }

                if (preserveSelectionForGizmo)
                {
                    AppendSelectionDebugLog("Selection preserved: click near gizmo pivot");
                    return;
                }

                m_SelectedAtomIndices.clear();
                m_SelectedTransformEmptyIndex = -1;
                m_SelectedSpecialNode = SpecialNodeSelection::None;
                m_SelectedBondKeys.clear();
                m_SelectedBondLabelKey = 0;
                AppendSelectionDebugLog("Selection cleared: no hit and Ctrl not pressed");
            }
            return;
        }

        if (!multiSelect)
        {
            m_SelectedAtomIndices.clear();
            m_SelectedAtomIndices.push_back(pickedAtomIndex);
            m_SelectedBondKeys.clear();
            m_SelectedBondLabelKey = 0;
            m_SelectedTransformEmptyIndex = -1;
            m_SelectedSpecialNode = SpecialNodeSelection::None;
            AppendSelectionDebugLog("Selection set to single atom index=" + std::to_string(pickedAtomIndex));
            return;
        }

        auto it = std::find(m_SelectedAtomIndices.begin(), m_SelectedAtomIndices.end(), pickedAtomIndex);
        if (it == m_SelectedAtomIndices.end())
        {
            m_SelectedAtomIndices.push_back(pickedAtomIndex);
            AppendSelectionDebugLog("Selection added atom index=" + std::to_string(pickedAtomIndex));
        }
        else
        {
            m_SelectedAtomIndices.erase(it);
            AppendSelectionDebugLog("Selection removed atom index=" + std::to_string(pickedAtomIndex));
        }

        AppendSelectionDebugLog("Selection size now=" + std::to_string(m_SelectedAtomIndices.size()));
    }


} // namespace ds
