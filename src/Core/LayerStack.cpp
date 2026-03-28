#include "Core/LayerStack.h"

namespace ds
{

    LayerStack::~LayerStack()
    {
        Clear();
    }

    void LayerStack::Clear()
    {
        for (Layer *layer : m_Layers)
        {
            layer->OnDetach();
            delete layer;
        }
        m_Layers.clear();
    }

    void LayerStack::PushLayer(Layer *layer)
    {
        m_Layers.push_back(layer);
        layer->OnAttach();
    }

} // namespace ds
