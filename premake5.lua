workspace "PathTracer"
	architecture "x64"
	startproject "PathTracer"

	configurations
	{
		"Debug",
		"Release"
	}

	flags
	{
		"MultiProcessorCompile"
	}

	outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

	target = "bin/" .. outputdir .. "/PathTracer"
	include "VulkanLibrary"	

project "PathTracer"
	location "PathTracer"
	kind "ConsoleApp"
	language "C++"
	staticruntime "on"

	targetdir (target)
	objdir ("bin/intermediates/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.cpp",
		"%{prj.name}/src/**.h",
	}

	includedirs
	{
		"PathTracer/vendor/FastNoise2/include",
	}

	links
	{
		"VulkanLibrary",
		"PathTracer/vendor/FastNoise2/lib/FastNoise.lib",
	}

	VulkanLibraryIncludeDirectories("VulkanLibrary")

	filter "system:windows"
		cppdialect "C++17"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"
		
		defines 
		{
			"ENABLE_ASSERTS"
		}

	filter "configurations:Release"
		runtime "Release"
		optimize "On"