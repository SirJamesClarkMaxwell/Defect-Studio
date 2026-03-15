#include "Renderer/OpenGLRendererBackend.h"

#include "Core/Logger.h"

#include <glad/gl.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <vector>

namespace ds
{

    namespace
    {
        constexpr float kDemoGeometry[] = {
            // back
            -1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            // front
            -1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            // left
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            // right
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            // bottom
            -1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            // top
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
        };
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

        glGenVertexArrays(1, &m_VAO);
        glGenBuffers(1, &m_VBO);
        glGenBuffers(1, &m_InstanceVBO);
        glGenBuffers(1, &m_InstanceColorVBO);

        glBindVertexArray(m_VAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kDemoGeometry), kDemoGeometry, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void *>(0));

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

        m_Shader.Destroy();
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

    void OpenGLRendererBackend::BeginFrame()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glViewport(0, 0, static_cast<int>(m_ViewportWidth), static_cast<int>(m_ViewportHeight));
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLRendererBackend::RenderDemoScene(const glm::mat4 &viewProjection)
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

        RenderAtomsScene(viewProjection, demoPositions, demoColors, 0.6f);
    }

    void OpenGLRendererBackend::RenderAtomsScene(
        const glm::mat4 &viewProjection,
        const std::vector<glm::vec3> &atomPositions,
        const std::vector<glm::vec3> &atomColors,
        float atomScale)
    {
        const std::size_t instanceCount = std::min(atomPositions.size(), atomColors.size());
        if (instanceCount == 0)
        {
            return;
        }

        std::vector<glm::mat4> instanceModels;
        instanceModels.reserve(instanceCount);

        for (std::size_t i = 0; i < instanceCount; ++i)
        {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), atomPositions[i]);
            model = glm::scale(model, glm::vec3(atomScale));
            instanceModels.push_back(model);
        }

        m_Shader.Bind();
        m_Shader.SetMat4("u_ViewProjection", viewProjection);

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
            atomColors.data(),
            GL_DYNAMIC_DRAW);

        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(instanceCount));

        glBindVertexArray(0);
    }

    void OpenGLRendererBackend::EndFrame()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

} // namespace ds
