#include "Renderer/OpenGLRendererBackend.h"

#include "Core/Logger.h"
#include "Core/Profiling.h"

#include <glad/gl.h>
#if defined(DS_ENABLE_TRACY)
#include <tracy/TracyOpenGL.hpp>
#endif

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
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

        struct SurfaceVertex
        {
            glm::vec3 position;
            glm::vec3 normal;
        };

        void ConfigureInstanceAttributes(std::uint32_t vao, std::uint32_t instanceVbo, std::uint32_t colorVbo)
        {
            glBindVertexArray(vao);

            glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
            const std::size_t matrixVectorSize = sizeof(glm::vec4);
            for (int i = 0; i < 4; ++i)
            {
                glEnableVertexAttribArray(1 + i);
                glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), reinterpret_cast<void *>(i * matrixVectorSize));
                glVertexAttribDivisor(1 + i, 1);
            }

            glBindBuffer(GL_ARRAY_BUFFER, colorVbo);
            glEnableVertexAttribArray(5);
            glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void *>(0));
            glVertexAttribDivisor(5, 1);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        }

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

        void BuildCylinderMesh(std::vector<SphereVertex> &outVertices, std::vector<std::uint32_t> &outIndices)
        {
            constexpr int radialSegments = 32;
            constexpr float halfHeight = 0.5f;
            constexpr float pi = 3.1415926535f;

            outVertices.clear();
            outIndices.clear();
            outVertices.reserve(static_cast<std::size_t>(radialSegments + 1) * 2);
            outIndices.reserve(static_cast<std::size_t>(radialSegments) * 12);

            for (int i = 0; i <= radialSegments; ++i)
            {
                const float u = static_cast<float>(i) / static_cast<float>(radialSegments);
                const float theta = u * 2.0f * pi;
                const float x = std::cos(theta);
                const float z = std::sin(theta);
                const glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
                outVertices.push_back({glm::vec3(x, -halfHeight, z), normal});
                outVertices.push_back({glm::vec3(x, halfHeight, z), normal});
            }

            for (int i = 0; i < radialSegments; ++i)
            {
                const std::uint32_t base = static_cast<std::uint32_t>(i * 2);
                outIndices.push_back(base + 0);
                outIndices.push_back(base + 1);
                outIndices.push_back(base + 2);

                outIndices.push_back(base + 1);
                outIndices.push_back(base + 3);
                outIndices.push_back(base + 2);
            }
        }
    }

    OpenGLRendererBackend::OpenGLRendererBackend(int msaaSamples)
        : m_MsaaSamples(std::max(1, msaaSamples))
    {
    }

    OpenGLRendererBackend::~OpenGLRendererBackend()
    {
        Shutdown();
    }

    bool OpenGLRendererBackend::Initialize()
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::Initialize");
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

        if (!m_SurfaceShader.LoadFromFiles("assets/shaders/surface_mesh.vert", "assets/shaders/surface_mesh.frag"))
        {
            LogError("OpenGL renderer failed to load volumetric surface shader files.");
            return false;
        }

        glGenBuffers(1, &m_InstanceVBO);
        glGenBuffers(1, &m_InstanceColorVBO);
        glGenVertexArrays(1, &m_GridVAO);
        glGenBuffers(1, &m_GridVBO);

        std::vector<SphereVertex> sphereVertices;
        std::vector<std::uint32_t> sphereIndices;
        BuildSphereMesh(sphereVertices, sphereIndices);
        glGenVertexArrays(1, &m_SphereMesh.vao);
        glGenBuffers(1, &m_SphereMesh.vbo);
        glGenBuffers(1, &m_SphereMesh.ebo);
        m_SphereMesh.indexCount = static_cast<std::uint32_t>(sphereIndices.size());

        glBindVertexArray(m_SphereMesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_SphereMesh.vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(sphereVertices.size() * sizeof(SphereVertex)),
            sphereVertices.data(),
            GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SphereVertex), reinterpret_cast<void *>(offsetof(SphereVertex, position)));

        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(SphereVertex), reinterpret_cast<void *>(offsetof(SphereVertex, normal)));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_SphereMesh.ebo);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(sphereIndices.size() * sizeof(std::uint32_t)),
            sphereIndices.data(),
            GL_STATIC_DRAW);

        std::vector<SphereVertex> cylinderVertices;
        std::vector<std::uint32_t> cylinderIndices;
        BuildCylinderMesh(cylinderVertices, cylinderIndices);
        glGenVertexArrays(1, &m_CylinderMesh.vao);
        glGenBuffers(1, &m_CylinderMesh.vbo);
        glGenBuffers(1, &m_CylinderMesh.ebo);
        m_CylinderMesh.indexCount = static_cast<std::uint32_t>(cylinderIndices.size());

        glBindVertexArray(m_CylinderMesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_CylinderMesh.vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(cylinderVertices.size() * sizeof(SphereVertex)),
            cylinderVertices.data(),
            GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SphereVertex), reinterpret_cast<void *>(offsetof(SphereVertex, position)));

        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(SphereVertex), reinterpret_cast<void *>(offsetof(SphereVertex, normal)));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_CylinderMesh.ebo);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(cylinderIndices.size() * sizeof(std::uint32_t)),
            cylinderIndices.data(),
            GL_STATIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, m_InstanceColorVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        ConfigureInstanceAttributes(m_SphereMesh.vao, m_InstanceVBO, m_InstanceColorVBO);
        ConfigureInstanceAttributes(m_CylinderMesh.vao, m_InstanceVBO, m_InstanceColorVBO);

        glBindVertexArray(m_GridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void *>(0));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

