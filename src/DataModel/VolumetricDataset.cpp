#include "DataModel/VolumetricDataset.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace ds
{

    namespace
    {
        std::size_t Product(glm::ivec3 dimensions)
        {
            if (dimensions.x <= 0 || dimensions.y <= 0 || dimensions.z <= 0)
            {
                return 0;
            }

            return static_cast<std::size_t>(dimensions.x) *
                   static_cast<std::size_t>(dimensions.y) *
                   static_cast<std::size_t>(dimensions.z);
        }
    } // namespace

    std::size_t ScalarFieldBlock::SampleCount() const
    {
        return Product(dimensions);
    }

    std::size_t ScalarFieldBlock::MemoryBytes() const
    {
        return samples.size() * sizeof(float);
    }

    std::size_t ScalarFieldBlock::EstimatedMemoryBytes() const
    {
        return SampleCount() * sizeof(float);
    }

    int ScalarFieldBlock::SuggestedDecimationStep(int maxAxis) const
    {
        const int clampedMaxAxis = std::max(8, maxAxis);
        const int longestAxis = std::max({dimensions.x, dimensions.y, dimensions.z});
        if (longestAxis <= clampedMaxAxis)
        {
            return 1;
        }

        return std::max(1, static_cast<int>(std::ceil(static_cast<float>(longestAxis) / static_cast<float>(clampedMaxAxis))));
    }

    glm::ivec3 ScalarFieldBlock::DownsampledDimensions(int maxAxis) const
    {
        const int step = SuggestedDecimationStep(maxAxis);
        if (step <= 1)
        {
            return dimensions;
        }

        return glm::ivec3(
            std::max(1, (dimensions.x + step - 1) / step),
            std::max(1, (dimensions.y + step - 1) / step),
            std::max(1, (dimensions.z + step - 1) / step));
    }

    std::size_t ScalarFieldBlock::DownsampledSampleCount(int maxAxis) const
    {
        return Product(DownsampledDimensions(maxAxis));
    }

    std::size_t VolumetricDataset::TotalSampleCount() const
    {
        std::size_t total = 0;
        for (const ScalarFieldBlock &block : blocks)
        {
            total += block.samples.size();
        }
        return total;
    }

    std::size_t VolumetricDataset::TotalMemoryBytes() const
    {
        std::size_t total = 0;
        for (const ScalarFieldBlock &block : blocks)
        {
            total += block.MemoryBytes();
        }
        return total;
    }

    std::size_t VolumetricDataset::TotalEstimatedMemoryBytes() const
    {
        std::size_t total = 0;
        for (const ScalarFieldBlock &block : blocks)
        {
            total += block.EstimatedMemoryBytes();
        }
        return total;
    }

    VolumetricFileKind InferVolumetricFileKind(const std::string &path)
    {
        std::string upper = std::filesystem::path(path).filename().string();
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c)
                       { return static_cast<char>(std::toupper(c)); });

        if (upper.find("PARCHG") != std::string::npos)
        {
            return VolumetricFileKind::Parchg;
        }
        if (upper.find("CHGCAR") != std::string::npos)
        {
            return VolumetricFileKind::Chgcar;
        }
        if (upper.find("CHG") != std::string::npos)
        {
            return VolumetricFileKind::Chg;
        }

        return VolumetricFileKind::Unknown;
    }

    const char *VolumetricFileKindName(VolumetricFileKind kind)
    {
        switch (kind)
        {
        case VolumetricFileKind::Chg:
            return "CHG";
        case VolumetricFileKind::Chgcar:
            return "CHGCAR";
        case VolumetricFileKind::Parchg:
            return "PARCHG";
        default:
            return "Unknown";
        }
    }

    bool VolumetricDatasetHasSpinSemantics(const VolumetricDataset &dataset)
    {
        return dataset.kind == VolumetricFileKind::Parchg && dataset.blocks.size() >= 2;
    }

    const char *VolumetricFieldModeName(VolumetricFieldMode mode)
    {
        switch (mode)
        {
        case VolumetricFieldMode::SelectedBlock:
            return "Selected block";
        case VolumetricFieldMode::TotalDensity:
            return "Total density";
        case VolumetricFieldMode::Magnetization:
            return "Magnetization";
        case VolumetricFieldMode::SpinUp:
            return "Spin up (derived)";
        case VolumetricFieldMode::SpinDown:
            return "Spin down (derived)";
        default:
            return "Unknown";
        }
    }

    const char *VolumetricIsosurfaceModeName(VolumetricIsosurfaceMode mode)
    {
        switch (mode)
        {
        case VolumetricIsosurfaceMode::PositiveOnly:
            return "Positive";
        case VolumetricIsosurfaceMode::NegativeOnly:
            return "Negative";
        case VolumetricIsosurfaceMode::PositiveAndNegative:
            return "Positive and negative";
        default:
            return "Unknown";
        }
    }

    std::string VolumetricBlockDefaultLabel(VolumetricFileKind kind, int blockIndex, int blockCount)
    {
        if (blockIndex < 0)
        {
            return "Block";
        }

        if (kind == VolumetricFileKind::Parchg)
        {
            if (blockIndex == 0)
            {
                return blockCount > 1 ? "Block 1 - Total density (up + down)" : "Total density (up + down)";
            }
            if (blockIndex == 1)
            {
                return "Block 2 - Magnetization (up - down)";
            }
        }

        return "Block " + std::to_string(blockIndex + 1);
    }

} // namespace ds
