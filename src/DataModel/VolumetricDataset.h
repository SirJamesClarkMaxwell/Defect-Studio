#pragma once

#include "DataModel/Structure.h"

#include <cstddef>
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

    struct ScalarFieldStatistics
    {
        float minValue = 0.0f;
        float maxValue = 0.0f;
        float meanValue = 0.0f;
        float absMaxValue = 0.0f;
    };

    struct ScalarFieldBlock
    {
        std::string label;
        glm::ivec3 dimensions = glm::ivec3(0);
        std::vector<float> samples;
        ScalarFieldStatistics statistics;

        std::size_t SampleCount() const;
        std::size_t MemoryBytes() const;
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

        std::size_t TotalSampleCount() const;
        std::size_t TotalMemoryBytes() const;
    };

    VolumetricFileKind InferVolumetricFileKind(const std::string &path);
    const char *VolumetricFileKindName(VolumetricFileKind kind);

} // namespace ds
