add_executable(test_platform
    test_platform.c
    ${CMAKE_SOURCE_DIR}/src/platform/platform_sdl3.c
    ${CMAKE_SOURCE_DIR}/src/rendering/gl_loader.c
    ${CMAKE_SOURCE_DIR}/src/input/input_manager.c
)

target_include_directories(test_platform PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_platform PRIVATE
    SDL3::SDL3
    OpenGL::GL
)

add_test(NAME platform COMMAND test_platform)

# JS Engine tests (no SDL/OpenGL dependency)
add_executable(test_js_engine
    test_js_engine.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_js_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_js_engine PRIVATE
    qjs
)

add_test(NAME js_engine COMMAND test_js_engine)

# File I/O tests (includes JS binding tests, needs QuickJS)
add_executable(test_file_io
    test_file_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_file_io PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_file_io PRIVATE
    qjs
)

add_test(NAME file_io COMMAND test_file_io)

# XMLHttpRequest shim tests (JS shim loaded via C harness, needs QuickJS + I/O)
add_executable(test_xhr
    test_xhr.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_xhr PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_xhr PRIVATE
    qjs
)

# Run from project root so the shim file path resolves correctly
add_test(NAME xhr COMMAND test_xhr WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# DOM shim tests (JS shims loaded via C harness, needs QuickJS + I/O)
add_executable(test_dom_shim
    test_dom_shim.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_dom_shim PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_dom_shim PRIVATE
    qjs
)

# Run from project root so the shim file paths resolve correctly
add_test(NAME dom_shim COMMAND test_dom_shim WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Game loop & script loader tests (needs QuickJS + I/O + script_loader)
add_executable(test_game_loop
    test_game_loop.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
    ${CMAKE_SOURCE_DIR}/src/engine/script_loader.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
)

target_include_directories(test_game_loop PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_game_loop PRIVATE
    qjs
)

# Run from project root so shim file paths resolve correctly
add_test(NAME game_loop COMMAND test_game_loop WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Image loader tests (C-level + JS binding + HTMLImageElement integration)
add_executable(test_image_loader
    test_image_loader.c
    ${CMAKE_SOURCE_DIR}/src/rendering/image_loader.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_image.c
    ${CMAKE_SOURCE_DIR}/src/rendering/apng.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_image_loader PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_link_libraries(test_image_loader PRIVATE
    qjs
)

# Run from project root for fixture and shim file paths
add_test(NAME image_loader COMMAND test_image_loader WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Canvas2D tests (C-level + JS binding + JS shim integration)
add_executable(test_canvas2d
    test_canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/rendering/canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/rendering/woff.c
    ${CMAKE_SOURCE_DIR}/src/rendering/image_loader.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_image.c
    ${CMAKE_SOURCE_DIR}/src/rendering/apng.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_canvas2d PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_link_libraries(test_canvas2d PRIVATE
    qjs
    ${RMMZ_SYS_LIBS}
)

# Run from project root for shim file paths and font access
add_test(NAME canvas2d COMMAND test_canvas2d WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# PIXI shim tests (C-level renderer + JS binding + JS shim integration)
add_executable(test_pixi_shim
    test_pixi_shim.c
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_renderer.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_pixi_shim PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_pixi_shim PRIVATE
    qjs
    ${RMMZ_SYS_LIBS}
)

# Run from project root for shim file paths
add_test(NAME pixi_shim COMMAND test_pixi_shim WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Font tests (C-level font_manager + JS bindings + FontFace/FontFaceSet shims)
add_executable(test_font
    test_font.c
    ${CMAKE_SOURCE_DIR}/src/rendering/font_manager.c
    ${CMAKE_SOURCE_DIR}/src/rendering/canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/rendering/woff.c
    ${CMAKE_SOURCE_DIR}/src/rendering/image_loader.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_font.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_image.c
    ${CMAKE_SOURCE_DIR}/src/rendering/apng.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_font PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_link_libraries(test_font PRIVATE
    qjs
    ${RMMZ_SYS_LIBS}
)

# Run from project root for shim file paths and font access
add_test(NAME font COMMAND test_font WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Tilemap tests (C-level tilemap + JS binding + PIXI stubs + shim integration)
add_executable(test_tilemap
    test_tilemap.c
    ${CMAKE_SOURCE_DIR}/src/rendering/tilemap.c
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_tilemap.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_renderer.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_tilemap PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_tilemap PRIVATE
    qjs
    ${RMMZ_SYS_LIBS}
)

# Run from project root for shim file paths
add_test(NAME tilemap COMMAND test_tilemap WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Integration test: Title screen rendering (full boot sequence)
add_executable(test_title_screen
    integration/test_title_screen.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
    ${CMAKE_SOURCE_DIR}/src/engine/script_loader.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_image.c
    ${CMAKE_SOURCE_DIR}/src/rendering/apng.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_renderer.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_font.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_tilemap.c
    ${CMAKE_SOURCE_DIR}/src/rendering/image_loader.c
    ${CMAKE_SOURCE_DIR}/src/rendering/canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/rendering/woff.c
    ${CMAKE_SOURCE_DIR}/src/rendering/font_manager.c
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/rendering/tilemap.c
)

target_include_directories(test_title_screen PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_link_libraries(test_title_screen PRIVATE
    qjs
    ${RMMZ_SYS_LIBS}
)

# Run from project root for shim file paths and mock game setup
add_test(NAME title_screen COMMAND test_title_screen WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Audio engine tests (C-level + JS binding tests, needs SoLoud + QuickJS)
add_executable(test_audio
    test_audio.c
    ${CMAKE_SOURCE_DIR}/src/audio/audio_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_audio.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_audio PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_audio PRIVATE
    qjs
    soloud
    SDL3::SDL3
    ${RMMZ_SYS_LIBS}
)

# Run from project root for fixture file paths
add_test(NAME audio COMMAND test_audio WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# WebAudio shim tests (JS-level, needs SoLoud + QuickJS + shims)
add_executable(test_webaudio
    test_webaudio.c
    ${CMAKE_SOURCE_DIR}/src/audio/audio_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_audio.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_webaudio PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_webaudio PRIVATE
    qjs
    soloud
    SDL3::SDL3
    ${RMMZ_SYS_LIBS}
)

# Run from project root for shim file paths and fixture access
add_test(NAME webaudio COMMAND test_webaudio WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Storage shim tests (localStorage, localforage, require("fs"), require("path"))
add_executable(test_storage
    test_storage.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_storage PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_storage PRIVATE
    qjs
)

# Run from project root for shim file paths and save directory access
add_test(NAME storage COMMAND test_storage WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Input system tests (C-level input_manager + JS bindings + DOM event dispatch)
add_executable(test_input
    test_input.c
    ${CMAKE_SOURCE_DIR}/src/input/input_manager.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_input.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_input PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_input PRIVATE
    qjs
)

# Run from project root for shim file paths
add_test(NAME input COMMAND test_input WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Integration test: Gameplay flow (boot -> title -> new game -> map -> save/load)
add_executable(test_gameplay
    integration/test_gameplay.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
    ${CMAKE_SOURCE_DIR}/src/engine/script_loader.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_image.c
    ${CMAKE_SOURCE_DIR}/src/rendering/apng.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_renderer.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_font.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_tilemap.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_audio.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_input.c
    ${CMAKE_SOURCE_DIR}/src/rendering/image_loader.c
    ${CMAKE_SOURCE_DIR}/src/rendering/canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/rendering/woff.c
    ${CMAKE_SOURCE_DIR}/src/rendering/font_manager.c
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/rendering/tilemap.c
    ${CMAKE_SOURCE_DIR}/src/audio/audio_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/input/input_manager.c
)

target_include_directories(test_gameplay PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_link_libraries(test_gameplay PRIVATE
    qjs
    soloud
    SDL3::SDL3
    ${RMMZ_SYS_LIBS}
)

# Run from project root for shim file paths and mock game setup
add_test(NAME gameplay COMMAND test_gameplay WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Filter tests (C-level filters + JS bindings + PIXI filter shims)
add_executable(test_filters
    test_filters.c
    ${CMAKE_SOURCE_DIR}/src/rendering/filters.c
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_filters.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_renderer.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
    ${CMAKE_SOURCE_DIR}/src/engine/error_handler.c
)

target_include_directories(test_filters PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_filters PRIVATE
    qjs
    ${RMMZ_SYS_LIBS}
    SDL3::SDL3
)

# Run from project root for shim file paths
add_test(NAME filters COMMAND test_filters WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Effekseer backend + binding + shim tests
add_executable(test_effekseer
    test_effekseer.c
    ${CMAKE_SOURCE_DIR}/src/effects/effekseer_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_effekseer.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_effekseer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_effekseer PRIVATE
    qjs
)

# Run from project root for shim file paths and fixture access
add_test(NAME effekseer COMMAND test_effekseer WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Plugin compatibility tests (loads actual game plugins with shims + mock RMMZ core)
add_executable(test_plugins
    test_plugins.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
    ${CMAKE_SOURCE_DIR}/src/engine/script_loader.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
)

target_include_directories(test_plugins PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_plugins PRIVATE
    qjs
)

# Run from project root for shim file paths and game plugin access
add_test(NAME plugins COMMAND test_plugins WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Display tests (fullscreen, resize, scaling — needs SDL3 + OpenGL + QuickJS + platform bindings)
add_executable(test_display
    test_display.c
    ${CMAKE_SOURCE_DIR}/src/platform/platform_sdl3.c
    ${CMAKE_SOURCE_DIR}/src/rendering/gl_loader.c
    ${CMAKE_SOURCE_DIR}/src/input/input_manager.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_platform.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
)

target_include_directories(test_display PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_display PRIVATE
    SDL3::SDL3
    OpenGL::GL
    qjs
)

# Run from project root for shim file paths
add_test(NAME display COMMAND test_display WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Error handler tests (structured logging, JS exception reporting, window.onerror, auto-save)
add_executable(test_error_handler
    test_error_handler.c
    ${CMAKE_SOURCE_DIR}/src/engine/error_handler.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
)

target_include_directories(test_error_handler PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_error_handler PRIVATE
    qjs
    SDL3::SDL3
)

# Run from project root for shim file paths
add_test(NAME error_handler COMMAND test_error_handler WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Performance optimization tests (sprite batch, bytecode caching, audio profiling, benchmarks)
add_executable(test_performance
    test_performance.c
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/audio/audio_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
    ${CMAKE_SOURCE_DIR}/src/engine/game_loop.c
    ${CMAKE_SOURCE_DIR}/src/engine/error_handler.c
    ${CMAKE_SOURCE_DIR}/src/platform/platform_sdl3.c
    ${CMAKE_SOURCE_DIR}/src/rendering/gl_loader.c
    ${CMAKE_SOURCE_DIR}/src/input/input_manager.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/video/video_player.c
)

target_include_directories(test_performance PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/pl_mpeg
)

target_link_libraries(test_performance PRIVATE
    qjs
    soloud
    SDL3::SDL3
    OpenGL::GL
    ${RMMZ_SYS_LIBS}
)

# Run from project root for file paths
add_test(NAME performance COMMAND test_performance WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Acceptance criteria verification (comprehensive end-to-end validation)
add_executable(test_acceptance
    integration/test_acceptance.c
    ${CMAKE_SOURCE_DIR}/src/engine/js_engine.c
    ${CMAKE_SOURCE_DIR}/src/engine/script_loader.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_io.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_image.c
    ${CMAKE_SOURCE_DIR}/src/rendering/apng.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_renderer.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_font.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_tilemap.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_audio.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_input.c
    ${CMAKE_SOURCE_DIR}/src/bindings/bind_effekseer.c
    ${CMAKE_SOURCE_DIR}/src/rendering/image_loader.c
    ${CMAKE_SOURCE_DIR}/src/rendering/canvas2d.c
    ${CMAKE_SOURCE_DIR}/src/rendering/woff.c
    ${CMAKE_SOURCE_DIR}/src/rendering/font_manager.c
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/rendering/tilemap.c
    ${CMAKE_SOURCE_DIR}/src/audio/audio_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/input/input_manager.c
    ${CMAKE_SOURCE_DIR}/src/effects/effekseer_backend.cpp
)

target_include_directories(test_acceptance PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_link_libraries(test_acceptance PRIVATE
    qjs
    soloud
    SDL3::SDL3
    ${RMMZ_SYS_LIBS}
)

# Run from project root for shim file paths and game directory access
add_test(NAME acceptance COMMAND test_acceptance WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Optional: a real RPG Maker MZ game directory for the tests that load actual
# game scripts (plugins, storage, acceptance). Those tests skip when unset.
# Video playback (pl_mpeg decode, PCM stream, state machine; headless)
add_executable(test_video
    test_video.c
    ${CMAKE_SOURCE_DIR}/src/video/video_player.c
    ${CMAKE_SOURCE_DIR}/src/audio/audio_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/sprite_batch.c
    ${CMAKE_SOURCE_DIR}/src/rendering/renderer.c
    ${CMAKE_SOURCE_DIR}/src/rendering/gl_loader.c
    ${CMAKE_SOURCE_DIR}/src/io/file_io.c
)

target_include_directories(test_video PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/pl_mpeg
)

target_link_libraries(test_video PRIVATE
    soloud
    SDL3::SDL3
    OpenGL::GL
    ${RMMZ_SYS_LIBS}
)

add_test(NAME video COMMAND test_video WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

set(RMMZ_TEST_GAME_DIR "" CACHE PATH "RPG Maker MZ game directory used by integration tests")
if(RMMZ_TEST_GAME_DIR)
    set_tests_properties(plugins storage acceptance PROPERTIES
        ENVIRONMENT "RMMZ_TEST_GAME_DIR=${RMMZ_TEST_GAME_DIR}")
endif()
