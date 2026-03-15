#pragma once

#include "DataModel/Structure.h"

#include <string>

namespace ds
{

    class PoscarParser
    {
    public:
        bool ParseFromFile(const std::string &path, Structure &outStructure, std::string &error) const;
        bool ParseFromString(const std::string &content, const std::string &sourcePath, Structure &outStructure, std::string &error) const;
    };

} // namespace ds