#if defined(DS_ENABLE_TRACY)
        TracyGpuContext;
        LogInfo("Tracy GPU context initialized.");
#endif

        CreateFramebuffer(m_ViewportWidth, m_ViewportHeight);
        LogInfo("OpenGL renderer backend initialized");
        return true;
    }

    void OpenGLRendererBackend::Shutdown()
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::Shutdown");
        DestroyFramebuffer();

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

        DestroyMesh(m_SphereMesh);
        DestroyMesh(m_CylinderMesh);

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

        DestroySurfaceMeshCache();

        m_Shader.Destroy();
        m_GridShader.Destroy();
        m_SurfaceShader.Destroy();
    }

    OpenGLRendererBackend::SurfaceMeshCacheEntry &OpenGLRendererBackend::GetOrCreateSurfaceMeshCache(std::uint64_t meshId)
    {
        auto [it, inserted] = m_SurfaceMeshCache.try_emplace(meshId);
        SurfaceMeshCacheEntry &entry = it->second;
        if (!inserted)
        {
            return entry;
        }

        glGenVertexArrays(1, &entry.vao);
        glGenBuffers(1, &entry.vbo);

        glBindVertexArray(entry.vao);
        glBindBuffer(GL_ARRAY_BUFFER, entry.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(SurfaceVertex), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex), reinterpret_cast<void *>(offsetof(SurfaceVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex), reinterpret_cast<void *>(offsetof(SurfaceVertex, normal)));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        return entry;
    }

    void OpenGLRendererBackend::DestroySurfaceMeshCache()
    {
        for (auto &[meshId, entry] : m_SurfaceMeshCache)
        {
            (void)meshId;
            if (entry.vbo != 0)
            {
                glDeleteBuffers(1, &entry.vbo);
                entry.vbo = 0;
            }

            if (entry.vao != 0)
            {
                glDeleteVertexArrays(1, &entry.vao);
                entry.vao = 0;
            }
        }

        m_SurfaceMeshCache.clear();
    }

    void OpenGLRendererBackend::ResizeViewport(std::uint32_t width, std::uint32_t height)
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::ResizeViewport");
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
        LogTrace("Renderer viewport resized to " + std::to_string(width) + "x" + std::to_string(height));
        CreateFramebuffer(m_ViewportWidth, m_ViewportHeight);
    }

    void OpenGLRendererBackend::CreateFramebuffer(std::uint32_t width, std::uint32_t height)
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::CreateFramebuffer");
        DestroyFramebuffer();

        glGenFramebuffers(1, &m_Framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);

        if (m_MsaaSamples > 1)
        {
            glGenRenderbuffers(1, &m_ColorRenderbuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, m_ColorRenderbuffer);
            glRenderbufferStorageMultisample(
                GL_RENDERBUFFER,
                m_MsaaSamples,
                GL_RGBA8,
                static_cast<int>(width),
                static_cast<int>(height));
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_ColorRenderbuffer);

            glGenRenderbuffers(1, &m_DepthRenderbuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, m_DepthRenderbuffer);
            glRenderbufferStorageMultisample(
                GL_RENDERBUFFER,
                m_MsaaSamples,
                GL_DEPTH24_STENCIL8,
                static_cast<int>(width),
                static_cast<int>(height));
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthRenderbuffer);
        }
        else
        {
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
        }

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            LogError("OpenGL framebuffer is incomplete.");
        }

        if (m_MsaaSamples > 1)
        {
            glGenFramebuffers(1, &m_ResolveFramebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, m_ResolveFramebuffer);

            glGenTextures(1, &m_ColorTexture);
            glBindTexture(GL_TEXTURE_2D, m_ColorTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(width), static_cast<int>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTexture, 0);

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                LogError("OpenGL resolve framebuffer is incomplete.");
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void OpenGLRendererBackend::DestroyFramebuffer()
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::DestroyFramebuffer");
        if (m_ColorRenderbuffer != 0)
        {
            glDeleteRenderbuffers(1, &m_ColorRenderbuffer);
            m_ColorRenderbuffer = 0;
        }

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

        if (m_ResolveFramebuffer != 0)
        {
            glDeleteFramebuffers(1, &m_ResolveFramebuffer);
            m_ResolveFramebuffer = 0;
        }

        if (m_Framebuffer != 0)
        {
            glDeleteFramebuffers(1, &m_Framebuffer);
            m_Framebuffer = 0;
        }
    }

    void OpenGLRendererBackend::DestroyMesh(MeshBuffers &mesh)
    {
        if (mesh.ebo != 0)
        {
            glDeleteBuffers(1, &mesh.ebo);
            mesh.ebo = 0;
        }

        if (mesh.vbo != 0)
        {
            glDeleteBuffers(1, &mesh.vbo);
            mesh.vbo = 0;
        }

        if (mesh.vao != 0)
        {
            glDeleteVertexArrays(1, &mesh.vao);
            mesh.vao = 0;
        }

        mesh.indexCount = 0;
    }

    void OpenGLRendererBackend::BeginFrame(const SceneRenderSettings &settings)
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::BeginFrame");
#if defined(DS_ENABLE_TRACY)
        TracyGpuZone("BeginFrame");
