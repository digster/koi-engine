# ============================================================================
#  Koi Engine — top-level build script
# ----------------------------------------------------------------------------
#  This file teaches CMake three things:
#    1. How to find SDL3 (our windowing + GPU library, installed via Homebrew).
#    2. How our source code is grouped into a reusable engine library + an app.
#    3. How to build and register our test runner.
#
#  Mental model: CMake is a *meta build system*. It doesn't compile code itself;
#  it generates the files (Ninja/Make/Xcode projects) that the real compiler
#  driver uses. We describe *what* we want ("an executable made of these files,
#  linked against SDL3"), and CMake figures out the platform-specific *how*.
# ============================================================================

cmake_minimum_required(VERSION 3.28)

project(koi_engine
    VERSION 0.0.1
    DESCRIPTION "A learn-by-building 3D engine using SDL3 and C++"
    LANGUAGES CXX)

# ----------------------------------------------------------------------------
#  Language standard
#  We target C++20: it gives us designated initializers (handy for the verbose
#  SDL_GPU "...CreateInfo" structs), <format>, concepts, etc.
#  EXTENSIONS OFF keeps us on standard C++ rather than compiler-specific dialects.
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json so editors / clangd get accurate IntelliSense.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to a Debug build if the user didn't pick one (single-config generators
# like Ninja need this; multi-config ones like Xcode ignore it).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ----------------------------------------------------------------------------
#  Dependencies
# ----------------------------------------------------------------------------

# SDL3: provides the window, the event loop, and the cross-platform GPU API.
# Homebrew ships a CMake "config package", so find_package gives us the
# imported target `SDL3::SDL3` with all include dirs + link flags attached.
find_package(SDL3 REQUIRED CONFIG)

# doctest: a single-header C++ test framework. doctest's own CMake project
# declares a minimum version too old for CMake 4.x, so instead of building that
# project we just download the one header it ships and wrap it in an INTERFACE
# target (a header-only "library" with no compiled output). Pinning the version
# keeps the test setup reproducible.
set(DOCTEST_VERSION 2.4.11)
set(DOCTEST_DIR ${CMAKE_BINARY_DIR}/_deps/doctest)
if(NOT EXISTS ${DOCTEST_DIR}/doctest/doctest.h)
    message(STATUS "Downloading doctest ${DOCTEST_VERSION} header...")
    file(DOWNLOAD
        https://raw.githubusercontent.com/doctest/doctest/v${DOCTEST_VERSION}/doctest/doctest.h
        ${DOCTEST_DIR}/doctest/doctest.h
        TLS_VERIFY ON
        STATUS doctest_dl_status)
    list(GET doctest_dl_status 0 doctest_dl_code)
    if(NOT doctest_dl_code EQUAL 0)
        list(GET doctest_dl_status 1 doctest_dl_msg)
        message(FATAL_ERROR "Failed to download doctest: ${doctest_dl_msg}")
    endif()
endif()
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${DOCTEST_DIR})
add_library(doctest::doctest ALIAS doctest)

# Model loaders: two single-header libraries, downloaded like doctest (Step 9).
# tinyobjloader parses Wavefront .obj; cgltf parses glTF (.gltf/.glb). We use a
# library here — rather than hand-rolling parsers — so the from-scratch effort goes
# into the graphics. They are header-only: exactly one .cpp (ModelLoader.cpp) defines
# their *_IMPLEMENTATION, and that file is compiled WITHOUT our strict warnings since
# we don't control third-party code.
set(MODELS_DIR ${CMAKE_BINARY_DIR}/_deps/models)
function(koi_fetch_header url dest)
    if(NOT EXISTS ${dest})
        message(STATUS "Downloading ${dest}...")
        file(DOWNLOAD ${url} ${dest} TLS_VERIFY ON STATUS _st)
        list(GET _st 0 _code)
        if(NOT _code EQUAL 0)
            list(GET _st 1 _msg)
            message(FATAL_ERROR "Failed to download ${url}: ${_msg}")
        endif()
    endif()
endfunction()
koi_fetch_header(
    https://raw.githubusercontent.com/tinyobjloader/tinyobjloader/v1.0.6/tiny_obj_loader.h
    ${MODELS_DIR}/tiny_obj_loader.h)
koi_fetch_header(
    https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.14/cgltf.h
    ${MODELS_DIR}/cgltf.h)

