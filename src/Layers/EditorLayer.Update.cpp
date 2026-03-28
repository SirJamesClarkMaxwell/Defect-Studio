#include "Layers/EditorLayerPrivate.h"

#include <chrono>

namespace ds
{
    namespace
    {
        float ComputeDefaultIsoValue(const ScalarFieldBlock &block, float factor)
        {
            const float absMax = std::max(1e-6f, block.statistics.absMaxValue);
            const float meanBias = std::max(std::abs(block.statistics.meanValue) * 6.0f, absMax * 0.05f);
            return std::max(meanBias, absMax * factor);
        }
    } // namespace

    void EditorLayer::MarkVolumetricMeshesDirty()
    {
        auto resetSurface = [](VolumetricSurfaceState &surface)
        {
            surface.dirty = true;
            surface.hasMesh = false;
            surface.sampledDimensions = glm::ivec3(0);
            surface.decimationStep = 1;
            surface.lastBuildMilliseconds = 0.0;
            surface.lastStatus.clear();
            surface.mesh.Clear();
        };

        resetSurface(m_PrimaryVolumetricSurface);
        resetSurface(m_SecondaryVolumetricSurface);
    }

    void EditorLayer::SyncVolumetricSurfaceDefaults()
    {
        EnsureVolumetricSelection();
        if (m_ActiveVolumetricDatasetIndex < 0 || m_ActiveVolumetricDatasetIndex >= static_cast<int>(m_VolumetricDatasets.size()))
        {
            m_VolumetricSurfaceDatasetKey.clear();
            return;
        }

        const VolumetricDataset &dataset = m_VolumetricDatasets[static_cast<std::size_t>(m_ActiveVolumetricDatasetIndex)];
        const std::string datasetKey = dataset.sourcePath + "#" + std::to_string(dataset.blocks.size());
        const auto clampSurfaceBlockIndex = [&](VolumetricSurfaceState &surface)
        {
            if (dataset.blocks.empty())
            {
                surface.blockIndex = 0;
                return;
            }

            surface.blockIndex = std::clamp(surface.blockIndex, 0, static_cast<int>(dataset.blocks.size()) - 1);
        };
        if (datasetKey == m_VolumetricSurfaceDatasetKey)
        {
            clampSurfaceBlockIndex(m_PrimaryVolumetricSurface);
            clampSurfaceBlockIndex(m_SecondaryVolumetricSurface);
            return;
        }

        m_VolumetricSurfaceDatasetKey = datasetKey;
        m_PrimaryVolumetricSurface.enabled = true;
        m_PrimaryVolumetricSurface.blockIndex = 0;
        m_PrimaryVolumetricSurface.color = glm::vec3(1.0f, 0.92f, 0.14f);
        m_PrimaryVolumetricSurface.opacity = 0.42f;
        m_PrimaryVolumetricSurface.isoValue =
            dataset.blocks.empty() ? 0.0f : ComputeDefaultIsoValue(dataset.blocks.front(), 0.26f);

        m_SecondaryVolumetricSurface.enabled = dataset.blocks.size() > 1;
        m_SecondaryVolumetricSurface.blockIndex = dataset.blocks.size() > 1 ? 1 : 0;
        m_SecondaryVolumetricSurface.color = glm::vec3(0.10f, 0.22f, 0.96f);
        m_SecondaryVolumetricSurface.opacity = 0.46f;
        m_SecondaryVolumetricSurface.isoValue =
            dataset.blocks.size() > 1 ? ComputeDefaultIsoValue(dataset.blocks[1], 0.42f) :
                                         (dataset.blocks.empty() ? 0.0f : ComputeDefaultIsoValue(dataset.blocks.front(), 0.55f));
        MarkVolumetricMeshesDirty();
    }

    bool EditorLayer::RebuildVolumetricSurfaceMesh(VolumetricSurfaceState &surfaceState)
    {
        surfaceState.dirty = false;
        surfaceState.hasMesh = false;
        surfaceState.mesh.Clear();
        surfaceState.lastStatus.clear();
        surfaceState.lastBuildMilliseconds = 0.0;
        surfaceState.sampledDimensions = glm::ivec3(0);
        surfaceState.decimationStep = 1;

        EnsureVolumetricSelection();
        if (!surfaceState.enabled ||
            m_ActiveVolumetricDatasetIndex < 0 ||
            m_ActiveVolumetricDatasetIndex >= static_cast<int>(m_VolumetricDatasets.size()))
        {
            return false;
        }

        const VolumetricDataset &dataset = m_VolumetricDatasets[static_cast<std::size_t>(m_ActiveVolumetricDatasetIndex)];
        if (dataset.blocks.empty())
        {
            surfaceState.lastStatus = "Dataset has no blocks.";
            return false;
        }

        surfaceState.blockIndex = std::clamp(surfaceState.blockIndex, 0, static_cast<int>(dataset.blocks.size()) - 1);
        const ScalarFieldBlock &block = dataset.blocks[static_cast<std::size_t>(surfaceState.blockIndex)];

        IsosurfaceBuildResult result;
        std::string error;
        const auto startedAt = std::chrono::steady_clock::now();
        const bool built = m_IsosurfaceExtractor.BuildPreviewMesh(
            dataset.structure,
            block,
            IsosurfaceBuildSettings{surfaceState.isoValue, m_VolumetricPreviewMaxDimension},
            result,
            error);
        const auto finishedAt = std::chrono::steady_clock::now();
        surfaceState.lastBuildMilliseconds = std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();
        surfaceState.sampledDimensions = result.sampledDimensions;
        surfaceState.decimationStep = result.decimationStep;

        if (!built)
        {
            surfaceState.lastStatus = error.empty() ? "Surface build failed." : error;
            return false;
        }

        surfaceState.mesh = std::move(result.mesh);
        surfaceState.hasMesh = !surfaceState.mesh.positions.empty();
        surfaceState.lastStatus =
            surfaceState.hasMesh ?
                ("Preview mesh: " + std::to_string(surfaceState.mesh.TriangleCount()) + " triangles") :
                (error.empty() ? "No surface for current iso value." : error);
        return surfaceState.hasMesh;
    }

