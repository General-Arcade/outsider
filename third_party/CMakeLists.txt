include(FetchContent)

# SDL3 — window, input, audio device and the GPU rendering backend.
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    TRUE
)
if(ANDROID)
    # SDLActivity loads libSDL3.so through JNI, so SDL must be shared there.
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# XTest only synthesises input for SDL's own tests; SDL treats its absence as
# a fatal configure error, so a Linux box without libxtst-dev could not build.
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# QuickJS-NG
FetchContent_Declare(
    quickjs
    GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
    GIT_TAG        v0.9.0
    GIT_SHALLOW    TRUE
)
set(BUILD_QJS_LIBC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(quickjs)

# SoLoud audio engine — fetch source and build manually for reliable integration
FetchContent_Declare(
    soloud
    GIT_REPOSITORY https://github.com/jarikomppa/soloud.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(soloud)
if(NOT soloud_POPULATED)
    FetchContent_Populate(soloud)
endif()

# Build SoLoud as a static C++ library from source files
set(SOLOUD_SRC ${soloud_SOURCE_DIR}/src)
add_library(soloud STATIC
    # Core
    ${SOLOUD_SRC}/core/soloud.cpp
    ${SOLOUD_SRC}/core/soloud_audiosource.cpp
    ${SOLOUD_SRC}/core/soloud_bus.cpp
    ${SOLOUD_SRC}/core/soloud_core_3d.cpp
    ${SOLOUD_SRC}/core/soloud_core_basicops.cpp
    ${SOLOUD_SRC}/core/soloud_core_faderops.cpp
    ${SOLOUD_SRC}/core/soloud_core_filterops.cpp
    ${SOLOUD_SRC}/core/soloud_core_getters.cpp
    ${SOLOUD_SRC}/core/soloud_core_setters.cpp
    ${SOLOUD_SRC}/core/soloud_core_voicegroup.cpp
    ${SOLOUD_SRC}/core/soloud_core_voiceops.cpp
    ${SOLOUD_SRC}/core/soloud_fader.cpp
    ${SOLOUD_SRC}/core/soloud_fft.cpp
    ${SOLOUD_SRC}/core/soloud_fft_lut.cpp
    ${SOLOUD_SRC}/core/soloud_file.cpp
    ${SOLOUD_SRC}/core/soloud_filter.cpp
    ${SOLOUD_SRC}/core/soloud_misc.cpp
    ${SOLOUD_SRC}/core/soloud_queue.cpp
    ${SOLOUD_SRC}/core/soloud_thread.cpp
    # Audio sources — WAV/OGG
    ${SOLOUD_SRC}/audiosource/wav/soloud_wav.cpp
    ${SOLOUD_SRC}/audiosource/wav/soloud_wavstream.cpp
    ${SOLOUD_SRC}/audiosource/wav/stb_vorbis.c
    ${SOLOUD_SRC}/audiosource/wav/dr_impl.cpp
    # Backend — SDL3. SoLoud ships no SDL3 backend, so ours stands in for the
    # sdl2_static one it does ship: same entry point, so Soloud::AUTO finds it.
    ${CMAKE_SOURCE_DIR}/src/audio/soloud_sdl3_backend.cpp
    # Backend — null (for headless testing)
    ${SOLOUD_SRC}/backend/null/soloud_null.cpp
)

target_include_directories(soloud PUBLIC ${soloud_SOURCE_DIR}/include)
target_compile_definitions(soloud PRIVATE WITH_SDL2_STATIC WITH_NULL
    # SoLoud selects Win32 threads/mutexes via WINDOWS_VERSION; without it
    # soloud_thread.cpp tries to include pthread.h.
    $<$<BOOL:${WIN32}>:WINDOWS_VERSION>
)
target_link_libraries(soloud PRIVATE SDL3::SDL3)
set_target_properties(soloud PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Runtime shader compilation — lets the GPU backend build shaders a game's
# plugins supply at run time, which SDL3 otherwise cannot accept (it takes
# bytecode, not source).
#
#   glslang        GLSL -> SPIR-V
#   SPIRV-Cross    SPIR-V -> HLSL / MSL
#   SDL_shadercross wraps both, and reaches SDL's GPU shader objects
#
# DXC is deliberately left out: shadercross reaches DXBC through Windows' own
# d3dcompiler_47.dll, which SDL's D3D12 backend accepts, so none of this ships
# a redistributable compiler DLL.
if(RMMZ_RUNTIME_SHADERS)
    set(RMMZ_SHADER_DEPS_TAG_GLSLANG      "16.6.0")
    set(RMMZ_SHADER_DEPS_TAG_SPIRV_CROSS  "vulkan-sdk-1.4.357.0")

    FetchContent_Declare(
        glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG        ${RMMZ_SHADER_DEPS_TAG_GLSLANG}
        GIT_SHALLOW    TRUE
    )
    # ENABLE_OPT pulls in SPIRV-Tools purely to optimise output we do not need
    # optimised; leaving it off keeps the dependency tree to glslang alone.
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glslang)

    FetchContent_Declare(
        spirv_cross
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
        GIT_TAG        ${RMMZ_SHADER_DEPS_TAG_SPIRV_CROSS}
        GIT_SHALLOW    TRUE
    )
    set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(spirv_cross)

    # SDL_shadercross looks for an *installed* SPIRV-Cross package under the
    # underscored name before falling back; aliasing the target we just built
    # lets it use that instead of requiring one on the system.
    if(TARGET spirv-cross-c AND NOT TARGET spirv_cross_c)
        add_library(spirv_cross_c ALIAS spirv-cross-c)
    endif()

    # The library is a single source file. Building it directly, rather than
    # through its own CMakeLists, avoids its unconditional export() rule --
    # which insists every dependency belong to an export set, something an
    # in-tree SPIRV-Cross does not.
    # No submodules: those are DirectXShaderCompiler and SPIRV-Tools, neither
    # of which this build uses, and DXC alone is a multi-gigabyte clone.
    FetchContent_Declare(
        sdl_shadercross
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_shadercross.git
        GIT_TAG        main
        GIT_SUBMODULES ""
    )
    FetchContent_GetProperties(sdl_shadercross)
    if(NOT sdl_shadercross_POPULATED)
        FetchContent_Populate(sdl_shadercross)
    endif()

    add_library(sdl_shadercross STATIC
        ${sdl_shadercross_SOURCE_DIR}/src/SDL_shadercross.c
    )
    target_include_directories(sdl_shadercross PUBLIC
        ${sdl_shadercross_SOURCE_DIR}/include
    )
    # Leaving SDL_SHADERCROSS_DXC undefined drops the DirectXShaderCompiler
    # path. DXBC is compiled separately through Windows' own d3dcompiler_47,
    # and SDL's D3D12 backend accepts DXBC, so nothing here ships a
    # redistributable compiler.
    target_link_libraries(sdl_shadercross PUBLIC SDL3::SDL3 spirv-cross-c)
    set_target_properties(sdl_shadercross PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Effekseer particle effects SDK (optional — stub backend used when unavailable)
option(RMMZ_USE_EFFEKSEER "Build with real Effekseer SDK (requires OpenGL)" OFF)
if(RMMZ_USE_EFFEKSEER)
    FetchContent_Declare(
        effekseer
        GIT_REPOSITORY https://github.com/effekseer/Effekseer.git
        GIT_TAG        170e
        GIT_SHALLOW    TRUE
    )
    # Disable everything except core libs
    set(BUILD_VIEWER OFF CACHE BOOL "" FORCE)
    set(BUILD_EDITOR OFF CACHE BOOL "" FORCE)
    set(BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GL_RENDERER ON CACHE BOOL "" FORCE)
    set(BUILD_VULKAN_RENDERER OFF CACHE BOOL "" FORCE)
    set(BUILD_DX9_RENDERER OFF CACHE BOOL "" FORCE)
    set(BUILD_DX11_RENDERER OFF CACHE BOOL "" FORCE)
    set(BUILD_DX12_RENDERER OFF CACHE BOOL "" FORCE)
    set(BUILD_METAL_RENDERER OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(effekseer)
endif()
