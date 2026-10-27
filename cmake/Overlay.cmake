# Sources are downloaded by tools/fetch-overlay-deps.ps1, pinned and SHA-256 checked.
function(jd_add_overlay target_name)
    if(NOT DEFINED JD_DEPENDENCY_ROOT)
        set(JD_DEPENDENCY_ROOT "${CMAKE_BINARY_DIR}/dependencies")
    endif()
    set(_imgui "${JD_DEPENDENCY_ROOT}/imgui-1.93.0")
    set(_minhook "${JD_DEPENDENCY_ROOT}/minhook-1.3.4")
    if(NOT EXISTS "${_imgui}/imgui.cpp" OR NOT EXISTS "${_minhook}/src/hook.c")
        message(FATAL_ERROR "Overlay dependencies missing; run tools/fetch-overlay-deps.ps1")
    endif()
    if(NOT TARGET jd_minhook)
        add_library(jd_minhook STATIC
            "${_minhook}/src/buffer.c" "${_minhook}/src/hook.c"
            "${_minhook}/src/trampoline.c" "${_minhook}/src/hde/hde64.c")
        target_include_directories(jd_minhook PUBLIC "${_minhook}/include")
        target_compile_definitions(jd_minhook PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    target_sources(${target_name} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/overlay.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/input_capture.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/preview.cpp"
        "${_imgui}/imgui.cpp" "${_imgui}/imgui_draw.cpp"
        "${_imgui}/imgui_tables.cpp" "${_imgui}/imgui_widgets.cpp"
        "${_imgui}/backends/imgui_impl_dx11.cpp"
        "${_imgui}/backends/imgui_impl_win32.cpp")
    target_include_directories(${target_name} PRIVATE "${_imgui}" "${_minhook}/include")
    target_compile_definitions(${target_name} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(${target_name} PRIVATE jd_minhook d3d11 dxgi d3dcompiler dwmapi imm32 gdi32 user32 comctl32 dxguid cabinet)
endfunction()
