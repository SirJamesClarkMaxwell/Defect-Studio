#include "Volumetrics/IsosurfaceExtractor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>

namespace ds
{

    namespace
    {
        constexpr std::array<std::array<int, 4>, 6> kCubeTetrahedra = {
            std::array<int, 4>{0, 5, 1, 6},
            std::array<int, 4>{0, 5, 6, 4},
            std::array<int, 4>{0, 1, 2, 6},
            std::array<int, 4>{0, 2, 3, 6},
            std::array<int, 4>{0, 3, 7, 6},
            std::array<int, 4>{0, 7, 4, 6}};

        constexpr std::array<std::array<int, 3>, 8> kCubeCornerOffsets = {
            std::array<int, 3>{0, 0, 0},
            std::array<int, 3>{1, 0, 0},
            std::array<int, 3>{1, 1, 0},
            std::array<int, 3>{0, 1, 0},
            std::array<int, 3>{0, 0, 1},
            std::array<int, 3>{1, 0, 1},
            std::array<int, 3>{1, 1, 1},
            std::array<int, 3>{0, 1, 1}};

        constexpr std::array<std::array<int, 2>, 6> kTetraEdges = {
            std::array<int, 2>{0, 1},
            std::array<int, 2>{1, 2},
            std::array<int, 2>{2, 0},
            std::array<int, 2>{0, 3},
            std::array<int, 2>{1, 3},
            std::array<int, 2>{2, 3}};

        struct GridVertex
        {
            glm::vec3 position = glm::vec3(0.0f);
            float value = 0.0f;
        };

        struct IntersectionVertex
        {
            glm::vec3 position = glm::vec3(0.0f);
        };

        std::size_t SampleIndex(glm::ivec3 dimensions, int x, int y, int z)
        {
            return static_cast<std::size_t>(z) * static_cast<std::size_t>(dimensions.y) * static_cast<std::size_t>(dimensions.x) +
                   static_cast<std::size_t>(y) * static_cast<std::size_t>(dimensions.x) +
                   static_cast<std::size_t>(x);
        }

        glm::vec3 LerpPosition(const GridVertex &a, const GridVertex &b, float isoValue)
        {
            const float delta = b.value - a.value;
            float t = 0.5f;
            if (std::abs(delta) > 1e-12f)
            {
                t = (isoValue - a.value) / delta;
            }
            t = std::clamp(t, 0.0f, 1.0f);
            return glm::mix(a.position, b.position, t);
        }

        void EmitTriangle(
            SurfaceTriangleMesh &mesh,
            const glm::vec3 &a,
            const glm::vec3 &b,
            const glm::vec3 &c,
            const glm::vec3 &preferredDirection)
        {
            glm::vec3 p0 = a;
            glm::vec3 p1 = b;
            glm::vec3 p2 = c;

            glm::vec3 normal = glm::cross(p1 - p0, p2 - p0);
            const float normalLength = glm::length(normal);
            if (normalLength <= 1e-8f)
            {
                return;
            }

            normal /= normalLength;
            if (glm::dot(preferredDirection, preferredDirection) > 1e-8f && glm::dot(normal, preferredDirection) < 0.0f)
            {
                std::swap(p1, p2);
                normal = -normal;
            }

            mesh.positions.push_back(p0);
            mesh.positions.push_back(p1);
            mesh.positions.push_back(p2);
            mesh.normals.push_back(normal);
            mesh.normals.push_back(normal);
            mesh.normals.push_back(normal);

            if (mesh.positions.size() == 3)
            {
                mesh.boundsMin = glm::min(glm::min(p0, p1), p2);
                mesh.boundsMax = glm::max(glm::max(p0, p1), p2);
            }
            else
            {
                mesh.boundsMin = glm::min(mesh.boundsMin, glm::min(glm::min(p0, p1), p2));
                mesh.boundsMax = glm::max(mesh.boundsMax, glm::max(glm::max(p0, p1), p2));
            }
        }

