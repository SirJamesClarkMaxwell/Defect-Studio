#pragma once

#include "Renderer/IRenderBackend.h"
#include "Renderer/Shader.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace ds
{

    class OpenGLRendererBackend : public IRenderBackend
    {
    public:
        OpenGLRendererBackend() = default;
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
        void RenderLineSegments(
            const glm::mat4 &viewProjection,
            const std::vector<glm::vec3> &lineVertices,
            const glm::vec3 &lineColor,
            float lineWidth) override;
        void EndFrame() override;

        std::uint32_t GetColorAttachmentRendererID() const override { return m_ColorTexture; }
        bool ReadColorAttachmentPixels(
            std::uint32_t &outWidth,
            std::uint32_t &outHeight,
            std::vector<std::uint8_t> &outRgbaPixels) const override;

    private:
        void RenderGrid(const glm::mat4 &viewProjection, const SceneRenderSettings &settings);
        void CreateFramebuffer(std::uint32_t width, std::uint32_t height);
        void DestroyFramebuffer();

        Shader m_Shader;
        Shader m_GridShader;

        std::uint32_t m_Framebuffer = 0;
        std::uint32_t m_ColorTexture = 0;
        std::uint32_t m_DepthRenderbuffer = 0;

        std::uint32_t m_VAO = 0;
        std::uint32_t m_VBO = 0;
        std::uint32_t m_EBO = 0;
        std::uint32_t m_InstanceVBO = 0;
        std::uint32_t m_InstanceColorVBO = 0;
        std::uint32_t m_IndexCount = 0;

        std::uint32_t m_GridVAO = 0;
        std::uint32_t m_GridVBO = 0;

        std::uint32_t m_ViewportWidth = 1;
        std::uint32_t m_ViewportHeight = 1;
    };

} // namespace ds
