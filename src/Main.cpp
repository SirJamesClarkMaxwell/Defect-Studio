#include "Core/Application.h"
#include "Core/Logger.h"
#include "Layers/EditorLayer.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <filesystem>
#include <iostream>
#include <string>

int main()
{
#if defined(_WIN32)
    char executablePath[MAX_PATH] = {};
    const DWORD executablePathLength = GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    if (executablePathLength > 0 && executablePathLength < MAX_PATH)
    {
        const std::filesystem::path executableDirectory = std::filesystem::path(executablePath).parent_path();
        SetCurrentDirectoryA(executableDirectory.string().c_str());
    }
#endif

    try
    {
        ds::Application app;
        app.PushLayer(new ds::EditorLayer());
        app.Run();
    }
    catch (const std::exception &ex)
    {
        ds::LogFatal(std::string("Fatal error: ") + ex.what());
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
