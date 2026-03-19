#pragma once

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace ds
{

    struct SceneRenderSettings
    {
        glm::vec3 clearColor = glm::vec3(0.14f, 0.16f, 0.20f);

        bool drawGrid = true;
        bool drawCellEdges = false;
        int gridHalfExtent = 12;
        float gridSpacing = 1.0f;
        float gridLineWidth = 1.0f;
        glm::vec3 gridOrigin = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 gridColor = glm::vec3(0.36f, 0.39f, 0.45f);
        float gridOpacity = 0.50f;
        glm::vec3 cellEdgeColor = glm::vec3(0.78f, 0.86f, 0.94f);
        float cellEdgeLineWidth = 1.8f;

        glm::vec3 lightDirection = glm::vec3(-0.5f, -1.0f, -0.4f);
        glm::vec3 lightColor = glm::vec3(1.0f, 0.98f, 0.92f);
        float ambientStrength = 0.45f;
        float diffuseStrength = 0.85f;

        float atomScale = 0.30f;
        bool overrideAtomColor = false;
        glm::vec3 atomOverrideColor = glm::vec3(0.90f, 0.65f, 0.35f);
        float atomBrightness = 1.0f;
        float atomGlowStrength = 0.08f;
        bool atomWireframe = false;
        float atomWireframeWidth = 1.0f;
    };

    class IRenderBackend
    {
    public:
        virtual ~IRenderBackend() = default;

        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;

        virtual void ResizeViewport(std::uint32_t width, std::uint32_t height) = 0;
        virtual void BeginFrame(const SceneRenderSettings &settings) = 0;
        virtual void RenderDemoScene(const glm::mat4 &viewProjection, const SceneRenderSettings &settings) = 0;
        virtual void RenderAtomsScene(
            const glm::mat4 &viewProjection,
            const std::vector<glm::vec3> &atomPositions,
            const std::vector<glm::vec3> &atomColors,
            const SceneRenderSettings &settings) = 0;
        virtual void RenderLineSegments(
            const glm::mat4 &viewProjection,
            const std::vector<glm::vec3> &lineVertices,
            const glm::vec3 &lineColor,
            float lineWidth) = 0;
        virtual void EndFrame() = 0;

        virtual std::uint32_t GetColorAttachmentRendererID() const = 0;
        virtual bool ReadColorAttachmentPixels(
            std::uint32_t &outWidth,
            std::uint32_t &outHeight,
            std::vector<std::uint8_t> &outRgbaPixels) const = 0;
    };

} // namespace ds
