#include "Renderer/OpenGLRendererBackend.h"

#include "Core/Logger.h"

#include <glad/gl.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/vec3.hpp>

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

        glBindVertexArray(m_VAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kDemoGeometry), kDemoGeometry, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void *>(0));

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
        m_Shader.Bind();
        m_Shader.SetMat4("u_ViewProjection", viewProjection);

        glBindVertexArray(m_VAO);

        const glm::vec3 colors[] = {
            glm::vec3(0.44f, 0.76f, 0.97f),
            glm::vec3(0.98f, 0.66f, 0.35f),
            glm::vec3(0.52f, 0.87f, 0.58f)};

        const glm::vec3 positions[] = {
            glm::vec3(-2.2f, 0.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(2.2f, 0.0f, 0.0f)};

        for (int i = 0; i < 3; ++i)
        {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), positions[i]);
            model = glm::scale(model, glm::vec3(0.6f));

            m_Shader.SetMat4("u_Model", model);
            m_Shader.SetFloat3("u_AtomColor", colors[i]);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        glBindVertexArray(0);
    }

    void OpenGLRendererBackend::EndFrame()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

} // namespace ds
