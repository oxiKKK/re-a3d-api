#
# CMakeLists.txt - a3dapi.dll reconstruction
#
# Configure for 32-bit Windows only; the original is an x86 in-proc COM server:
#
#   cmake -B build -G "Visual Studio 17 2022" -A Win32
#   cmake --build build --config Release
#
# docs/ARCHITECTURE.md describes the engine; ref/README.md lists the binaries.
#

cmake_minimum_required(VERSION 3.21)

project(a3dapi
        VERSION 3.3.677.0
        DESCRIPTION "Aureal A3D API DLL (reconstruction)"
        LANGUAGES C CXX)

if(NOT MSVC)
	message(FATAL_ERROR "a3dapi must be built with MSVC (x86)")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "a3dapi is a 32-bit DLL - configure with -A Win32")
endif()

# Match Aureal's static CRT linkage: /MTd for Debug, /MT for Release.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 98)

# Optional runtime-selectable extensions, separate from reconstruction.
option(A3D_FIXES "Include runtime-selectable project additions" ON)

foreach(_a3d_removed IN ITEMS
	A3D_EMULATION A3D_HW_REVERB_EMULATION A3D_HW_REFLECTION_EMULATION
	A3D_MP3_MINIMP3 A3D_AC3_LIBA52 A3D_ROLLING_CREDITS_NEW)
	if(DEFINED ${_a3d_removed})
		message(WARNING "${_a3d_removed} was removed; use A3D_FIXES and a3dapi.conf. See docs/reference/configuration.md.")
	endif()
endforeach()

if(A3D_FIXES)
	set(A3D_AC3_SOURCES
		${CMAKE_SOURCE_DIR}/src/a3dapi/ac3_liba52.cpp
		${CMAKE_SOURCE_DIR}/third_party/liba52/parse.c
		${CMAKE_SOURCE_DIR}/third_party/liba52/bitstream.c
		${CMAKE_SOURCE_DIR}/third_party/liba52/bit_allocate.c
		${CMAKE_SOURCE_DIR}/third_party/liba52/imdct.c
		${CMAKE_SOURCE_DIR}/third_party/liba52/downmix.c
	)
endif()

add_subdirectory(src/a3dapi)

option(A3D_BUILD_A3D "Build the a3d.dll (A3D 1.x) reconstruction" ON)

if(A3D_BUILD_A3D)
	add_subdirectory(src/a3d)
endif()

# Build the SDK samples as end-to-end runtime checks (samples/README.md).

option(A3D_BUILD_SAMPLES "Build the A3D 2.0 SDK sample programs" ON)

if(A3D_BUILD_SAMPLES)
	add_subdirectory(samples)
endif()

option(A3D_BUILD_DIAGNOSTICS "Build runtime probes and process diagnostics" ON)
option(A3D_BUILD_TESTS "Build ABI, API, audio, and linked implementation tests" OFF)
add_subdirectory(tools)
if(A3D_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_test(NAME audible_render_gate
        COMMAND a3d_pcm_capture --dll "${A3D_COMPARISON_DLL}"
            --wave "${CMAKE_SOURCE_DIR}/samples/data/heli.wav" --scene default)
    set_tests_properties(audible_render_gate PROPERTIES LABELS "audio" TIMEOUT 30)
endif()

# Retain the Python cache entry used by existing build directories.
find_program(A3D_PYTHON NAMES python3 python py)

# Show effective project settings after configuration.
message(STATUS "")
message(STATUS "A3D ${PROJECT_VERSION} configuration")
message(STATUS "----------------------------------------")
message(STATUS "  A3D_BUILD_A3D               : ${A3D_BUILD_A3D}")
message(STATUS "  A3D_BUILD_DIAGNOSTICS       : ${A3D_BUILD_DIAGNOSTICS}")
message(STATUS "  A3D_BUILD_SAMPLES           : ${A3D_BUILD_SAMPLES}")
message(STATUS "  A3D_BUILD_TESTS             : ${A3D_BUILD_TESTS}")
message(STATUS "  A3D_COMPARISON_DLL          : ${A3D_COMPARISON_DLL}")
message(STATUS "  A3D_DX_D3DIM                : ${A3D_DX_D3DIM}")
message(STATUS "  A3D_EAX_INCLUDE             : ${A3D_EAX_INCLUDE}")
message(STATUS "  A3D_FIXES                   : ${A3D_FIXES}")
message(STATUS "  A3D_PYTHON                  : ${A3D_PYTHON}")
message(STATUS "")
