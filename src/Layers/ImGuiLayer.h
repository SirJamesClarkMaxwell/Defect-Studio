#pragma once

#include "Core/Layer.h"

struct ImFont;

namespace ds
{
    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer() override = default;

        static bool LoadSavedStyle();
        static void SaveCurrentStyle();
        static void ResetSavedStyle();
        static ::ImFont *GetUIFont();
        static ::ImFont *GetMonospaceFont();
        static ::ImFont *GetBondLabelFont();

        void OnAttach() override;
        void OnDetach() override;

        void Begin();
        void End();
    };

} // namespace ds