    void EditorLayer::EnsureVolumetricSurfaceMeshes()
    {
        SyncVolumetricSurfaceDefaults();
        if (m_ActiveVolumetricDatasetIndex < 0 || m_ActiveVolumetricDatasetIndex >= static_cast<int>(m_VolumetricDatasets.size()))
        {
            return;
        }

        if (m_PrimaryVolumetricSurface.dirty)
        {
            RebuildVolumetricSurfaceMesh(m_PrimaryVolumetricSurface);
        }

        if (m_SecondaryVolumetricSurface.dirty)
        {
            RebuildVolumetricSurfaceMesh(m_SecondaryVolumetricSurface);
        }
    }

    void EditorLayer::RenderVolumetricSurfaces(IRenderBackend &backend, const OrbitCamera &camera, const SceneRenderSettings &settings)
    {
        const glm::mat4 viewProjection = camera.GetViewProjectionMatrix();
        auto renderSurface = [&](const VolumetricSurfaceState &surfaceState)
        {
            if (!surfaceState.enabled || !surfaceState.hasMesh || surfaceState.mesh.positions.empty())
            {
                return;
            }

            backend.RenderSurfaceMesh(
                viewProjection,
                surfaceState.mesh.positions,
                surfaceState.mesh.normals,
                surfaceState.color,
                surfaceState.opacity,
                settings);
        };

        renderSurface(m_PrimaryVolumetricSurface);
        renderSurface(m_SecondaryVolumetricSurface);
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        DS_PROFILE_SCOPE_N("EditorLayer::OnUpdate");
        if (!m_RenderBackend || !m_Camera)
        {
            return;
        }

        const bool hasQueuedRenderRequest = !m_RenderImageRequest.outputPath.empty();

        const float clampedRenderScale = glm::clamp(m_ViewportRenderScale, 0.25f, 1.0f);
        std::uint32_t renderWidth = static_cast<std::uint32_t>(std::max(1.0f, m_ViewportSize.x * clampedRenderScale));
        std::uint32_t renderHeight = static_cast<std::uint32_t>(std::max(1.0f, m_ViewportSize.y * clampedRenderScale));

        m_Camera->SetViewportSize(static_cast<float>(renderWidth), static_cast<float>(renderHeight));
        const bool transformModeActive = m_TranslateModeActive || m_RotateModeActive;
        const bool allowCameraInput = m_ViewportHovered && m_ViewportFocused && !transformModeActive;
        const bool blockViewportZoomForCircleSelect =
            allowCameraInput &&
            m_InteractionMode == InteractionMode::Select &&
            m_CircleSelectArmed;
        const float scrollDelta = ApplicationContext::Get().ConsumeScrollDelta();
        m_Camera->OnUpdate(
            deltaTime,
            allowCameraInput,
            (allowCameraInput && !blockViewportZoomForCircleSelect) ? scrollDelta : 0.0f,
            m_TouchpadNavigationEnabled,
            m_InvertViewportZoom);

        if (allowCameraInput && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (!blockViewportZoomForCircleSelect && std::abs(scrollDelta) > 0.0001f)))
        {
            m_CameraTransitionActive = false;
        }
        UpdateCameraOrbitTransition(deltaTime);
        EnsureVolumetricSurfaceMeshes();

        m_SceneOriginPosition = glm::vec3(0.0f);
        const glm::vec3 lightToOrigin = m_SceneOriginPosition - m_LightPosition;
        if (glm::length2(lightToOrigin) > 1e-8f)
        {
            m_SceneSettings.lightDirection = glm::normalize(lightToOrigin);
        }

        m_SceneSettings.drawCellEdges = m_ShowCellEdges;
        m_SceneSettings.cellEdgeColor = glm::clamp(m_CellEdgeColor, glm::vec3(0.0f), glm::vec3(1.0f));
        m_SceneSettings.cellEdgeLineWidth = glm::clamp(m_CellEdgeLineWidth, 0.5f, 10.0f);

