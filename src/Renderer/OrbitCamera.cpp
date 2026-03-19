#include "Renderer/OrbitCamera.h"

#include "Core/ApplicationContext.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ds
{

    OrbitCamera::OrbitCamera()
    {
        RecalculateProjection();
        RecalculateView();
    }

    void OrbitCamera::SetViewportSize(float width, float height)
    {
        if (width < 1.0f)
            width = 1.0f;
        if (height < 1.0f)
            height = 1.0f;

        if (m_ViewportWidth == width && m_ViewportHeight == height)
        {
            return;
        }

        m_ViewportWidth = width;
        m_ViewportHeight = height;
        RecalculateProjection();
    }

    glm::mat4 OrbitCamera::GetViewProjectionMatrix() const
    {
        return m_Projection * m_View;
    }

    void OrbitCamera::SetSensitivity(float orbit, float pan, float zoom)
    {
        m_OrbitSensitivity = orbit;
        m_PanSensitivity = pan;
        m_ZoomSensitivity = zoom;
    }

    void OrbitCamera::OnUpdate(float deltaTime, bool allowInput, float scrollDelta, bool touchpadNavigationEnabled)
    {
        GLFWwindow *window = ApplicationContext::Get().GetWindow();

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        const glm::vec2 mousePos = glm::vec2(static_cast<float>(mouseX), static_cast<float>(mouseY));

        if (!allowInput)
        {
            m_DragActive = false;
            m_LastMousePos = mousePos;
            return;
        }
        const bool mmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
        const bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        const bool altPressed = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        const bool shiftPressed = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        const bool touchpadOrbit = touchpadNavigationEnabled && altPressed && lmb;
        const bool touchpadPan = touchpadNavigationEnabled && altPressed && shiftPressed && lmb;
        const bool touchpadZoom = touchpadNavigationEnabled && altPressed && rmb;
        const bool dragActiveInput = mmb || touchpadOrbit || touchpadPan || touchpadZoom;

        if (!dragActiveInput)
        {
            m_DragActive = false;
            m_LastMousePos = mousePos;
        }

        if (scrollDelta != 0.0f)
        {
            if (m_ProjectionMode == ProjectionMode::Orthographic)
            {
                float zoomFactor = 1.0f - scrollDelta * 0.12f * m_ZoomSensitivity;
                if (zoomFactor < 0.1f)
                    zoomFactor = 0.1f;
                if (zoomFactor > 4.0f)
                    zoomFactor = 4.0f;

                m_OrthographicSize *= zoomFactor;
                if (m_OrthographicSize < 0.1f)
                    m_OrthographicSize = 0.1f;
                if (m_OrthographicSize > 100.0f)
                    m_OrthographicSize = 100.0f;

                RecalculateProjection();
            }
            else
            {
                const float zoomFromWheel = 0.9f * m_Distance * m_ZoomSensitivity;
                m_Distance -= scrollDelta * zoomFromWheel;
                if (m_Distance < 0.5f)
                    m_Distance = 0.5f;
                if (m_Distance > 100.0f)
                    m_Distance = 100.0f;

                RecalculateView();
            }
        }

        if (!dragActiveInput)
            return;

        if (!m_DragActive)
        {
            m_DragActive = true;
            m_LastMousePos = mousePos;
            return;
        }

        const glm::vec2 delta = (mousePos - m_LastMousePos) * deltaTime * 60.0f;
        m_LastMousePos = mousePos;

        const float orbitSpeed = 1.8f * m_OrbitSensitivity;
        const float panSpeed = 0.006f * m_Distance * m_PanSensitivity;

        const glm::vec3 forward = glm::normalize(glm::vec3(
            std::cos(m_Pitch) * std::sin(m_Yaw),
            std::cos(m_Pitch) * std::cos(m_Yaw),
            std::sin(m_Pitch)));
        const glm::vec3 worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 right = glm::cross(forward, worldUp);
        if (glm::dot(right, right) < 1e-8f)
        {
            right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        right = glm::normalize(right);
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        if (std::abs(m_Roll) > 1e-6f)
        {
            const glm::mat4 rollRotation = glm::rotate(glm::mat4(1.0f), m_Roll, forward);
            right = glm::normalize(glm::vec3(rollRotation * glm::vec4(right, 0.0f)));
            up = glm::normalize(glm::vec3(rollRotation * glm::vec4(up, 0.0f)));
        }

        if (touchpadZoom)
        {
            m_Distance += delta.y * 0.020f * m_Distance * m_ZoomSensitivity;
            if (m_Distance < 0.5f)
                m_Distance = 0.5f;
            if (m_Distance > 100.0f)
                m_Distance = 100.0f;
        }
        else if (!(shiftPressed || touchpadPan))
        {
            // Orbit in camera-local frame: horizontal drag rotates around current local up,
            // vertical drag rotates around current local right.
            const glm::quat qYaw = glm::angleAxis(-delta.x * orbitSpeed, glm::normalize(up));
            glm::vec3 localForward = glm::normalize(qYaw * forward);
            glm::vec3 localUp = glm::normalize(qYaw * up);

            glm::vec3 localRight = glm::cross(localForward, localUp);
            if (glm::dot(localRight, localRight) < 1e-8f)
            {
                localRight = right;
            }
            localRight = glm::normalize(localRight);

            const glm::quat qPitch = glm::angleAxis(-delta.y * orbitSpeed, localRight);
            localForward = glm::normalize(qPitch * localForward);
            localUp = glm::normalize(qPitch * localUp);

            m_Yaw = std::atan2(localForward.x, localForward.y);
            m_Pitch = std::asin(glm::clamp(localForward.z, -1.0f, 1.0f));

            const glm::vec3 worldUpBasis = glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 baseRight = glm::cross(localForward, worldUpBasis);
            if (glm::dot(baseRight, baseRight) < 1e-8f)
            {
                baseRight = glm::cross(localForward, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            baseRight = glm::normalize(baseRight);
            const glm::vec3 baseUp = glm::normalize(glm::cross(baseRight, localForward));

            const float sinRoll = glm::dot(glm::cross(baseUp, localUp), localForward);
            const float cosRoll = glm::dot(baseUp, localUp);
            m_Roll = std::atan2(sinRoll, cosRoll);

            const float limit = glm::half_pi<float>() - 0.0015f;
            if (m_Pitch > limit)
                m_Pitch = limit;
            if (m_Pitch < -limit)
                m_Pitch = -limit;

            m_Roll = std::fmod(m_Roll, glm::two_pi<float>());
            if (m_Roll > glm::pi<float>())
                m_Roll -= glm::two_pi<float>();
            else if (m_Roll < -glm::pi<float>())
                m_Roll += glm::two_pi<float>();
        }
        else
        {
            m_Target += (-right * delta.x + up * delta.y) * panSpeed;
        }

        RecalculateView();
    }

    void OrbitCamera::FrameBounds(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax)
    {
        m_Target = 0.5f * (boundsMin + boundsMax);

        const glm::vec3 extents = glm::max(boundsMax - boundsMin, glm::vec3(0.001f));
        const float radius = 0.5f * glm::length(extents);
        const float halfFovRadians = glm::radians(m_FovYDegrees) * 0.5f;
        const float minDistance = radius / std::max(std::sin(halfFovRadians), 0.15f);

        m_Distance = std::max(minDistance * 1.35f, 0.5f);
        if (m_Distance > 250.0f)
        {
            m_Distance = 250.0f;
        }

        RecalculateView();
    }

    void OrbitCamera::SetProjectionMode(ProjectionMode mode)
    {
        if (m_ProjectionMode == mode)
        {
            return;
        }

        m_ProjectionMode = mode;
        RecalculateProjection();
    }

    void OrbitCamera::SetPerspectiveFovDegrees(float fovDegrees)
    {
        if (fovDegrees < 10.0f)
            fovDegrees = 10.0f;
        if (fovDegrees > 100.0f)
            fovDegrees = 100.0f;

        m_FovYDegrees = fovDegrees;
        RecalculateProjection();
    }

    void OrbitCamera::SetOrthographicSize(float orthographicSize)
    {
        if (orthographicSize < 0.1f)
            orthographicSize = 0.1f;
        if (orthographicSize > 100.0f)
            orthographicSize = 100.0f;

        m_OrthographicSize = orthographicSize;
        RecalculateProjection();
    }

    void OrbitCamera::SetOrbitState(const glm::vec3 &target, float distance, float yaw, float pitch)
    {
        m_Target = target;

        if (distance < 0.5f)
            distance = 0.5f;
        if (distance > 250.0f)
            distance = 250.0f;
        m_Distance = distance;

        m_Yaw = yaw;
        m_Pitch = pitch;
        const float limit = glm::half_pi<float>() - 0.0015f;
        if (m_Pitch > limit)
            m_Pitch = limit;
        if (m_Pitch < -limit)
            m_Pitch = -limit;

        RecalculateView();
    }

    void OrbitCamera::SetRoll(float rollRadians)
    {
        m_Roll = std::fmod(rollRadians, glm::two_pi<float>());
        if (m_Roll > glm::pi<float>())
            m_Roll -= glm::two_pi<float>();
        else if (m_Roll < -glm::pi<float>())
            m_Roll += glm::two_pi<float>();

        RecalculateView();
    }

    void OrbitCamera::RecalculateProjection()
    {
        const float aspect = m_ViewportWidth / m_ViewportHeight;
        if (m_ProjectionMode == ProjectionMode::Orthographic)
        {
            const float halfHeight = m_OrthographicSize;
            const float halfWidth = m_OrthographicSize * aspect;
            m_Projection = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, m_NearClip, m_FarClip);
            return;
        }

        m_Projection = glm::perspective(glm::radians(m_FovYDegrees), aspect, m_NearClip, m_FarClip);
    }

    void OrbitCamera::RecalculateView()
    {
        const glm::vec3 direction = glm::normalize(glm::vec3(
            std::cos(m_Pitch) * std::sin(m_Yaw),
            std::cos(m_Pitch) * std::cos(m_Yaw),
            std::sin(m_Pitch)));

        const glm::vec3 position = m_Target - direction * m_Distance;

        const glm::vec3 worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 right = glm::cross(direction, worldUp);
        if (glm::dot(right, right) < 1e-8f)
        {
            right = glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        right = glm::normalize(right);

        glm::vec3 up = glm::normalize(glm::cross(right, direction));
        if (std::abs(m_Roll) > 1e-6f)
        {
            const glm::mat4 rollRotation = glm::rotate(glm::mat4(1.0f), m_Roll, direction);
            up = glm::normalize(glm::vec3(rollRotation * glm::vec4(up, 0.0f)));
        }

        m_View = glm::lookAt(position, m_Target, up);
    }

} // namespace ds
