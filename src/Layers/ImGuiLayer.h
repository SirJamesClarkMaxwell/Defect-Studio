#pragma once

#include "Core/Layer.h"

namespace ds
{

    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer() override = default;

        void OnAttach() override;
        void OnDetach() override;

        void Begin();
        void End();
    };

} // namespace ds
