#include "Core/Application.h"
#include "Core/Logger.h"
#include "Layers/EditorLayer.h"

#include <iostream>
#include <string>

int main()
{
    try
    {
        ds::Application app;
        app.PushLayer(new ds::EditorLayer());
        app.Run();
    }
    catch (const std::exception &ex)
    {
        ds::LogError(std::string("Fatal error: ") + ex.what());
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
