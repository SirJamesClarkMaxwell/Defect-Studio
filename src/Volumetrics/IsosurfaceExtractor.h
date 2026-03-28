#pragma once

#include "DataModel/Structure.h"
#include "DataModel/VolumetricDataset.h"

#include <cstddef>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace ds
{

    struct SurfaceTriangleMesh
    {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        glm::vec3 boundsMin = glm::vec3(0.0f);
        glm::vec3 boundsMax = glm::vec3(0.0f);

        std::size_t TriangleCount() const;
        std::size_t MemoryBytes() const;
        void Clear();
    };

    struct IsosurfaceBuildSettings
    {
        float isoValue = 0.0f;
        int maxAxis = 96;
    };

    struct IsosurfaceBuildResult
    {
        SurfaceTriangleMesh mesh;
        glm::ivec3 sampledDimensions = glm::ivec3(0);
        int decimationStep = 1;
    };

    class IsosurfaceExtractor
    {
    public:
        bool BuildPreviewMesh(
            const Structure &structure,
            const ScalarFieldBlock &block,
            const IsosurfaceBuildSettings &settings,
            IsosurfaceBuildResult &outResult,
            std::string &error) const;
    };

} // namespace ds
