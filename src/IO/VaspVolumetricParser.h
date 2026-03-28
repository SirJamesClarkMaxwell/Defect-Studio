#pragma once

#include "DataModel/VolumetricDataset.h"

#include <string>

namespace ds
{

    class VaspVolumetricParser
    {
    public:
        bool ParseFromFile(const std::string &path, VolumetricDataset &outDataset, std::string &error) const;
    };

} // namespace ds
