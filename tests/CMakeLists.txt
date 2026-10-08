set(A3D_COMPARISON_DLL "${CMAKE_BINARY_DIR}/Release/a3dapi.dll" CACHE FILEPATH "Release reconstruction used by runtime comparisons, including Debug test builds")
# Project-added reference comparisons and statically linked integration tests.
# Keep a3dapi_obj compilation flags identical to the production DLL.

set(_a3dapi_dir ${CMAKE_SOURCE_DIR}/src/a3dapi)
set(_ref ${CMAKE_SOURCE_DIR}/ref)

# Vendored GoogleTest is built only when tests are enabled.

set(_gtest_dir ${CMAKE_SOURCE_DIR}/third_party/googletest)
add_library(gtest STATIC ${_gtest_dir}/src/gtest-all.cc)
target_include_directories(gtest PUBLIC ${_gtest_dir}/include PRIVATE ${_gtest_dir})
target_compile_features(gtest PUBLIC cxx_std_20)
target_compile_definitions(gtest PUBLIC
	GTEST_HAS_PTHREAD=0
	_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
	_SILENCE_ALL_CXX20_DEPRECATION_WARNINGS)

# Compile the reconstruction with the DLL compiler definitions and x87 flags.

set(_obj_srcs "")
foreach(s IN LISTS A3DAPI_SOURCES)
	list(APPEND _obj_srcs ${_a3dapi_dir}/${s})
endforeach()

add_library(a3dapi_obj OBJECT ${_obj_srcs})
target_include_directories(a3dapi_obj PRIVATE
	${_a3dapi_dir}
	${CMAKE_SOURCE_DIR}/inc
)
target_compile_definitions(a3dapi_obj PRIVATE
	WIN32 _WINDOWS _USRDLL STRICT WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
)
target_compile_options(a3dapi_obj PRIVATE /arch:IA32 /fp:precise)

if(A3D_FIXES)
	target_sources(a3dapi_obj PRIVATE ${A3D_AC3_SOURCES}
		${_a3dapi_dir}/A3dReverbEmu.cpp
		${_a3dapi_dir}/A3dReflectionEmu.cpp)
	target_include_directories(a3dapi_obj PRIVATE
		"${CMAKE_SOURCE_DIR}/third_party/minimp3"
		"${CMAKE_SOURCE_DIR}/third_party/liba52")
	target_compile_definitions(a3dapi_obj PRIVATE A3D_FIXES)
endif()

# The reference DLL matched to the build configuration (Layer 1 reads both).
set(_refdll "$<IF:$<CONFIG:Debug>,${_ref}/a3dapi_33_dbg.dll,${_ref}/a3dapi_33_rtl.dll>")

# ABI checks inspect the matching configuration. Runtime comparisons use Release
# to avoid Debug DLL assertion dialogs. Child processes load explicit DLL paths.

add_executable(a3d_reference_comparison_tests
	gtest_main.cpp
	guids.cpp
	support/support_headers_tests.cpp
	support/tooling_contract_tests.cpp
	abi/reference_abi_tests.cpp
	support/comparison_fixture.cpp
	support/scenario_runner.cpp
	support/child_process_runner.cpp
	support/result_protocol.cpp
	api/root_comparison_tests.cpp
	api/source_comparison_tests.cpp
	api/source_data_comparison_tests.cpp
	api/scene_comparison_tests.cpp
	api/mapper_comparison_tests.cpp
	api/listener_comparison_tests.cpp
	api/reverb_comparison_tests.cpp
	api/geometry_comparison_tests.cpp
	api/occlusion_comparison_tests.cpp
	api/scenerooms_comparison_tests.cpp
	api/material_comparison_tests.cpp
	api/reflection_comparison_tests.cpp
	api/propertyset_comparison_tests.cpp
	api/legacy_comparison_tests.cpp
	api/error_comparison_tests.cpp
	audio/reference_pcm_tests.cpp
)
target_include_directories(a3d_reference_comparison_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/support
	${CMAKE_SOURCE_DIR}/tools/support
	${CMAKE_SOURCE_DIR}/inc
	# src for the published ia3ddal.h: the child denies CLSID_A3dDal so a
	# registered build cannot enter the process (support/com_server_isolation.hpp).
	${CMAKE_SOURCE_DIR}/src
)
target_compile_features(a3d_reference_comparison_tests PRIVATE cxx_std_20)
target_compile_definitions(a3d_reference_comparison_tests PRIVATE
	A3D_OUR_DLL="${CMAKE_BINARY_DIR}/$<CONFIG>/a3dapi.dll"
	A3D_OUR_MAP="${CMAKE_BINARY_DIR}/$<CONFIG>/a3dapi.map"
	A3D_REF_DLL="${_refdll}"
	A3D_GROUNDTRUTH="${CMAKE_SOURCE_DIR}/docs/llm/groundtruth"
	A3D_AB_OUR_DLL="${A3D_COMPARISON_DLL}"
	A3D_AB_REF_DLL="${_ref}/a3dapi_33_rtl.dll"
	A3D_AB_WAV="${CMAKE_SOURCE_DIR}/samples/data/heli.wav"
	A3D_SCENE_DATA_DIR="${CMAKE_SOURCE_DIR}/samples/data"
	A3D_PCM_CAPTURE_EXE="$<TARGET_FILE:a3d_pcm_capture>"
	A3D_CAPTURE_OUTPUT_DIR="${CMAKE_BINARY_DIR}/artifacts/audio"
	A3D_INTERFACE_DIFF_CHILD
	WIN32_LEAN_AND_MEAN
	NOMINMAX
	_CRT_SECURE_NO_WARNINGS
)


