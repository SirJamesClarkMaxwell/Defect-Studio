#pragma once

#include <string>

namespace ds
{

    class Layer
    {
    public:
        explicit Layer(std::string name) : m_Name(std::move(name)) {}
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(float /*deltaTime*/) {}
        virtual void OnImGuiRender() {}

        const std::string &GetName() const { return m_Name; }

    private:
        std::string m_Name;
    };

} // namespace ds
