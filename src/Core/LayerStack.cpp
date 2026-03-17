#include "Core/LayerStack.h"

namespace ds
{

    LayerStack::~LayerStack()
    {
        for (Layer *layer : m_Layers)
        {
            layer->OnDetach();
            delete layer;
        }
    }

    void LayerStack::PushLayer(Layer *layer)
    {
        m_Layers.push_back(layer);
        layer->OnAttach();
    }

} // namespace ds
