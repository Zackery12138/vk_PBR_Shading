-- Third party projects

includedirs( "volk/include" );
includedirs( "vulkan/include" );
includedirs( "stb/include" );
includedirs( "glfw/include" );
includedirs( "VulkanMemoryAllocator/include" );
includedirs( "glm/include" );
includedirs( "rapidobj/include" );
includedirs( "tgen/include" );

defines( "GLM_FORCE_RADIANS=1" )
defines( "GLM_FORCE_SIZE_T_LENGTH=1" )

filter "system:macosx"
	-- Additional dependencies required by GLFW on MacOS.
	links {
		"Cocoa.framework", 
		"OpenGL.framework", 
		"IOKit.framework", 
		"CoreVideo.framework" 
	};
filter "*"

project( "x-volk" )
	kind "StaticLib"

	location "."

	files( "volk/src/*.c" )
	files( "volk/include/volk/*.h" )

project( "x-vulkan-headers" )
	kind "Utility"

	location "."

	files( "vulkan/include/**.h*" )

project( "x-stb" )
	kind "StaticLib"

	location "."

	files( "stb/src/*.c" )

project( "x-glfw" )
	kind "StaticLib"

	location "."

	filter "system:linux"
		defines { "_GLFW_X11=1" }

	filter "system:windows"
		defines { "_GLFW_WIN32=1" }

	filter "system:macosx"
		-- completely untested.
		defines { "_GLFW_COCOA=1" }

	filter "*"

	files {
		"glfw/src/context.c",
		"glfw/src/egl_context.c",
		"glfw/src/init.c",
		"glfw/src/input.c",
		"glfw/src/internal.h",
		"glfw/src/mappings.h",
		"glfw/src/monitor.c",
		"glfw/src/null_init.c",
		"glfw/src/null_joystick.c",
		"glfw/src/null_joystick.h",
		"glfw/src/null_monitor.c",
		"glfw/src/null_platform.h",
		"glfw/src/null_window.c",
		"glfw/src/platform.c",
		"glfw/src/platform.h",
		"glfw/src/vulkan.c",
		"glfw/src/window.c",
		"glfw/src/osmesa_context.c"
	};

	filter "system:linux"
		files {
			"glfw/src/posix_*",
			"glfw/src/x11_*", 
			"glfw/src/xkb_*",
			"glfw/src/glx_*",
			"glfw/src/linux_*",
		};
	filter "system:windows"
		files {
			"glfw/src/win32_*",
			"glfw/src/wgl_*", 
		};
	filter "system:macosx"
		-- WARNING: this is completely untested!
		files {
			"glfw/src/cocoa_*",
			"glfw/src/nsgl_context.m",
			"glfw/src/posix_thread.*",
			"glfw/src/posix_module.*"

		};

	filter "*"

project( "x-vma" )
	kind "StaticLib"

	location "."

	filter "toolset:gcc or toolset:clang"
		buildoptions { 
			"-Wno-unused-variable",
			"-Wno-reorder",
			"-Wno-parentheses"
		}
	filter "toolset:clang"
		buildoptions {
			"-Wno-nullability-completeness"
		}
	filter {}

	files( "VulkanMemoryAllocator/src/*.cpp" )

project( "x-glm" )
	kind "Utility"

	location "."

	files( "glm/include/**.h" )
	files( "glm/include/**.hpp" )
	files( "glm/include/**.inl" )

project( "x-rapidobj" )
	kind "Utility"

	location "."

	files( "rapidobj/include/**.h*" )

project( "x-tgen" )
	kind "StaticLib"

	location "."

	files( "tgen/src/*.cpp" )

--EOF
