#
# CMakeLists.txt - the seven A3D 3.0 SDK samples, built against the reconstruction
#
# SDK samples load the registered CLSID_A3dApi through CoCreateInstance.
# Changing registration selects the DLL without rebuilding the samples.
# See samples/README.md for prerequisites and test limitations.
#

# glaux.lib has not shipped in a Windows SDK since 2003.  Ours: eight entry
# points, in samples/lib.  polygon and reflect need it; the rest do not.
add_library(a3dglaux STATIC lib/glaux.c)

target_include_directories(a3dglaux PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib")
target_link_libraries(a3dglaux PUBLIC opengl32 glu32 user32 gdi32)
set_property(TARGET a3dglaux PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Build the SDK ia3dutil source; its 3.0 A3dCreate requests IID_IA3d5.
add_library(a3dutil STATIC lib/ia3dutil.c)

target_include_directories(a3dutil PUBLIC "${CMAKE_SOURCE_DIR}/inc")
target_link_libraries(a3dutil PUBLIC ole32 advapi32)
set_property(TARGET a3dutil PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Four of the seven share Aureal's own helper file.
add_library(a3dsamplecommon STATIC common/samplecommon.cpp)

target_include_directories(a3dsamplecommon PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/common"
	"${CMAKE_SOURCE_DIR}/inc")
set_property(TARGET a3dsamplecommon PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

# --------------------------------------------------------------------------

function(a3d_sample name)
	cmake_parse_arguments(S "WIN32APP" "DIR" "SOURCES;LIBS" ${ARGN})

	if(NOT S_DIR)
		set(S_DIR ${name})
	endif()
	if(NOT S_SOURCES)
		set(S_SOURCES ${S_DIR}/${name}.cpp)
	endif()

	if(S_WIN32APP)
		add_executable(${name} WIN32 ${S_SOURCES})
	else()
		add_executable(${name} ${S_SOURCES})
	endif()

	target_include_directories(${name} PRIVATE
		"${CMAKE_SOURCE_DIR}/inc"
		"${CMAKE_CURRENT_SOURCE_DIR}/${S_DIR}")
	target_link_libraries(${name} PRIVATE
		a3dutil ole32 user32 winmm ${S_LIBS})

	set_target_properties(${name} PROPERTIES
		LINK_FLAGS "/MANIFEST:NO /MAP"
		MSVC_RUNTIME_LIBRARY "MultiThreaded"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

	# void main() - Aureal's, left alone; C4326 is that and nothing else.
	target_compile_options(${name} PRIVATE /wd4326)

	# No WIN32_LEAN_AND_MEAN here, unlike the two DLLs: several of the samples
	# call timeGetTime() and expect windows.h to have pulled in mmsystem.h,
	# which it did in 1999 and does not under LEAN_AND_MEAN.
	target_compile_definitions(${name} PRIVATE
		WIN32 _CRT_SECURE_NO_WARNINGS)
endfunction()

a3d_sample(polygon            LIBS a3dglaux)
a3d_sample(reflect            LIBS a3dglaux)
a3d_sample(loading            LIBS a3dsamplecommon)
a3d_sample(reverb             LIBS a3dsamplecommon)
a3d_sample(ManualReflections  LIBS a3dsamplecommon)

#
# Project-added SceneRooms compares eight effects in separate rooms using
# OpenGL. Its command line selects the DLL directly; version.lib supplies
# the loaded DLL version. Add other scenes as separate sample targets.
#
a3d_sample(SceneRooms  DIR SceneRooms  SOURCES SceneRooms/SceneRooms.cpp
           LIBS version opengl32 glu32)

#
# Optional SDK samples require separately distributed headers:
#   PropSets: A3D_EAX_INCLUDE points to Creative's eax.h.
#   volsrc: A3D_DX_D3DIM points to the DirectX 6 D3DIM sample framework.
# See each sample's original building.txt.
#

set(A3D_EAX_INCLUDE "" CACHE PATH
    "Directory holding eax.h from the Creative EAX SDK, for the PropSets sample")

if(A3D_EAX_INCLUDE)
	a3d_sample(PropSets DIR propsets SOURCES propsets/PropSets.cpp
	           LIBS a3dsamplecommon)
	target_include_directories(PropSets PRIVATE "${A3D_EAX_INCLUDE}")
else()
	message(STATUS "samples: PropSets skipped, set A3D_EAX_INCLUDE to build it")
endif()

# volsrc requires the DirectX 6 D3DIM framework (include and src\d3dframe).
# DirectX 7 renamed CD3DFramework and removed D3DEnum_DriverInfo, so it is
# incompatible. Compile the framework source; modern linkers reject its
# shipped d3dframe.lib. Current Windows SDK headers and ddraw.lib supply
# the DirectDraw/Direct3D APIs; no d3dim.lib is needed.
#
# Stage the five required headers under dx in the build tree to match
# volsrc's include paths.
#
set(A3D_DX_D3DIM "" CACHE PATH
    "The DirectX SDK's samples/Multimedia/D3DIM directory, for the volsrc sample")

if(A3D_DX_D3DIM)
	set(dxstage "${CMAKE_CURRENT_BINARY_DIR}/dxinc/dx")
	file(MAKE_DIRECTORY "${dxstage}")
	foreach(h D3DFrame D3DEnum D3DUtil D3DTextr D3DMath D3DApp D3DFile D3DRes)
		if(EXISTS "${A3D_DX_D3DIM}/include/${h}.h")
			configure_file("${A3D_DX_D3DIM}/include/${h}.h"
				       "${dxstage}/${h}.h" COPYONLY)
		endif()
	endforeach()

	# Copy required headers and dependencies; omit unused d3dapp/d3dfile code.
	set(dxframe "")
	foreach(c D3DFrame d3denum d3dutil d3dtextr d3dmath)
		file(GLOB found "${A3D_DX_D3DIM}/src/d3dframe/${c}.cpp")
		list(APPEND dxframe ${found})
	endforeach()

	# DEBUG_ERR and DEBUG_MSG are diagnostic macros the framework expects from
	# a DX SDK header that the sample mirrors do not carry.  They print and
	# nothing else, so they compile away.
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dxinc/a3ddxdebug.h"
"/* Project-added.  See samples/CMakeLists.txt and samples/README.md. */\n"
"#ifndef _A3D_DX_DEBUG_H\n#define _A3D_DX_DEBUG_H\n"
"#define DEBUG_MSG(s)		((void) 0)\n"
"#define DEBUG_ERR(hr, s)	((void) 0)\n"
"#endif\n")

	add_library(a3dd3dframe STATIC ${dxframe})
	target_include_directories(a3dd3dframe PUBLIC
		"${CMAKE_CURRENT_BINARY_DIR}/dxinc"
		"${A3D_DX_D3DIM}/include")
	target_compile_definitions(a3dd3dframe PRIVATE
		WIN32 _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
	# 1999 code: `for (int i...)` leaks its counter, which MSVC has not done
	# by default since VC7.
	target_compile_options(a3dd3dframe PRIVATE
		/Zc:forScope- /FI"${CMAKE_CURRENT_BINARY_DIR}/dxinc/a3ddxdebug.h")
	set_property(TARGET a3dd3dframe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

	a3d_sample(volsrc WIN32APP
		SOURCES volsrc/winmain.cpp volsrc/volsrc.cpp volsrc/winmain.rc
		LIBS a3dd3dframe gdi32 comdlg32 ddraw dxguid)
	target_include_directories(volsrc PRIVATE
		"${CMAKE_CURRENT_BINARY_DIR}/dxinc"
		"${A3D_DX_D3DIM}/include")
else()
	message(STATUS "samples: volsrc skipped, set A3D_DX_D3DIM to build it")
endif()

# --------------------------------------------------------------------------
# Samples open media by filename, so copy it beside the executables.
# Scooter.mp3 and Spoken_Chan1.ac3 still require an available decoder.
# --------------------------------------------------------------------------

set(A3D_SAMPLE_MEDIA
	Heli.wav linn1.wav linn48.wav FILE0038.WAV
	Scooter.mp3 Spoken_Chan1.ac3)

foreach(media ${A3D_SAMPLE_MEDIA})
	add_custom_command(TARGET polygon POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/data/${media}"
			"$<TARGET_FILE_DIR:polygon>/${media}"
		VERBATIM)
endforeach()
