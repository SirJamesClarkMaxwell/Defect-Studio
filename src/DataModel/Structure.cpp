#include "DataModel/Structure.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include <glm/gtc/matrix_inverse.hpp>

namespace ds
{

    int Structure::GetAtomCount() const
    {
        return static_cast<int>(atoms.size());
    }

    bool Structure::HasSelectiveDynamics() const
    {
        for (const Atom &atom : atoms)
        {
            if (atom.selectiveDynamics)
            {
                return true;
            }
        }

        return false;
    }

    glm::mat3 Structure::LatticeMatrix() const
    {
        return glm::mat3(lattice[0], lattice[1], lattice[2]);
    }

    glm::vec3 Structure::DirectToCartesian(const glm::vec3 &directPosition) const
    {
        return LatticeMatrix() * directPosition;
    }

    glm::vec3 Structure::CartesianToDirect(const glm::vec3 &cartesianPosition) const
    {
        const glm::mat3 latticeMatrix = LatticeMatrix();
        const float determinant = glm::determinant(latticeMatrix);
        if (std::abs(determinant) <= std::numeric_limits<float>::epsilon())
        {
            return glm::vec3(0.0f);
        }

        return glm::inverse(latticeMatrix) * cartesianPosition;
    }

    bool Structure::ConvertAtomsTo(CoordinateMode targetMode, std::string &error)
    {
        if (coordinateMode == targetMode)
        {
            return true;
        }

        const glm::mat3 latticeMatrix = LatticeMatrix();
        const float determinant = glm::determinant(latticeMatrix);
        if (std::abs(determinant) <= std::numeric_limits<float>::epsilon())
        {
            error = "Cannot convert coordinates because lattice matrix is singular.";
            return false;
        }

        if (targetMode == CoordinateMode::Cartesian)
        {
            for (Atom &atom : atoms)
            {
                atom.position = latticeMatrix * atom.position;
            }
        }
        else
        {
            const glm::mat3 inverseLattice = glm::inverse(latticeMatrix);
            for (Atom &atom : atoms)
            {
                atom.position = inverseLattice * atom.position;
            }
        }

        coordinateMode = targetMode;
        return true;
    }

    void Structure::RebuildSpeciesFromAtoms()
    {
        species.clear();
        counts.clear();

        std::unordered_map<std::string, std::size_t> speciesIndexByName;
        speciesIndexByName.reserve(atoms.size());

        for (const Atom &atom : atoms)
        {
            const auto it = speciesIndexByName.find(atom.element);
            if (it == speciesIndexByName.end())
            {
                const std::size_t index = species.size();
                species.push_back(atom.element);
                counts.push_back(1);
                speciesIndexByName.emplace(atom.element, index);
            }
            else
            {
                counts[it->second] += 1;
            }
        }
    }

} // namespace ds
