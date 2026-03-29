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

        float ResolveDefaultIsoFactor(VolumetricFieldMode fieldMode, VolumetricIsosurfaceMode isosurfaceMode, bool secondarySurface)
        {
            switch (fieldMode)
            {
            case VolumetricFieldMode::Magnetization:
                return (isosurfaceMode == VolumetricIsosurfaceMode::PositiveAndNegative) ? 0.0575f : 0.0600f;
            case VolumetricFieldMode::SpinUp:
            case VolumetricFieldMode::SpinDown:
                return 0.0850f;
            case VolumetricFieldMode::TotalDensity:
                return secondarySurface ? 0.18f : 0.12f;
            case VolumetricFieldMode::SelectedBlock:
            default:
                return secondarySurface ? 0.22f : 0.15f;
            }
        }

        bool FieldModeRequiresTwoBlocks(VolumetricFieldMode fieldMode)
        {
            return fieldMode == VolumetricFieldMode::SpinUp || fieldMode == VolumetricFieldMode::SpinDown;
        }

        std::optional<int> ResolveSemanticBlockIndex(const VolumetricDataset &dataset, VolumetricFieldMode fieldMode)
        {
            if (dataset.blocks.empty())
            {
                return std::nullopt;
            }

            switch (fieldMode)
            {
            case VolumetricFieldMode::TotalDensity:
                return 0;
            case VolumetricFieldMode::Magnetization:
                if (VolumetricDatasetHasSpinSemantics(dataset))
                {
                    return 1;
                }
                return std::nullopt;
            default:
                return std::nullopt;
            }
        }

        ScalarFieldStatistics ComputeStatisticsFromSamples(const std::vector<float> &samples)
        {
            ScalarFieldStatistics stats;
            if (samples.empty())
            {
                return stats;
            }

            float minValue = samples.front();
            float maxValue = samples.front();
            double sum = 0.0;
            float absMaxValue = std::abs(samples.front());
            for (float value : samples)
            {
                minValue = std::min(minValue, value);
                maxValue = std::max(maxValue, value);
                absMaxValue = std::max(absMaxValue, std::abs(value));
                sum += static_cast<double>(value);
            }

            stats.minValue = minValue;
            stats.maxValue = maxValue;
            stats.meanValue = static_cast<float>(sum / static_cast<double>(samples.size()));
            stats.absMaxValue = absMaxValue;
            stats.valid = true;
            return stats;
        }

        bool BuildDerivedSpinFieldBlock(const ScalarFieldBlock &totalDensity,
                                        const ScalarFieldBlock &magnetization,
                                        bool spinUp,
                                        ScalarFieldBlock &outBlock,
                                        std::string &error)
        {
            if (totalDensity.dimensions != magnetization.dimensions)
            {
                error = "Total-density and magnetization grids do not match.";
                return false;
            }
            if (totalDensity.samples.size() != magnetization.samples.size())
            {
                error = "Total-density and magnetization sample counts do not match.";
                return false;
            }

            outBlock = totalDensity;
            outBlock.label = spinUp ? "Spin up (derived)" : "Spin down (derived)";
            outBlock.samples.resize(totalDensity.samples.size());
            const float sign = spinUp ? 1.0f : -1.0f;
            for (std::size_t i = 0; i < totalDensity.samples.size(); ++i)
            {
                outBlock.samples[i] = 0.5f * (totalDensity.samples[i] + sign * magnetization.samples[i]);
            }
            outBlock.statistics = ComputeStatisticsFromSamples(outBlock.samples);
            outBlock.samplesLoaded = true;
            outBlock.loadFailed = false;
            outBlock.failedLoadAttempts = 0;
            outBlock.lastLoadError.clear();
            return true;
        }

        void TrackSurfaceMeshAllocation(const SurfaceTriangleMesh &mesh)
        {
            if (!mesh.positions.empty())
            {
                DS_PROFILE_ALLOC_N(mesh.positions.data(), mesh.positions.size() * sizeof(glm::vec3), "VolumetricSurfacePositions");
            }
            if (!mesh.normals.empty())
            {
                DS_PROFILE_ALLOC_N(mesh.normals.data(), mesh.normals.size() * sizeof(glm::vec3), "VolumetricSurfaceNormals");
            }
        }

        void TrackSurfaceMeshRelease(const SurfaceTriangleMesh &mesh)
        {
            if (!mesh.positions.empty())
            {
                DS_PROFILE_FREE_N(mesh.positions.data(), "VolumetricSurfacePositions");
            }
            if (!mesh.normals.empty())
            {
                DS_PROFILE_FREE_N(mesh.normals.data(), "VolumetricSurfaceNormals");
            }
        }
    } // namespace

    void EditorLayer::MarkVolumetricMeshesDirty()
    {
        auto resetSurface = [](VolumetricSurfaceState &surface)
        {
            TrackSurfaceMeshRelease(surface.mesh);
            TrackSurfaceMeshRelease(surface.negativeMesh);
            surface.dirty = true;
            surface.buildQueued = false;
            surface.hasMesh = false;
            surface.hasNegativeMesh = false;
            surface.sampledDimensions = glm::ivec3(0);
            surface.decimationStep = 1;
            surface.lastBuildMilliseconds = 0.0;
            surface.lastStatus.clear();
            surface.mesh.Clear();
            surface.negativeMesh.Clear();
            surface.pendingBuildRequestId = 0;
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
        m_PrimaryVolumetricSurface.color = glm::vec3(1.0f, 0.92f, 0.14f);
        m_PrimaryVolumetricSurface.negativeColor = glm::vec3(0.10f, 0.22f, 0.96f);
        m_PrimaryVolumetricSurface.opacity = 0.90f;
        m_PrimaryVolumetricSurface.negativeOpacity = 0.90f;
        m_SecondaryVolumetricSurface.color = glm::vec3(0.10f, 0.22f, 0.96f);
        m_SecondaryVolumetricSurface.negativeColor = glm::vec3(1.0f, 0.92f, 0.14f);
        m_SecondaryVolumetricSurface.opacity = 0.90f;
        m_SecondaryVolumetricSurface.negativeOpacity = 0.90f;

        if (VolumetricDatasetHasSpinSemantics(dataset))
        {
            m_PrimaryVolumetricSurface.blockIndex = 1;
            m_PrimaryVolumetricSurface.fieldMode = VolumetricFieldMode::Magnetization;
            m_PrimaryVolumetricSurface.isosurfaceMode = VolumetricIsosurfaceMode::PositiveOnly;
            m_PrimaryVolumetricSurface.isoValue =
                (dataset.blocks[1].statistics.valid) ? ComputeDefaultIsoValue(dataset.blocks[1], ResolveDefaultIsoFactor(m_PrimaryVolumetricSurface.fieldMode, m_PrimaryVolumetricSurface.isosurfaceMode, false)) : 0.0f;

            m_SecondaryVolumetricSurface.enabled = true;
            m_SecondaryVolumetricSurface.blockIndex = 1;
            m_SecondaryVolumetricSurface.fieldMode = VolumetricFieldMode::Magnetization;
            m_SecondaryVolumetricSurface.isosurfaceMode = VolumetricIsosurfaceMode::NegativeOnly;
            m_SecondaryVolumetricSurface.isoValue =
                (dataset.blocks[1].statistics.valid) ? ComputeDefaultIsoValue(dataset.blocks[1], ResolveDefaultIsoFactor(m_SecondaryVolumetricSurface.fieldMode, m_SecondaryVolumetricSurface.isosurfaceMode, true)) : 0.0f;
        }
        else
        {
            m_PrimaryVolumetricSurface.blockIndex = 0;
            m_PrimaryVolumetricSurface.fieldMode = VolumetricFieldMode::SelectedBlock;
            m_PrimaryVolumetricSurface.isosurfaceMode = VolumetricIsosurfaceMode::PositiveOnly;
            m_PrimaryVolumetricSurface.isoValue =
                (dataset.blocks.empty() || !dataset.blocks.front().statistics.valid) ? 0.0f :
                                                                                    ComputeDefaultIsoValue(dataset.blocks.front(), ResolveDefaultIsoFactor(m_PrimaryVolumetricSurface.fieldMode, m_PrimaryVolumetricSurface.isosurfaceMode, false));

            m_SecondaryVolumetricSurface.enabled = dataset.blocks.size() > 1;
            m_SecondaryVolumetricSurface.blockIndex = dataset.blocks.size() > 1 ? 1 : 0;
            m_SecondaryVolumetricSurface.fieldMode = VolumetricFieldMode::SelectedBlock;
            m_SecondaryVolumetricSurface.isosurfaceMode = VolumetricIsosurfaceMode::PositiveOnly;
            const ScalarFieldBlock &secondaryBlock = dataset.blocks[static_cast<std::size_t>(m_SecondaryVolumetricSurface.blockIndex)];
            m_SecondaryVolumetricSurface.isoValue =
                (secondaryBlock.statistics.valid) ? ComputeDefaultIsoValue(secondaryBlock, ResolveDefaultIsoFactor(m_SecondaryVolumetricSurface.fieldMode, m_SecondaryVolumetricSurface.isosurfaceMode, true)) : 0.0f;
        }
        MarkVolumetricMeshesDirty();
    }

    void EditorLayer::PumpVolumetricSurfaceBuildJobs()
    {
        auto pendingIt = m_PendingVolumetricSurfaceBuilds.begin();
        while (pendingIt != m_PendingVolumetricSurfaceBuilds.end())
        {
            if (pendingIt->future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            {
                ++pendingIt;
                continue;
            }

            VolumetricSurfaceBuildResult result = pendingIt->future.get();
            pendingIt = m_PendingVolumetricSurfaceBuilds.erase(pendingIt);

            VolumetricSurfaceState *surfaceState = nullptr;
            if (result.surfaceSlot == 0)
            {
                surfaceState = &m_PrimaryVolumetricSurface;
            }
            else if (result.surfaceSlot == 1)
            {
                surfaceState = &m_SecondaryVolumetricSurface;
            }

            if (surfaceState == nullptr)
            {
                continue;
            }

            if (result.generation != m_VolumetricLoadGeneration || result.requestId != surfaceState->pendingBuildRequestId)
            {
                continue;
            }

            surfaceState->buildQueued = false;
            surfaceState->pendingBuildRequestId = 0;
            surfaceState->lastBuildMilliseconds = result.wallMilliseconds;
            surfaceState->sampledDimensions = result.sampledDimensions;
            surfaceState->decimationStep = result.decimationStep;

            if (!result.success)
            {
                surfaceState->hasMesh = false;
                surfaceState->lastStatus = result.error.empty() ? "Surface build failed." : result.error;
                continue;
            }

            TrackSurfaceMeshRelease(surfaceState->mesh);
            TrackSurfaceMeshRelease(surfaceState->negativeMesh);
            surfaceState->mesh = std::move(result.mesh);
            surfaceState->negativeMesh = std::move(result.negativeMesh);
            TrackSurfaceMeshAllocation(surfaceState->mesh);
            TrackSurfaceMeshAllocation(surfaceState->negativeMesh);
            surfaceState->hasMesh = !surfaceState->mesh.positions.empty();
            surfaceState->hasNegativeMesh = !surfaceState->negativeMesh.positions.empty();
            ++surfaceState->meshRevision;
            if (surfaceState->hasNegativeMesh)
            {
                ++surfaceState->negativeMeshRevision;
            }

            if (surfaceState->hasMesh || surfaceState->hasNegativeMesh)
            {
                std::ostringstream message;
                message << "Preview mesh: ";
                if (surfaceState->hasMesh)
                {
                    message << surfaceState->mesh.TriangleCount() << " +";
                }
                else
                {
                    message << "0 +";
                }
                message << ' ' << (surfaceState->hasNegativeMesh ? surfaceState->negativeMesh.TriangleCount() : 0) << " triangles";
                surfaceState->lastStatus = message.str();
            }
            else
            {
                surfaceState->lastStatus = "No surface for current iso value.";
            }
        }
    }

    bool EditorLayer::QueueVolumetricSurfaceBuild(int surfaceSlot)
    {
        VolumetricSurfaceState *surfaceState = nullptr;
        if (surfaceSlot == 0)
        {
            surfaceState = &m_PrimaryVolumetricSurface;
        }
        else if (surfaceSlot == 1)
        {
            surfaceState = &m_SecondaryVolumetricSurface;
        }

        if (surfaceState == nullptr || surfaceState->buildQueued)
        {
            return false;
        }

        EnsureVolumetricSelection();
        if (!surfaceState->enabled ||
            m_ActiveVolumetricDatasetIndex < 0 ||
            m_ActiveVolumetricDatasetIndex >= static_cast<int>(m_VolumetricDatasets.size()))
        {
            return false;
        }

        const VolumetricDataset &dataset = m_VolumetricDatasets[static_cast<std::size_t>(m_ActiveVolumetricDatasetIndex)];
        if (dataset.blocks.empty())
        {
            return false;
        }

        surfaceState->blockIndex = std::clamp(surfaceState->blockIndex, 0, static_cast<int>(dataset.blocks.size()) - 1);
        std::vector<int> requiredBlocks;
        requiredBlocks.push_back(surfaceState->blockIndex);

        if (surfaceState->fieldMode != VolumetricFieldMode::SelectedBlock)
        {
            const std::optional<int> semanticIndex = ResolveSemanticBlockIndex(dataset, surfaceState->fieldMode);
            if (semanticIndex.has_value())
            {
                requiredBlocks = {semanticIndex.value()};
            }
            else if (FieldModeRequiresTwoBlocks(surfaceState->fieldMode))
            {
                if (!VolumetricDatasetHasSpinSemantics(dataset))
                {
                    surfaceState->lastStatus = "Current dataset does not provide spin-resolved volumetric blocks.";
                    return false;
                }
                requiredBlocks = {0, 1};
            }
            else
            {
                surfaceState->lastStatus = "Selected volumetric field is not available in this dataset.";
                return false;
            }
        }

        for (int requiredBlockIndex : requiredBlocks)
        {
            if (requiredBlockIndex < 0 || requiredBlockIndex >= static_cast<int>(dataset.blocks.size()))
            {
                surfaceState->lastStatus = "Selected volumetric field references a missing block.";
                return false;
            }
            if (!dataset.blocks[static_cast<std::size_t>(requiredBlockIndex)].samplesLoaded)
            {
                return false;
            }
        }

        const std::uint64_t requestId = m_VolumetricSurfaceBuildRequestCounter++;
        const std::uint64_t generation = m_VolumetricLoadGeneration;
        const float isoMagnitude = std::max(std::abs(surfaceState->isoValue), 1e-7f);
        const int previewMaxDimension = m_VolumetricPreviewMaxDimension;
        const int blockIndex = surfaceState->blockIndex;
        const std::string datasetPath = dataset.sourcePath;
        const VolumetricFieldMode fieldMode = surfaceState->fieldMode;
        const VolumetricIsosurfaceMode isosurfaceMode = surfaceState->isosurfaceMode;

        surfaceState->dirty = false;
        surfaceState->buildQueued = true;
        surfaceState->pendingBuildRequestId = requestId;
        surfaceState->lastStatus = "Building preview mesh...";

        ScalarFieldBlock selectedBlockCopy = dataset.blocks[static_cast<std::size_t>(requiredBlocks.front())];
        std::optional<ScalarFieldBlock> secondarySourceBlock;
        if (requiredBlocks.size() > 1)
        {
            secondarySourceBlock = dataset.blocks[static_cast<std::size_t>(requiredBlocks[1])];
        }

        PendingVolumetricSurfaceBuild pendingBuild;
        pendingBuild.surfaceSlot = surfaceSlot;
        pendingBuild.requestId = requestId;
        pendingBuild.future = m_BackgroundThreadPool.submit_task(
            [generation,
             surfaceSlot,
             requestId,
             datasetPath,
             blockIndex,
             fieldMode,
             isosurfaceMode,
             structureCopy = Structure(dataset.structure),
             selectedBlockCopy = std::move(selectedBlockCopy),
             secondarySourceBlock = std::move(secondarySourceBlock),
             isoMagnitude,
             previewMaxDimension]() mutable
            {
                VolumetricSurfaceBuildResult buildResult;
                buildResult.generation = generation;
                buildResult.surfaceSlot = surfaceSlot;
                buildResult.requestId = requestId;
                buildResult.datasetPath = datasetPath;
                buildResult.blockIndex = blockIndex;

                IsosurfaceExtractor extractor;
                std::string error;
                const auto startedAt = std::chrono::steady_clock::now();
                ScalarFieldBlock fieldBlock = selectedBlockCopy;
                if (fieldMode == VolumetricFieldMode::SpinUp || fieldMode == VolumetricFieldMode::SpinDown)
                {
                    if (!secondarySourceBlock.has_value())
                    {
                        buildResult.error = "Spin-resolved field requires two loaded blocks.";
                        buildResult.success = false;
                        return buildResult;
                    }

                    if (!BuildDerivedSpinFieldBlock(selectedBlockCopy, secondarySourceBlock.value(), fieldMode == VolumetricFieldMode::SpinUp, fieldBlock, error))
                    {
                        buildResult.error = error;
                        buildResult.success = false;
                        return buildResult;
                    }
                }

                auto buildMeshForIso = [&](float isoLevel, SurfaceTriangleMesh &outMesh) -> bool
                {
                    IsosurfaceBuildResult rawResult;
                    std::string buildError;
                    const bool success = extractor.BuildPreviewMesh(
                        structureCopy,
                        fieldBlock,
                        IsosurfaceBuildSettings{isoLevel, previewMaxDimension},
                        rawResult,
                        buildError);
                    if (!success)
                    {
                        error = buildError.empty() ? "Surface build failed." : buildError;
                        return false;
                    }

                    buildResult.sampledDimensions = rawResult.sampledDimensions;
                    buildResult.decimationStep = rawResult.decimationStep;
                    outMesh = std::move(rawResult.mesh);
                    return true;
                };

                buildResult.success = true;
                if (isosurfaceMode == VolumetricIsosurfaceMode::PositiveOnly ||
                    isosurfaceMode == VolumetricIsosurfaceMode::PositiveAndNegative)
                {
                    buildResult.success = buildMeshForIso(isoMagnitude, buildResult.mesh);
                }
                if (buildResult.success &&
                    (isosurfaceMode == VolumetricIsosurfaceMode::NegativeOnly ||
                     isosurfaceMode == VolumetricIsosurfaceMode::PositiveAndNegative))
                {
                    SurfaceTriangleMesh negativeMesh;
                    buildResult.success = buildMeshForIso(-isoMagnitude, negativeMesh);
                    if (buildResult.success)
                    {
                        if (isosurfaceMode == VolumetricIsosurfaceMode::NegativeOnly)
                        {
                            buildResult.mesh = std::move(negativeMesh);
                        }
                        else
                        {
                            buildResult.negativeMesh = std::move(negativeMesh);
                        }
                    }
                }
                const auto finishedAt = std::chrono::steady_clock::now();
                buildResult.wallMilliseconds = std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();
                if (!buildResult.success)
                {
                    buildResult.error = error.empty() ? "Surface build failed." : error;
                }
                return buildResult;
            });
        m_PendingVolumetricSurfaceBuilds.push_back(std::move(pendingBuild));
        return true;
    }

    bool EditorLayer::RebuildVolumetricSurfaceMesh(VolumetricSurfaceState &surfaceState)
    {
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
        std::vector<int> requiredBlocks;
        requiredBlocks.push_back(surfaceState.blockIndex);

        if (surfaceState.fieldMode != VolumetricFieldMode::SelectedBlock)
        {
            const std::optional<int> semanticIndex = ResolveSemanticBlockIndex(dataset, surfaceState.fieldMode);
            if (semanticIndex.has_value())
            {
                requiredBlocks = {semanticIndex.value()};
            }
            else if (FieldModeRequiresTwoBlocks(surfaceState.fieldMode))
            {
                if (!VolumetricDatasetHasSpinSemantics(dataset))
                {
                    surfaceState.lastStatus = "Current dataset does not provide spin-resolved volumetric blocks.";
                    return false;
                }
                requiredBlocks = {0, 1};
            }
            else
            {
                surfaceState.lastStatus = "Selected volumetric field is not available in this dataset.";
                return false;
            }
        }

        for (int requiredBlockIndex : requiredBlocks)
        {
            const ScalarFieldBlock &block = dataset.blocks[static_cast<std::size_t>(requiredBlockIndex)];
            if (block.samplesLoaded)
            {
                continue;
            }

            if (block.loadFailed)
            {
                surfaceState.lastStatus = block.lastLoadError.empty()
                                              ? "Block load failed. Use manual retry."
                                              : ("Block load failed: " + block.lastLoadError);
            }
            else
            {
                surfaceState.lastStatus = HasPendingVolumetricBlockLoad(dataset.sourcePath, requiredBlockIndex)
                                              ? "Loading volumetric block samples..."
                                              : "Block samples not resident yet.";
                QueueVolumetricBlockLoad(m_ActiveVolumetricDatasetIndex, requiredBlockIndex);
            }
            return false;
        }

        const int surfaceSlot = (&surfaceState == &m_PrimaryVolumetricSurface) ? 0 : 1;
        return QueueVolumetricSurfaceBuild(surfaceSlot);
    }

    void EditorLayer::EnsureVolumetricSurfaceMeshes()
    {
        SyncVolumetricSurfaceDefaults();
        if (m_ActiveVolumetricDatasetIndex < 0 || m_ActiveVolumetricDatasetIndex >= static_cast<int>(m_VolumetricDatasets.size()))
        {
            return;
        }

        VolumetricDataset &dataset = m_VolumetricDatasets[static_cast<std::size_t>(m_ActiveVolumetricDatasetIndex)];
        auto ensureSurfaceBlockLoaded = [&](VolumetricSurfaceState &surface)
        {
            if (!surface.enabled || dataset.blocks.empty())
            {
                return;
            }
            surface.blockIndex = std::clamp(surface.blockIndex, 0, static_cast<int>(dataset.blocks.size()) - 1);
            std::vector<int> requiredBlocks;
            requiredBlocks.push_back(surface.blockIndex);

            if (surface.fieldMode != VolumetricFieldMode::SelectedBlock)
            {
                const std::optional<int> semanticIndex = ResolveSemanticBlockIndex(dataset, surface.fieldMode);
                if (semanticIndex.has_value())
                {
                    requiredBlocks = {semanticIndex.value()};
                    surface.blockIndex = semanticIndex.value();
                }
                else if (FieldModeRequiresTwoBlocks(surface.fieldMode))
                {
                    if (!VolumetricDatasetHasSpinSemantics(dataset))
                    {
                        surface.lastStatus = "Current dataset does not provide spin-resolved volumetric blocks.";
                        return;
                    }
                    requiredBlocks = {0, 1};
                }
                else
                {
                    surface.lastStatus = "Selected volumetric field is not available in this dataset.";
                    return;
                }
            }

            for (int requiredBlockIndex : requiredBlocks)
            {
                ScalarFieldBlock &block = dataset.blocks[static_cast<std::size_t>(requiredBlockIndex)];
                if (block.samplesLoaded)
                {
                    continue;
                }

                if (block.loadFailed)
                {
                    surface.lastStatus = block.lastLoadError.empty()
                                             ? "Block load failed. Manual retry required."
                                             : ("Block load failed: " + block.lastLoadError);
                }
                else
                {
                    QueueVolumetricBlockLoad(m_ActiveVolumetricDatasetIndex, requiredBlockIndex);
                    surface.lastStatus = HasPendingVolumetricBlockLoad(dataset.sourcePath, requiredBlockIndex)
                                             ? "Queued block load..."
                                             : "Waiting for block load...";
                }
                return;
            }

            const auto resolveStatsBlock = [&]() -> const ScalarFieldBlock *
            {
                if (surface.fieldMode == VolumetricFieldMode::SelectedBlock)
                {
                    return &dataset.blocks[static_cast<std::size_t>(surface.blockIndex)];
                }

                const std::optional<int> semanticIndex = ResolveSemanticBlockIndex(dataset, surface.fieldMode);
                if (semanticIndex.has_value())
                {
                    return &dataset.blocks[static_cast<std::size_t>(semanticIndex.value())];
                }

                if (FieldModeRequiresTwoBlocks(surface.fieldMode) && !dataset.blocks.empty())
                {
                    return &dataset.blocks.front();
                }

                return nullptr;
            };

            const ScalarFieldBlock *statsBlock = resolveStatsBlock();
            if (statsBlock != nullptr && statsBlock->statistics.valid && std::abs(surface.isoValue) <= 1e-6f)
            {
                const bool isSecondarySurface = (&surface == &m_SecondaryVolumetricSurface);
                surface.isoValue = ComputeDefaultIsoValue(*statsBlock, ResolveDefaultIsoFactor(surface.fieldMode, surface.isosurfaceMode, isSecondarySurface));
                surface.dirty = true;
            }
        };
        ensureSurfaceBlockLoaded(m_PrimaryVolumetricSurface);
        ensureSurfaceBlockLoaded(m_SecondaryVolumetricSurface);

        if (m_PrimaryVolumetricSurface.dirty && !m_PrimaryVolumetricSurface.buildQueued)
        {
            RebuildVolumetricSurfaceMesh(m_PrimaryVolumetricSurface);
        }

        if (m_SecondaryVolumetricSurface.dirty && !m_SecondaryVolumetricSurface.buildQueued)
        {
            RebuildVolumetricSurfaceMesh(m_SecondaryVolumetricSurface);
        }
    }

    void EditorLayer::RenderVolumetricSurfaces(IRenderBackend &backend, const OrbitCamera &camera, const SceneRenderSettings &settings)
    {
        const glm::mat4 viewProjection = camera.GetViewProjectionMatrix();
        auto renderSurfaceWithId = [&](const VolumetricSurfaceState &surfaceState, std::uint64_t surfaceId)
        {
            if (!surfaceState.enabled)
            {
                return;
            }

            if (surfaceState.hasMesh && !surfaceState.mesh.positions.empty())
            {
                const glm::vec3 renderColor =
                    (surfaceState.isosurfaceMode == VolumetricIsosurfaceMode::NegativeOnly) ? surfaceState.negativeColor : surfaceState.color;
                const float renderOpacity =
                    (surfaceState.isosurfaceMode == VolumetricIsosurfaceMode::NegativeOnly) ? surfaceState.negativeOpacity : surfaceState.opacity;

                backend.RenderSurfaceMesh(
                    viewProjection,
                    surfaceState.mesh.positions,
                    surfaceState.mesh.normals,
                    surfaceId,
                    surfaceState.meshRevision,
                    camera.GetPosition(),
                    renderColor,
                    m_VolumetricSpecularColor,
                    m_VolumetricShininess,
                    renderOpacity,
                    settings);
            }

            if (surfaceState.isosurfaceMode == VolumetricIsosurfaceMode::PositiveAndNegative &&
                surfaceState.hasNegativeMesh &&
                !surfaceState.negativeMesh.positions.empty())
            {
                backend.RenderSurfaceMesh(
                    viewProjection,
                    surfaceState.negativeMesh.positions,
                    surfaceState.negativeMesh.normals,
                    surfaceId + 100,
                    surfaceState.negativeMeshRevision,
                    camera.GetPosition(),
                    surfaceState.negativeColor,
                    m_VolumetricSpecularColor,
                    m_VolumetricShininess,
                    surfaceState.negativeOpacity,
                    settings);
            }
        };

        renderSurfaceWithId(m_PrimaryVolumetricSurface, 1);
        renderSurfaceWithId(m_SecondaryVolumetricSurface, 2);
        DS_PROFILE_PLOT("Volumetrics/PreviewMeshBytes",
                        static_cast<double>(m_PrimaryVolumetricSurface.mesh.MemoryBytes() +
                                            m_PrimaryVolumetricSurface.negativeMesh.MemoryBytes() +
                                            m_SecondaryVolumetricSurface.mesh.MemoryBytes() +
                                            m_SecondaryVolumetricSurface.negativeMesh.MemoryBytes()));
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
        PumpVolumetricLoadingJobs();
        PumpVolumetricSurfaceBuildJobs();
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
