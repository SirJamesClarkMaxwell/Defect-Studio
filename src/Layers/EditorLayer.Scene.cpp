#include "Layers/EditorLayerPrivate.h"

namespace ds
{
    namespace
    {
        void BuildOrbitBasis(const OrbitCamera &camera, glm::vec3 &outForward, glm::vec3 &outRight, glm::vec3 &outUp)
        {
            outForward = glm::normalize(camera.GetTarget() - camera.GetPosition());
            const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
            outRight = glm::cross(outForward, worldUp);
            if (glm::dot(outRight, outRight) < 1e-8f)
            {
                outRight = glm::cross(outForward, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            outRight = glm::normalize(outRight);
            outUp = glm::normalize(glm::cross(outRight, outForward));

            if (std::abs(camera.GetRoll()) > 1e-6f)
            {
                const glm::quat qRoll = glm::angleAxis(camera.GetRoll(), outForward);
                outRight = glm::normalize(qRoll * outRight);
                outUp = glm::normalize(qRoll * outUp);
            }
        }

        void ComposeOrbitAngles(const glm::vec3 &forward, const glm::vec3 &desiredUp, float &outYaw, float &outPitch, float &outRoll)
        {
            const glm::vec3 safeForward = glm::normalize(forward);
            outYaw = std::atan2(safeForward.x, safeForward.y);
            outPitch = std::asin(glm::clamp(safeForward.z, -1.0f, 1.0f));

            const glm::vec3 worldUpBasis(0.0f, 0.0f, 1.0f);
            glm::vec3 baseRight = glm::cross(safeForward, worldUpBasis);
            if (glm::dot(baseRight, baseRight) < 1e-8f)
            {
                baseRight = glm::cross(safeForward, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            baseRight = glm::normalize(baseRight);
            const glm::vec3 baseUp = glm::normalize(glm::cross(baseRight, safeForward));

            glm::vec3 safeUp = desiredUp - glm::dot(desiredUp, safeForward) * safeForward;
            if (glm::dot(safeUp, safeUp) < 1e-8f)
            {
                safeUp = baseUp;
            }
            safeUp = glm::normalize(safeUp);

            const float sinRoll = glm::dot(glm::cross(baseUp, safeUp), safeForward);
            const float cosRoll = glm::dot(baseUp, safeUp);
            outRoll = std::atan2(sinRoll, cosRoll);

            const float limit = glm::half_pi<float>() - 0.0015f;
            outPitch = glm::clamp(outPitch, -limit, limit);
            outRoll = NormalizeAngleRadians(outRoll);
        }

        glm::vec3 ResolveReciprocalAxis(const Structure &structure, int axisIndex)
        {
            const int axisA = (axisIndex + 1) % 3;
            const int axisB = (axisIndex + 2) % 3;
            glm::vec3 reciprocal = glm::cross(structure.lattice[axisA], structure.lattice[axisB]);
            if (glm::dot(reciprocal, reciprocal) < 1e-8f)
            {
                reciprocal = structure.lattice[axisIndex];
            }
            return reciprocal;
        }
    } // namespace

    bool EditorLayer::IsAtomSelected(std::size_t index) const
    {
        return std::find(m_SelectedAtomIndices.begin(), m_SelectedAtomIndices.end(), index) != m_SelectedAtomIndices.end();
    }

    bool EditorLayer::IsAtomHidden(std::size_t index) const
    {
        return m_HiddenAtomIndices.find(index) != m_HiddenAtomIndices.end();
    }

    int EditorLayer::ResolveAtomCollectionIndex(std::size_t atomIndex) const
    {
        if (atomIndex >= m_AtomCollectionIndices.size())
        {
            return 0;
        }

        const int collectionIndex = m_AtomCollectionIndices[atomIndex];
        if (collectionIndex < 0 || collectionIndex >= static_cast<int>(m_Collections.size()))
        {
            return 0;
        }

        return collectionIndex;
    }

    bool EditorLayer::IsAtomCollectionVisible(std::size_t atomIndex) const
    {
        return IsCollectionVisible(ResolveAtomCollectionIndex(atomIndex));
    }

    bool EditorLayer::IsAtomCollectionSelectable(std::size_t atomIndex) const
    {
        return IsCollectionSelectable(ResolveAtomCollectionIndex(atomIndex));
    }

    void EditorLayer::EnsureAtomCollectionAssignments()
    {
        if (m_AtomCollectionIndices.size() < m_WorkingStructure.atoms.size())
        {
            m_AtomCollectionIndices.resize(m_WorkingStructure.atoms.size(), 0);
        }
        else if (m_AtomCollectionIndices.size() > m_WorkingStructure.atoms.size())
        {
            m_AtomCollectionIndices.resize(m_WorkingStructure.atoms.size());
        }

        if (m_Collections.empty())
        {
            return;
        }

        for (int &collectionIndex : m_AtomCollectionIndices)
        {
            if (collectionIndex < 0 || collectionIndex >= static_cast<int>(m_Collections.size()))
            {
                collectionIndex = 0;
            }
        }
    }

    float EditorLayer::ResolveBondThresholdScaleForPair(const std::string &elementA, const std::string &elementB) const
    {
        const float globalScale = glm::clamp(m_BondThresholdScale, 0.80f, 1.80f);
        if (!m_BondUsePairThresholdOverrides)
        {
            return globalScale;
        }

        const std::string key = BuildElementPairScaleKey(elementA, elementB);
        if (key.empty())
        {
            return globalScale;
        }

        const auto it = m_BondPairThresholdScaleOverrides.find(key);
        if (it == m_BondPairThresholdScaleOverrides.end())
        {
            return globalScale;
        }

        return glm::clamp(it->second, 0.40f, 3.00f);
    }

    void EditorLayer::ToggleInteractionMode()
    {
        if (m_TranslateModeActive)
        {
            bool hasAppliedChange = glm::length2(m_TranslateCurrentOffset) > 1e-10f;
            if (!hasAppliedChange && m_TranslateEmptyIndex >= 0 && m_TranslateEmptyIndex < static_cast<int>(m_TransformEmpties.size()))
            {
                hasAppliedChange = glm::length2(m_TransformEmpties[static_cast<std::size_t>(m_TranslateEmptyIndex)].position - m_TranslateEmptyInitialPosition) > 1e-10f;
            }
            if (!hasAppliedChange && m_TranslateSpecialNode == 1)
            {
                hasAppliedChange = glm::length2(m_LightPosition - m_TranslateEmptyInitialPosition) > 1e-10f;
            }
            if (hasAppliedChange && m_PendingTransformUndoValid)
            {
                PushUndoSnapshot(m_PendingTransformUndoLabel.empty() ? "Translate selection" : m_PendingTransformUndoLabel, m_PendingTransformUndoSnapshot);
            }
            m_TranslateModeActive = false;
            m_TranslateConstraintAxis = -1;
            m_TranslatePlaneLockAxis = -1;
            m_TranslateIndices.clear();
            m_TranslateInitialCartesian.clear();
            m_TranslateCurrentOffset = glm::vec3(0.0f);
            m_PendingTransformUndoValid = false;
            m_PendingTransformUndoDirty = false;
            m_PendingTransformUndoLabel.clear();
            AppendSelectionDebugLog("Translate mode exited by Tab");
        }

        if (m_RotateModeActive)
        {
            if (std::abs(m_RotateCurrentAngle) > 1e-6f && m_PendingTransformUndoValid)
            {
                PushUndoSnapshot(m_PendingTransformUndoLabel.empty() ? "Rotate selection" : m_PendingTransformUndoLabel, m_PendingTransformUndoSnapshot);
            }
            m_RotateModeActive = false;
            m_RotateConstraintAxis = -1;
            m_RotateIndices.clear();
            m_RotateInitialCartesian.clear();
            m_RotateCurrentAngle = 0.0f;
            m_PendingTransformUndoValid = false;
            m_PendingTransformUndoDirty = false;
            m_PendingTransformUndoLabel.clear();
            AppendSelectionDebugLog("Rotate mode exited by Tab");
        }

        if (m_InteractionMode == InteractionMode::Navigate)
        {
            if (m_HasStructureLoaded && !m_WorkingStructure.atoms.empty())
            {
                m_InteractionMode = InteractionMode::Select;
                LogInfo("Interaction mode: Select");
                AppendSelectionDebugLog("Mode switched to Select");
                return;
            }

            m_InteractionMode = InteractionMode::ViewSet;
            LogInfo("Interaction mode: ViewSet");
            AppendSelectionDebugLog("Mode switched to ViewSet (Select unavailable)");
            return;
        }

        if (m_InteractionMode == InteractionMode::Select)
        {
            m_InteractionMode = InteractionMode::ViewSet;
            LogInfo("Interaction mode: ViewSet");
            AppendSelectionDebugLog("Mode switched to ViewSet");
            return;
        }

        m_InteractionMode = InteractionMode::Navigate;
        LogInfo("Interaction mode: Navigate");
        AppendSelectionDebugLog("Mode switched to Navigate");
    }

    bool EditorLayer::RotateCameraRelative(float yawDeltaRadians, float pitchDeltaRadians, float rollDeltaRadians)
    {
        if (!m_Camera)
        {
            return false;
        }

        glm::vec3 forward(0.0f);
        glm::vec3 right(0.0f);
        glm::vec3 up(0.0f);
        BuildOrbitBasis(*m_Camera, forward, right, up);

        if (std::abs(yawDeltaRadians) > 1e-6f)
        {
            const glm::quat qYaw = glm::angleAxis(-yawDeltaRadians, glm::normalize(up));
            forward = glm::normalize(qYaw * forward);
            right = glm::normalize(qYaw * right);
        }

        if (std::abs(pitchDeltaRadians) > 1e-6f)
        {
            const glm::quat qPitch = glm::angleAxis(-pitchDeltaRadians, glm::normalize(right));
            forward = glm::normalize(qPitch * forward);
            up = glm::normalize(qPitch * up);
        }
        else
        {
            up = glm::normalize(glm::cross(right, forward));
        }

        if (std::abs(rollDeltaRadians) > 1e-6f)
        {
            const glm::quat qRoll = glm::angleAxis(rollDeltaRadians, glm::normalize(forward));
            up = glm::normalize(qRoll * up);
        }

        float yaw = 0.0f;
        float pitch = 0.0f;
        float roll = 0.0f;
        ComposeOrbitAngles(forward, up, yaw, pitch, roll);
        StartCameraOrbitTransition(m_Camera->GetTarget(), m_Camera->GetDistance(), yaw, pitch, roll);
        m_LastStructureOperationFailed = false;
        return true;
    }

    bool EditorLayer::PanCameraRelativePixels(float deltaXPixels, float deltaYPixels)
    {
        if (!m_Camera)
        {
            return false;
        }

        glm::vec3 forward(0.0f);
        glm::vec3 right(0.0f);
        glm::vec3 up(0.0f);
        BuildOrbitBasis(*m_Camera, forward, right, up);

        const float panSpeed = 0.006f * m_Camera->GetDistance() * m_CameraPanSensitivity;
        const glm::vec3 newTarget = m_Camera->GetTarget() + (-right * deltaXPixels + up * deltaYPixels) * panSpeed;
        StartCameraOrbitTransition(newTarget, m_Camera->GetDistance(), m_Camera->GetYaw(), m_Camera->GetPitch(), m_Camera->GetRoll());
        m_LastStructureOperationFailed = false;
        return true;
    }

    bool EditorLayer::ZoomCameraRelativePercent(float zoomPercentDelta)
    {
        if (!m_Camera)
        {
            return false;
        }

        const float clampedPercent = glm::clamp(zoomPercentDelta, -95.0f, 95.0f);
        if (m_Camera->GetProjectionMode() == OrbitCamera::ProjectionMode::Perspective)
        {
            const float zoomScale = glm::clamp(1.0f - clampedPercent * 0.01f, 0.05f, 4.0f);
            const float newDistance = glm::clamp(m_Camera->GetDistance() * zoomScale, 0.10f, 250.0f);
            StartCameraOrbitTransition(m_Camera->GetTarget(), newDistance, m_Camera->GetYaw(), m_Camera->GetPitch(), m_Camera->GetRoll());
        }
        else
        {
            const float zoomScale = glm::clamp(1.0f - clampedPercent * 0.01f, 0.05f, 4.0f);
            m_Camera->SetOrthographicSize(glm::clamp(m_Camera->GetOrthographicSize() * zoomScale, 0.05f, 500.0f));
        }

        m_LastStructureOperationFailed = false;
        return true;
    }

    bool EditorLayer::SetCameraViewToCrystalAxis(int axisIndex, bool reciprocalAxis, bool invertDirection)
    {
        if (!m_Camera || axisIndex < 0 || axisIndex > 2)
        {
            return false;
        }

        const Structure &structure = m_HasStructureLoaded ? m_WorkingStructure : Structure{};
        glm::vec3 axis = reciprocalAxis ? ResolveReciprocalAxis(structure, axisIndex) : structure.lattice[axisIndex];
        if (glm::dot(axis, axis) < 1e-8f)
        {
            axis = (axisIndex == 0) ? glm::vec3(1.0f, 0.0f, 0.0f) :
                   (axisIndex == 1) ? glm::vec3(0.0f, 1.0f, 0.0f) :
                                      glm::vec3(0.0f, 0.0f, 1.0f);
        }

        axis = glm::normalize(invertDirection ? -axis : axis);
        glm::vec3 upHint = (axisIndex == 2) ? structure.lattice[1] : structure.lattice[2];
        if (glm::dot(upHint, upHint) < 1e-8f || std::abs(glm::dot(glm::normalize(upHint), axis)) > 0.98f)
        {
            upHint = (axisIndex == 0) ? structure.lattice[1] : structure.lattice[0];
        }
        if (glm::dot(upHint, upHint) < 1e-8f || std::abs(glm::dot(glm::normalize(upHint), axis)) > 0.98f)
        {
            upHint = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        float yaw = 0.0f;
        float pitch = 0.0f;
        float roll = 0.0f;
        ComposeOrbitAngles(axis, upHint, yaw, pitch, roll);
        StartCameraOrbitTransition(m_Camera->GetTarget(), m_Camera->GetDistance(), yaw, pitch, roll);
        m_LastStructureOperationFailed = false;
        return true;
    }

    std::string EditorLayer::DescribeVolumetricStructureMatch(const Structure &datasetStructure, bool &outMatches) const
    {
        outMatches = false;
        if (!m_HasStructureLoaded)
        {
            return "No active scene structure.";
        }
        if (datasetStructure.GetAtomCount() == 0)
        {
            return "Dataset header has no atoms.";
        }
        if (datasetStructure.GetAtomCount() != m_WorkingStructure.GetAtomCount())
        {
            return "Atom count differs.";
        }
        if (datasetStructure.coordinateMode != m_WorkingStructure.coordinateMode)
        {
            return "Coordinate mode differs.";
        }
        if (datasetStructure.species != m_WorkingStructure.species || datasetStructure.counts != m_WorkingStructure.counts)
        {
            return "Species order or counts differ.";
        }

        constexpr float kLatticeTolerance = 1e-4f;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (glm::length(datasetStructure.lattice[axis] - m_WorkingStructure.lattice[axis]) > kLatticeTolerance)
            {
                return "Lattice vectors differ.";
            }
        }

        constexpr float kAtomPositionTolerance = 1e-3f;
        for (std::size_t atomIndex = 0; atomIndex < datasetStructure.atoms.size(); ++atomIndex)
        {
            if (datasetStructure.atoms[atomIndex].element != m_WorkingStructure.atoms[atomIndex].element)
            {
                return "Atom element order differs.";
            }

            const glm::vec3 datasetPosition =
                (datasetStructure.coordinateMode == CoordinateMode::Direct)
                    ? datasetStructure.DirectToCartesian(datasetStructure.atoms[atomIndex].position)
                    : datasetStructure.atoms[atomIndex].position;
            const glm::vec3 scenePosition =
                (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
                    ? m_WorkingStructure.DirectToCartesian(m_WorkingStructure.atoms[atomIndex].position)
                    : m_WorkingStructure.atoms[atomIndex].position;
            if (glm::length(datasetPosition - scenePosition) > kAtomPositionTolerance)
            {
                return "Atomic positions differ.";
            }
        }

        outMatches = true;
        return "Matches lattice, species, and atom positions.";
    }

    void EditorLayer::StartCameraOrbitTransition(const glm::vec3 &target, float distance, float yaw, float pitch, std::optional<float> roll)
    {
        if (!m_Camera)
        {
            return;
        }

        m_CameraTransitionActive = true;
        m_CameraTransitionElapsed = 0.0f;

        m_CameraTransitionStartTarget = m_Camera->GetTarget();
        m_CameraTransitionEndTarget = target;

        m_CameraTransitionStartDistance = m_Camera->GetDistance();
        m_CameraTransitionEndDistance = distance;

        m_CameraTransitionStartYaw = m_Camera->GetYaw();
        m_CameraTransitionEndYaw = yaw;

        m_CameraTransitionStartPitch = m_Camera->GetPitch();
        m_CameraTransitionEndPitch = pitch;

        const float currentRoll = m_Camera->GetRoll();
        m_CameraTransitionStartRoll = currentRoll;
        m_CameraTransitionEndRoll = roll.value_or(currentRoll);
    }

    void EditorLayer::UpdateCameraOrbitTransition(float deltaTime)
    {
        if (!m_CameraTransitionActive || !m_Camera)
        {
            return;
        }

        m_CameraTransitionElapsed += std::max(0.0f, deltaTime);
        const float duration = std::max(0.01f, m_CameraTransitionDuration);
        const float alpha = glm::clamp(m_CameraTransitionElapsed / duration, 0.0f, 1.0f);
        const float t = EaseOutCubic(alpha);

        const glm::vec3 target = glm::mix(m_CameraTransitionStartTarget, m_CameraTransitionEndTarget, t);
        const float distance = glm::mix(m_CameraTransitionStartDistance, m_CameraTransitionEndDistance, t);
        const float yaw = LerpAngleRadians(m_CameraTransitionStartYaw, m_CameraTransitionEndYaw, t);
        const float pitch = LerpAngleRadians(m_CameraTransitionStartPitch, m_CameraTransitionEndPitch, t);
        const float roll = LerpAngleRadians(m_CameraTransitionStartRoll, m_CameraTransitionEndRoll, t);

        m_Camera->SetOrbitState(target, distance, yaw, pitch);
        m_Camera->SetRoll(roll);

        if (alpha >= 1.0f)
        {
            m_CameraTransitionActive = false;
        }
    }

    void EditorLayer::AppendSelectionDebugLog(const std::string &message) const
    {
        if (!m_SelectionDebugToFile)
        {
            return;
        }

        std::filesystem::create_directories("logs");
        std::ofstream out(kSelectionDebugLogPath, std::ios::app);
        if (!out.is_open())
        {
            return;
        }

        out << '[' << BuildDebugTimestampNow() << "] " << message << '\n';
    }

    bool EditorLayer::PickAtomAtScreenPoint(const glm::vec2 &mousePos, std::size_t &outAtomIndex) const
    {
        if (!m_Camera || !m_HasStructureLoaded || m_WorkingStructure.atoms.empty())
        {
            return false;
        }

        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return false;
        }

        const glm::mat4 viewProjection = m_Camera->GetViewProjectionMatrix();
        bool found = false;
        float bestDepth = std::numeric_limits<float>::max();
        float bestDistancePixels = std::numeric_limits<float>::max();
        const float pickRadiusPixels = 14.0f + m_SceneSettings.atomScale * 20.0f;
        const float pickRadiusPixelsSq = pickRadiusPixels * pickRadiusPixels;

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
            if (ndc.x < -1.05f || ndc.x > 1.05f || ndc.y < -1.05f || ndc.y > 1.05f || ndc.z < -1.0f || ndc.z > 1.0f)
            {
                continue;
            }

            const float screenX = m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width;
            const float screenY = m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height;
            const float dx = mousePos.x - screenX;
            const float dy = mousePos.y - screenY;
            const float distSq = dx * dx + dy * dy;
            if (distSq > pickRadiusPixelsSq)
            {
                continue;
            }

            const float distPixels = std::sqrt(distSq);
            if (!found || ndc.z < bestDepth - 1e-4f || (std::abs(ndc.z - bestDepth) <= 1e-4f && distPixels < bestDistancePixels))
            {
                bestDepth = ndc.z;
                bestDistancePixels = distPixels;
                outAtomIndex = i;
                found = true;
            }
        }

        return found;
    }

    bool EditorLayer::PickBondAtScreenPoint(const glm::vec2 &mousePos, std::uint64_t &outBondKey) const
    {
        if (!m_Camera || m_GeneratedBonds.empty())
        {
            return false;
        }

        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return false;
        }

        const glm::mat4 viewProjection = m_Camera->GetViewProjectionMatrix();
        bool found = false;
        float bestDepth = std::numeric_limits<float>::max();
        float bestDistanceSq = std::numeric_limits<float>::max();
        const float pickRadius = 8.0f;
        const float pickRadiusSq = pickRadius * pickRadius;

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

            const glm::vec2 seg = screenB - screenA;
            const float segLenSq = glm::dot(seg, seg);
            if (segLenSq < 1e-6f)
            {
                continue;
            }

            const float t = glm::clamp(glm::dot(mousePos - screenA, seg) / segLenSq, 0.0f, 1.0f);
            const glm::vec2 closest = screenA + seg * t;
            const float distSq = glm::length2(mousePos - closest);
            if (distSq > pickRadiusSq)
            {
                continue;
            }

            const float depth = glm::mix(ndcA.z, ndcB.z, t);
            if (!found || depth < bestDepth - 1e-4f || (std::abs(depth - bestDepth) <= 1e-4f && distSq < bestDistanceSq))
            {
                bestDepth = depth;
                bestDistanceSq = distSq;
                outBondKey = key;
                found = true;
            }
        }

        return found;
    }

