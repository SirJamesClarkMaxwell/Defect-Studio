#pragma once

#include "Renderer/IRenderBackend.h"
#include "Renderer/Shader.h"

#include <cstdint>
#include <chrono>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

namespace ds
{

    class OpenGLRendererBackend : public IRenderBackend
    {
    public:
        explicit OpenGLRendererBackend(int msaaSamples = 4);
        ~OpenGLRendererBackend() override;

        bool Initialize() override;
        void Shutdown() override;

        void ResizeViewport(std::uint32_t width, std::uint32_t height) override;
        void BeginFrame(const SceneRenderSettings &settings) override;
        void RenderDemoScene(const glm::mat4 &viewProjection, const SceneRenderSettings &settings) override;
        void RenderAtomsScene(
            const glm::mat4 &viewProjection,
            const std::vector<glm::vec3> &atomPositions,
            const std::vector<glm::vec3> &atomColors,
            const SceneRenderSettings &settings) override;
        void RenderCylinderInstances(
            const glm::mat4 &viewProjection,
            const std::vector<glm::mat4> &instanceModels,
            const std::vector<glm::vec3> &instanceColors,
            const SceneRenderSettings &settings) override;
        void RenderLineSegments(
            const glm::mat4 &viewProjection,
            const std::vector<glm::vec3> &lineVertices,
            const glm::vec3 &lineColor,
            float lineWidth) override;
        void RenderColoredLineSegments(
            const glm::mat4 &viewProjection,
            const std::vector<glm::vec3> &lineVertices,
            const std::vector<glm::vec3> &lineColors,
            float lineWidth) override;
        void RenderSurfaceMesh(
            const glm::mat4 &viewProjection,
            const std::vector<glm::vec3> &positions,
            const std::vector<glm::vec3> &normals,
            std::uint64_t meshId,
            std::uint64_t meshRevision,
            const glm::vec3 &cameraPosition,
            const glm::vec3 &surfaceColor,
            const glm::vec3 &surfaceSpecularColor,
            float surfaceShininess,
            float surfaceOpacity,
            const SceneRenderSettings &settings) override;
        void EndFrame() override;

        std::uint32_t GetColorAttachmentRendererID() const override { return m_ColorTexture; }
        bool ReadColorAttachmentPixels(
            std::uint32_t &outWidth,
            std::uint32_t &outHeight,
            std::vector<std::uint8_t> &outRgbaPixels) const override;

    private:
        struct MeshBuffers
        {
            std::uint32_t vao = 0;
            std::uint32_t vbo = 0;
            std::uint32_t ebo = 0;
            std::uint32_t indexCount = 0;
        };
        struct SurfaceMeshCacheEntry
        {
            std::uint32_t vao = 0;
            std::uint32_t vbo = 0;
            std::size_t vertexCount = 0;
            std::size_t capacityBytes = 0;
            std::uint64_t uploadedRevision = 0;
        };

        void RenderGrid(const glm::mat4 &viewProjection, const SceneRenderSettings &settings);
        void RenderInstancedMesh(
            const MeshBuffers &mesh,
            const glm::mat4 &viewProjection,
            const std::vector<glm::mat4> &instanceModels,
            const std::vector<glm::vec3> &instanceColors,
            const SceneRenderSettings &settings);
        void CreateFramebuffer(std::uint32_t width, std::uint32_t height);
        void DestroyFramebuffer();
        void DestroyMesh(MeshBuffers &mesh);
        SurfaceMeshCacheEntry &GetOrCreateSurfaceMeshCache(std::uint64_t meshId);
        void DestroySurfaceMeshCache();

        Shader m_Shader;
        Shader m_GridShader;
        Shader m_SurfaceShader;

        std::uint32_t m_Framebuffer = 0;
        std::uint32_t m_ColorRenderbuffer = 0;
        std::uint32_t m_ColorTexture = 0;
        std::uint32_t m_DepthRenderbuffer = 0;
        std::uint32_t m_ResolveFramebuffer = 0;

        MeshBuffers m_SphereMesh;
        MeshBuffers m_CylinderMesh;
        std::uint32_t m_InstanceVBO = 0;
        std::uint32_t m_InstanceColorVBO = 0;

        std::uint32_t m_GridVAO = 0;
        std::uint32_t m_GridVBO = 0;
        std::uint32_t m_ColoredLineVAO = 0;
        std::uint32_t m_ColoredLineVBO = 0;
        std::unordered_map<std::uint64_t, SurfaceMeshCacheEntry> m_SurfaceMeshCache;

        std::uint32_t m_ViewportWidth = 1;
        std::uint32_t m_ViewportHeight = 1;
        int m_MsaaSamples = 4;
        std::uint64_t m_FrameIndex = 0;
        double m_FrameLineSegmentCpuMilliseconds = 0.0;
        std::size_t m_FrameLineSegmentDrawCalls = 0;
        std::size_t m_FrameLineSegmentVertexCount = 0;
        double m_FrameGridCpuMilliseconds = 0.0;
        std::size_t m_FrameGridDrawCalls = 0;
        std::size_t m_FrameGridVertexCount = 0;
        std::chrono::steady_clock::time_point m_LastLineProfilingLogTime{};
    };

} // namespace ds
