cmake_minimum_required( VERSION 3.15 )
project( MediaPipeParticles VERSION 1.0 LANGUAGES CXX )

set( CMAKE_CXX_STANDARD 14 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

# Resolume 7.11+ runs natively on Apple Silicon and refuses to load an x86_64
# only plugin, so the whole link -- the FFGL SDK static library included -- has
# to be universal. This has to be set before the SDK target is created, which
# is why it is up here rather than next to the other Apple settings.
if( APPLE AND NOT CMAKE_OSX_ARCHITECTURES )
	set( CMAKE_OSX_ARCHITECTURES "x86_64;arm64" )
endif()

# ---------------------------------------------------------------- FFGL SDK
#
# Point FFGL_SDK_DIR at a checkout of https://github.com/resolume/ffgl, or let
# CMake fetch one. Resolume 7.3.1+ is the target; the SDK's master branch is
# what its own example plugins are built from.
#
set( FFGL_SDK_DIR "" CACHE PATH "Existing checkout of the Resolume FFGL SDK" )
set( FFGL_SDK_TAG "master" CACHE STRING "Git ref to fetch when FFGL_SDK_DIR is empty" )

set( FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE )

if( FFGL_SDK_DIR )
	add_subdirectory( ${FFGL_SDK_DIR} ${CMAKE_BINARY_DIR}/ffgl-sdk EXCLUDE_FROM_ALL )
else()
	include( FetchContent )
	FetchContent_Declare(
		ffgl
		GIT_REPOSITORY https://github.com/resolume/ffgl.git
		GIT_TAG ${FFGL_SDK_TAG}
	)
	FetchContent_MakeAvailable( ffgl )
endif()

# The SDK ships as a static library; a plugin is a shared module, so it has to
# be position independent (a no-op on Windows, required by GCC/Clang).
if( TARGET ffgl-sdk )
	set_property( TARGET ffgl-sdk PROPERTY POSITION_INDEPENDENT_CODE ON )
endif()

# ---------------------------------------------------------------- plugin

add_library( MediaPipeParticles MODULE
	src/MediaPipeParticles.cpp
	src/MediaPipeParticles.h
	src/OscPose.cpp
	src/OscPose.h
	src/ParticleSystem.cpp
	src/ParticleSystem.h
	src/PoseTracker.cpp
	src/PoseTracker.h
	src/PoseProtocol.h
	src/GLInclude.h
)

target_include_directories( MediaPipeParticles PRIVATE src )
target_link_libraries( MediaPipeParticles PRIVATE ffgl::sdk )

find_package( Threads REQUIRED )
target_link_libraries( MediaPipeParticles PRIVATE Threads::Threads )

if( WIN32 )
	# plugMain is the only symbol Resolume looks for.
	target_sources( MediaPipeParticles PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/MediaPipeParticles.def )
	target_link_libraries( MediaPipeParticles PRIVATE Ws2_32 opengl32 )
	set_target_properties( MediaPipeParticles PROPERTIES SUFFIX ".dll" )
elseif( APPLE )
	# Resolume loads .bundle plugins; the architectures were pinned above.
	set_target_properties( MediaPipeParticles PROPERTIES
		BUNDLE TRUE
		BUNDLE_EXTENSION "bundle"
		MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
		MACOSX_BUNDLE_BUNDLE_NAME "MediaPipeParticles"
		MACOSX_BUNDLE_GUI_IDENTIFIER "net.ffgl.mediapipeparticles"
		MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
	)
	target_link_libraries( MediaPipeParticles PRIVATE "-framework OpenGL" )
else()
	# Linux is not a Resolume platform; this configuration exists so the
	# plugin can be compiled and statically checked in CI.
	find_package( OpenGL REQUIRED )
	find_package( GLEW REQUIRED )
	target_link_libraries( MediaPipeParticles PRIVATE OpenGL::GL GLEW::GLEW )
endif()

if( MSVC )
	target_compile_options( MediaPipeParticles PRIVATE /W3 )
else()
	target_compile_options( MediaPipeParticles PRIVATE -Wall -Wextra -Wno-unused-parameter )
endif()

# ---------------------------------------------------------------- tests

option( MPP_BUILD_TESTS "Build the host side unit tests" ON )
if( MPP_BUILD_TESTS )
	enable_testing()
	add_executable( mpp_tests
		tests/test_pose.cpp
		src/OscPose.cpp
		src/PoseTracker.cpp
	)
	target_include_directories( mpp_tests PRIVATE src )
	target_link_libraries( mpp_tests PRIVATE Threads::Threads )
	if( WIN32 )
		target_link_libraries( mpp_tests PRIVATE Ws2_32 )
	endif()
	add_test( NAME pose COMMAND mpp_tests )
endif()

# Linux-only smoke test that actually runs the shaders against Mesa. Needs
# EGL and GLEW; not part of a normal Windows/macOS plugin build.
option( MPP_BUILD_HEADLESS_TEST "Build the headless GL render check (Linux)" OFF )
if( MPP_BUILD_HEADLESS_TEST AND NOT WIN32 AND NOT APPLE )
	find_package( OpenGL REQUIRED )
	find_package( GLEW REQUIRED )
	add_executable( mpp_headless
		tests/headless_render.cpp
		src/ParticleSystem.cpp
		src/PoseTracker.cpp
		src/OscPose.cpp
	)
	target_include_directories( mpp_headless PRIVATE src )
	target_link_libraries( mpp_headless PRIVATE ffgl::sdk OpenGL::GL GLEW::GLEW EGL Threads::Threads )
endif()