        void EmitSortedQuad(
            SurfaceTriangleMesh &mesh,
            std::array<glm::vec3, 4> points,
            const glm::vec3 &preferredDirection)
        {
            const glm::vec3 center = 0.25f * (points[0] + points[1] + points[2] + points[3]);

            glm::vec3 seedNormal = glm::cross(points[1] - points[0], points[2] - points[0]);
            if (glm::dot(seedNormal, seedNormal) <= 1e-8f)
            {
                seedNormal = glm::cross(points[1] - points[0], points[3] - points[0]);
            }
            if (glm::dot(seedNormal, seedNormal) <= 1e-8f)
            {
                return;
            }

            seedNormal = glm::normalize(seedNormal);
            glm::vec3 basisX = points[0] - center;
            if (glm::dot(basisX, basisX) <= 1e-8f)
            {
                basisX = points[1] - center;
            }
            if (glm::dot(basisX, basisX) <= 1e-8f)
            {
                return;
            }

            basisX = glm::normalize(basisX);
            const glm::vec3 basisY = glm::normalize(glm::cross(seedNormal, basisX));

            std::array<float, 4> angles = {};
            for (int i = 0; i < 4; ++i)
            {
                const glm::vec3 d = points[i] - center;
                angles[i] = std::atan2(glm::dot(d, basisY), glm::dot(d, basisX));
            }

            std::array<int, 4> order = {0, 1, 2, 3};
            std::sort(order.begin(), order.end(), [&](int lhs, int rhs)
                      { return angles[lhs] < angles[rhs]; });

            const glm::vec3 p0 = points[order[0]];
            const glm::vec3 p1 = points[order[1]];
            const glm::vec3 p2 = points[order[2]];
            const glm::vec3 p3 = points[order[3]];

            EmitTriangle(mesh, p0, p1, p2, preferredDirection);
            EmitTriangle(mesh, p0, p2, p3, preferredDirection);
        }

        void ProcessTetra(
            const std::array<GridVertex, 4> &tetra,
            float isoValue,
            SurfaceTriangleMesh &mesh)
        {
            std::array<IntersectionVertex, 4> intersections = {};
            int intersectionCount = 0;

            glm::vec3 insideCenter(0.0f);
            glm::vec3 outsideCenter(0.0f);
            int insideCount = 0;
            int outsideCount = 0;

            std::array<bool, 4> inside = {};
            for (int i = 0; i < 4; ++i)
            {
                inside[i] = tetra[i].value >= isoValue;
                if (inside[i])
                {
                    insideCenter += tetra[i].position;
                    ++insideCount;
                }
                else
                {
                    outsideCenter += tetra[i].position;
                    ++outsideCount;
                }
            }

            if (insideCount == 0 || insideCount == 4)
            {
                return;
            }

            if (insideCount > 0)
            {
                insideCenter /= static_cast<float>(insideCount);
            }
            if (outsideCount > 0)
            {
                outsideCenter /= static_cast<float>(outsideCount);
            }

            const glm::vec3 preferredDirection =
                (outsideCount > 0 && insideCount > 0) ? glm::normalize(outsideCenter - insideCenter) : glm::vec3(0.0f, 0.0f, 1.0f);

            for (const auto &edge : kTetraEdges)
            {
                const int a = edge[0];
                const int b = edge[1];
                if (inside[a] == inside[b])
                {
                    continue;
                }

                intersections[intersectionCount++].position = LerpPosition(tetra[a], tetra[b], isoValue);
            }

            if (intersectionCount == 3)
            {
                EmitTriangle(
                    mesh,
                    intersections[0].position,
                    intersections[1].position,
                    intersections[2].position,
                    preferredDirection);
            }
            else if (intersectionCount == 4)
            {
                EmitSortedQuad(
                    mesh,
                    {intersections[0].position, intersections[1].position, intersections[2].position, intersections[3].position},
                    preferredDirection);
            }
        }

        float SafeIsoValue(float isoValue)
        {
            if (std::isfinite(isoValue))
            {
                return isoValue;
            }

            return 0.0f;
        }
    } // namespace

    std::size_t SurfaceTriangleMesh::TriangleCount() const
    {
        return positions.size() / 3u;
    }

    std::size_t SurfaceTriangleMesh::MemoryBytes() const
    {
        return positions.size() * sizeof(glm::vec3) + normals.size() * sizeof(glm::vec3);
    }

    void SurfaceTriangleMesh::Clear()
    {
        positions.clear();
        normals.clear();
        boundsMin = glm::vec3(0.0f);
        boundsMax = glm::vec3(0.0f);
    }

