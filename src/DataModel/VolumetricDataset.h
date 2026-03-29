#pragma once

#include "DataModel/Structure.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace ds
{

    enum class VolumetricFileKind
    {
        Unknown = 0,
        Chg = 1,
        Chgcar = 2,
        Parchg = 3
    };

    enum class VolumetricFieldMode
    {
        SelectedBlock = 0,
        TotalDensity = 1,
        Magnetization = 2,
        SpinUp = 3,
        SpinDown = 4
    };

    enum class VolumetricIsosurfaceMode
    {
        PositiveOnly = 0,
        NegativeOnly = 1,
        PositiveAndNegative = 2
    };

    struct ScalarFieldStatistics
    {
        float minValue = 0.0f;
        float maxValue = 0.0f;
        float meanValue = 0.0f;
        float absMaxValue = 0.0f;
        bool valid = false;
    };

    struct ScalarFieldBlock
    {
        std::string label;
        glm::ivec3 dimensions = glm::ivec3(0);
        std::uint64_t dataOffsetBytes = 0;
        std::vector<float> samples;
        ScalarFieldStatistics statistics;
        bool samplesLoaded = false;
        bool loadFailed = false;
        int failedLoadAttempts = 0;
        double lastLoadMilliseconds = 0.0;
        std::string lastLoadError;

        std::size_t SampleCount() const;
        std::size_t MemoryBytes() const;
        std::size_t EstimatedMemoryBytes() const;
        int SuggestedDecimationStep(int maxAxis) const;
        glm::ivec3 DownsampledDimensions(int maxAxis) const;
        std::size_t DownsampledSampleCount(int maxAxis) const;
    };

    struct VolumetricDataset
    {
        std::string sourcePath;
        std::string title;
        std::string sourceLabel;
        Structure structure;
        VolumetricFileKind kind = VolumetricFileKind::Unknown;
        std::vector<ScalarFieldBlock> blocks;
        double metadataParseMilliseconds = 0.0;

        std::size_t TotalSampleCount() const;
        std::size_t TotalMemoryBytes() const;
        std::size_t TotalEstimatedMemoryBytes() const;
    };

    VolumetricFileKind InferVolumetricFileKind(const std::string &path);
    const char *VolumetricFileKindName(VolumetricFileKind kind);
    bool VolumetricDatasetHasSpinSemantics(const VolumetricDataset &dataset);
    const char *VolumetricFieldModeName(VolumetricFieldMode mode);
    const char *VolumetricIsosurfaceModeName(VolumetricIsosurfaceMode mode);
    std::string VolumetricBlockDefaultLabel(VolumetricFileKind kind, int blockIndex, int blockCount);

} // namespace ds