#endif
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glViewport(0, 0, static_cast<int>(m_ViewportWidth), static_cast<int>(m_ViewportHeight));
        glEnable(GL_DEPTH_TEST);
        if (m_MsaaSamples > 1)
        {
            glEnable(GL_MULTISAMPLE);
        }
        glClearColor(settings.clearColor.r, settings.clearColor.g, settings.clearColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLRendererBackend::RenderDemoScene(const glm::mat4 &viewProjection, const SceneRenderSettings &settings)
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::RenderDemoScene");
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
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::RenderAtomsScene");
#if defined(DS_ENABLE_TRACY)
        TracyGpuZone("RenderAtomsScene");
#endif
        RenderGrid(viewProjection, settings);

        const std::size_t instanceCount = std::min(atomPositions.size(), atomColors.size());
        if (instanceCount == 0 || m_SphereMesh.indexCount == 0)
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

        RenderInstancedMesh(m_SphereMesh, viewProjection, instanceModels, effectiveColors, settings);
    }

    void OpenGLRendererBackend::RenderCylinderInstances(
        const glm::mat4 &viewProjection,
        const std::vector<glm::mat4> &instanceModels,
        const std::vector<glm::vec3> &instanceColors,
        const SceneRenderSettings &settings)
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::RenderCylinderInstances");
#if defined(DS_ENABLE_TRACY)
        TracyGpuZone("RenderCylinderInstances");
