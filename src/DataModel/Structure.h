#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

namespace ds
{

    enum class CoordinateMode
    {
        Direct,
        Cartesian
    };

    struct Atom
    {
        std::string element;
        glm::vec3 position = glm::vec3(0.0f);
        bool selectiveDynamics = false;
        std::array<bool, 3> selectiveFlags = {true, true, true};
    };

    struct Structure
    {
        std::string sourcePath;
        std::string title;

        float scale = 1.0f;
        std::array<glm::vec3, 3> lattice = {
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f)};

        std::vector<std::string> species;
        std::vector<int> counts;

        CoordinateMode coordinateMode = CoordinateMode::Direct;
        std::vector<Atom> atoms;

        int GetAtomCount() const;
        bool HasSelectiveDynamics() const;

        glm::mat3 LatticeMatrix() const;
        glm::vec3 DirectToCartesian(const glm::vec3 &directPosition) const;
        glm::vec3 CartesianToDirect(const glm::vec3 &cartesianPosition) const;

        bool ConvertAtomsTo(CoordinateMode targetMode, std::string &error);
        void RebuildSpeciesFromAtoms();
    };

} // namespace ds
