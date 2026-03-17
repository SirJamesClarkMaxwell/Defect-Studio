#include "Renderer/OrbitCamera.h"

#include "Core/ApplicationContext.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

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

    void OrbitCamera::OnUpdate(float deltaTime, bool allowInput, float scrollDelta)
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
        const bool shiftPressed = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

        if (!mmb)
        {
            m_DragActive = false;
            m_LastMousePos = mousePos;
        }

        if (scrollDelta != 0.0f)
        {
            const float zoomFromWheel = 0.9f * m_Distance * m_ZoomSensitivity;
            m_Distance -= scrollDelta * zoomFromWheel;
            if (m_Distance < 0.5f)
                m_Distance = 0.5f;
            if (m_Distance > 100.0f)
                m_Distance = 100.0f;

            RecalculateView();
        }

        if (!mmb)
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
            std::sin(m_Pitch),
            std::cos(m_Pitch) * std::cos(m_Yaw)));
        const glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));

        if (!shiftPressed)
        {
            m_Yaw -= delta.x * orbitSpeed;
            m_Pitch -= delta.y * orbitSpeed;

            const float limit = glm::half_pi<float>() - 0.05f;
            if (m_Pitch > limit)
                m_Pitch = limit;
            if (m_Pitch < -limit)
                m_Pitch = -limit;
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

    void OrbitCamera::RecalculateProjection()
    {
        const float aspect = m_ViewportWidth / m_ViewportHeight;
        m_Projection = glm::perspective(glm::radians(m_FovYDegrees), aspect, m_NearClip, m_FarClip);
    }

    void OrbitCamera::RecalculateView()
    {
        const glm::vec3 direction = glm::normalize(glm::vec3(
            std::cos(m_Pitch) * std::sin(m_Yaw),
            std::sin(m_Pitch),
            std::cos(m_Pitch) * std::cos(m_Yaw)));

        const glm::vec3 position = m_Target - direction * m_Distance;
        m_View = glm::lookAt(position, m_Target, glm::vec3(0.0f, 1.0f, 0.0f));
    }

} // namespace ds