#endif
        if (instanceModels.empty() || instanceColors.empty() || m_CylinderMesh.indexCount == 0)
        {
            return;
        }

        SceneRenderSettings cylinderSettings = settings;
        cylinderSettings.drawGrid = false;
        cylinderSettings.atomWireframe = false;
        RenderInstancedMesh(m_CylinderMesh, viewProjection, instanceModels, instanceColors, cylinderSettings);
    }

    void OpenGLRendererBackend::RenderInstancedMesh(
        const MeshBuffers &mesh,
        const glm::mat4 &viewProjection,
        const std::vector<glm::mat4> &instanceModels,
        const std::vector<glm::vec3> &instanceColors,
        const SceneRenderSettings &settings)
    {
        const std::size_t instanceCount = std::min(instanceModels.size(), instanceColors.size());
        if (instanceCount == 0 || mesh.vao == 0 || mesh.indexCount == 0)
        {
            return;
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

        glBindVertexArray(mesh.vao);

        glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVBO);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(instanceCount * sizeof(glm::mat4)),
            instanceModels.data(),
            GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, m_InstanceColorVBO);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(instanceCount * sizeof(glm::vec3)),
            instanceColors.data(),
            GL_DYNAMIC_DRAW);

        glDrawElementsInstanced(
            GL_TRIANGLES,
            static_cast<GLsizei>(mesh.indexCount),
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

    void OpenGLRendererBackend::RenderLineSegments(
        const glm::mat4 &viewProjection,
        const std::vector<glm::vec3> &lineVertices,
        const glm::vec3 &lineColor,
        float lineWidth)
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::RenderLineSegments");
#if defined(DS_ENABLE_TRACY)
        TracyGpuZone("RenderLineSegments");
#endif
        if (lineVertices.size() < 2)
        {
            return;
        }

        m_GridShader.Bind();
        m_GridShader.SetMat4("u_ViewProjection", viewProjection);
        m_GridShader.SetFloat3("u_GridColor", lineColor);

        glBindVertexArray(m_GridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(lineVertices.size() * sizeof(glm::vec3)),
            lineVertices.data(),
            GL_DYNAMIC_DRAW);

        float widthRange[2] = {1.0f, 1.0f};
        glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, widthRange);
        const float requestedLineWidth = std::clamp(lineWidth, widthRange[0], widthRange[1]);
        glLineWidth(requestedLineWidth);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertices.size()));
        glLineWidth(1.0f);

        glBindVertexArray(0);
    }

    void OpenGLRendererBackend::RenderSurfaceMesh(
        const glm::mat4 &viewProjection,
        const std::vector<glm::vec3> &positions,
        const std::vector<glm::vec3> &normals,
        std::uint64_t meshId,
        std::uint64_t meshRevision,
        const glm::vec3 &surfaceColor,
        float surfaceOpacity,
        const SceneRenderSettings &settings)
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::RenderSurfaceMesh");
#if defined(DS_ENABLE_TRACY)
        TracyGpuZone("RenderSurfaceMesh");