    bool EditorLayer::PickTransformEmptyAtScreenPoint(const glm::vec2 &mousePos, std::size_t &outEmptyIndex) const
    {
        if (!m_Camera || m_TransformEmpties.empty())
        {
            return false;
        }

        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return false;
        }

        const glm::mat4 viewProjection = m_Camera->GetViewProjectionMatrix();
        bool found = false;
        float bestDepth = std::numeric_limits<float>::max();
        float bestDistancePixels = std::numeric_limits<float>::max();
        const float pickRadiusPixels = glm::clamp(10.0f + 22.0f * m_TransformEmptyVisualScale, 10.0f, 22.0f);
        const float pickRadiusPixelsSq = pickRadiusPixels * pickRadiusPixels;

        for (std::size_t i = 0; i < m_TransformEmpties.size(); ++i)
        {
            const glm::vec3 center = m_TransformEmpties[i].position;
            if (!m_TransformEmpties[i].visible || !m_TransformEmpties[i].selectable)
            {
                continue;
            }
            if (!IsCollectionVisible(m_TransformEmpties[i].collectionIndex) || !IsCollectionSelectable(m_TransformEmpties[i].collectionIndex))
            {
                continue;
            }
            const glm::vec4 clip = viewProjection * glm::vec4(center, 1.0f);
            if (clip.w <= 1e-6f)
            {
                continue;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f || ndc.z < -1.0f || ndc.z > 1.0f)
            {
                continue;
            }

            const float screenX = m_ViewportRectMin.x + (ndc.x * 0.5f + 0.5f) * width;
            const float screenY = m_ViewportRectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * height;
            const float dx = mousePos.x - screenX;
            const float dy = mousePos.y - screenY;
            const float distSq = dx * dx + dy * dy;
            if (distSq > pickRadiusPixelsSq)
            {
                continue;
            }

            const float distPixels = std::sqrt(distSq);
            if (!found || ndc.z < bestDepth - 1e-4f || (std::abs(ndc.z - bestDepth) <= 1e-4f && distPixels < bestDistancePixels))
            {
                bestDepth = ndc.z;
                bestDistancePixels = distPixels;
                outEmptyIndex = i;
                found = true;
            }
        }

