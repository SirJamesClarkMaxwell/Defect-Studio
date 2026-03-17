#pragma once

#include "Core/Layer.h"

#include <vector>

namespace ds
{

    class LayerStack
    {
    public:
        LayerStack() = default;
        ~LayerStack();

        void PushLayer(Layer *layer);
        const std::vector<Layer *> &GetLayers() const { return m_Layers; }

    private:
        std::vector<Layer *> m_Layers;
    };

} // namespace ds