#endif
        const std::size_t vertexCount = std::min(positions.size(), normals.size());
        if (vertexCount < 3 || meshId == 0)
        {
            return;
        }

        SurfaceMeshCacheEntry &cache = GetOrCreateSurfaceMeshCache(meshId);
        if (cache.vao == 0 || cache.vbo == 0)
        {
            return;
        }

        if (cache.uploadedRevision != meshRevision || cache.vertexCount != vertexCount)
        {
            std::vector<SurfaceVertex> vertices;
            {
                DS_PROFILE_SCOPE_N("OpenGLRendererBackend::BuildSurfaceVertices");
                vertices.reserve(vertexCount);
                for (std::size_t i = 0; i < vertexCount; ++i)
                {
                    vertices.push_back({positions[i], normals[i]});
                }
            }

            if (!vertices.empty())
            {
                DS_PROFILE_ALLOC_N(vertices.data(), vertices.size() * sizeof(SurfaceVertex), "VolumetricSurfaceUploadVertices");
            }

            {
                DS_PROFILE_SCOPE_N("OpenGLRendererBackend::UploadSurfaceMesh");
                glBindVertexArray(cache.vao);
                glBindBuffer(GL_ARRAY_BUFFER, cache.vbo);
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(vertices.size() * sizeof(SurfaceVertex)),
                    vertices.data(),
                    GL_STATIC_DRAW);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindVertexArray(0);
            }

            if (!vertices.empty())
            {
                DS_PROFILE_FREE_N(vertices.data(), "VolumetricSurfaceUploadVertices");
            }

            cache.vertexCount = vertexCount;
            cache.capacityBytes = vertexCount * sizeof(SurfaceVertex);
            cache.uploadedRevision = meshRevision;
        }

        m_SurfaceShader.Bind();
        m_SurfaceShader.SetMat4("u_ViewProjection", viewProjection);
        m_SurfaceShader.SetFloat3("u_LightDirection", glm::normalize(settings.lightDirection));
        m_SurfaceShader.SetFloat3("u_LightColor", settings.lightColor);
        m_SurfaceShader.SetFloat4(
            "u_SurfaceColor",
            surfaceColor.r,
            surfaceColor.g,
            surfaceColor.b,
            std::clamp(surfaceOpacity, 0.0f, 1.0f));
        m_SurfaceShader.SetFloat4(
            "u_LightFactors",
            std::max(settings.ambientStrength, 0.0f),
            std::max(settings.diffuseStrength, 0.0f),
            0.12f,
            0.0f);

        {
            DS_PROFILE_SCOPE_N("OpenGLRendererBackend::DrawSurfaceMesh");
            glBindVertexArray(cache.vao);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCount));
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        glBindVertexArray(0);
    }

    void OpenGLRendererBackend::RenderGrid(const glm::mat4 &viewProjection, const SceneRenderSettings &settings)
    {
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::RenderGrid");
#if defined(DS_ENABLE_TRACY)
        TracyGpuZone("RenderGrid");
#endif
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
        DS_PROFILE_SCOPE_N("OpenGLRendererBackend::EndFrame");
#if defined(DS_ENABLE_TRACY)
        TracyGpuCollect;
#endif
        if (m_MsaaSamples > 1 && m_Framebuffer != 0 && m_ResolveFramebuffer != 0)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Framebuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ResolveFramebuffer);
            glBlitFramebuffer(
                0,
                0,
                static_cast<int>(m_ViewportWidth),
                static_cast<int>(m_ViewportHeight),
                0,
                0,
                static_cast<int>(m_ViewportWidth),
                static_cast<int>(m_ViewportHeight),
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool OpenGLRendererBackend::ReadColorAttachmentPixels(
        std::uint32_t &outWidth,
        std::uint32_t &outHeight,
        std::vector<std::uint8_t> &outRgbaPixels) const
    {
        outWidth = 0;
        outHeight = 0;
        outRgbaPixels.clear();

        if (m_ColorTexture == 0 || m_ViewportWidth == 0 || m_ViewportHeight == 0)
        {
            return false;
        }

        const std::size_t pixelCount = static_cast<std::size_t>(m_ViewportWidth) * static_cast<std::size_t>(m_ViewportHeight);
        if (pixelCount == 0)
        {
            return false;
        }

        outRgbaPixels.resize(pixelCount * 4u);

        glBindTexture(GL_TEXTURE_2D, m_ColorTexture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, outRgbaPixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        outWidth = m_ViewportWidth;
        outHeight = m_ViewportHeight;
        return true;
    }

} // namespace ds