        return found;
    }

    glm::vec3 EditorLayer::GetAtomCartesianPosition(std::size_t atomIndex) const
    {
        if (atomIndex >= m_WorkingStructure.atoms.size())
        {
            return glm::vec3(0.0f);
        }

        glm::vec3 position = m_WorkingStructure.atoms[atomIndex].position;
        if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
        {
            position = m_WorkingStructure.DirectToCartesian(position);
        }
        return position;
    }

    void EditorLayer::SetAtomCartesianPosition(std::size_t atomIndex, const glm::vec3 &position)
    {
        if (atomIndex >= m_WorkingStructure.atoms.size())
        {
            return;
        }

        if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
        {
            m_WorkingStructure.atoms[atomIndex].position = m_WorkingStructure.CartesianToDirect(position);
            m_AutoBondsDirty = true;
            return;
        }

        m_WorkingStructure.atoms[atomIndex].position = position;
        m_AutoBondsDirty = true;
    }

    glm::vec3 EditorLayer::ComputeSelectionCenter() const
    {
        if (m_SelectedAtomIndices.empty())
        {
            return glm::vec3(0.0f);
        }

        glm::vec3 center(0.0f);
        std::size_t validCount = 0;
        for (std::size_t atomIndex : m_SelectedAtomIndices)
        {
            if (atomIndex >= m_WorkingStructure.atoms.size())
            {
                continue;
            }

            center += GetAtomCartesianPosition(atomIndex);
            ++validCount;
        }

        if (validCount == 0)
        {
            return glm::vec3(0.0f);
        }

        return center / static_cast<float>(validCount);
    }

    bool EditorLayer::ComputeSelectionAxesAround(const glm::vec3 &pivot, std::array<glm::vec3, 3> &outAxes) const
    {
        if (m_SelectedAtomIndices.empty())
        {
            return false;
        }

        std::vector<glm::vec3> points;
        points.reserve(m_SelectedAtomIndices.size());
        for (std::size_t atomIndex : m_SelectedAtomIndices)
        {
            if (atomIndex < m_WorkingStructure.atoms.size())
            {
                points.push_back(GetAtomCartesianPosition(atomIndex));
            }
        }

        if (points.empty())
        {
            return false;
        }

        return BuildAxesFromPoints(points, pivot, outAxes);
    }