# ----------------------------------------------------------------------------
#  Shared warning flags
#  We apply strict warnings to *our* code only — not to SDL3 or doctest, whose
#  headers we don't control. INTERFACE library = "a bag of settings" with no
#  compiled output; targets that link it inherit the flags.
# ----------------------------------------------------------------------------
add_library(koi_warnings INTERFACE)
target_compile_options(koi_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

# ----------------------------------------------------------------------------
#  koi_core — the engine itself, as a static library.
#  Building the engine as a library (rather than dumping everything into the
#  app's main.cpp) means the test runner can link the *same* code the app runs.
#  As the engine grows, new subsystems get added to this target's source list.
# ----------------------------------------------------------------------------
add_library(koi_core STATIC
    src/core/Engine.cpp
    src/core/Window.cpp
    src/renderer/GpuRenderer.cpp
    src/renderer/Mesh.cpp
    src/renderer/ModelLoader.cpp
    src/renderer/Primitives.cpp
    src/renderer/Shader.cpp
    src/renderer/Texture.cpp
    src/scene/Camera.cpp
    src/scene/Node.cpp
)

# PUBLIC include dir: anything linking koi_core (the app, the tests) can
# #include "core/Engine.hpp" etc. using paths rooted at src/.
target_include_directories(koi_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The model-loader headers are PRIVATE (only ModelLoader.cpp uses them). That file
# also #defines the *_IMPLEMENTATION macros, so it pulls in thousands of lines of
# third-party code — compile it with warnings off (-w, appended after our flags) so
# our strict-warning build stays clean and meaningful for the code we own.
target_include_directories(koi_core PRIVATE ${MODELS_DIR})
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/ModelLoader.cpp
    PROPERTIES COMPILE_OPTIONS "-w")

# PUBLIC link to SDL3: koi_core's headers expose SDL types, so consumers need
# SDL3's include paths too — PUBLIC propagates them transitively.
target_link_libraries(koi_core
    PUBLIC  SDL3::SDL3
    PRIVATE koi_warnings)

# ----------------------------------------------------------------------------
#  koi-engine — the actual application executable.
# ----------------------------------------------------------------------------
add_executable(koi-engine src/main.cpp)
target_link_libraries(koi-engine PRIVATE koi_core koi_warnings)

# ----------------------------------------------------------------------------
#  koi-tests — the doctest-based test runner.
#  Pure, headless unit tests (no GPU/window) covering the logic we can isolate:
#  shader-variant selection, color-format mapping, the vertex layout contract,
#  and (Step 3) the hand-rolled vector/matrix math.
# ----------------------------------------------------------------------------
enable_testing()
add_executable(koi-tests
    tests/test_core.cpp
    tests/test_shader.cpp
    tests/test_vertex.cpp
    tests/test_math.cpp
    tests/test_camera.cpp
    tests/test_transform.cpp
    tests/test_node.cpp
    tests/test_post.cpp
    tests/test_light.cpp
    tests/test_pbr.cpp
    tests/test_tangent.cpp
)
target_link_libraries(koi-tests PRIVATE koi_core doctest::doctest koi_warnings)
add_test(NAME koi-tests COMMAND koi-tests)

# ----------------------------------------------------------------------------
#  Shaders — compiled offline, once per backend format.
#  GPUs run compiled bytecode, and each backend wants a different dialect
#  (SPIR-V for Vulkan, MSL for Metal). We author our shaders once in GLSL and
#  compile them at build time:
#       GLSL --(glslc)--> SPIR-V --(spirv-cross)--> MSL
#  The runtime loader (src/renderer/Shader.cpp) then loads whichever the active
#  backend supports. Compiled output lands next to the executable so it can be
#  found via SDL_GetBasePath() at runtime.
# ----------------------------------------------------------------------------
find_program(GLSLC glslc)
find_program(SPIRV_CROSS spirv-cross)
if(NOT GLSLC OR NOT SPIRV_CROSS)
    message(FATAL_ERROR
        "Shader tools not found (glslc and/or spirv-cross).\n"
        "Install them with:  brew install shaderc spirv-cross")
endif()

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

# koi_add_shader(<relative-source>) registers the two-stage compile for one
# shader and appends its outputs to KOI_SHADER_OUTPUTS (in the parent scope).
function(koi_add_shader SHADER_SRC)
    get_filename_component(name ${SHADER_SRC} NAME)   # e.g. triangle.vert
    set(spv ${SHADER_OUT_DIR}/${name}.spv)
    set(msl ${SHADER_OUT_DIR}/${name}.msl)

    # Stage 1: GLSL -> SPIR-V. glslc infers the shader stage from the .vert/.frag
    # extension. -g keeps debug info; --target-env=vulkan1.0 is glslc's default.
    add_custom_command(
        OUTPUT  ${spv}
        COMMAND ${GLSLC} ${SHADER_SRC_DIR}/${SHADER_SRC} -o ${spv}
        DEPENDS ${SHADER_SRC_DIR}/${SHADER_SRC}
        COMMENT "glslc  ${name} -> SPIR-V"
        VERBATIM)

    # Stage 2: SPIR-V -> MSL (Metal Shading Language) for the Metal backend.
    # --msl-decoration-binding makes MSL resource indices follow the GLSL
    # `layout(binding = N)` decorations rather than spirv-cross's default
    # variable order. SDL binds fragment samplers/uniforms by slot, so without
    # this a shader with multiple samplers (e.g. the material texture + the shadow
    # map) can get them swapped in the generated MSL.
    add_custom_command(
        OUTPUT  ${msl}
        COMMAND ${SPIRV_CROSS} ${spv} --msl --msl-decoration-binding --output ${msl}
        DEPENDS ${spv}
        COMMENT "spirv-cross  ${name} -> MSL"
        VERBATIM)

    list(APPEND KOI_SHADER_OUTPUTS ${spv} ${msl})
    set(KOI_SHADER_OUTPUTS ${KOI_SHADER_OUTPUTS} PARENT_SCOPE)
endfunction()

koi_add_shader(triangle.vert)
koi_add_shader(triangle.frag)
koi_add_shader(shadow.vert)   # Step 9: depth-only pass from the light's point of view
koi_add_shader(shadow.frag)
# Step 10: post-processing. One shared fullscreen-triangle vertex shader feeds four
# fullscreen fragment passes (bloom bright-pass + blur, tone-map composite, FXAA).
koi_add_shader(fullscreen.vert)
koi_add_shader(bright.frag)
koi_add_shader(blur.frag)
koi_add_shader(composite.frag)
koi_add_shader(fxaa.frag)

# A target that builds all shader outputs, and a dependency so the app always has
# up-to-date compiled shaders next to it.
add_custom_target(koi_shaders ALL DEPENDS ${KOI_SHADER_OUTPUTS})
add_dependencies(koi-engine koi_shaders)

# ----------------------------------------------------------------------------
#  Assets — copied (not compiled) next to the executable.
#  Textures are loaded at runtime via SDL_GetBasePath() + "assets/...", so the
#  source images under assets/ must sit beside the binary in build/assets/. We
#  mirror the shader setup: a copy command per asset + an ALL target the app
#  depends on. Since Step 8 uses several textures (per-object materials), we copy
#  every assets/*.bmp plus Step 9's model files (*.obj, *.glb). NOTE: file(GLOB) is
#  evaluated at configure time, so a brand-new asset file needs a re-run of CMake.
# ----------------------------------------------------------------------------
set(ASSET_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(ASSET_OUT_DIR ${CMAKE_BINARY_DIR}/assets)
file(MAKE_DIRECTORY ${ASSET_OUT_DIR})

file(GLOB ASSET_FILES ${ASSET_SRC_DIR}/*.bmp ${ASSET_SRC_DIR}/*.obj ${ASSET_SRC_DIR}/*.glb)
set(KOI_ASSET_OUTPUTS "")
foreach(asset ${ASSET_FILES})
    get_filename_component(asset_name ${asset} NAME)
    add_custom_command(
        OUTPUT  ${ASSET_OUT_DIR}/${asset_name}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${asset} ${ASSET_OUT_DIR}/${asset_name}
        DEPENDS ${asset}
        COMMENT "copy  ${asset_name} -> build/assets"
        VERBATIM)
    list(APPEND KOI_ASSET_OUTPUTS ${ASSET_OUT_DIR}/${asset_name})
endforeach()

add_custom_target(koi_assets ALL DEPENDS ${KOI_ASSET_OUTPUTS})
add_dependencies(koi-engine koi_assets)
