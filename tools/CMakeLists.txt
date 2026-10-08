# Project-added diagnostics and the PCM capture backend used by audio tests.
if(A3D_BUILD_DIAGNOSTICS)
    add_executable(dump_thread_stacks diagnostics/dump_thread_stacks.cpp)
    target_link_libraries(dump_thread_stacks PRIVATE dbghelp)
    add_executable(a3d_goldsrc_init_probe probes/goldsrc_init/main.cpp)
    target_link_libraries(a3d_goldsrc_init_probe PRIVATE ole32 winmm user32 dbghelp dsound dxguid)
    add_executable(a3d_software_playback_probe probes/software_playback/main.cpp)
    target_link_libraries(a3d_software_playback_probe PRIVATE ole32)
    add_executable(a3d_sdk_loading_probe probes/sdk_loading/main.cpp)
    target_link_libraries(a3d_sdk_loading_probe PRIVATE ole32 winmm)
    foreach(target dump_thread_stacks a3d_goldsrc_init_probe a3d_software_playback_probe a3d_sdk_loading_probe)
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/inc" "${CMAKE_CURRENT_SOURCE_DIR}/support")
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
    endforeach()
endif()

if(A3D_BUILD_DIAGNOSTICS OR A3D_BUILD_TESTS)
    add_executable(a3d_pcm_capture
        capture/main.cpp capture/capture_session.cpp capture/capture_scenes.cpp
        capture/recording_directsound.cpp capture/pcm_statistics.cpp capture/wav_writer.cpp)
    target_include_directories(a3d_pcm_capture PRIVATE
        "${PROJECT_SOURCE_DIR}/inc" "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/support")
    target_compile_features(a3d_pcm_capture PRIVATE cxx_std_17)
    target_link_libraries(a3d_pcm_capture PRIVATE ole32 winmm psapi advapi32)
    set_target_properties(a3d_pcm_capture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
    add_dependencies(a3d_pcm_capture a3dapi)
endif()
