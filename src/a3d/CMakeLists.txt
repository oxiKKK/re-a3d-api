#
# CMakeLists.txt - a3d.dll reconstruction
#
# Defines the a3d.dll shared-library target for A3D 1.x compatibility.
# It combines the device and buffer wrappers, spatial DSP implementation,
# COM exports, GUID definitions and version resource into one DLL.
#
# The root CMakeLists.txt controls whether this target is built and supplies
# the toolchain configuration. This file sets local includes and defines,
# required Windows libraries, linker options and the DLL output location.
#

add_library(a3d SHARED
	A3d.cpp
	Listener.cpp
	dsbuffer.cpp
	A3dSource.cpp
	a3dclsfc.cpp
	Plex.cpp
	a3ddsp.cpp
	a3dguid.cpp
	a3d.rc
	a3d.def
)

target_include_directories(a3d PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/inc"
)

target_compile_definitions(a3d PRIVATE
	WIN32
	_WINDOWS
	_USRDLL
	STRICT
	WIN32_LEAN_AND_MEAN
	_CRT_SECURE_NO_WARNINGS
)

# In-process server without an import-library consumer or manifest.
set_target_properties(a3d PROPERTIES
	PREFIX ""
	OUTPUT_NAME "a3d"
	LINK_FLAGS "/MANIFEST:NO /MAP"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_link_libraries(a3d PRIVATE ole32 advapi32 user32 shell32 dsound)