        const SceneRenderSettings savedSceneSettings = m_SceneSettings;
        std::unordered_map<std::string, std::vector<glm::vec3>> atomPositionsByElement;
        std::unordered_map<std::string, std::vector<glm::vec3>> atomColorsByElement;
        std::unordered_map<std::string, std::vector<glm::vec3>> selectedPositionsByElement;
        std::unordered_map<std::string, std::vector<glm::vec3>> selectedColorsByElement;
        std::vector<glm::vec3> atomCartesianPositions;
        std::vector<glm::vec3> atomResolvedColors;

        m_RenderBackend->ResizeViewport(renderWidth, renderHeight);
        m_RenderBackend->BeginFrame(m_SceneSettings);
        if (m_HasStructureLoaded && !m_WorkingStructure.atoms.empty())
        {
            atomPositionsByElement.reserve(m_WorkingStructure.species.size() + 4);
            atomColorsByElement.reserve(m_WorkingStructure.species.size() + 4);
            selectedPositionsByElement.reserve(m_WorkingStructure.species.size() + 4);
            selectedColorsByElement.reserve(m_WorkingStructure.species.size() + 4);

            atomCartesianPositions.reserve(m_WorkingStructure.atoms.size());
            atomResolvedColors.reserve(m_WorkingStructure.atoms.size());

            for (std::size_t i = 0; i < m_WorkingStructure.atoms.size(); ++i)
            {
                const Atom &atom = m_WorkingStructure.atoms[i];
                glm::vec3 position = atom.position;
                if (m_WorkingStructure.coordinateMode == CoordinateMode::Direct)
                {
                    position = m_WorkingStructure.DirectToCartesian(position);
                }

                atomCartesianPositions.push_back(position);

                const std::string elementKey = NormalizeElementSymbol(atom.element);
                glm::vec3 atomColor = ResolveElementColor(elementKey);
                if (m_SceneSettings.overrideAtomColor)
                {
                    atomColor = m_SceneSettings.atomOverrideColor;
                }
                if (i < m_AtomNodeIds.size())
                {
                    const SceneUUID atomId = m_AtomNodeIds[i];
                    const auto overrideIt = m_AtomColorOverrides.find(atomId);
                    if (overrideIt != m_AtomColorOverrides.end())
                    {
                        atomColor = overrideIt->second;
                    }
                }

                const bool atomVisible = !IsAtomHidden(i) && IsAtomCollectionVisible(i);
                if (atomVisible)
                {
                    atomPositionsByElement[elementKey].push_back(position);
                    atomColorsByElement[elementKey].push_back(atomColor);
                }
                atomResolvedColors.push_back(atomColor);

                if (atomVisible && IsAtomSelected(i))
                {
                    selectedPositionsByElement[elementKey].push_back(position);
                    selectedColorsByElement[elementKey].push_back(atomColor);
                }
            }

            const glm::mat4 cameraView = m_Camera->GetViewMatrix();
            float nearestSceneDepth = std::numeric_limits<float>::max();
            float farthestSceneDepth = 0.0f;
            bool hasSceneDepth = false;

            for (std::size_t atomIndex = 0; atomIndex < atomCartesianPositions.size(); ++atomIndex)
            {
                if (IsAtomHidden(atomIndex) || !IsAtomCollectionVisible(atomIndex))
                {
                    continue;
                }

                const glm::vec4 viewPosition = cameraView * glm::vec4(atomCartesianPositions[atomIndex], 1.0f);
                const float sceneDepth = -viewPosition.z;
                if (sceneDepth <= 1e-4f)
                {
                    continue;
                }

                const std::string elementKey = NormalizeElementSymbol(m_WorkingStructure.atoms[atomIndex].element);
                const float atomRadius =
                    m_SceneSettings.atomScale *
                    ElementRadiusScale(elementKey) *
                    ResolveElementVisualScale(elementKey);

                nearestSceneDepth = std::min(nearestSceneDepth, sceneDepth - atomRadius * m_CameraClipNearPadding);
                farthestSceneDepth = std::max(farthestSceneDepth, sceneDepth + atomRadius * m_CameraClipFarPadding);
                hasSceneDepth = true;
            }

            for (const TransformEmpty &empty : m_TransformEmpties)
            {
                if (!empty.visible || !IsCollectionVisible(empty.collectionIndex))
                {
                    continue;
                }

                const glm::vec4 viewPosition = cameraView * glm::vec4(empty.position, 1.0f);
                const float sceneDepth = -viewPosition.z;
                if (sceneDepth <= 1e-4f)
                {
                    continue;
                }

                const float emptyRadius = std::max(0.08f, m_TransformEmptyVisualScale * 0.75f);
                nearestSceneDepth = std::min(nearestSceneDepth, sceneDepth - emptyRadius * m_CameraClipNearPadding);
                farthestSceneDepth = std::max(farthestSceneDepth, sceneDepth + emptyRadius * m_CameraClipFarPadding);
                hasSceneDepth = true;
            }

            const auto accumulateSurfaceDepth = [&](const VolumetricSurfaceState &surfaceState)
            {
                if (!surfaceState.enabled || !surfaceState.hasMesh || surfaceState.mesh.positions.empty())
                {
                    return;
                }

                const glm::vec3 &bmin = surfaceState.mesh.boundsMin;
                const glm::vec3 &bmax = surfaceState.mesh.boundsMax;
                const glm::vec3 corners[8] = {
                    glm::vec3(bmin.x, bmin.y, bmin.z),
                    glm::vec3(bmax.x, bmin.y, bmin.z),
                    glm::vec3(bmin.x, bmax.y, bmin.z),
                    glm::vec3(bmax.x, bmax.y, bmin.z),
                    glm::vec3(bmin.x, bmin.y, bmax.z),
                    glm::vec3(bmax.x, bmin.y, bmax.z),
                    glm::vec3(bmin.x, bmax.y, bmax.z),
                    glm::vec3(bmax.x, bmax.y, bmax.z)};

                for (const glm::vec3 &corner : corners)
                {
                    const glm::vec4 viewPosition = cameraView * glm::vec4(corner, 1.0f);
                    const float sceneDepth = -viewPosition.z;
                    if (sceneDepth <= 1e-4f)
                    {
                        continue;
                    }

                    nearestSceneDepth = std::min(nearestSceneDepth, sceneDepth - 0.08f * m_CameraClipNearPadding);
                    farthestSceneDepth = std::max(farthestSceneDepth, sceneDepth + 0.12f * m_CameraClipFarPadding);
                    hasSceneDepth = true;
                }
            };

            accumulateSurfaceDepth(m_PrimaryVolumetricSurface);
            accumulateSurfaceDepth(m_SecondaryVolumetricSurface);

            if (hasSceneDepth)
            {
                const float focusAwareNearLimit = std::min(10.0f, std::max(0.01f, m_Camera->GetDistance() * 0.08f));
                const float clampedNear = glm::clamp(nearestSceneDepth, 0.0025f, focusAwareNearLimit);
                const float paddedFar = std::max(farthestSceneDepth, clampedNear + 4.0f);
                const float clipSpan = std::max(paddedFar - clampedNear, 4.0f);
                m_Camera->SetClipPlanes(clampedNear, clampedNear + clipSpan);
            }
            else
            {
                const float fallbackNear = glm::clamp(m_Camera->GetDistance() * 0.02f, 0.01f, 1.0f);
                const float fallbackFar = std::max(fallbackNear + 50.0f, m_Camera->GetDistance() * 20.0f);
                m_Camera->SetClipPlanes(fallbackNear, fallbackFar);
            }

            if (m_AutoBondGenerationEnabled)
            {
                if (m_AutoBondsDirty && m_AutoRecalculateBondsOnEdit)
                {
                    RebuildAutoBonds(atomCartesianPositions);
                    m_AutoBondsDirty = false;
                }
            }
            else
            {
                // Keep only manually created bonds when auto generation is disabled.
                RebuildAutoBonds(atomCartesianPositions);
            }

            if (!m_GeneratedBonds.empty())
            {
                std::unordered_set<std::size_t> selectedIndices;
                selectedIndices.reserve(m_SelectedAtomIndices.size());
                for (std::size_t atomIndex : m_SelectedAtomIndices)
                {
                    selectedIndices.insert(atomIndex);
                }

                std::vector<glm::vec3> regularBondVertices;
                std::vector<glm::vec3> selectedBondVertices;
                regularBondVertices.reserve(m_GeneratedBonds.size() * 2);
                selectedBondVertices.reserve(m_GeneratedBonds.size());
                std::vector<glm::vec3> segmentVertices(2, glm::vec3(0.0f));
                auto renderLineSegment = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &color)
                {
                    segmentVertices[0] = a;
                    segmentVertices[1] = b;
                    m_RenderBackend->RenderLineSegments(
                        m_Camera->GetViewProjectionMatrix(),
                        segmentVertices,
                        color,
                        m_BondLineWidth);
                };

                for (const BondSegment &bond : m_GeneratedBonds)
                {
                    const std::uint64_t bondKey = MakeBondPairKey(bond.atomA, bond.atomB);
                    if (m_DeletedBondKeys.find(bondKey) != m_DeletedBondKeys.end() ||
                        m_HiddenBondKeys.find(bondKey) != m_HiddenBondKeys.end() ||
                        IsAtomHidden(bond.atomA) || IsAtomHidden(bond.atomB) ||
                        !IsAtomCollectionVisible(bond.atomA) || !IsAtomCollectionVisible(bond.atomB))
                    {
                        continue;
                    }

                    const bool bondSelected =
                        (selectedIndices.find(bond.atomA) != selectedIndices.end() ||
                         selectedIndices.find(bond.atomB) != selectedIndices.end() ||
                         m_SelectedBondKeys.find(bondKey) != m_SelectedBondKeys.end());
                    if (bondSelected)
                    {
                        selectedBondVertices.push_back(bond.start);
                        selectedBondVertices.push_back(bond.end);
                    }
                    else
                    {
                        if (m_BondRenderStyle == BondRenderStyle::UnicolorLine)
                        {
                            regularBondVertices.push_back(bond.start);
                            regularBondVertices.push_back(bond.end);
                        }
                        else
                        {
                            const glm::vec3 colorA = (bond.atomA < atomResolvedColors.size()) ? atomResolvedColors[bond.atomA] : m_BondColor;
                            const glm::vec3 colorB = (bond.atomB < atomResolvedColors.size()) ? atomResolvedColors[bond.atomB] : m_BondColor;
                            if (m_BondRenderStyle == BondRenderStyle::BicolorLine)
                            {
                                renderLineSegment(bond.start, bond.midpoint, colorA);
                                renderLineSegment(bond.midpoint, bond.end, colorB);
                            }
                            else
                            {
                                constexpr int kGradientSteps = 12;
                                for (int step = 0; step < kGradientSteps; ++step)
                                {
                                    const float t0 = static_cast<float>(step) / static_cast<float>(kGradientSteps);
                                    const float t1 = static_cast<float>(step + 1) / static_cast<float>(kGradientSteps);
                                    const glm::vec3 p0 = glm::mix(bond.start, bond.end, t0);
                                    const glm::vec3 p1 = glm::mix(bond.start, bond.end, t1);
                                    const glm::vec3 c = glm::mix(colorA, colorB, (t0 + t1) * 0.5f);
                                    renderLineSegment(p0, p1, c);
                                }
                            }
                        }
                    }
                }

                if (!regularBondVertices.empty())
                {
                    m_RenderBackend->RenderLineSegments(
                        m_Camera->GetViewProjectionMatrix(),
                        regularBondVertices,
                        m_BondColor,
                        m_BondLineWidth);
                }

                if (!selectedBondVertices.empty())
                {
                    m_RenderBackend->RenderLineSegments(
                        m_Camera->GetViewProjectionMatrix(),
                        selectedBondVertices,
                        m_BondSelectedColor,
                        m_BondLineWidth + 0.8f);
                }
            }

