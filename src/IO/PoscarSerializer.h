#pragma once

#include "DataModel/Structure.h"

#include <string>

namespace ds
{

    struct PoscarWriteOptions
    {
        CoordinateMode coordinateMode = CoordinateMode::Direct;
        int precision = 8;
        bool forceSelectiveDynamics = false;
    };

    class PoscarSerializer
    {
    public:
        bool WriteToFile(const Structure &structure, const std::string &path, const PoscarWriteOptions &options, std::string &error) const;
        bool WriteToString(const Structure &structure, const PoscarWriteOptions &options, std::string &outText, std::string &error) const;
    };

} // namespace ds