if(A3D_FIXES)
	target_compile_definitions(a3d_reference_comparison_tests PRIVATE
		A3D_FIXES A3D_CREDITS_REF_DLL="${_ref}/a3dapi_33_678.dll")
endif()

target_link_libraries(a3d_reference_comparison_tests PRIVATE gtest ole32 uuid version winmm user32)
set_target_properties(a3d_reference_comparison_tests PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_dependencies(a3d_reference_comparison_tests a3dapi)
# The audio differential (Layer 3) spawns a3d_pcm_capture against both DLLs.
add_dependencies(a3d_reference_comparison_tests a3d_pcm_capture)
add_test(NAME a3d_reference_comparison_tests COMMAND a3d_reference_comparison_tests)

# Link the reconstruction directly for initialization and optional decoder tests.

add_executable(a3d_linked_implementation_tests
	gtest_main.cpp
	integration/linked_initialization_tests.cpp
	integration/diagnostic_macro_tests.cpp
	integration/a2d_capabilities_tests.cpp
	integration/runtime_config_tests.cpp
	integration/resman_buffer_lifetime_tests.cpp
	$<TARGET_OBJECTS:a3dapi_obj>
)
target_include_directories(a3d_linked_implementation_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/support
	${CMAKE_SOURCE_DIR}/tools/support
	${CMAKE_SOURCE_DIR}/inc
	${_a3dapi_dir}
)
target_compile_features(a3d_linked_implementation_tests PRIVATE cxx_std_20)
target_compile_definitions(a3d_linked_implementation_tests PRIVATE
	WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

if(A3D_FIXES)
	target_compile_definitions(a3d_linked_implementation_tests PRIVATE
		A3D_FIXES)
endif()
target_link_libraries(a3d_linked_implementation_tests PRIVATE gtest ole32 uuid version winmm user32 dsound)
set_target_properties(a3d_linked_implementation_tests PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(A3D_FIXES)
	target_sources(a3d_linked_implementation_tests PRIVATE decoders/mp3_decode_tests.cpp)
	target_include_directories(a3d_linked_implementation_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/minimp3)
	target_compile_definitions(a3d_linked_implementation_tests PRIVATE
		A3D_FIXES
		A3D_MP3_SAMPLE="${CMAKE_SOURCE_DIR}/samples/data/Scooter.mp3")
endif()

if(A3D_FIXES)
	target_sources(a3d_linked_implementation_tests PRIVATE decoders/ac3_decode_tests.cpp)
	target_include_directories(a3d_linked_implementation_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/liba52)
	target_compile_definitions(a3d_linked_implementation_tests PRIVATE
		A3D_FIXES
		A3D_AC3_SAMPLE="${CMAKE_SOURCE_DIR}/samples/data/Spoken_Chan1.ac3")
endif()

add_test(NAME a3d_linked_implementation_tests COMMAND a3d_linked_implementation_tests)

set_tests_properties(a3d_reference_comparison_tests PROPERTIES LABELS "abi;api;audio" TIMEOUT 240)
set_tests_properties(a3d_linked_implementation_tests PROPERTIES LABELS "integration;decoders" TIMEOUT 60)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/artifacts/audio")

if(A3D_FIXES)
	add_test(NAME q3_emulation_playback COMMAND a3d_pcm_capture
		--dll "$<TARGET_FILE:a3dapi>" --wave "${CMAKE_SOURCE_DIR}/samples/data/heli.wav"
		--client q3 --scene right)
	set_tests_properties(q3_emulation_playback PROPERTIES LABELS "emulation;audio" TIMEOUT 35)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME diagnostic_side_effect_audit
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/analysis/check_assertion_side_effects.py)
add_test(NAME diagnostic_audit_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/analysis/test_check_assertion_side_effects.py)

if(A3D_FIXES)
	target_link_libraries(a3d_linked_implementation_tests PRIVATE dbghelp)
	add_test(NAME runtime_configuration
		COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/integration/runtime_config_matrix.py
			--exe $<TARGET_FILE:a3d_linked_implementation_tests>
			--dll $<TARGET_FILE:a3dapi>
			--capture $<TARGET_FILE:a3d_pcm_capture>
			--wave ${CMAKE_SOURCE_DIR}/samples/data/heli.wav
			--output ${CMAKE_BINARY_DIR}/artifacts/runtime-config/$<CONFIG>)
	set_tests_properties(runtime_configuration PROPERTIES
		LABELS "integration;configuration" TIMEOUT 120)
endif()