            for (auto &entry : atomPositionsByElement)
            {
                const std::string &elementKey = entry.first;
                const std::vector<glm::vec3> &positions = entry.second;
                std::vector<glm::vec3> &colors = atomColorsByElement[elementKey];
                SceneRenderSettings elementSettings = m_SceneSettings;
                elementSettings.atomScale = m_SceneSettings.atomScale * ElementRadiusScale(elementKey) * ResolveElementVisualScale(elementKey);
                m_RenderBackend->RenderAtomsScene(m_Camera->GetViewProjectionMatrix(), positions, colors, elementSettings);
            }

            for (auto &entry : selectedPositionsByElement)
            {
                const std::string &elementKey = entry.first;
                const std::vector<glm::vec3> &positions = entry.second;
                std::vector<glm::vec3> &colors = selectedColorsByElement[elementKey];

                SceneRenderSettings highlightShellSettings = m_SceneSettings;
                highlightShellSettings.drawGrid = false;
                highlightShellSettings.overrideAtomColor = true;
                highlightShellSettings.atomOverrideColor = m_SelectionColor;
                highlightShellSettings.atomScale = m_SceneSettings.atomScale * ElementRadiusScale(elementKey) * ResolveElementVisualScale(elementKey) * 1.10f;
                highlightShellSettings.atomBrightness = std::max(1.05f, m_SceneSettings.atomBrightness + 0.12f);
                highlightShellSettings.atomGlowStrength = std::max(m_SceneSettings.atomGlowStrength, 0.05f);
                highlightShellSettings.atomWireframe = false;
                m_RenderBackend->RenderAtomsScene(m_Camera->GetViewProjectionMatrix(), positions, colors, highlightShellSettings);

                SceneRenderSettings selectedAtomSettings = m_SceneSettings;
                selectedAtomSettings.drawGrid = false;
                selectedAtomSettings.atomScale = m_SceneSettings.atomScale * ElementRadiusScale(elementKey) * ResolveElementVisualScale(elementKey);
                m_RenderBackend->RenderAtomsScene(m_Camera->GetViewProjectionMatrix(), positions, colors, selectedAtomSettings);
            }

