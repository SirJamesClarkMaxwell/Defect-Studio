#include "IO/PoscarSerializer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace ds
{

    namespace
    {

        int ClampPrecision(int precision)
        {
            if (precision < 1)
            {
                return 1;
            }
            if (precision > 16)
            {
                return 16;
            }
            return precision;
        }

        std::string FormatScalarFixed(double value, int width, int precision)
        {
            std::ostringstream out;
            out << std::fixed << std::setw(width) << std::setprecision(precision) << value;
            return out.str();
        }

        std::string FormatVec3Fixed(const glm::vec3 &v, int width, int precision)
        {
            std::ostringstream out;
            out << std::fixed << std::setprecision(precision)
                << std::setw(width) << v.x
                << std::setw(width) << v.y
                << std::setw(width) << v.z;
            return out.str();
        }

        bool BuildSpeciesOrder(const Structure &structure, std::vector<std::string> &outSpecies, std::vector<int> &outCounts)
        {
            struct SpeciesEntry
            {
                std::string name;
                int count = 0;
                std::size_t firstAtomIndex = 0;
            };

            outSpecies.clear();
            outCounts.clear();

            if (structure.atoms.empty())
            {
                return true;
            }

            std::vector<SpeciesEntry> entries;
            entries.reserve(structure.atoms.size());
            std::unordered_map<std::string, std::size_t> entryIndexByName;
            entryIndexByName.reserve(structure.atoms.size());

            for (std::size_t atomIndex = 0; atomIndex < structure.atoms.size(); ++atomIndex)
            {
                const std::string &element = structure.atoms[atomIndex].element;
                const auto existing = entryIndexByName.find(element);
                if (existing == entryIndexByName.end())
                {
                    const std::size_t entryIndex = entries.size();
                    entries.push_back({element, 1, atomIndex});
                    entryIndexByName.emplace(element, entryIndex);
                }
                else
                {
                    ++entries[existing->second].count;
                }
            }

            const bool preserveImportedOrder = !structure.sourcePath.empty() && !structure.species.empty();
            if (preserveImportedOrder)
            {
                std::unordered_map<std::string, bool> emitted;
                emitted.reserve(entries.size());
                for (const std::string &speciesName : structure.species)
                {
                    const auto entryIt = entryIndexByName.find(speciesName);
                    if (entryIt == entryIndexByName.end() || emitted[speciesName])
                    {
                        continue;
                    }

                    const SpeciesEntry &entry = entries[entryIt->second];
                    outSpecies.push_back(entry.name);
                    outCounts.push_back(entry.count);
                    emitted[speciesName] = true;
                }

                std::vector<SpeciesEntry> appendedEntries;
                appendedEntries.reserve(entries.size());
                for (const SpeciesEntry &entry : entries)
                {
                    if (!emitted[entry.name])
                    {
                        appendedEntries.push_back(entry);
                    }
                }

                std::stable_sort(appendedEntries.begin(), appendedEntries.end(), [](const SpeciesEntry &left, const SpeciesEntry &right)
                {
                    if (left.count != right.count)
                    {
                        return left.count > right.count;
                    }

                    return left.firstAtomIndex < right.firstAtomIndex;
                });

                for (const SpeciesEntry &entry : appendedEntries)
                {
                    outSpecies.push_back(entry.name);
                    outCounts.push_back(entry.count);
                }

                return !outSpecies.empty();
            }

            std::stable_sort(entries.begin(), entries.end(), [](const SpeciesEntry &left, const SpeciesEntry &right)
            {
                if (left.count != right.count)
                {
                    return left.count > right.count;
                }

                return left.firstAtomIndex < right.firstAtomIndex;
            });

            outSpecies.reserve(entries.size());
            outCounts.reserve(entries.size());
            for (const SpeciesEntry &entry : entries)
            {
                outSpecies.push_back(entry.name);
                outCounts.push_back(entry.count);
            }

            return !outSpecies.empty();
        }

        float WrapUnitCoordinate(float value)
        {
            float wrapped = static_cast<float>(value - std::floor(value));
            if (wrapped < 0.0f)
            {
                wrapped += 1.0f;
            }
            if (wrapped >= 1.0f)
            {
                wrapped = 0.0f;
            }
            return wrapped;
        }

        void CanonicalizeDirectStructureTranslation(Structure &structure)
        {
            if (structure.atoms.empty())
            {
                return;
            }

            glm::vec3 boundsMin(std::numeric_limits<float>::max());
            glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
            for (const Atom &atom : structure.atoms)
            {
                boundsMin = glm::min(boundsMin, atom.position);
                boundsMax = glm::max(boundsMax, atom.position);
            }

            const glm::vec3 boundsCenter = 0.5f * (boundsMin + boundsMax);
            const glm::vec3 translationToCenter = boundsCenter - glm::vec3(0.5f);
            if (glm::dot(translationToCenter, translationToCenter) <= 1e-10f)
            {
                return;
            }

            for (Atom &atom : structure.atoms)
            {
                atom.position -= translationToCenter;
            }
        }

    } // namespace

    bool PoscarSerializer::WriteToFile(const Structure &structure, const std::string &path, const PoscarWriteOptions &options, std::string &error) const
    {
        std::string text;
        if (!WriteToString(structure, options, text, error))
        {
            return false;
        }

        const std::filesystem::path outputPath(path);
        const std::filesystem::path parent = outputPath.parent_path();
        if (!parent.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                error = "Failed to create output directory: " + parent.string();
                return false;
            }
        }

        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open())
        {
            error = "Failed to open output file: " + path;
            return false;
        }

        out << text;
        if (!out.good())
        {
            error = "Failed to write output file: " + path;
            return false;
        }

        return true;
    }

    bool PoscarSerializer::WriteToString(const Structure &structure, const PoscarWriteOptions &options, std::string &outText, std::string &error) const
    {
        Structure writable = structure;

        std::string conversionError;
        if (!writable.ConvertAtomsTo(options.coordinateMode, conversionError))
        {
            error = conversionError;
            return false;
        }

        if (options.coordinateMode == CoordinateMode::Direct)
        {
            if (options.canonicalizeDirectTranslation)
            {
                CanonicalizeDirectStructureTranslation(writable);
            }

            if (options.wrapDirectCoordinates)
            {
                for (Atom &atom : writable.atoms)
                {
                    atom.position.x = WrapUnitCoordinate(atom.position.x);
                    atom.position.y = WrapUnitCoordinate(atom.position.y);
                    atom.position.z = WrapUnitCoordinate(atom.position.z);
                }
            }
        }

        std::vector<std::string> species;
        std::vector<int> counts;
        if (!BuildSpeciesOrder(writable, species, counts))
        {
            error = "Failed to build species list for export.";
            return false;
        }

        const bool hasSelectiveDynamics = options.forceSelectiveDynamics || writable.HasSelectiveDynamics();
        const int coordinatePrecision = ClampPrecision(options.precision);
        constexpr int kScalePrecision = 14;
        constexpr int kLatticeWidth = 23;
        constexpr int kCoordinateWidth = 20;

        std::ostringstream stream;

        const std::string title = writable.title.empty() ? "Generated by DefectsStudio" : writable.title;
        stream << title << '\n';
        stream << FormatScalarFixed(1.0, 19, kScalePrecision) << '\n';

        for (int i = 0; i < 3; ++i)
        {
            stream << FormatVec3Fixed(writable.lattice[i], kLatticeWidth, coordinatePrecision) << '\n';
        }

        if (!species.empty())
        {
            stream << "   ";
            for (std::size_t i = 0; i < species.size(); ++i)
            {
                if (i > 0)
                {
                    stream << ' ';
                }
                stream << species[i];
            }
            stream << '\n';

            stream << "   ";
            for (std::size_t i = 0; i < counts.size(); ++i)
            {
                if (i > 0)
                {
                    stream << ' ';
                }
                stream << counts[i];
            }
            stream << '\n';
        }
        else
        {
            stream << "X\n";
            stream << "0\n";
        }

        if (hasSelectiveDynamics)
        {
            stream << "Selective Dynamics\n";
        }

        stream << (options.coordinateMode == CoordinateMode::Direct ? "Direct\n" : "Cartesian\n");

        std::vector<std::size_t> writtenIndices;
        writtenIndices.reserve(writable.atoms.size());

        for (const std::string &speciesName : species)
        {
            for (std::size_t atomIndex = 0; atomIndex < writable.atoms.size(); ++atomIndex)
            {
                const Atom &atom = writable.atoms[atomIndex];
                if (atom.element != speciesName)
                {
                    continue;
                }

                writtenIndices.push_back(atomIndex);
                stream << FormatVec3Fixed(atom.position, kCoordinateWidth, coordinatePrecision);

                if (hasSelectiveDynamics)
                {
                    const bool sx = atom.selectiveDynamics ? atom.selectiveFlags[0] : true;
                    const bool sy = atom.selectiveDynamics ? atom.selectiveFlags[1] : true;
                    const bool sz = atom.selectiveDynamics ? atom.selectiveFlags[2] : true;
                    stream << ' ' << (sx ? 'T' : 'F') << ' ' << (sy ? 'T' : 'F') << ' ' << (sz ? 'T' : 'F');
                }

                stream << '\n';
            }
        }

        if (writtenIndices.size() != writable.atoms.size())
        {
            error = "Species list does not cover all atoms; cannot export.";
            return false;
        }

        outText = stream.str();
        return true;
    }

} // namespace ds
