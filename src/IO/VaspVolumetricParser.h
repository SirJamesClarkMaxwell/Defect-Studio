#pragma once

#include "DataModel/VolumetricDataset.h"

#include <string>

namespace ds
{

    class VaspVolumetricParser
    {
    public:
        bool ParseStructureFromFile(const std::string &path, Structure &outStructure, std::string &error) const;
        bool ParsePreviewDatasetFromFile(const std::string &path, VolumetricDataset &outDataset, std::string &error) const;
        bool ParseMetadataFromFile(const std::string &path, VolumetricDataset &outDataset, std::string &error) const;
        bool LoadBlockSamplesFromFile(const std::string &path, const ScalarFieldBlock &blockMetadata, ScalarFieldBlock &outBlock, std::string &error) const;
        bool ParseFromFile(const std::string &path, VolumetricDataset &outDataset, std::string &error) const;
    };

} // namespace ds
