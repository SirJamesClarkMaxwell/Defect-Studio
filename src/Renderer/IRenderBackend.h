#pragma once

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace ds
{

    class IRenderBackend
    {
    public:
        virtual ~IRenderBackend() = default;

        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;

        virtual void ResizeViewport(std::uint32_t width, std::uint32_t height) = 0;
        virtual void BeginFrame() = 0;
        virtual void RenderDemoScene(const glm::mat4 &viewProjection) = 0;
        virtual void RenderAtomsScene(
            const glm::mat4 &viewProjection,
            const std::vector<glm::vec3> &atomPositions,
            const std::vector<glm::vec3> &atomColors,
            float atomScale) = 0;
        virtual void EndFrame() = 0;

        virtual std::uint32_t GetColorAttachmentRendererID() const = 0;
    };

} // namespace ds
