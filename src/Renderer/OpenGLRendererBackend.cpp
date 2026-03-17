#include "Renderer/OpenGLRendererBackend.h"

#include "Core/Logger.h"

#include <glad/gl.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace ds
{

    namespace
    {
        struct SphereVertex
        {
            glm::vec3 position;
            glm::vec3 normal;
        };

        void BuildSphereMesh(std::vector<SphereVertex> &outVertices, std::vector<std::uint32_t> &outIndices)
        {
            constexpr int stacks = 20;
            constexpr int slices = 28;

            outVertices.clear();
            outIndices.clear();
            outVertices.reserve(static_cast<std::size_t>((stacks + 1) * (slices + 1)));
            outIndices.reserve(static_cast<std::size_t>(stacks * slices * 6));

            constexpr float pi = 3.1415926535f;

            for (int stack = 0; stack <= stacks; ++stack)
            {
                const float v = static_cast<float>(stack) / static_cast<float>(stacks);
                const float phi = v * pi;
                const float y = std::cos(phi);
                const float ringRadius = std::sin(phi);

                for (int slice = 0; slice <= slices; ++slice)
                {
                    const float u = static_cast<float>(slice) / static_cast<float>(slices);
                    const float theta = u * 2.0f * pi;

                    glm::vec3 position(
                        ringRadius * std::cos(theta),
                        y,
                        ringRadius * std::sin(theta));

                    outVertices.push_back({position, glm::normalize(position)});
                }
            }

            const int stride = slices + 1;
            for (int stack = 0; stack < stacks; ++stack)
            {
                for (int slice = 0; slice < slices; ++slice)
                {
                    const std::uint32_t i0 = static_cast<std::uint32_t>(stack * stride + slice);
                    const std::uint32_t i1 = static_cast<std::uint32_t>(i0 + 1);
                    const std::uint32_t i2 = static_cast<std::uint32_t>(i0 + stride);
                    const std::uint32_t i3 = static_cast<std::uint32_t>(i2 + 1);

                    outIndices.push_back(i0);
                    outIndices.push_back(i2);
                    outIndices.push_back(i1);

                    outIndices.push_back(i1);
                    outIndices.push_back(i2);
                    outIndices.push_back(i3);
                }
            }
        }
    }

    OpenGLRendererBackend::~OpenGLRendererBackend()
    {
        Shutdown();
    }

    bool OpenGLRendererBackend::Initialize()
    {
        if (!m_Shader.LoadFromFiles("assets/shaders/atoms_instanced.vert", "assets/shaders/atoms_instanced.frag"))
        {
            LogError("OpenGL renderer failed to load shader files.");
            return false;
        }

        if (!m_GridShader.LoadFromFiles("assets/shaders/grid_lines.vert", "assets/shaders/grid_lines.frag"))
        {
            LogError("OpenGL renderer failed to load grid shader files.");
            return false;
        }

        glGenVertexArrays(1, &m_VAO);
        glGenBuffers(1, &m_VBO);
        glGenBuffers(1, &m_EBO);
        glGenBuffers(1, &m_InstanceVBO);
        glGenBuffers(1, &m_InstanceColorVBO);
        glGenVertexArrays(1, &m_GridVAO);
        glGenBuffers(1, &m_GridVBO);

        std::vector<SphereVertex> sphereVertices;
        std::vector<std::uint32_t> sphereIndices;
        BuildSphereMesh(sphereVertices, sphereIndices);
        m_IndexCount = static_cast<std::uint32_t>(sphereIndices.size());

        glBindVertexArray(m_VAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(sphereVertices.size() * sizeof(SphereVertex)),
            sphereVertices.data(),
            GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SphereVertex), reinterpret_cast<void *>(offsetof(SphereVertex, position)));

        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(SphereVertex), reinterpret_cast<void *>(offsetof(SphereVertex, normal)));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(sphereIndices.size() * sizeof(std::uint32_t)),
            sphereIndices.data(),
            GL_STATIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
        const std::size_t matrixVectorSize = sizeof(glm::vec4);
        for (int i = 0; i < 4; ++i)
        {
            glEnableVertexAttribArray(1 + i);
            glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), reinterpret_cast<void *>(i * matrixVectorSize));
            glVertexAttribDivisor(1 + i, 1);
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_InstanceColorVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void *>(0));
        glVertexAttribDivisor(5, 1);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        glBindVertexArray(m_GridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void *>(0));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        CreateFramebuffer(m_ViewportWidth, m_ViewportHeight);
        LogInfo("OpenGL renderer backend initialized");
        return true;
    }

    void OpenGLRendererBackend::Shutdown()
    {
        DestroyFramebuffer();

        if (m_VBO != 0)
        {
            glDeleteBuffers(1, &m_VBO);
            m_VBO = 0;
        }

        if (m_EBO != 0)
        {
            glDeleteBuffers(1, &m_EBO);
            m_EBO = 0;
        }

        if (m_InstanceVBO != 0)
        {
            glDeleteBuffers(1, &m_InstanceVBO);
            m_InstanceVBO = 0;
        }

        if (m_InstanceColorVBO != 0)
        {
            glDeleteBuffers(1, &m_InstanceColorVBO);
            m_InstanceColorVBO = 0;
        }

        if (m_VAO != 0)
        {
            glDeleteVertexArrays(1, &m_VAO);
            m_VAO = 0;
        }

        if (m_GridVBO != 0)
        {
            glDeleteBuffers(1, &m_GridVBO);
            m_GridVBO = 0;
        }

        if (m_GridVAO != 0)
        {
            glDeleteVertexArrays(1, &m_GridVAO);
            m_GridVAO = 0;
        }

        m_Shader.Destroy();
        m_GridShader.Destroy();
    }

    void OpenGLRendererBackend::ResizeViewport(std::uint32_t width, std::uint32_t height)
    {
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;

        if (m_ViewportWidth == width && m_ViewportHeight == height)
        {
            return;
        }

        m_ViewportWidth = width;
        m_ViewportHeight = height;
        CreateFramebuffer(m_ViewportWidth, m_ViewportHeight);
    }

    void OpenGLRendererBackend::CreateFramebuffer(std::uint32_t width, std::uint32_t height)
    {
        DestroyFramebuffer();

        glGenFramebuffers(1, &m_Framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);

        glGenTextures(1, &m_ColorTexture);
        glBindTexture(GL_TEXTURE_2D, m_ColorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(width), static_cast<int>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTexture, 0);

        glGenRenderbuffers(1, &m_DepthRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, m_DepthRenderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, static_cast<int>(width), static_cast<int>(height));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthRenderbuffer);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            LogError("OpenGL framebuffer is incomplete.");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLRendererBackend::DestroyFramebuffer()
    {
        if (m_DepthRenderbuffer != 0)
        {
            glDeleteRenderbuffers(1, &m_DepthRenderbuffer);
            m_DepthRenderbuffer = 0;
        }

        if (m_ColorTexture != 0)
        {
            glDeleteTextures(1, &m_ColorTexture);
            m_ColorTexture = 0;
        }

        if (m_Framebuffer != 0)
        {
            glDeleteFramebuffers(1, &m_Framebuffer);
            m_Framebuffer = 0;
        }
    }

    void OpenGLRendererBackend::BeginFrame(const SceneRenderSettings &settings)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glViewport(0, 0, static_cast<int>(m_ViewportWidth), static_cast<int>(m_ViewportHeight));
        glEnable(GL_DEPTH_TEST);
        glClearColor(settings.clearColor.r, settings.clearColor.g, settings.clearColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLRendererBackend::RenderDemoScene(const glm::mat4 &viewProjection, const SceneRenderSettings &settings)
    {
        const glm::vec3 colors[] = {
            glm::vec3(0.44f, 0.76f, 0.97f),
            glm::vec3(0.98f, 0.66f, 0.35f),
            glm::vec3(0.52f, 0.87f, 0.58f)};

        const glm::vec3 positions[] = {
            glm::vec3(-2.2f, 0.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(2.2f, 0.0f, 0.0f)};

        std::vector<glm::vec3> demoPositions;
        std::vector<glm::vec3> demoColors;
        demoPositions.reserve(3);
        demoColors.reserve(3);

        for (int i = 0; i < 3; ++i)
        {
            demoPositions.push_back(positions[i]);
            demoColors.push_back(colors[i]);
        }

        RenderAtomsScene(viewProjection, demoPositions, demoColors, settings);
    }

    void OpenGLRendererBackend::RenderAtomsScene(
        const glm::mat4 &viewProjection,
        const std::vector<glm::vec3> &atomPositions,
        const std::vector<glm::vec3> &atomColors,
        const SceneRenderSettings &settings)
    {
        RenderGrid(viewProjection, settings);

        const std::size_t instanceCount = std::min(atomPositions.size(), atomColors.size());
        if (instanceCount == 0 || m_IndexCount == 0)
        {
            return;
        }

        std::vector<glm::mat4> instanceModels;
        instanceModels.reserve(instanceCount);

        for (std::size_t i = 0; i < instanceCount; ++i)
        {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), atomPositions[i]);
            model = glm::scale(model, glm::vec3(settings.atomScale));
            instanceModels.push_back(model);
        }

        std::vector<glm::vec3> effectiveColors;
        effectiveColors.reserve(instanceCount);
        if (settings.overrideAtomColor)
        {
            const glm::vec3 overrideColor = settings.atomOverrideColor * settings.atomBrightness;
            for (std::size_t i = 0; i < instanceCount; ++i)
            {
                effectiveColors.push_back(overrideColor);
            }
        }
        else
        {
            for (std::size_t i = 0; i < instanceCount; ++i)
            {
                effectiveColors.push_back(atomColors[i] * settings.atomBrightness);
            }
        }

        m_Shader.Bind();
        m_Shader.SetMat4("u_ViewProjection", viewProjection);
        m_Shader.SetFloat3("u_LightDirection", glm::normalize(settings.lightDirection));
        m_Shader.SetFloat3("u_LightFactors", glm::vec3(settings.ambientStrength, settings.diffuseStrength, settings.atomBrightness));
        m_Shader.SetFloat3("u_LightColor", settings.lightColor);
        m_Shader.SetFloat("u_GlowStrength", settings.atomGlowStrength);

        bool wireframeEnabled = false;
        if (settings.atomWireframe)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            float widthRange[2] = {1.0f, 1.0f};
            glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, widthRange);
            const float requestedWidth = std::clamp(settings.atomWireframeWidth, widthRange[0], widthRange[1]);
            glLineWidth(requestedWidth);
            wireframeEnabled = true;
        }

        glBindVertexArray(m_VAO);

        glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVBO);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(instanceModels.size() * sizeof(glm::mat4)),
            instanceModels.data(),
            GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, m_InstanceColorVBO);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(instanceCount * sizeof(glm::vec3)),
            effectiveColors.data(),
            GL_DYNAMIC_DRAW);

        glDrawElementsInstanced(
            GL_TRIANGLES,
            static_cast<GLsizei>(m_IndexCount),
            GL_UNSIGNED_INT,
            nullptr,
            static_cast<GLsizei>(instanceCount));

        glBindVertexArray(0);

        if (wireframeEnabled)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glLineWidth(1.0f);
        }
    }

    void OpenGLRendererBackend::RenderGrid(const glm::mat4 &viewProjection, const SceneRenderSettings &settings)
    {
        if (!settings.drawGrid || settings.gridSpacing <= 0.0f)
        {
            return;
        }

        const int halfExtent = std::clamp(settings.gridHalfExtent, 1, 128);
        const float spacing = settings.gridSpacing;
        const glm::vec3 origin = settings.gridOrigin;

        std::vector<glm::vec3> lineVertices;
        lineVertices.reserve(static_cast<std::size_t>((halfExtent * 2 + 1) * 4));

        const float span = static_cast<float>(halfExtent) * spacing;
        for (int i = -halfExtent; i <= halfExtent; ++i)
        {
            const float p = static_cast<float>(i) * spacing;

            lineVertices.push_back(glm::vec3(origin.x - span, origin.y + p, origin.z));
            lineVertices.push_back(glm::vec3(origin.x + span, origin.y + p, origin.z));

            lineVertices.push_back(glm::vec3(origin.x + p, origin.y - span, origin.z));
            lineVertices.push_back(glm::vec3(origin.x + p, origin.y + span, origin.z));
        }

        if (lineVertices.empty())
        {
            return;
        }

        m_GridShader.Bind();
        m_GridShader.SetMat4("u_ViewProjection", viewProjection);
        m_GridShader.SetFloat3("u_GridColor", settings.gridColor * std::clamp(settings.gridOpacity, 0.0f, 1.0f));

        glBindVertexArray(m_GridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(lineVertices.size() * sizeof(glm::vec3)),
            lineVertices.data(),
            GL_DYNAMIC_DRAW);

        float widthRange[2] = {1.0f, 1.0f};
        glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, widthRange);
        const float requestedLineWidth = std::clamp(settings.gridLineWidth, widthRange[0], widthRange[1]);
        glLineWidth(requestedLineWidth);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertices.size()));

        glBindVertexArray(0);
    }

    void OpenGLRendererBackend::EndFrame()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

} // namespace ds
