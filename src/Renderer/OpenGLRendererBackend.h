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
        void BeginFrame() override;
        void RenderDemoScene(const glm::mat4 &viewProjection) override;
        void RenderAtomsScene(
            const glm::mat4 &viewProjection,
            const std::vector<glm::vec3> &atomPositions,
            const std::vector<glm::vec3> &atomColors,
            float atomScale) override;
        void EndFrame() override;

        std::uint32_t GetColorAttachmentRendererID() const override { return m_ColorTexture; }

    private:
        void CreateFramebuffer(std::uint32_t width, std::uint32_t height);
        void DestroyFramebuffer();

        Shader m_Shader;

        std::uint32_t m_Framebuffer = 0;
        std::uint32_t m_ColorTexture = 0;
        std::uint32_t m_DepthRenderbuffer = 0;

        std::uint32_t m_VAO = 0;
        std::uint32_t m_VBO = 0;
        std::uint32_t m_EBO = 0;
        std::uint32_t m_InstanceVBO = 0;
        std::uint32_t m_InstanceColorVBO = 0;
        std::uint32_t m_IndexCount = 0;

        std::uint32_t m_ViewportWidth = 1;
        std::uint32_t m_ViewportHeight = 1;
    };

} // namespace ds
