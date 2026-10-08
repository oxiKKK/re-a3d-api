#
# CMakeLists.txt - a3dapi.dll sources and build settings.
#
# Defines the shared-library target for the API engine, geometry, resource
# manager and DAL backends. It also selects optional decoder and software
# effect sources, includes the export and version-resource files, and sets
# the target's system libraries, output location and compiler options.
#
# The source list is exported to the parent build for reuse by validation
# targets. Root configuration supplies the project options and toolchain.
#
# Configured from the root CMakeLists.txt. See docs/FILEMAP.md for attribution.
# Preserve the three 3.3 link-order groups; MSBuild does not guarantee object order.
# File comments record Debug region starts or other placement evidence.
#
set(A3DAPI_SOURCES
	# Run 1: the API core.
	A3d3.cpp			# dbg:0x100087db
	a3dclsfc.cpp			# dbg:0x1000cb97
	A3dGeom.cpp			# dbg:0x1000e8bd
	a3dlegacy.cpp			# dbg:0x1001584e
	A3dList.cpp			# dbg:0x100173c0
	A3dReflection.cpp		# dbg:0x100195ef
	A3dReverb.cpp			# dbg:0x1001b6dd
	A3dRoot.cpp			# dbg:0x1001ce40
	A3dSource.cpp			# dbg:0x1001e25b
	a3dsourcecom.cpp		# dbg:0x1002f639
	ac3fgraph.cpp			# dbg:0x1003211a
	apimapper.cpp			# dbg:0x10036e73
	ChunkPage.cpp			# dbg:0x1003c200
	fmath.cpp			# dbg:0x1003c700	(ours; the fmath region ChunkPage.cpp's tail folds in)
	Listener.cpp			# dbg:0x1003cb07
	NamedObject.cpp			# dbg:0x1003d6f0	(ours; the span no 3.3 image names)
	PropertySetItem.cpp		# dbg:0x1003d8c0
	refaudbin.cpp			# dbg:0x1003e550
	splash.cpp			# no region; dbg:0x1003f340 is its one witnessed function

	# Run 2: the DAL and the resource manager.
	a2dbuffer.cpp			# dbg:0x1003f7eb
	d2dbuffer.cpp			# dbg:0x10041f10
	d3dbuffer.cpp			# dbg:0x10043f6c
	dal_a2d.cpp			# dbg:0x10048bc5
	dal_d2d.cpp			# dbg:0x1004c49d
	dal_d3d.cpp			# dbg:0x1004e142
	dal_emu.cpp			# dbg:0x10052176
	dalinfo.cpp			# dbg:0x1005395c
	emubuffer.cpp			# dbg:0x10058308
	Plex.cpp			# dbg:0x1005a641
	resman.cpp			# dbg:0x1005b6dd
	rmbuffer.cpp			# dbg:0x1006adc9
	rmstatbuffer.cpp		# dbg:0x1006d0d0
	rmstreambuffer.cpp		# dbg:0x100703f0
	Waveform.cpp			# no region; the link table places it after rmstreambuffer.obj
	softmix.cpp			# no region; the link table places it after Waveform.obj

	# Run 3: the geometry.
	A3dFrame.cpp			# no region; named only by the Debug link table
	A3dOpening.cpp			# no region; named only by the Debug link table
	A3dOpeningBuilder.cpp		# no region; named only by the Debug link table
	A3dRoom.cpp			# dbg:0x10076d96
	A3dRoomBuilder.cpp		# dbg:0x1007b40a
	A3dScene.cpp			# dbg:0x1007d090
	A3dWall.cpp			# dbg:0x1007f8fd
	A3dWallBuilder.cpp		# dbg:0x1008384c
	MaterialObject.cpp		# dbg:0x10084d2a
	Polygon.cpp			# dbg:0x10085130
	PolygonBuilder.cpp		# dbg:0x10085d19
	sceneman.cpp			# dbg:0x10087993
	WallEdge.cpp			# no region; named only by the Debug link table

	# 3.3 names no file for this code.
	A3dMatrix.cpp			# (ours)
	A3dPrivate.cpp			# (ours)
	Corners.cpp			# (ours)
	hrtfmgr.cpp			# (ours)
	MaterialLink.cpp		# (ours)
	outqueue.cpp			# (ours)

	a3dguid.cpp			# data, no .text of its own
)

# The project's minimp3 MP3 decoder, behind the seam.  NOT PART OF THE
# ORIGINAL; compiled into the DLL and the test harness only when the option is
# on, so the differential measures the same translation units either way.
if(A3D_FIXES)
	list(APPEND A3DAPI_SOURCES A3dConfig.cpp mp3ssc_minimp3.cpp)
endif()

set(A3DAPI_SOURCES ${A3DAPI_SOURCES} PARENT_SCOPE)

add_library(a3dapi SHARED
	${A3DAPI_SOURCES}
	a3dapi.def
	a3dapi.rc
)

target_include_directories(a3dapi PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/inc"
)

target_compile_definitions(a3dapi PRIVATE
	WIN32
	_WINDOWS
	_USRDLL
	STRICT
	WIN32_LEAN_AND_MEAN
	_CRT_SECURE_NO_WARNINGS
)

if(A3D_FIXES)
	target_sources(a3dapi PRIVATE ${A3D_AC3_SOURCES}
		A3dReverbEmu.cpp A3dReflectionEmu.cpp)
	target_include_directories(a3dapi PRIVATE
		"${CMAKE_SOURCE_DIR}/third_party/minimp3"
		"${CMAKE_SOURCE_DIR}/third_party/liba52")
	target_compile_definitions(a3dapi PRIVATE A3D_FIXES)
	add_custom_command(TARGET a3dapi POST_BUILD
		COMMAND ${CMAKE_COMMAND}
			-DINPUT=${CMAKE_SOURCE_DIR}/a3dapi.conf
			-DOUTPUT=$<TARGET_FILE_DIR:a3dapi>/a3dapi.conf
			-P ${CMAKE_SOURCE_DIR}/tools/cmake/CopyConfig.cmake
		VERBATIM)
endif()

#
# Use /arch:IA32 to retain x87 arithmetic. MSVC 5/6 keeps 80-bit
# intermediates; modern x86 defaults to SSE2. Float underflow can change
# A3dToPolar's fFlat == 0.0f branch. x87 C0 also represents unordered
# comparisons, affecting NaN rejection in fGain < 0.0f.
#
target_compile_options(a3dapi PRIVATE /arch:IA32 /fp:precise)

# In-process server without an import-library consumer or manifest.
set_target_properties(a3dapi PROPERTIES
	PREFIX ""
	OUTPUT_NAME "a3dapi"
	LINK_FLAGS "/MANIFEST:NO /MAP"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_link_libraries(a3dapi PRIVATE ole32 version winmm user32 dsound)
