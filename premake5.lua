workspace "COMP5822M-cw2"
	language "C++"
	cppdialect "C++17"

	platforms { "x64" }
	configurations { "debug", "release" }

	flags "NoPCH"
	flags "MultiProcessorCompile"

	startproject "cw2-bake"

	debugdir "%{wks.location}"
	objdir "_build_/%{cfg.buildcfg}-%{cfg.platform}-%{cfg.toolset}"
	targetsuffix "-%{cfg.buildcfg}-%{cfg.platform}-%{cfg.toolset}"
	
	-- Default toolset options
	filter "toolset:gcc or toolset:clang"
		linkoptions { "-pthread" }
		buildoptions { "-march=native", "-Wall", "-pthread" }

	filter "toolset:msc-*"
		defines { "_CRT_SECURE_NO_WARNINGS=1" }
		defines { "_SCL_SECURE_NO_WARNINGS=1" }
		buildoptions { "/utf-8" }
	
	filter "*"

	-- default libraries
	filter "system:linux"
		links "dl"
	
	filter "system:windows"

	filter "*"

	-- default outputs
	filter "kind:StaticLib"
		targetdir "lib/"

	filter "kind:ConsoleApp"
		targetdir "bin/"
		targetextension ".exe"
	
	filter "*"

	--configurations
	filter "debug"
		symbols "On"
		defines { "_DEBUG=1" }

	filter "release"
		optimize "On"
		defines { "NDEBUG=1" }

	filter "*"

-- Third party dependencies
include "third_party" 

-- GLSLC helpers
dofile( "util/glslc.lua" )

-- Projects
project "cw2"
	local sources = { 
		"cw2/**.cpp",
		"cw2/**.hpp",
		"cw2/**.hxx"
	}

	kind "ConsoleApp"
	location "cw2"

	files( sources )

	dependson "cw2-shaders"

	links "labutils"
	links "x-volk"
	links "x-stb"
	links "x-glfw"
	links "x-vma"

	dependson "x-glm" 
	dependson "x-rapidobj"

project "cw2-shaders"
	local shaders = { 
		"cw2/shaders/*.vert",
		"cw2/shaders/*.frag",
		"cw2/shaders/*.comp",
		"cw2/shaders/*.geom",
		"cw2/shaders/*.tesc",
		"cw2/shaders/*.tese"
	}

	kind "Utility"
	location "cw2/shaders"

	files( shaders )

	handle_glsl_files( "-O", "assets/cw2/shaders", {} )

project "cw2-bake"
	local sources = { 
		"cw2-bake/**.cpp",
		"cw2-bake/**.hpp",
		"cw2-bake/**.hxx"
	}

	kind "ConsoleApp"
	location "cw2-bake"

	files( sources )

	links "labutils" -- for lut::Error
	links "x-tgen" -- Task 1.4

	dependson "x-glm" 
	dependson "x-rapidobj"

project "labutils"
	local sources = { 
		"labutils/**.cpp",
		"labutils/**.hpp",
		"labutils/**.hxx"
	}

	kind "StaticLib"
	location "labutils"

	files( sources )

--EOF
