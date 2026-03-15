workspace "DefectsStudio"
    architecture "x64"
    startproject "DefectsStudio"

    configurations
    {
        "Debug",
        "Release"
    }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["glfw"] = "vendor/glfw/include"
IncludeDir["imgui"] = "vendor/imgui"
IncludeDir["glm"] = "vendor/glm"

group "Dependencies"

project "GLFW"
    location "vendor/glfw"
    kind "StaticLib"
    language "C"
    staticruntime "off"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "%{prj.location}/include/GLFW/**.h",
        "%{prj.location}/src/context.c",
        "%{prj.location}/src/init.c",
        "%{prj.location}/src/input.c",
        "%{prj.location}/src/monitor.c",
        "%{prj.location}/src/platform.c",
        "%{prj.location}/src/vulkan.c",
        "%{prj.location}/src/window.c",
        "%{prj.location}/src/egl_context.c",
        "%{prj.location}/src/osmesa_context.c",
        "%{prj.location}/src/null_init.c",
        "%{prj.location}/src/null_joystick.c",
        "%{prj.location}/src/null_monitor.c",
        "%{prj.location}/src/null_window.c",
        "%{prj.location}/src/wgl_context.c",
        "%{prj.location}/src/win32_init.c",
        "%{prj.location}/src/win32_joystick.c",
        "%{prj.location}/src/win32_module.c",
        "%{prj.location}/src/win32_monitor.c",
        "%{prj.location}/src/win32_time.c",
        "%{prj.location}/src/win32_thread.c",
        "%{prj.location}/src/win32_window.c"
    }

    defines
    {
        "_GLFW_WIN32",
        "_CRT_SECURE_NO_WARNINGS"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        runtime "Release"
        optimize "on"

project "ImGui"
    location "vendor/imgui"
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "%{prj.location}/imconfig.h",
        "%{prj.location}/imgui.h",
        "%{prj.location}/imgui.cpp",
        "%{prj.location}/imgui_draw.cpp",
        "%{prj.location}/imgui_internal.h",
        "%{prj.location}/imgui_tables.cpp",
        "%{prj.location}/imgui_widgets.cpp",
        "%{prj.location}/imgui_demo.cpp",
        "%{prj.location}/backends/imgui_impl_glfw.h",
        "%{prj.location}/backends/imgui_impl_glfw.cpp",
        "%{prj.location}/backends/imgui_impl_opengl3.h",
        "%{prj.location}/backends/imgui_impl_opengl3.cpp"
    }

    includedirs
    {
        "%{prj.location}",
        "%{prj.location}/backends",
        IncludeDir["glfw"]
    }

    links
    {
        "GLFW",
        "opengl32.lib"
    }

    defines
    {
        "IMGUI_ENABLE_DOCKING"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        runtime "Release"
        optimize "on"

group ""

project "DefectsStudio"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/**.h",
        "src/**.cpp"
    }

    includedirs
    {
        "src",
        IncludeDir["glfw"],
        IncludeDir["imgui"],
        IncludeDir["glm"]
    }

    links
    {
        "GLFW",
        "ImGui",
        "opengl32.lib"
    }

    defines
    {
        "IMGUI_ENABLE_DOCKING",
        "GLFW_INCLUDE_NONE"
    }

    filter "system:windows"
        systemversion "latest"
        defines
        {
            "DS_PLATFORM_WINDOWS"
        }

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"
        defines
        {
            "DS_CONFIG_DEBUG"
        }

    filter "configurations:Release"
        runtime "Release"
        optimize "on"
        defines
        {
            "DS_CONFIG_RELEASE"
        }
