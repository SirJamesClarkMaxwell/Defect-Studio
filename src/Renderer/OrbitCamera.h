#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace ds
{

    class OrbitCamera
    {
    public:
        enum class ProjectionMode
        {
            Perspective = 0,
            Orthographic = 1
        };

        OrbitCamera();

        void SetViewportSize(float width, float height);
        void SetSensitivity(float orbit, float pan, float zoom);
        void OnUpdate(float deltaTime, bool allowInput, float scrollDelta);
        void FrameBounds(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax);
        void SetProjectionMode(ProjectionMode mode);
        void SetPerspectiveFovDegrees(float fovDegrees);
        void SetOrthographicSize(float orthographicSize);
        void SetOrbitState(const glm::vec3 &target, float distance, float yaw, float pitch);

        const glm::mat4 &GetViewMatrix() const { return m_View; }
        const glm::mat4 &GetProjectionMatrix() const { return m_Projection; }
        glm::mat4 GetViewProjectionMatrix() const;

        ProjectionMode GetProjectionMode() const { return m_ProjectionMode; }
        float GetPerspectiveFovDegrees() const { return m_FovYDegrees; }
        float GetOrthographicSize() const { return m_OrthographicSize; }
        const glm::vec3 &GetTarget() const { return m_Target; }
        float GetDistance() const { return m_Distance; }
        float GetYaw() const { return m_Yaw; }
        float GetPitch() const { return m_Pitch; }

    private:
        void RecalculateProjection();
        void RecalculateView();

        float m_ViewportWidth = 1280.0f;
        float m_ViewportHeight = 720.0f;

        glm::vec3 m_Target = glm::vec3(0.0f, 0.0f, 0.0f);
        float m_Distance = 6.0f;
        float m_Yaw = 0.6f;
        float m_Pitch = 0.5f;

        float m_FovYDegrees = 45.0f;
        float m_OrthographicSize = 5.0f;
        float m_NearClip = 0.01f;
        float m_FarClip = 100.0f;
        ProjectionMode m_ProjectionMode = ProjectionMode::Perspective;

        float m_OrbitSensitivity = 1.0f;
        float m_PanSensitivity = 1.0f;
        float m_ZoomSensitivity = 1.0f;

        bool m_DragActive = false;
        glm::vec2 m_LastMousePos = glm::vec2(0.0f);

        glm::mat4 m_View = glm::mat4(1.0f);
        glm::mat4 m_Projection = glm::mat4(1.0f);
    };

} // namespace ds