            RenderVolumetricSurfaces(*m_RenderBackend, *m_Camera, m_SceneSettings);
        }
        else
        {
            const float fallbackNear = glm::clamp(m_Camera->GetDistance() * 0.02f, 0.01f, 1.0f);
            const float fallbackFar = std::max(fallbackNear + 50.0f, m_Camera->GetDistance() * 20.0f);
            m_Camera->SetClipPlanes(fallbackNear, fallbackFar);
            m_GeneratedBonds.clear();
            m_AutoBondsDirty = true;
            m_RenderBackend->RenderDemoScene(m_Camera->GetViewProjectionMatrix(), m_SceneSettings);
        }

        if (m_Show3DCursor)
        {
            const std::vector<glm::vec3> cursorPosition = {m_CursorPosition};
            const std::vector<glm::vec3> cursorColor = {m_CursorColor};
            SceneRenderSettings cursorSettings = m_SceneSettings;
            cursorSettings.drawGrid = false;
            cursorSettings.overrideAtomColor = true;
            cursorSettings.atomOverrideColor = m_CursorColor;
            cursorSettings.atomScale = m_CursorVisualScale;
            cursorSettings.atomBrightness = std::max(1.0f, m_SceneSettings.atomBrightness + 0.2f);
            cursorSettings.atomWireframe = true;
            cursorSettings.atomWireframeWidth = std::max(1.0f, m_SelectionOutlineThickness);
            m_RenderBackend->RenderAtomsScene(m_Camera->GetViewProjectionMatrix(), cursorPosition, cursorColor, cursorSettings);
        }

        {
            const std::vector<glm::vec3> helperPositions = {m_LightPosition};
            std::vector<glm::vec3> helperColors;
            helperColors.reserve(1);

            const bool lightSelected = (m_SelectedSpecialNode == SpecialNodeSelection::Light);
            helperColors.push_back(lightSelected ? glm::vec3(1.0f, 0.95f, 0.92f) : glm::vec3(1.0f, 0.82f, 0.55f));

            SceneRenderSettings helperSettings = m_SceneSettings;
            helperSettings.drawGrid = false;
            helperSettings.overrideAtomColor = false;
            helperSettings.atomScale = std::max(0.08f, m_CursorVisualScale * 0.85f);
            helperSettings.atomBrightness = std::max(1.1f, m_SceneSettings.atomBrightness + 0.15f);
            helperSettings.atomWireframe = true;
            helperSettings.atomWireframeWidth = 1.7f;
            m_RenderBackend->RenderAtomsScene(m_Camera->GetViewProjectionMatrix(), helperPositions, helperColors, helperSettings);
        }

        m_RenderBackend->EndFrame();

        if (m_RenderPreviewBackend && (m_ShowRenderPreviewWindow || hasQueuedRenderRequest))
        {
            const std::uint32_t logicalWidth = hasQueuedRenderRequest ? std::max(1u, m_RenderImageRequest.width) : static_cast<std::uint32_t>(std::max(1, m_RenderImageWidth));
            const std::uint32_t logicalHeight = hasQueuedRenderRequest ? std::max(1u, m_RenderImageRequest.height) : static_cast<std::uint32_t>(std::max(1, m_RenderImageHeight));

            std::uint32_t previewWidth = logicalWidth;
            std::uint32_t previewHeight = logicalHeight;
            if (!hasQueuedRenderRequest)
            {
                const float aspect = static_cast<float>(logicalWidth) / static_cast<float>(logicalHeight);
                const glm::vec2 contentSize = glm::max(m_RenderPreviewContentSize, glm::vec2(240.0f, 160.0f));
                float imageWidth = contentSize.x;
                float imageHeight = imageWidth / std::max(0.01f, aspect);
                if (imageHeight > contentSize.y)
                {
                    imageHeight = contentSize.y;
                    imageWidth = imageHeight * aspect;
                }

                float targetLongSide = std::max(imageWidth, imageHeight) * 1.5f;
                targetLongSide = glm::clamp(targetLongSide, 512.0f, m_RenderPreviewLongSideCap);
                const float scale = targetLongSide / std::max(imageWidth, imageHeight);
                previewWidth = static_cast<std::uint32_t>(std::max(1.0f, std::round(imageWidth * scale)));
                previewHeight = static_cast<std::uint32_t>(std::max(1.0f, std::round(imageHeight * scale)));
            }

            m_RenderPreviewTargetWidth = previewWidth;
            m_RenderPreviewTargetHeight = previewHeight;

            OrbitCamera previewCamera = *m_Camera;
            previewCamera.SetViewportSize(static_cast<float>(previewWidth), static_cast<float>(previewHeight));

            SceneRenderSettings previewSceneSettings = hasQueuedRenderRequest ? m_RenderImageRequest.sceneSettings : m_RenderSceneSettings;
            previewSceneSettings.gridOrigin = savedSceneSettings.gridOrigin;

            m_RenderPreviewBackend->ResizeViewport(previewWidth, previewHeight);
            m_RenderPreviewBackend->BeginFrame(previewSceneSettings);

            if (m_HasStructureLoaded && !m_WorkingStructure.atoms.empty())
            {
                std::unordered_set<std::size_t> selectedIndices;
                selectedIndices.reserve(m_SelectedAtomIndices.size());
                for (std::size_t atomIndex : m_SelectedAtomIndices)
                {
                    selectedIndices.insert(atomIndex);
                }

                if (!m_GeneratedBonds.empty())
                {
                    std::vector<glm::mat4> regularBondModels;
                    std::vector<glm::vec3> regularBondColors;
                    std::vector<glm::mat4> selectedBondModels;
                    std::vector<glm::vec3> selectedBondColors;
                    regularBondModels.reserve(m_GeneratedBonds.size());
                    regularBondColors.reserve(m_GeneratedBonds.size());
                    selectedBondModels.reserve(m_GeneratedBonds.size());
                    selectedBondColors.reserve(m_GeneratedBonds.size());

                    auto appendBondCylinder = [&](const glm::vec3 &start, const glm::vec3 &end, const glm::vec3 &color, bool selected)
                    {
                        const glm::vec3 delta = end - start;
                        const float length = glm::length(delta);
                        if (length <= 1e-5f)
                        {
                            return;
                        }

                        glm::mat4 model = glm::translate(glm::mat4(1.0f), (start + end) * 0.5f);
                        const glm::vec3 direction = delta / length;
                        const glm::vec3 yAxis(0.0f, 1.0f, 0.0f);
                        const float cosTheta = glm::clamp(glm::dot(yAxis, direction), -1.0f, 1.0f);
                        if (cosTheta < 0.9999f)
                        {
                            glm::vec3 axis = glm::cross(yAxis, direction);
                            if (glm::length2(axis) < 1e-8f)
                            {
                                axis = glm::vec3(1.0f, 0.0f, 0.0f);
                            }
                            model = model * glm::rotate(glm::mat4(1.0f), std::acos(cosTheta), glm::normalize(axis));
                        }

                        const float radius = std::max(0.015f, previewSceneSettings.atomScale * 0.18f * (selected ? 1.18f : 1.0f));
                        model = model * glm::scale(glm::mat4(1.0f), glm::vec3(radius, length, radius));

                        if (selected)
                        {
                            selectedBondModels.push_back(model);
                            selectedBondColors.push_back(color);
                        }
                        else
                        {
                            regularBondModels.push_back(model);
                            regularBondColors.push_back(color);
                        }
                    };

                    for (const BondSegment &bond : m_GeneratedBonds)
                    {
                        const std::uint64_t bondKey = MakeBondPairKey(bond.atomA, bond.atomB);
                        if (m_DeletedBondKeys.find(bondKey) != m_DeletedBondKeys.end() ||
                            m_HiddenBondKeys.find(bondKey) != m_HiddenBondKeys.end() ||
                            IsAtomHidden(bond.atomA) || IsAtomHidden(bond.atomB) ||
                            !IsAtomCollectionVisible(bond.atomA) || !IsAtomCollectionVisible(bond.atomB))
                        {
                            continue;
                        }

                        const bool bondSelected =
                            (selectedIndices.find(bond.atomA) != selectedIndices.end() ||
                             selectedIndices.find(bond.atomB) != selectedIndices.end() ||
                             m_SelectedBondKeys.find(bondKey) != m_SelectedBondKeys.end());

                        if (bondSelected)
                        {
                            appendBondCylinder(bond.start, bond.end, m_BondSelectedColor, true);
                        }
                        else if (m_BondRenderStyle == BondRenderStyle::UnicolorLine)
                        {
                            appendBondCylinder(bond.start, bond.end, m_BondColor, false);
                        }
                        else
                        {
                            const glm::vec3 colorA =
                                previewSceneSettings.overrideAtomColor
                                    ? previewSceneSettings.atomOverrideColor
                                    : ((bond.atomA < atomResolvedColors.size()) ? atomResolvedColors[bond.atomA] : m_BondColor);
                            const glm::vec3 colorB =
                                previewSceneSettings.overrideAtomColor
                                    ? previewSceneSettings.atomOverrideColor
                                    : ((bond.atomB < atomResolvedColors.size()) ? atomResolvedColors[bond.atomB] : m_BondColor);

                            if (m_BondRenderStyle == BondRenderStyle::BicolorLine)
                            {
                                appendBondCylinder(bond.start, bond.midpoint, colorA, false);
                                appendBondCylinder(bond.midpoint, bond.end, colorB, false);
                            }
                            else
                            {
                                constexpr int kGradientSteps = 12;
                                for (int step = 0; step < kGradientSteps; ++step)
                                {
                                    const float t0 = static_cast<float>(step) / static_cast<float>(kGradientSteps);
                                    const float t1 = static_cast<float>(step + 1) / static_cast<float>(kGradientSteps);
                                    const glm::vec3 p0 = glm::mix(bond.start, bond.end, t0);
                                    const glm::vec3 p1 = glm::mix(bond.start, bond.end, t1);
                                    const glm::vec3 c = glm::mix(colorA, colorB, (t0 + t1) * 0.5f);
                                    appendBondCylinder(p0, p1, c, false);
                                }
                            }
                        }
                    }

                    if (!regularBondModels.empty())
                    {
                        m_RenderPreviewBackend->RenderCylinderInstances(previewCamera.GetViewProjectionMatrix(), regularBondModels, regularBondColors, previewSceneSettings);
                    }
                    if (!selectedBondModels.empty())
                    {
                        m_RenderPreviewBackend->RenderCylinderInstances(previewCamera.GetViewProjectionMatrix(), selectedBondModels, selectedBondColors, previewSceneSettings);
                    }
                }

                for (auto &entry : atomPositionsByElement)
                {
                    const std::string &elementKey = entry.first;
                    const std::vector<glm::vec3> &positions = entry.second;
                    std::vector<glm::vec3> &colors = atomColorsByElement[elementKey];
                    SceneRenderSettings elementSettings = previewSceneSettings;
                    elementSettings.atomScale = previewSceneSettings.atomScale * ElementRadiusScale(elementKey) * ResolveElementVisualScale(elementKey);
                    m_RenderPreviewBackend->RenderAtomsScene(previewCamera.GetViewProjectionMatrix(), positions, colors, elementSettings);
                }

                for (auto &entry : selectedPositionsByElement)
                {
                    const std::string &elementKey = entry.first;
                    const std::vector<glm::vec3> &positions = entry.second;
                    std::vector<glm::vec3> &colors = selectedColorsByElement[elementKey];

                    SceneRenderSettings highlightShellSettings = previewSceneSettings;
                    highlightShellSettings.drawGrid = false;
                    highlightShellSettings.overrideAtomColor = true;
                    highlightShellSettings.atomOverrideColor = m_SelectionColor;
                    highlightShellSettings.atomScale = previewSceneSettings.atomScale * ElementRadiusScale(elementKey) * ResolveElementVisualScale(elementKey) * 1.10f;
                    highlightShellSettings.atomBrightness = std::max(1.05f, previewSceneSettings.atomBrightness + 0.12f);
                    highlightShellSettings.atomGlowStrength = std::max(previewSceneSettings.atomGlowStrength, 0.05f);
                    highlightShellSettings.atomWireframe = false;
                    m_RenderPreviewBackend->RenderAtomsScene(previewCamera.GetViewProjectionMatrix(), positions, colors, highlightShellSettings);

                    SceneRenderSettings selectedAtomSettings = previewSceneSettings;
                    selectedAtomSettings.drawGrid = false;
                    selectedAtomSettings.atomScale = previewSceneSettings.atomScale * ElementRadiusScale(elementKey) * ResolveElementVisualScale(elementKey);
                    m_RenderPreviewBackend->RenderAtomsScene(previewCamera.GetViewProjectionMatrix(), positions, colors, selectedAtomSettings);
                }

                RenderVolumetricSurfaces(*m_RenderPreviewBackend, previewCamera, previewSceneSettings);
            }
            else
            {
                m_RenderPreviewBackend->RenderDemoScene(previewCamera.GetViewProjectionMatrix(), previewSceneSettings);
            }

            if (m_Show3DCursor)
            {
                const std::vector<glm::vec3> cursorPosition = {m_CursorPosition};
                const std::vector<glm::vec3> cursorColor = {m_CursorColor};
                SceneRenderSettings cursorSettings = previewSceneSettings;
                cursorSettings.drawGrid = false;
                cursorSettings.overrideAtomColor = true;
                cursorSettings.atomOverrideColor = m_CursorColor;
                cursorSettings.atomScale = m_CursorVisualScale;
                cursorSettings.atomBrightness = std::max(1.0f, previewSceneSettings.atomBrightness + 0.2f);
                cursorSettings.atomWireframe = true;
                cursorSettings.atomWireframeWidth = std::max(1.0f, m_SelectionOutlineThickness);
                m_RenderPreviewBackend->RenderAtomsScene(previewCamera.GetViewProjectionMatrix(), cursorPosition, cursorColor, cursorSettings);
            }

            const std::vector<glm::vec3> helperPositions = {m_LightPosition};
            std::vector<glm::vec3> helperColors;
            helperColors.reserve(1);
            const bool lightSelected = (m_SelectedSpecialNode == SpecialNodeSelection::Light);
            helperColors.push_back(lightSelected ? glm::vec3(1.0f, 0.95f, 0.92f) : glm::vec3(1.0f, 0.82f, 0.55f));

            SceneRenderSettings helperSettings = previewSceneSettings;
            helperSettings.drawGrid = false;
            helperSettings.overrideAtomColor = false;
            helperSettings.atomScale = std::max(0.08f, m_CursorVisualScale * 0.85f);
            helperSettings.atomBrightness = std::max(1.1f, previewSceneSettings.atomBrightness + 0.15f);
            helperSettings.atomWireframe = true;
            helperSettings.atomWireframeWidth = 1.7f;
            m_RenderPreviewBackend->RenderAtomsScene(previewCamera.GetViewProjectionMatrix(), helperPositions, helperColors, helperSettings);

            m_RenderPreviewBackend->EndFrame();
        }

        if (hasQueuedRenderRequest)
        {
            const bool saved = SaveCurrentFrameAsImage(
                m_RenderImageRequest.outputPath,
                m_RenderImageRequest.width,
                m_RenderImageRequest.height,
                m_RenderImageRequest.format,
                m_RenderImageRequest.useCrop,
                m_RenderImageRequest.cropRectNormalized);

            if (saved)
            {
                m_LastStructureOperationFailed = false;
                m_LastStructureMessage = "Saved render image: " + m_RenderImageRequest.outputPath;
                LogInfo(m_LastStructureMessage);
            }
            else
            {
                m_LastStructureOperationFailed = true;
                m_LastStructureMessage = "Render image export failed.";
                LogError(m_LastStructureMessage);
            }

            m_RenderImageRequest = RenderImageRequest{};
            m_SceneSettings = savedSceneSettings;
        }
    }


} // namespace ds
