#include "DataModel/Structure.h"
#include "IO/PoscarSerializer.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::vector<std::string> SplitLines(const std::string &text)
    {
        std::vector<std::string> lines;
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line))
        {
            lines.push_back(line);
        }
        return lines;
    }
}

TEST(PoscarSerializerTest, UsesRequestedCoordinatePrecision)
{
    ds::Structure structure;
    structure.title = "Precision test";
    structure.coordinateMode = ds::CoordinateMode::Direct;
    structure.atoms.push_back(ds::Atom{"H", glm::vec3(0.1234567f, 0.5000000f, 0.2500000f)});
    structure.RebuildSpeciesFromAtoms();

    ds::PoscarWriteOptions options;
    options.coordinateMode = ds::CoordinateMode::Direct;
    options.precision = 3;
    options.canonicalizeDirectTranslation = false;

    ds::PoscarSerializer serializer;
    std::string output;
    std::string error;
    ASSERT_TRUE(serializer.WriteToString(structure, options, output, error)) << error;

    const std::vector<std::string> lines = SplitLines(output);
    ASSERT_GE(lines.size(), 8u);

    // Coordinates should obey requested precision instead of always printing 16 decimals.
    EXPECT_NE(lines[7].find("0.123"), std::string::npos);
    EXPECT_EQ(lines[7].find("0.123456"), std::string::npos);
}

TEST(PoscarSerializerTest, SortsSpeciesByCountDescendingWhenDerivedFromAtoms)
{
    ds::Structure structure;
    structure.title = "Species sort test";
    structure.coordinateMode = ds::CoordinateMode::Direct;
    structure.atoms.push_back(ds::Atom{"H", glm::vec3(0.1f, 0.1f, 0.1f)});
    structure.atoms.push_back(ds::Atom{"C", glm::vec3(0.2f, 0.2f, 0.2f)});
    structure.atoms.push_back(ds::Atom{"C", glm::vec3(0.3f, 0.3f, 0.3f)});
    structure.atoms.push_back(ds::Atom{"C", glm::vec3(0.4f, 0.4f, 0.4f)});
    structure.species.clear();
    structure.counts.clear();

    ds::PoscarWriteOptions options;
    options.coordinateMode = ds::CoordinateMode::Direct;
    options.precision = 4;
    options.canonicalizeDirectTranslation = false;

    ds::PoscarSerializer serializer;
    std::string output;
    std::string error;
    ASSERT_TRUE(serializer.WriteToString(structure, options, output, error)) << error;

    const std::vector<std::string> lines = SplitLines(output);
    ASSERT_GE(lines.size(), 7u);

    // Species/count rows should be sorted by descending count: C (3) then H (1).
    EXPECT_NE(lines[5].find("C H"), std::string::npos) << lines[5];
    EXPECT_NE(lines[6].find("3 1"), std::string::npos) << lines[6];
}
