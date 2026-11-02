cmake_minimum_required(VERSION 3.20)
project(solenshaded VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(MSVC)
    add_compile_options(/W4 /O2 /permissive- /wd4244 /wd4267 /wd4324 /openmp)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(EMSCRIPTEN)
    add_compile_options(-Wall -Wextra -O3 -fexceptions)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -O3)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# 1. Bundled GLFW 3.4 Windowing
if(NOT EMSCRIPTEN)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
endif()

# 2. Standalone Eatscript Scripting & Host ABI Library
add_library(eats_script STATIC
    src/eatscript/lexer.cpp
    src/eatscript/parser.cpp
    src/eatscript/vm.cpp
    src/eatscript/transpiler.cpp
    src/eatscript/evaluator.cpp
    src/eatscript/host_abi.cpp
    src/eatscript/stdlib.cpp
    src/eatscript/repl.cpp
    src/eatscript/dispatch_scanner.cpp
)
target_include_directories(eats_script PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 3. SolEnshaded Core Library (Headless Solitaire Rules, Procedural 2D Batch Renderer, SVG Path & Foley)
add_library(solenshaded_core STATIC
    src/game/solitaire_board.cpp
    src/audio/audio_foley.cpp
    src/ui/batch_renderer_2d.cpp
    src/ui/svg_path.cpp
    src/ui/base64.cpp
    src/ui/monospace_font_8x16.cpp
    src/ui/theme.cpp
    src/ui/glyph/ebf_font.cpp
    src/ui/glyph/vector_font.cpp
    src/ui/glyph/glyph_packs.cpp
    third_party/stb/stb_image.c
)
target_include_directories(solenshaded_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)
if(NOT EMSCRIPTEN)
    target_include_directories(solenshaded_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/include
    )
endif()
target_compile_definitions(solenshaded_core PUBLIC EATS_ENABLE_WEBGPU)
target_link_libraries(solenshaded_core PUBLIC eats_script)

if(WIN32 AND NOT EMSCRIPTEN)
    set(WGPU_NATIVE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/lib/wgpu_native.dll.lib")
    target_link_libraries(solenshaded_core PUBLIC glfw ole32 winmm user32 gdi32 "${WGPU_NATIVE_LIB}")
elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(solenshaded_core PUBLIC pthread dl m)
endif()

# 4. SolEnshaded Procedural Table Renderer Library
add_library(solenshaded_render STATIC
    src/render/table_renderer.cpp
    src/render/card_texture_cache.cpp
    src/render/procedural_background.cpp
)
target_include_directories(solenshaded_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanosvg
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)
if(NOT EMSCRIPTEN)
    target_include_directories(solenshaded_render PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/include
    )
endif()

if(EMSCRIPTEN)
    target_compile_options(solenshaded_core PUBLIC --use-port=emdawnwebgpu)
    target_compile_options(solenshaded_render PUBLIC --use-port=emdawnwebgpu)
    target_link_libraries(solenshaded_render PUBLIC solenshaded_core)

    # 5. Main SolEnshaded WebAssembly & WebGPU Application
    add_executable(solenshaded_web src/main.cpp)
    target_compile_options(solenshaded_web PRIVATE -O3 -msimd128 -fexceptions --use-port=emdawnwebgpu)
    target_compile_definitions(solenshaded_web PRIVATE NDEBUG EATS_ENABLE_WEBGPU)
    target_link_libraries(solenshaded_web PRIVATE solenshaded_render)
    set_target_properties(solenshaded_web PROPERTIES
        OUTPUT_NAME "solenshaded"
        SUFFIX ".html"
        LINK_FLAGS "-O3 -msimd128 -sNO_EXIT_RUNTIME=1 -fexceptions --use-port=emdawnwebgpu -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_FUNCTIONS=['_main','_solenshaded_load_file_from_web'] -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/decks/classic.eats@/decks/classic.eats --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/themes/rosewood_parlor.eats@/themes/rosewood_parlor.eats --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
    )
    add_custom_command(TARGET solenshaded_web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/solenshaded.html"
            "${CMAKE_BINARY_DIR}/index.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/manifest.json"
            "${CMAKE_BINARY_DIR}/manifest.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/sw.js"
            "${CMAKE_BINARY_DIR}/sw.js"
        COMMENT "Copying PWA manifest, service worker, and index.html to web root"
    )
else()
    target_link_libraries(solenshaded_render PUBLIC solenshaded_core glfw)
    if(WIN32)
        set(WGPU_NATIVE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/lib/wgpu_native.dll.lib")
        target_link_libraries(solenshaded_render PUBLIC
            user32 gdi32 shell32 opengl32 "${WGPU_NATIVE_LIB}"
        )
    endif()

    # 5. Main SolEnshaded Executable
    add_executable(solenshaded src/main.cpp)
    target_link_libraries(solenshaded PRIVATE solenshaded_render)

    if(WIN32)
        add_custom_command(TARGET solenshaded POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/lib/wgpu_native.dll"
                "$<TARGET_FILE_DIR:solenshaded>/wgpu_native.dll"
        )
    endif()
endif()
