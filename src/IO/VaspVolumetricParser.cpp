#include "IO/VaspVolumetricParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>

namespace ds
{

    namespace
    {
        std::string Trim(const std::string &value)
        {
            const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c)
                                                { return std::isspace(c) != 0; });
            if (first == value.end())
            {
                return {};
            }

            const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c)
                                               { return std::isspace(c) != 0; })
                                  .base();
            return std::string(first, last);
        }

        std::vector<std::string> SplitWhitespace(const std::string &line)
        {
            std::vector<std::string> tokens;
            std::istringstream stream(line);
            std::string token;
            while (stream >> token)
            {
                tokens.push_back(token);
            }
            return tokens;
        }

        bool ParseVec3(const std::string &line, glm::vec3 &out)
        {
            std::istringstream stream(line);
            return static_cast<bool>(stream >> out.x >> out.y >> out.z);
        }

        bool IsSelectiveFlagTrue(const std::string &flag)
        {
            if (flag.empty())
            {
                return true;
            }

            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(flag[0])));
            return c == 't' || c == '1' || c == 'y';
        }

        bool ParseStructureHeader(std::istream &stream, const std::string &sourcePath, Structure &outStructure, std::string &error)
        {
            std::string line;

            Structure structure;
            structure.sourcePath = sourcePath;

            if (!std::getline(stream, line))
            {
                error = "Volumetric file is empty";
                return false;
            }
            structure.title = Trim(line);

            if (!std::getline(stream, line))
            {
                error = "Missing scale factor line";
                return false;
            }

            float rawScale = 1.0f;
            {
                std::istringstream scaleStream(line);
                if (!(scaleStream >> rawScale))
                {
                    error = "Invalid scale factor line";
                    return false;
                }
            }

            if (std::abs(rawScale) <= std::numeric_limits<float>::epsilon())
            {
                error = "Scale factor cannot be zero";
                return false;
            }

            for (int i = 0; i < 3; ++i)
            {
                if (!std::getline(stream, line) || !ParseVec3(line, structure.lattice[i]))
                {
                    error = "Invalid lattice vector at line " + std::to_string(3 + i);
                    return false;
                }
            }

            float latticeScaleFactor = rawScale;
            if (rawScale < 0.0f)
            {
                const glm::mat3 unscaledLattice(structure.lattice[0], structure.lattice[1], structure.lattice[2]);
                const float unscaledVolume = std::abs(glm::determinant(unscaledLattice));
                if (unscaledVolume <= std::numeric_limits<float>::epsilon())
                {
                    error = "Lattice volume is zero, cannot apply negative scale factor";
                    return false;
                }

                const float targetVolume = -rawScale;
                latticeScaleFactor = std::cbrt(targetVolume / unscaledVolume);
            }

            for (glm::vec3 &vector : structure.lattice)
            {
                vector *= latticeScaleFactor;
            }

            structure.scale = 1.0f;

            if (!std::getline(stream, line))
            {
                error = "Missing element symbols line";
                return false;
            }
            structure.species = SplitWhitespace(line);
            if (structure.species.empty())
            {
                error = "Element symbols line is empty";
                return false;
            }

            if (!std::getline(stream, line))
            {
                error = "Missing element counts line";
                return false;
            }
            {
                const auto countTokens = SplitWhitespace(line);
                if (countTokens.size() != structure.species.size())
                {
                    error = "Element count does not match symbols count";
                    return false;
                }

                structure.counts.reserve(countTokens.size());
                for (const auto &token : countTokens)
                {
                    try
                    {
                        const int count = std::stoi(token);
                        if (count < 0)
                        {
                            error = "Element count cannot be negative: " + token;
                            return false;
                        }

                        structure.counts.push_back(count);
                    }
                    catch (...)
                    {
                        error = "Invalid element count token: " + token;
                        return false;
                    }
                }
            }

            if (!std::getline(stream, line))
            {
                error = "Missing coordinate mode line";
                return false;
            }

            bool hasSelectiveDynamics = false;
            std::string modeLine = Trim(line);
            if (!modeLine.empty())
            {
                const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(modeLine[0])));
                if (c == 's')
                {
                    hasSelectiveDynamics = true;
                    if (!std::getline(stream, modeLine))
                    {
                        error = "Missing coordinate mode line after Selective Dynamics";
                        return false;
                    }
                    modeLine = Trim(modeLine);
                }
            }

            if (modeLine.empty())
            {
                error = "Coordinate mode line is empty";
                return false;
            }

            {
                const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(modeLine[0])));
                if (c == 'd')
                {
                    structure.coordinateMode = CoordinateMode::Direct;
                }
                else if (c == 'c' || c == 'k')
                {
                    structure.coordinateMode = CoordinateMode::Cartesian;
                }
                else
                {
                    error = "Unsupported coordinate mode: " + modeLine;
                    return false;
                }
            }

            int atomCount = 0;
            for (int value : structure.counts)
            {
                atomCount += value;
            }

            structure.atoms.reserve(static_cast<std::size_t>(atomCount));
            for (std::size_t speciesIndex = 0; speciesIndex < structure.species.size(); ++speciesIndex)
            {
                const int count = structure.counts[speciesIndex];
                for (int i = 0; i < count; ++i)
                {
                    if (!std::getline(stream, line))
                    {
                        error = "Unexpected end of file while reading atoms";
                        return false;
                    }

                    const auto tokens = SplitWhitespace(line);
                    if (tokens.size() < 3)
                    {
                        error = "Atom line has fewer than 3 components";
                        return false;
                    }

                    Atom atom;
                    atom.element = structure.species[speciesIndex];
                    atom.selectiveDynamics = hasSelectiveDynamics;

                    try
                    {
                        atom.position.x = std::stof(tokens[0]);
                        atom.position.y = std::stof(tokens[1]);
                        atom.position.z = std::stof(tokens[2]);
                    }
                    catch (...)
                    {
                        error = "Invalid atom coordinates";
                        return false;
                    }

                    if (structure.coordinateMode == CoordinateMode::Cartesian)
                    {
                        atom.position *= latticeScaleFactor;
                    }

                    if (hasSelectiveDynamics && tokens.size() >= 6)
                    {
                        atom.selectiveFlags[0] = IsSelectiveFlagTrue(tokens[3]);
                        atom.selectiveFlags[1] = IsSelectiveFlagTrue(tokens[4]);
                        atom.selectiveFlags[2] = IsSelectiveFlagTrue(tokens[5]);
                    }

                    structure.atoms.push_back(atom);
                }
            }

            outStructure = std::move(structure);
            return true;
        }

        bool ReadGridDimensions(std::istream &stream, glm::ivec3 &outDimensions)
        {
            int nx = 0;
            int ny = 0;
            int nz = 0;
            if (!(stream >> nx >> ny >> nz))
            {
                return false;
            }

            if (nx <= 0 || ny <= 0 || nz <= 0)
            {
                return false;
            }

            outDimensions = glm::ivec3(nx, ny, nz);
            return true;
        }

        std::size_t SampleCountForGrid(glm::ivec3 dimensions)
        {
            return static_cast<std::size_t>(dimensions.x) *
                   static_cast<std::size_t>(dimensions.y) *
                   static_cast<std::size_t>(dimensions.z);
        }
    } // namespace

    bool VaspVolumetricParser::ParseFromFile(const std::string &path, VolumetricDataset &outDataset, std::string &error) const
    {
        std::ifstream in(path);
        if (!in.is_open())
        {
            error = "Could not open volumetric file: " + path;
            return false;
        }

        VolumetricDataset dataset;
        dataset.sourcePath = path;
        dataset.sourceLabel = std::filesystem::path(path).filename().string();
        dataset.kind = InferVolumetricFileKind(path);

        if (!ParseStructureHeader(in, path, dataset.structure, error))
        {
            return false;
        }

        dataset.title = dataset.structure.title;

        std::size_t blockIndex = 0;
        while (true)
        {
            glm::ivec3 dimensions(0);
            if (!ReadGridDimensions(in, dimensions))
            {
                if (blockIndex == 0)
                {
                    error = "Missing volumetric grid dimensions after structure header";
                    return false;
                }

                in.clear();
                break;
            }

            ScalarFieldBlock block;
            block.label = "Block " + std::to_string(blockIndex + 1);
            block.dimensions = dimensions;

            const std::size_t sampleCount = SampleCountForGrid(dimensions);
            block.samples.resize(sampleCount);

            double sum = 0.0;
            float minValue = std::numeric_limits<float>::max();
            float maxValue = std::numeric_limits<float>::lowest();
            float absMaxValue = 0.0f;

            for (std::size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
            {
                double rawValue = 0.0;
                if (!(in >> rawValue))
                {
                    error = "Unexpected end of volumetric data while reading " + block.label;
                    return false;
                }

                const float value = static_cast<float>(rawValue);
                block.samples[sampleIndex] = value;
                sum += static_cast<double>(value);
                minValue = std::min(minValue, value);
                maxValue = std::max(maxValue, value);
                absMaxValue = std::max(absMaxValue, std::abs(value));
            }

            block.statistics.minValue = minValue;
            block.statistics.maxValue = maxValue;
            block.statistics.meanValue = sampleCount > 0 ? static_cast<float>(sum / static_cast<double>(sampleCount)) : 0.0f;
            block.statistics.absMaxValue = absMaxValue;

            dataset.blocks.push_back(std::move(block));
            ++blockIndex;
        }

        if (dataset.blocks.empty())
        {
            error = "No volumetric blocks were parsed from file";
            return false;
        }

        outDataset = std::move(dataset);
        return true;
    }

} // namespace ds