    bool EditorLayer::HasActiveTransformEmpty() const
    {
        return m_ActiveTransformEmptyIndex >= 0 &&
               m_ActiveTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size());
    }

    bool EditorLayer::IsCollectionVisible(int collectionIndex) const
    {
        if (collectionIndex < 0 || collectionIndex >= static_cast<int>(m_Collections.size()))
        {
            return true;
        }
        return m_Collections[static_cast<std::size_t>(collectionIndex)].visible;
    }

    bool EditorLayer::IsCollectionSelectable(int collectionIndex) const
    {
        if (collectionIndex < 0 || collectionIndex >= static_cast<int>(m_Collections.size()))
        {
            return true;
        }
        return m_Collections[static_cast<std::size_t>(collectionIndex)].selectable;
    }

    void EditorLayer::EnsureAtomNodeIds()
    {
        if (!m_HasStructureLoaded)
        {
            return;
        }

        const std::size_t atomCount = m_WorkingStructure.atoms.size();
        if (m_AtomNodeIds.size() < atomCount)
        {
            const std::size_t oldSize = m_AtomNodeIds.size();
            m_AtomNodeIds.resize(atomCount, 0);
            for (std::size_t i = oldSize; i < atomCount; ++i)
            {
                m_AtomNodeIds[i] = GenerateSceneUUID();
            }
        }
        else if (m_AtomNodeIds.size() > atomCount)
        {
            m_AtomNodeIds.resize(atomCount);
        }

        for (SceneUUID &id : m_AtomNodeIds)
        {
            if (id == 0)
            {
                id = GenerateSceneUUID();
            }
        }
    }

    void EditorLayer::EnsureSceneDefaults()
    {
        EnsureAtomNodeIds();

        if (m_Collections.empty())
        {
            SceneCollection rootCollection;
            rootCollection.id = GenerateSceneUUID();
            rootCollection.name = "Scene Collection";
            rootCollection.visible = true;
            rootCollection.selectable = true;
            m_Collections.push_back(rootCollection);
        }

        for (SceneCollection &collection : m_Collections)
        {
            if (collection.id == 0)
            {
                collection.id = GenerateSceneUUID();
            }
        }

        if (m_ActiveCollectionIndex < 0 || m_ActiveCollectionIndex >= static_cast<int>(m_Collections.size()))
        {
            m_ActiveCollectionIndex = 0;
        }

        std::unordered_set<SceneUUID> usedEmptyIds;

        auto findCollectionIndexById = [&](SceneUUID collectionId) -> int
        {
            if (collectionId == 0)
            {
                return -1;
            }

            for (std::size_t i = 0; i < m_Collections.size(); ++i)
            {
                if (m_Collections[i].id == collectionId)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };

        auto hasEmptyId = [&](SceneUUID emptyId) -> bool
        {
            if (emptyId == 0)
            {
                return false;
            }
            for (const TransformEmpty &empty : m_TransformEmpties)
            {
                if (empty.id == emptyId)
                {
                    return true;
                }
            }
            return false;
        };

        auto findEmptyIndexById = [&](SceneUUID emptyId) -> int
        {
            if (emptyId == 0)
            {
                return -1;
            }

            for (std::size_t i = 0; i < m_TransformEmpties.size(); ++i)
            {
                if (m_TransformEmpties[i].id == emptyId)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };

        for (TransformEmpty &empty : m_TransformEmpties)
        {
            if (empty.id == 0 || usedEmptyIds.find(empty.id) != usedEmptyIds.end())
            {
                if (empty.id != 0)
                {
                    LogWarn("EnsureSceneDefaults: duplicate empty UUID detected, assigning a new UUID.");
                }
                empty.id = GenerateSceneUUID();
            }
            usedEmptyIds.insert(empty.id);

            if (empty.collectionId != 0)
            {
                const int resolvedIndex = findCollectionIndexById(empty.collectionId);
                if (resolvedIndex >= 0)
                {
                    empty.collectionIndex = resolvedIndex;
                }
            }

            if (empty.collectionIndex < 0 || empty.collectionIndex >= static_cast<int>(m_Collections.size()))
            {
                empty.collectionIndex = 0;
            }

            empty.collectionId = m_Collections[static_cast<std::size_t>(empty.collectionIndex)].id;

            if (empty.parentEmptyId == empty.id || !hasEmptyId(empty.parentEmptyId))
            {
                empty.parentEmptyId = 0;
            }
        }

        for (std::size_t i = 0; i < m_TransformEmpties.size(); ++i)
        {
            TransformEmpty &empty = m_TransformEmpties[i];
            std::unordered_set<SceneUUID> chain;
            chain.insert(empty.id);

            SceneUUID parentId = empty.parentEmptyId;
            bool hasCycle = false;
            std::size_t guard = 0;

            while (parentId != 0 && guard <= m_TransformEmpties.size())
            {
                if (chain.find(parentId) != chain.end())
                {
                    hasCycle = true;
                    break;
                }

                chain.insert(parentId);
                const int parentIndex = findEmptyIndexById(parentId);
                if (parentIndex < 0)
                {
                    break;
                }

                parentId = m_TransformEmpties[static_cast<std::size_t>(parentIndex)].parentEmptyId;
                ++guard;
            }

            if (hasCycle || guard > m_TransformEmpties.size())
            {
                LogWarn("EnsureSceneDefaults: hierarchy cycle detected for empty '" + empty.name + "'. Parent cleared.");
                empty.parentEmptyId = 0;
            }
        }

        for (SceneGroup &group : m_ObjectGroups)
        {
            if (group.id == 0)
            {
                group.id = GenerateSceneUUID();
            }
        }

        EnsureAtomCollectionAssignments();
    }

    void EditorLayer::DeleteTransformEmptyAtIndex(int emptyIndex)
    {
        if (emptyIndex < 0 || emptyIndex >= static_cast<int>(m_TransformEmpties.size()))
        {
            return;
        }

        const SceneUUID deletedEmptyId = m_TransformEmpties[static_cast<std::size_t>(emptyIndex)].id;

        for (TransformEmpty &empty : m_TransformEmpties)
        {
            if (empty.parentEmptyId == deletedEmptyId)
            {
                empty.parentEmptyId = 0;
            }
        }

        for (SceneGroup &group : m_ObjectGroups)
        {
            group.emptyIds.erase(
                std::remove(group.emptyIds.begin(), group.emptyIds.end(), deletedEmptyId),
                group.emptyIds.end());

            for (std::size_t i = 0; i < group.emptyIndices.size();)
            {
                if (group.emptyIndices[i] == emptyIndex)
                {
                    group.emptyIndices.erase(group.emptyIndices.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
                if (group.emptyIndices[i] > emptyIndex)
                {
                    group.emptyIndices[i] -= 1;
                }
                ++i;
            }
        }

        m_TransformEmpties.erase(m_TransformEmpties.begin() + emptyIndex);

        if (m_TransformEmpties.empty())
        {
            m_ActiveTransformEmptyIndex = -1;
            m_SelectedTransformEmptyIndex = -1;
            return;
        }

        if (m_ActiveTransformEmptyIndex == emptyIndex)
        {
            m_ActiveTransformEmptyIndex = std::min(emptyIndex, static_cast<int>(m_TransformEmpties.size()) - 1);
        }
        else if (m_ActiveTransformEmptyIndex > emptyIndex)
        {
            m_ActiveTransformEmptyIndex -= 1;
        }

        if (m_SelectedTransformEmptyIndex == emptyIndex)
        {
            m_SelectedTransformEmptyIndex = -1;
        }
        else if (m_SelectedTransformEmptyIndex > emptyIndex)
        {
            m_SelectedTransformEmptyIndex -= 1;
        }
    }

    bool EditorLayer::HasClipboardPayload() const
    {
        if (m_EditorClipboard.kind == ClipboardPayloadKind::None)
        {
            return false;
        }

        return !m_EditorClipboard.atoms.empty() || !m_EditorClipboard.empties.empty();
    }

    void EditorLayer::BeginCollectionRename(int collectionIndex)
    {
        if (collectionIndex < 0 || collectionIndex >= static_cast<int>(m_Collections.size()))
        {
            return;
        }

        m_ActiveCollectionIndex = collectionIndex;
        m_RenameCollectionTargetIndex = collectionIndex;
        const std::string &activeName = m_Collections[static_cast<std::size_t>(collectionIndex)].name;
        std::snprintf(m_RenameCollectionBuffer.data(), m_RenameCollectionBuffer.size(), "%s", activeName.c_str());
        m_RenameCollectionDialogOpen = true;
    }

    bool EditorLayer::CopyCurrentSelectionToClipboard()
    {
        const bool hasSelectedEmpty =
            (m_SelectedTransformEmptyIndex >= 0 &&
             m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size()));
        if ((!m_HasStructureLoaded || m_SelectedAtomIndices.empty()) && !hasSelectedEmpty)
        {
            return false;
        }

        m_EditorClipboard = EditorClipboard{};
        m_EditorClipboard.kind = ClipboardPayloadKind::Selection;
        m_EditorClipboard.label = "Selection";

        glm::vec3 pivot(0.0f);
        std::size_t pivotCount = 0;

        if (m_HasStructureLoaded)
        {
            for (std::size_t atomIndex : m_SelectedAtomIndices)
            {
                if (atomIndex >= m_WorkingStructure.atoms.size())
                {
                    continue;
                }

                ClipboardAtom atom;
                atom.element = NormalizeElementSymbol(m_WorkingStructure.atoms[atomIndex].element);
                atom.positionCartesian = GetAtomCartesianPosition(atomIndex);
                atom.collectionIndex = ResolveAtomCollectionIndex(atomIndex);
                if (atomIndex < m_AtomNodeIds.size())
                {
                    atom.sourceAtomId = m_AtomNodeIds[atomIndex];
                    const auto overrideIt = m_AtomColorOverrides.find(atom.sourceAtomId);
                    if (overrideIt != m_AtomColorOverrides.end())
                    {
                        atom.hasCustomColorOverride = true;
                        atom.customColorOverride = overrideIt->second;
                    }
                }

                pivot += atom.positionCartesian;
                ++pivotCount;
                m_EditorClipboard.sourceCollectionIndex = atom.collectionIndex;
                m_EditorClipboard.atoms.push_back(atom);
            }
        }

        if (hasSelectedEmpty)
        {
            const TransformEmpty &sourceEmpty = m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)];
            ClipboardEmpty empty;
            empty.sourceId = sourceEmpty.id;
            empty.empty = sourceEmpty;
            m_EditorClipboard.empties.push_back(empty);
            pivot += sourceEmpty.position;
            ++pivotCount;
            m_EditorClipboard.sourceCollectionIndex = sourceEmpty.collectionIndex;
        }

        if (pivotCount > 0)
        {
            m_EditorClipboard.sourcePivot = pivot / static_cast<float>(pivotCount);
        }
        else
        {
            m_EditorClipboard.sourcePivot = m_CursorPosition;
        }

        const bool hasPayload = HasClipboardPayload();
        if (hasPayload)
        {
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Copied current selection to editor clipboard.";
            LogInfo(m_LastStructureMessage);
        }
        return hasPayload;
    }

    bool EditorLayer::CopyCollectionToClipboard(int collectionIndex)
    {
        if (collectionIndex < 0 || collectionIndex >= static_cast<int>(m_Collections.size()))
        {
            return false;
        }

        m_EditorClipboard = EditorClipboard{};
        m_EditorClipboard.kind = ClipboardPayloadKind::Collection;
        m_EditorClipboard.sourceCollectionIndex = collectionIndex;
        m_EditorClipboard.label = m_Collections[static_cast<std::size_t>(collectionIndex)].name;

        glm::vec3 pivot(0.0f);
        std::size_t pivotCount = 0;

        if (m_HasStructureLoaded)
        {
            for (std::size_t atomIndex = 0; atomIndex < m_WorkingStructure.atoms.size(); ++atomIndex)
            {
                if (ResolveAtomCollectionIndex(atomIndex) != collectionIndex)
                {
                    continue;
                }

                ClipboardAtom atom;
                atom.element = NormalizeElementSymbol(m_WorkingStructure.atoms[atomIndex].element);
                atom.positionCartesian = GetAtomCartesianPosition(atomIndex);
                atom.collectionIndex = collectionIndex;
                if (atomIndex < m_AtomNodeIds.size())
                {
                    atom.sourceAtomId = m_AtomNodeIds[atomIndex];
                    const auto overrideIt = m_AtomColorOverrides.find(atom.sourceAtomId);
                    if (overrideIt != m_AtomColorOverrides.end())
                    {
                        atom.hasCustomColorOverride = true;
                        atom.customColorOverride = overrideIt->second;
                    }
                }

                pivot += atom.positionCartesian;
                ++pivotCount;
                m_EditorClipboard.atoms.push_back(atom);
            }
        }

        for (const TransformEmpty &sourceEmpty : m_TransformEmpties)
        {
            if (sourceEmpty.collectionIndex != collectionIndex)
            {
                continue;
            }

            ClipboardEmpty empty;
            empty.sourceId = sourceEmpty.id;
            empty.empty = sourceEmpty;
            pivot += sourceEmpty.position;
            ++pivotCount;
            m_EditorClipboard.empties.push_back(empty);
        }

        if (pivotCount > 0)
        {
            m_EditorClipboard.sourcePivot = pivot / static_cast<float>(pivotCount);
        }

        const bool hasPayload = HasClipboardPayload();
        if (hasPayload)
        {
            m_LastStructureOperationFailed = false;
            m_LastStructureMessage = "Copied collection to editor clipboard: " + m_EditorClipboard.label;
            LogInfo(m_LastStructureMessage);
        }
        return hasPayload;
    }

    bool EditorLayer::PasteClipboard(bool applyPlacementOffset)
    {
        if (!HasClipboardPayload())
        {
            return false;
        }

        const bool clipboardHasAtoms = !m_EditorClipboard.atoms.empty();
        if (clipboardHasAtoms && !m_HasStructureLoaded)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Paste failed: atom clipboard payload requires a loaded structure.";
            LogWarn(m_LastStructureMessage);
            return false;
        }

        auto computeSceneAwareOffset = [&]() -> glm::vec3
        {
            float baseOffset = 0.35f;
            if (m_HasStructureLoaded && !m_WorkingStructure.atoms.empty())
            {
                glm::vec3 boundsMin(std::numeric_limits<float>::max());
                glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
                for (const Atom &atom : m_WorkingStructure.atoms)
                {
                    glm::vec3 position = atom.position;
                    if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
                    {
                        position = m_WorkingStructure.DirectToCartesian(position);
                    }

                    boundsMin = glm::min(boundsMin, position);
                    boundsMax = glm::max(boundsMax, position);
                }

                const float diagonal = glm::length(boundsMax - boundsMin);
                baseOffset = glm::clamp(diagonal * 0.06f, 0.20f, 1.40f);
            }

            return glm::vec3(baseOffset, baseOffset * 0.30f, 0.0f);
        };

        auto makeUniqueCollectionName = [&](const std::string &baseName) -> std::string
        {
            std::string candidate = baseName.empty() ? "Collection Copy" : baseName;
            auto nameExists = [&](const std::string &name)
            {
                return std::any_of(m_Collections.begin(), m_Collections.end(), [&](const SceneCollection &collection)
                                   { return collection.name == name; });
            };

            if (!nameExists(candidate))
            {
                return candidate;
            }

            int suffix = 2;
            while (true)
            {
                const std::string numbered = candidate + " " + std::to_string(suffix);
                if (!nameExists(numbered))
                {
                    return numbered;
                }
                ++suffix;
            }
        };

        auto resolveTargetCollectionIndex = [&](int preferredCollectionIndex) -> int
        {
            if (preferredCollectionIndex >= 0 && preferredCollectionIndex < static_cast<int>(m_Collections.size()))
            {
                return preferredCollectionIndex;
            }
            if (m_ActiveCollectionIndex >= 0 && m_ActiveCollectionIndex < static_cast<int>(m_Collections.size()))
            {
                return m_ActiveCollectionIndex;
            }
            return 0;
        };

        const glm::vec3 pasteOffset = applyPlacementOffset ? computeSceneAwareOffset() : glm::vec3(0.0f);
        std::vector<std::size_t> pastedAtomIndices;
        int pastedEmptyIndex = -1;
        std::unordered_map<SceneUUID, SceneUUID> emptyIdRemap;
        std::vector<std::size_t> newlyAddedEmptyIndices;

        PushUndoSnapshot(m_EditorClipboard.kind == ClipboardPayloadKind::Collection ? "Paste collection" : "Paste selection");

        int destinationCollectionIndex = resolveTargetCollectionIndex(m_EditorClipboard.sourceCollectionIndex);
        if (m_EditorClipboard.kind == ClipboardPayloadKind::Collection)
        {
            SceneCollection newCollection = m_Collections[static_cast<std::size_t>(destinationCollectionIndex)];
            newCollection.id = GenerateSceneUUID();
            newCollection.name = makeUniqueCollectionName((m_EditorClipboard.label.empty() ? "Collection" : m_EditorClipboard.label) + " Copy");
            destinationCollectionIndex = std::clamp(m_EditorClipboard.sourceCollectionIndex + 1, 0, static_cast<int>(m_Collections.size()));
            m_Collections.insert(m_Collections.begin() + destinationCollectionIndex, newCollection);

            for (int &collectionIndex : m_AtomCollectionIndices)
            {
                if (collectionIndex >= destinationCollectionIndex)
                {
                    ++collectionIndex;
                }
            }
            for (TransformEmpty &empty : m_TransformEmpties)
            {
                if (empty.collectionIndex >= destinationCollectionIndex)
                {
                    ++empty.collectionIndex;
                }
            }

            m_ActiveCollectionIndex = destinationCollectionIndex;
            m_SelectedCollectionIndices.clear();
            m_SelectedCollectionIndices.insert(destinationCollectionIndex);
            m_OutlinerCollectionSelectionAnchor = static_cast<std::size_t>(destinationCollectionIndex);
        }

        if (clipboardHasAtoms)
        {
            for (const ClipboardAtom &copiedAtom : m_EditorClipboard.atoms)
            {
                Atom atom;
                atom.element = copiedAtom.element;
                atom.selectiveDynamics = false;
                atom.selectiveFlags = {true, true, true};

                const glm::vec3 pastedPosition = copiedAtom.positionCartesian + pasteOffset;
                atom.position = (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
                                    ? m_WorkingStructure.CartesianToDirect(pastedPosition)
                                    : pastedPosition;

                m_WorkingStructure.atoms.push_back(atom);
                const SceneUUID newAtomId = GenerateSceneUUID();
                m_AtomNodeIds.push_back(newAtomId);
                const int atomCollectionIndex = (m_EditorClipboard.kind == ClipboardPayloadKind::Collection)
                                                    ? destinationCollectionIndex
                                                    : resolveTargetCollectionIndex(copiedAtom.collectionIndex);
                m_AtomCollectionIndices.push_back(atomCollectionIndex);
                pastedAtomIndices.push_back(m_WorkingStructure.atoms.size() - 1);

                if (copiedAtom.hasCustomColorOverride)
                {
                    m_AtomColorOverrides[newAtomId] = copiedAtom.customColorOverride;
                }
            }
        }

        for (const ClipboardEmpty &copiedEmpty : m_EditorClipboard.empties)
        {
            TransformEmpty empty = copiedEmpty.empty;
            emptyIdRemap[copiedEmpty.sourceId] = GenerateSceneUUID();
            empty.id = emptyIdRemap[copiedEmpty.sourceId];
            empty.position += pasteOffset;
            empty.collectionIndex = (m_EditorClipboard.kind == ClipboardPayloadKind::Collection)
                                        ? destinationCollectionIndex
                                        : resolveTargetCollectionIndex(copiedEmpty.empty.collectionIndex);
            empty.collectionId = (!m_Collections.empty() && empty.collectionIndex >= 0 && empty.collectionIndex < static_cast<int>(m_Collections.size()))
                                     ? m_Collections[static_cast<std::size_t>(empty.collectionIndex)].id
                                     : 0;
            m_TransformEmpties.push_back(empty);
            newlyAddedEmptyIndices.push_back(m_TransformEmpties.size() - 1);
            if (pastedEmptyIndex < 0)
            {
                pastedEmptyIndex = static_cast<int>(m_TransformEmpties.size()) - 1;
            }
        }

        for (std::size_t emptyIndex : newlyAddedEmptyIndices)
        {
            TransformEmpty &empty = m_TransformEmpties[emptyIndex];
            const auto remapIt = emptyIdRemap.find(empty.parentEmptyId);
            if (remapIt != emptyIdRemap.end())
            {
                empty.parentEmptyId = remapIt->second;
            }
        }

        if (clipboardHasAtoms)
        {
            m_WorkingStructure.RebuildSpeciesFromAtoms();
        }
        EnsureAtomCollectionAssignments();
        m_CollectionCounter = std::max(m_CollectionCounter, static_cast<int>(m_Collections.size()) + 1);
        m_TransformEmptyCounter = std::max(m_TransformEmptyCounter, static_cast<int>(m_TransformEmpties.size()) + 1);
        m_AutoBondsDirty = true;

        m_SelectedAtomIndices = std::move(pastedAtomIndices);
        m_OutlinerAtomSelectionAnchor = m_SelectedAtomIndices.empty() ? std::optional<std::size_t>{} : std::optional<std::size_t>(m_SelectedAtomIndices.back());
        m_SelectedTransformEmptyIndex = pastedEmptyIndex;
        if (pastedEmptyIndex >= 0)
        {
            m_ActiveTransformEmptyIndex = pastedEmptyIndex;
        }
        m_SelectedBondKeys.clear();
        m_SelectedBondLabelKey = 0;
        m_SelectedSpecialNode = SpecialNodeSelection::None;

        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = (m_EditorClipboard.kind == ClipboardPayloadKind::Collection ? "Pasted collection copy." : "Pasted selection copy.");
        LogInfo(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::DuplicateCurrentSelection()
    {
        if (!CopyCurrentSelectionToClipboard())
        {
            return false;
        }

        return PasteClipboard(m_DuplicateAppliesOffset);
    }

    bool EditorLayer::DuplicateCollection(int collectionIndex)
    {
        if (!CopyCollectionToClipboard(collectionIndex))
        {
            return false;
        }

        return PasteClipboard(m_DuplicateAppliesOffset);
    }

    bool EditorLayer::ExtractSelectionToNewCollection()
    {
        const bool hasSelectedAtoms = !m_SelectedAtomIndices.empty();
        const bool hasSelectedEmpty = m_SelectedTransformEmptyIndex >= 0 &&
                                      m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size());
        if (!hasSelectedAtoms && !hasSelectedEmpty)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Extract to collection failed: no atoms or Empty selected.";
            LogWarn(m_LastStructureMessage);
            return false;
        }

        PushUndoSnapshot("Extract selection to new collection");

        SceneCollection collection;
        collection.id = GenerateSceneUUID();
        char label[64] = {};
        std::snprintf(label, sizeof(label), "Collection %d", m_CollectionCounter++);
        collection.name = label;
        m_Collections.push_back(collection);
        const int newCollectionIndex = static_cast<int>(m_Collections.size()) - 1;
        m_ActiveCollectionIndex = newCollectionIndex;
        m_SelectedCollectionIndices.clear();
        m_SelectedCollectionIndices.insert(newCollectionIndex);
        m_OutlinerCollectionSelectionAnchor = static_cast<std::size_t>(newCollectionIndex);

        std::size_t movedAtomCount = 0;
        for (std::size_t atomIndex : m_SelectedAtomIndices)
        {
            if (atomIndex < m_AtomCollectionIndices.size())
            {
                m_AtomCollectionIndices[atomIndex] = newCollectionIndex;
                ++movedAtomCount;
            }
        }

        if (hasSelectedEmpty)
        {
            TransformEmpty &selectedEmpty = m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)];
            selectedEmpty.collectionIndex = newCollectionIndex;
            selectedEmpty.collectionId = collection.id;

            bool parentStillInCollection = false;
            if (selectedEmpty.parentEmptyId != 0)
            {
                for (const TransformEmpty &empty : m_TransformEmpties)
                {
                    if (empty.id == selectedEmpty.parentEmptyId)
                    {
                        parentStillInCollection = (empty.collectionIndex == newCollectionIndex);
                        break;
                    }
                }
            }
            if (!parentStillInCollection)
            {
                selectedEmpty.parentEmptyId = 0;
            }
        }

        m_LastStructureOperationFailed = false;
        m_LastStructureMessage =
            "Extracted selection to new collection '" + collection.name +
            "' (atoms=" + std::to_string(movedAtomCount) +
            ", empty=" + std::to_string(hasSelectedEmpty ? 1 : 0) + ").";
        LogInfo(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::DeleteCollectionAtIndex(int collectionIndex, std::string *outStatusMessage)
    {
        if (m_Collections.size() <= 1 ||
            collectionIndex < 0 ||
            collectionIndex >= static_cast<int>(m_Collections.size()))
        {
            return false;
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
            m_OutlinerAtomSelectionAnchor.reset();
            return uniqueIndices.size();
        };

        std::vector<std::size_t> atomIndicesToDelete;
        atomIndicesToDelete.reserve(m_AtomCollectionIndices.size());
        for (std::size_t atomIndex = 0; atomIndex < m_AtomCollectionIndices.size(); ++atomIndex)
        {
            if (m_AtomCollectionIndices[atomIndex] == collectionIndex)
            {
                atomIndicesToDelete.push_back(atomIndex);
            }
        }

        std::vector<int> emptyIndicesToDelete;
        emptyIndicesToDelete.reserve(m_TransformEmpties.size());
        for (int emptyIndex = 0; emptyIndex < static_cast<int>(m_TransformEmpties.size()); ++emptyIndex)
        {
            if (m_TransformEmpties[static_cast<std::size_t>(emptyIndex)].collectionIndex == collectionIndex)
            {
                emptyIndicesToDelete.push_back(emptyIndex);
            }
        }

        const std::size_t removedAtomCount = deleteAtomsByIndices(atomIndicesToDelete);
        const std::size_t removedEmptyCount = emptyIndicesToDelete.size();
        for (auto it = emptyIndicesToDelete.rbegin(); it != emptyIndicesToDelete.rend(); ++it)
        {
            DeleteTransformEmptyAtIndex(*it);
        }

        m_Collections.erase(m_Collections.begin() + collectionIndex);
        std::unordered_set<int> remappedSelectedCollections;
        remappedSelectedCollections.reserve(m_SelectedCollectionIndices.size());
        for (int selectedCollectionIndex : m_SelectedCollectionIndices)
        {
            if (selectedCollectionIndex == collectionIndex)
            {
                continue;
            }

            if (selectedCollectionIndex > collectionIndex)
            {
                remappedSelectedCollections.insert(selectedCollectionIndex - 1);
            }
            else if (selectedCollectionIndex >= 0)
            {
                remappedSelectedCollections.insert(selectedCollectionIndex);
            }
        }
        m_SelectedCollectionIndices = std::move(remappedSelectedCollections);
        for (int &assignedCollectionIndex : m_AtomCollectionIndices)
        {
            if (assignedCollectionIndex > collectionIndex)
            {
                --assignedCollectionIndex;
            }
        }

        for (TransformEmpty &empty : m_TransformEmpties)
        {
            if (empty.collectionIndex > collectionIndex)
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
        if (m_SelectedCollectionIndices.empty() && !m_Collections.empty())
        {
            m_SelectedCollectionIndices.insert(m_ActiveCollectionIndex);
        }
        m_OutlinerCollectionSelectionAnchor =
            (!m_SelectedCollectionIndices.empty() && m_ActiveCollectionIndex >= 0)
                ? std::optional<std::size_t>(static_cast<std::size_t>(m_ActiveCollectionIndex))
                : std::nullopt;

        for (int groupIndex = 0; groupIndex < static_cast<int>(m_ObjectGroups.size()); ++groupIndex)
        {
            SceneGroupingBackend::SanitizeGroup(*this, groupIndex);
        }

        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Deleted collection with atoms=" + std::to_string(removedAtomCount) +
                                 ", empties=" + std::to_string(removedEmptyCount) + ".";
        if (outStatusMessage != nullptr)
        {
            *outStatusMessage = m_LastStructureMessage;
        }
        LogInfo(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::AlignEmptyZAxisFromSelectedAtoms(int emptyIndex)
    {
        if (emptyIndex < 0 || emptyIndex >= static_cast<int>(m_TransformEmpties.size()))
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Align Empty Z failed: active empty index is invalid.";
            AppendSelectionDebugLog(m_LastStructureMessage);
            return false;
        }

        if (m_SelectedAtomIndices.size() < 2)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Align Empty Z failed: select at least 2 atoms.";
            AppendSelectionDebugLog(m_LastStructureMessage);
            return false;
        }

        const std::size_t atomA = m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 2];
        const std::size_t atomB = m_SelectedAtomIndices[m_SelectedAtomIndices.size() - 1];
        if (atomA >= m_WorkingStructure.atoms.size() || atomB >= m_WorkingStructure.atoms.size() || atomA == atomB)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Align Empty Z failed: selected atom pair is invalid.";
            AppendSelectionDebugLog(m_LastStructureMessage);
            return false;
        }

        const glm::vec3 posA = GetAtomCartesianPosition(atomA);
        const glm::vec3 posB = GetAtomCartesianPosition(atomB);
        const glm::vec3 zAxis = posB - posA;
        if (glm::length2(zAxis) < 1e-8f)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Align Empty Z failed: atom pair is coincident.";
            AppendSelectionDebugLog(m_LastStructureMessage);
            return false;
        }

        TransformEmpty &empty = m_TransformEmpties[static_cast<std::size_t>(emptyIndex)];
        const glm::vec3 previousZ = glm::normalize(empty.axes[2]);
        const glm::vec3 axisZ = glm::normalize(zAxis);
        glm::vec3 up = (std::abs(axisZ.z) < 0.95f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 axisX = glm::cross(up, axisZ);
        if (glm::length2(axisX) < 1e-8f)
        {
            up = glm::vec3(1.0f, 0.0f, 0.0f);
            axisX = glm::cross(up, axisZ);
            if (glm::length2(axisX) < 1e-8f)
            {
                m_LastStructureOperationFailed = true;
                m_LastStructureMessage = "Align Empty Z failed: could not build orthogonal frame.";
                AppendSelectionDebugLog(m_LastStructureMessage);
                return false;
            }
        }

        axisX = glm::normalize(axisX);
        const glm::vec3 axisY = glm::normalize(glm::cross(axisZ, axisX));

        empty.axes[0] = axisX;
        empty.axes[1] = axisY;
        empty.axes[2] = axisZ;

        const float alignmentDot = glm::clamp(glm::dot(previousZ, axisZ), -1.0f, 1.0f);
        const float rotationDeltaDeg = glm::degrees(std::acos(alignmentDot));
        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Aligned Empty Z to selected atoms.";

        std::ostringstream alignLog;
        alignLog << "Align Empty Z success: emptyIndex=" << emptyIndex
                 << " atomA=" << atomA
                 << " atomB=" << atomB
                 << " posA=(" << posA.x << "," << posA.y << "," << posA.z << ")"
                 << " posB=(" << posB.x << "," << posB.y << "," << posB.z << ")"
                 << " newZ=(" << axisZ.x << "," << axisZ.y << "," << axisZ.z << ")"
                 << " deltaDeg=" << rotationDeltaDeg;
        AppendSelectionDebugLog(alignLog.str());
        return true;
    }

    bool EditorLayer::AlignEmptyAxesToCameraView(int emptyIndex)
    {
        if (emptyIndex < 0 || emptyIndex >= static_cast<int>(m_TransformEmpties.size()))
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Align Empty to camera failed: active empty index is invalid.";
            AppendSelectionDebugLog(m_LastStructureMessage);
            return false;
        }
        if (!m_Camera)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Align Empty to camera failed: camera is unavailable.";
            AppendSelectionDebugLog(m_LastStructureMessage);
            return false;
        }

        const glm::mat4 inverseView = glm::inverse(m_Camera->GetViewMatrix());
        glm::vec3 axisX = glm::vec3(inverseView[0]);
        glm::vec3 axisY = glm::vec3(inverseView[1]);
        if (glm::length2(axisX) < 1e-8f || glm::length2(axisY) < 1e-8f)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Align Empty to camera failed: could not read camera basis.";
            AppendSelectionDebugLog(m_LastStructureMessage);
            return false;
        }

        axisX = glm::normalize(axisX);
        axisY = glm::normalize(axisY);
        glm::vec3 axisZ = glm::cross(axisX, axisY);
        if (glm::length2(axisZ) < 1e-8f)
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "Align Empty to camera failed: camera basis is degenerate.";
            AppendSelectionDebugLog(m_LastStructureMessage);
            return false;
        }

        axisZ = glm::normalize(axisZ);
        axisY = glm::normalize(glm::cross(axisZ, axisX));

        TransformEmpty &empty = m_TransformEmpties[static_cast<std::size_t>(emptyIndex)];
        empty.axes[0] = axisX;
        empty.axes[1] = axisY;
        empty.axes[2] = axisZ;

        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Aligned Empty axes to camera view.";
        AppendSelectionDebugLog(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::BuildAxesFromPoints(const std::vector<glm::vec3> &points, const glm::vec3 &pivot, std::array<glm::vec3, 3> &outAxes) const
    {
        std::vector<glm::vec3> offsets;
        offsets.reserve(points.size());

        for (const glm::vec3 &point : points)
        {
            const glm::vec3 offset = point - pivot;
            if (glm::length2(offset) > 1e-8f)
            {
                offsets.push_back(offset);
            }
        }

        if (offsets.empty())
        {
            return false;
        }

        glm::vec3 axisX(0.0f);
        float maxLen2 = 0.0f;
        for (const glm::vec3 &offset : offsets)
        {
            const float len2 = glm::length2(offset);
            if (len2 > maxLen2)
            {
                maxLen2 = len2;
                axisX = offset;
            }
        }

        if (maxLen2 < 1e-8f)
        {
            return false;
        }
        axisX = glm::normalize(axisX);

        glm::vec3 axisY(0.0f);
        float bestOrthogonality = 0.0f;
        for (const glm::vec3 &offset : offsets)
        {
            const glm::vec3 normalized = glm::normalize(offset);
            const float orthogonality = glm::length(glm::cross(axisX, normalized));
            if (orthogonality > bestOrthogonality)
            {
                bestOrthogonality = orthogonality;
                axisY = normalized;
            }
        }

        if (bestOrthogonality < 1e-4f)
        {
            axisY = (std::abs(axisX.z) < 0.9f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        glm::vec3 axisZ = glm::cross(axisX, axisY);
        if (glm::length2(axisZ) < 1e-8f)
        {
            return false;
        }

        axisZ = glm::normalize(axisZ);
        axisY = glm::normalize(glm::cross(axisZ, axisX));

        outAxes[0] = axisX;
        outAxes[1] = axisY;
        outAxes[2] = axisZ;
        return true;
    }

    bool EditorLayer::ResolveTemporaryLocalAxes(std::array<glm::vec3, 3> &outAxes) const
    {
        if (!m_UseTemporaryLocalAxes || !m_HasStructureLoaded)
        {
            return false;
        }

        if (m_TemporaryAxesSource == TemporaryAxesSource::ActiveEmpty)
        {
            if (!HasActiveTransformEmpty())
            {
                return false;
            }

            outAxes = m_TransformEmpties[static_cast<std::size_t>(m_ActiveTransformEmptyIndex)].axes;
            return true;
        }

        if (m_TemporaryAxisAtomA < 0 || m_TemporaryAxisAtomB < 0)
        {
            return false;
        }

        const std::size_t atomA = static_cast<std::size_t>(m_TemporaryAxisAtomA);
        const std::size_t atomB = static_cast<std::size_t>(m_TemporaryAxisAtomB);
        if (atomA >= m_WorkingStructure.atoms.size() || atomB >= m_WorkingStructure.atoms.size() || atomA == atomB)
        {
            return false;
        }

        const glm::vec3 origin = GetAtomCartesianPosition(atomA);
        std::vector<glm::vec3> framePoints;
        framePoints.reserve(2);
        framePoints.push_back(GetAtomCartesianPosition(atomB));

        if (m_TemporaryAxisAtomC >= 0)
        {
            const std::size_t atomC = static_cast<std::size_t>(m_TemporaryAxisAtomC);
            if (atomC < m_WorkingStructure.atoms.size() && atomC != atomA && atomC != atomB)
            {
                framePoints.push_back(GetAtomCartesianPosition(atomC));
            }
        }

        return BuildAxesFromPoints(framePoints, origin, outAxes);
    }

    std::array<glm::vec3, 3> EditorLayer::ResolveTransformAxes(const glm::vec3 &pivot) const
    {
        std::array<glm::vec3, 3> axes = {
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f)};

        if (m_GizmoModeIndex == 1)
        {
            return axes;
        }

        std::vector<glm::vec3> points;
        if (m_GizmoModeIndex == 0)
        {
            std::array<glm::vec3, 3> temporaryAxes = axes;
            if (ResolveTemporaryLocalAxes(temporaryAxes))
            {
                return temporaryAxes;
            }

            points.reserve(m_SelectedAtomIndices.size());
            for (std::size_t atomIndex : m_SelectedAtomIndices)
            {
                if (atomIndex < m_WorkingStructure.atoms.size())
                {
                    points.push_back(GetAtomCartesianPosition(atomIndex));
                }
            }
        }
        else
        {
            float selectionRadius = 0.0f;
            for (std::size_t atomIndex : m_SelectedAtomIndices)
            {
                if (atomIndex < m_WorkingStructure.atoms.size())
                {
                    const float dist = glm::length(GetAtomCartesianPosition(atomIndex) - pivot);
                    selectionRadius = std::max(selectionRadius, dist);
                }
            }

            const float searchRadius = std::max(1.0f, selectionRadius * 1.6f);
            constexpr std::size_t kMaxNeighbors = 48;
            std::vector<std::pair<float, glm::vec3>> nearest;
            nearest.reserve(kMaxNeighbors);

            for (std::size_t atomIndex = 0; atomIndex < m_WorkingStructure.atoms.size(); ++atomIndex)
            {
                const glm::vec3 atomPos = GetAtomCartesianPosition(atomIndex);
                const float dist = glm::length(atomPos - pivot);
                if (dist < 1e-4f || dist > searchRadius)
                {
                    continue;
                }

                if (nearest.size() < kMaxNeighbors)
                {
                    nearest.emplace_back(dist, atomPos);
                    continue;
                }

                std::size_t farthestIndex = 0;
                float farthestDistance = nearest[0].first;
                for (std::size_t i = 1; i < nearest.size(); ++i)
                {
                    if (nearest[i].first > farthestDistance)
                    {
                        farthestDistance = nearest[i].first;
                        farthestIndex = i;
                    }
                }

                if (dist < farthestDistance)
                {
                    nearest[farthestIndex] = std::make_pair(dist, atomPos);
                }
            }

            points.reserve(nearest.size());
            for (const auto &entry : nearest)
            {
                points.push_back(entry.second);
            }
        }

        std::array<glm::vec3, 3> computedAxes = axes;
        if (BuildAxesFromPoints(points, pivot, computedAxes))
        {
            return computedAxes;
        }

        return axes;
    }

    bool EditorLayer::Set3DCursorToSelectionCenterOfMass()
    {
        if (!m_HasStructureLoaded || m_WorkingStructure.atoms.empty() || m_SelectedAtomIndices.empty())
        {
            return false;
        }

        glm::vec3 weightedSum(0.0f);
        float totalMass = 0.0f;
        for (std::size_t atomIndex : m_SelectedAtomIndices)
        {
            if (atomIndex >= m_WorkingStructure.atoms.size())
            {
                continue;
            }

            const Atom &atom = m_WorkingStructure.atoms[atomIndex];
            const float mass = AtomicMassByElementSymbol(atom.element);
            weightedSum += GetAtomCartesianPosition(atomIndex) * mass;
            totalMass += mass;
        }

        if (totalMass <= 1e-6f)
        {
            return false;
        }

        m_CursorPosition = weightedSum / totalMass;
        m_AddAtomPosition = m_CursorPosition;
        m_AddAtomCoordinateModeIndex = 1;
        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "3D cursor moved to selection center of mass.";
        LogInfo(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::SelectAllVisibleByCurrentFilter()
    {
        if (!m_HasStructureLoaded || m_WorkingStructure.atoms.empty())
        {
            return false;
        }

        const bool includeAtoms =
            (m_SelectionFilter == SelectionFilter::AtomsOnly) ||
            (m_SelectionFilter == SelectionFilter::AtomsAndBonds);
        const bool includeBonds =
            (m_SelectionFilter == SelectionFilter::AtomsAndBonds) ||
            (m_SelectionFilter == SelectionFilter::BondsOnly) ||
            (m_SelectionFilter == SelectionFilter::BondLabelsOnly);

        m_SelectedAtomIndices.clear();
        m_SelectedBondKeys.clear();
        m_SelectedBondLabelKey = 0;
        m_SelectedTransformEmptyIndex = -1;
        m_SelectedSpecialNode = SpecialNodeSelection::None;

        if (includeAtoms)
        {
            m_SelectedAtomIndices.reserve(m_WorkingStructure.atoms.size());
            for (std::size_t atomIndex = 0; atomIndex < m_WorkingStructure.atoms.size(); ++atomIndex)
            {
                if (!IsAtomHidden(atomIndex) && IsAtomCollectionVisible(atomIndex) && IsAtomCollectionSelectable(atomIndex))
                {
                    m_SelectedAtomIndices.push_back(atomIndex);
                }
            }
        }

        if (includeBonds)
        {
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

                m_SelectedBondKeys.insert(key);
            }
        }

        m_OutlinerAtomSelectionAnchor = m_SelectedAtomIndices.empty() ? std::nullopt : std::optional<std::size_t>(m_SelectedAtomIndices.front());
        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "Selected all visible items for current filter.";
        AppendSelectionDebugLog(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::FocusCameraOnCursor(float distanceFactorMultiplier, bool persistAdjustment)
    {
        if (!m_Camera)
        {
            return false;
        }

        if (persistAdjustment)
        {
            m_CursorFocusDistanceFactor = glm::clamp(m_CursorFocusDistanceFactor * distanceFactorMultiplier, 0.25f, 2.50f);
        }

        std::vector<glm::vec3> focusPoints;
        focusPoints.reserve(
            m_SelectedAtomIndices.size() +
            m_SelectedBondKeys.size() * 2 +
            (m_SelectedTransformEmptyIndex >= 0 ? 1u : 0u) +
            (m_SelectedSpecialNode != SpecialNodeSelection::None ? 1u : 0u));

        float minimumFocusRadius = 0.0f;

        auto appendAtomPoint = [&](std::size_t atomIndex)
        {
            if (atomIndex >= m_WorkingStructure.atoms.size())
            {
                return;
            }

            const glm::vec3 atomPosition = GetAtomCartesianPosition(atomIndex);
            focusPoints.push_back(atomPosition);

            const std::string elementKey = NormalizeElementSymbol(m_WorkingStructure.atoms[atomIndex].element);
            const float atomRadius =
                m_SceneSettings.atomScale *
                ElementRadiusScale(elementKey) *
                ResolveElementVisualScale(elementKey);
            minimumFocusRadius = std::max(minimumFocusRadius, atomRadius);
        };

        for (std::size_t atomIndex : m_SelectedAtomIndices)
        {
            appendAtomPoint(atomIndex);
        }

        if (m_SelectedTransformEmptyIndex >= 0 && m_SelectedTransformEmptyIndex < static_cast<int>(m_TransformEmpties.size()))
        {
            focusPoints.push_back(m_TransformEmpties[static_cast<std::size_t>(m_SelectedTransformEmptyIndex)].position);
            minimumFocusRadius = std::max(minimumFocusRadius, std::max(0.08f, m_TransformEmptyVisualScale * 0.75f));
        }

        if (m_SelectedSpecialNode == SpecialNodeSelection::Light)
        {
            focusPoints.push_back(m_LightPosition);
            minimumFocusRadius = std::max(minimumFocusRadius, std::max(0.08f, m_CursorVisualScale * 0.85f));
        }

        auto appendBondPoint = [&](std::uint64_t bondKey)
        {
            for (const BondSegment &bond : m_GeneratedBonds)
            {
                if (MakeBondPairKey(bond.atomA, bond.atomB) != bondKey)
                {
                    continue;
                }

                focusPoints.push_back(bond.start);
                focusPoints.push_back(bond.end);
                minimumFocusRadius = std::max(minimumFocusRadius, glm::length(bond.end - bond.start) * 0.5f);
                break;
            }
        };

        for (std::uint64_t bondKey : m_SelectedBondKeys)
        {
            appendBondPoint(bondKey);
        }

        if (m_SelectedBondLabelKey != 0)
        {
            appendBondPoint(m_SelectedBondLabelKey);
        }

        const bool focusSelection = !focusPoints.empty();
        glm::vec3 focusTarget = m_CursorPosition;
        float selectionRadius = minimumFocusRadius;
        glm::vec3 selectionExtents(0.0f);
        if (focusSelection)
        {
            glm::vec3 boundsMin(std::numeric_limits<float>::max());
            glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
            for (const glm::vec3 &point : focusPoints)
            {
                boundsMin = glm::min(boundsMin, point);
                boundsMax = glm::max(boundsMax, point);
            }
            focusTarget = 0.5f * (boundsMin + boundsMax);
            selectionExtents = 0.5f * (boundsMax - boundsMin);

            for (const glm::vec3 &point : focusPoints)
            {
                selectionRadius = std::max(selectionRadius, glm::length(point - focusTarget));
            }
        }

        const float currentDistance = m_Camera->GetDistance();
        float focusDistance = std::max(m_CursorFocusMinDistance, currentDistance * m_CursorFocusDistanceFactor);
        if (focusSelection)
        {
            const float paddedRadius = std::max(minimumFocusRadius, selectionRadius) * std::max(1.0f, m_CursorFocusSelectionPadding);
            if (m_Camera->GetProjectionMode() == OrbitCamera::ProjectionMode::Perspective)
            {
                const float viewportWidth = std::max(1.0f, m_ViewportSize.x);
                const float viewportHeight = std::max(1.0f, m_ViewportSize.y);
                const float aspect = viewportWidth / viewportHeight;
                const float halfFovY = glm::radians(m_Camera->GetPerspectiveFovDegrees()) * 0.5f;
                const float halfFovX = std::atan(std::tan(halfFovY) * aspect);
                const float limitingHalfFov = std::max(0.15f, std::min(halfFovX, halfFovY));

                const float fitSphereDistance = paddedRadius / std::tan(limitingHalfFov);
                const float fitHorizontalDistance = std::max(selectionExtents.x, selectionExtents.y) / std::max(0.15f, std::tan(halfFovX));
                const float fitVerticalDistance = selectionExtents.z / std::max(0.15f, std::tan(halfFovY));
                const float fitDistance = std::max({fitSphereDistance, fitHorizontalDistance, fitVerticalDistance, m_CursorFocusMinDistance});
                focusDistance = fitDistance * m_CursorFocusDistanceFactor;
            }
            else
            {
                const float fitOrthoSize = std::max(m_CursorFocusMinDistance * 0.5f, paddedRadius * m_CursorFocusDistanceFactor);
                m_Camera->SetOrthographicSize(glm::clamp(fitOrthoSize, 0.05f, 500.0f));
                focusDistance = currentDistance;
            }
        }
        focusDistance = glm::clamp(focusDistance, 0.10f, 250.0f);

        StartCameraOrbitTransition(
            focusTarget,
            focusDistance,
            m_Camera->GetYaw(),
            m_Camera->GetPitch(),
            m_Camera->GetRoll());
        m_LastStructureOperationFailed = false;
        std::ostringstream focusMessage;
        focusMessage << "Camera focused on "
                     << (focusSelection ? "selection" : "3D cursor")
                     << " (distance factor " << std::fixed << std::setprecision(2) << m_CursorFocusDistanceFactor << ").";
        m_LastStructureMessage = focusMessage.str();
        LogInfo(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::Set3DCursorToSelectedAtom(bool useLastSelected)
    {
        if (m_SelectedAtomIndices.empty())
        {
            return false;
        }

        const std::size_t atomIndex = useLastSelected ? m_SelectedAtomIndices.back() : m_SelectedAtomIndices.front();
        if (atomIndex >= m_WorkingStructure.atoms.size())
        {
            return false;
        }

        m_CursorPosition = GetAtomCartesianPosition(atomIndex);
        m_AddAtomPosition = m_CursorPosition;
        m_AddAtomCoordinateModeIndex = 1;
        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = std::string("3D cursor moved to ") + (useLastSelected ? "last" : "first") + " selected atom.";
        LogInfo(m_LastStructureMessage);
        return true;
    }

    bool EditorLayer::PickWorldPositionOnGrid(const glm::vec2 &mousePos, glm::vec3 &outWorldPosition) const
    {
        if (!m_Camera)
        {
            return false;
        }

        const float width = m_ViewportRectMax.x - m_ViewportRectMin.x;
        const float height = m_ViewportRectMax.y - m_ViewportRectMin.y;
        if (width < 1.0f || height < 1.0f)
        {
            return false;
        }

        const float x = (mousePos.x - m_ViewportRectMin.x) / width;
        const float y = (mousePos.y - m_ViewportRectMin.y) / height;
        if (x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f)
        {
            return false;
        }

        const float ndcX = x * 2.0f - 1.0f;
        const float ndcY = 1.0f - y * 2.0f;

        const glm::mat4 inverseViewProjection = glm::inverse(m_Camera->GetViewProjectionMatrix());
        glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

        if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f)
        {
            return false;
        }

        const glm::vec3 worldNear = glm::vec3(nearPoint) / nearPoint.w;
        const glm::vec3 worldFar = glm::vec3(farPoint) / farPoint.w;
        const glm::vec3 ray = worldFar - worldNear;
        const float rayZ = ray.z;
        if (std::abs(rayZ) < 1e-6f)
        {
            return false;
        }

        const float planeZ = m_SceneSettings.gridOrigin.z;
        const float t = (planeZ - worldNear.z) / rayZ;
        if (t < 0.0f)
        {
            return false;
        }

        glm::vec3 hit = worldNear + ray * t;
        hit.z = planeZ;

        if (m_CursorSnapToGrid)
        {
            const float spacing = std::max(0.0001f, m_SceneSettings.gridSpacing);
            hit.x = std::round((hit.x - m_SceneSettings.gridOrigin.x) / spacing) * spacing + m_SceneSettings.gridOrigin.x;
            hit.y = std::round((hit.y - m_SceneSettings.gridOrigin.y) / spacing) * spacing + m_SceneSettings.gridOrigin.y;
        }

        outWorldPosition = hit;
        return true;
    }

    void EditorLayer::Set3DCursorFromScreenPoint(const glm::vec2 &mousePos)
    {
        glm::vec3 worldHit(0.0f);
        if (!PickWorldPositionOnGrid(mousePos, worldHit))
        {
            m_LastStructureOperationFailed = true;
            m_LastStructureMessage = "3D cursor placement failed: could not project onto grid plane.";
            LogWarn(m_LastStructureMessage);
            return;
        }

        m_CursorPosition = worldHit;
        m_AddAtomPosition = worldHit;
        m_AddAtomCoordinateModeIndex = 1;
        m_LastStructureOperationFailed = false;
        m_LastStructureMessage = "3D cursor moved to (" + std::to_string(worldHit.x) + ", " + std::to_string(worldHit.y) + ", " + std::to_string(worldHit.z) + ")";
        LogInfo(m_LastStructureMessage);
    }


} // namespace ds
