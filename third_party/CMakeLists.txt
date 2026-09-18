include(FetchContent)

# SDL3 — window, input, audio device and the GPU rendering backend.
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
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