    bool IsosurfaceExtractor::BuildPreviewMesh(
        const Structure &structure,
        const ScalarFieldBlock &block,
        const IsosurfaceBuildSettings &settings,
        IsosurfaceBuildResult &outResult,
        std::string &error) const
    {
        outResult = {};
        error.clear();

        const glm::ivec3 dimensions = block.dimensions;
        if (dimensions.x < 2 || dimensions.y < 2 || dimensions.z < 2)
        {
            error = "Scalar field grid is too small for isosurface extraction.";
            return false;
        }

        if (block.samples.size() != block.SampleCount())
        {
            error = "Scalar field sample count does not match declared dimensions.";
            return false;
        }

        const int decimationStep = std::max(1, block.SuggestedDecimationStep(settings.maxAxis));
        const glm::ivec3 sampledDimensions = block.DownsampledDimensions(settings.maxAxis);
        const std::size_t sampledCount =
            static_cast<std::size_t>(sampledDimensions.x) *
            static_cast<std::size_t>(sampledDimensions.y) *
            static_cast<std::size_t>(sampledDimensions.z);

        std::vector<float> sampledValues(sampledCount, 0.0f);
        std::vector<glm::vec3> sampledPositions(sampledCount, glm::vec3(0.0f));

        for (int z = 0; z < sampledDimensions.z; ++z)
        {
            const int sourceZ = std::min(z * decimationStep, dimensions.z - 1);
            const float fz = (dimensions.z > 1) ? static_cast<float>(sourceZ) / static_cast<float>(dimensions.z - 1) : 0.0f;
            for (int y = 0; y < sampledDimensions.y; ++y)
            {
                const int sourceY = std::min(y * decimationStep, dimensions.y - 1);
                const float fy = (dimensions.y > 1) ? static_cast<float>(sourceY) / static_cast<float>(dimensions.y - 1) : 0.0f;
                for (int x = 0; x < sampledDimensions.x; ++x)
                {
                    const int sourceX = std::min(x * decimationStep, dimensions.x - 1);
                    const float fx = (dimensions.x > 1) ? static_cast<float>(sourceX) / static_cast<float>(dimensions.x - 1) : 0.0f;

                    const std::size_t sourceIndex = SampleIndex(dimensions, sourceX, sourceY, sourceZ);
                    const std::size_t sampledIndex = SampleIndex(sampledDimensions, x, y, z);
                    sampledValues[sampledIndex] = block.samples[sourceIndex];
                    sampledPositions[sampledIndex] = structure.DirectToCartesian(glm::vec3(fx, fy, fz));
                }
            }
        }

        SurfaceTriangleMesh mesh;
        const float isoValue = SafeIsoValue(settings.isoValue);

        for (int z = 0; z < sampledDimensions.z - 1; ++z)
        {
            for (int y = 0; y < sampledDimensions.y - 1; ++y)
            {
                for (int x = 0; x < sampledDimensions.x - 1; ++x)
                {
                    std::array<GridVertex, 8> cube = {};
                    for (int corner = 0; corner < 8; ++corner)
                    {
                        const int gx = x + kCubeCornerOffsets[corner][0];
                        const int gy = y + kCubeCornerOffsets[corner][1];
                        const int gz = z + kCubeCornerOffsets[corner][2];
                        const std::size_t index = SampleIndex(sampledDimensions, gx, gy, gz);
                        cube[corner].position = sampledPositions[index];
                        cube[corner].value = sampledValues[index];
                    }

                    for (const auto &tetraIndices : kCubeTetrahedra)
                    {
                        std::array<GridVertex, 4> tetra = {
                            cube[tetraIndices[0]],
                            cube[tetraIndices[1]],
                            cube[tetraIndices[2]],
                            cube[tetraIndices[3]]};
                        ProcessTetra(tetra, isoValue, mesh);
                    }
                }
            }
        }

        outResult.mesh = std::move(mesh);
        outResult.sampledDimensions = sampledDimensions;
        outResult.decimationStep = decimationStep;

        if (outResult.mesh.positions.empty())
        {
            error = "No isosurface triangles were generated for the current iso value.";
        }

        return true;
    }

} // namespace ds
