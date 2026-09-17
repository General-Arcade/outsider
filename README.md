# Outsider: RPG Maker MZ Native Runtime

Outsider is a native, browser-free runtime for RPG Maker MZ games. The game's original
JavaScript runs unmodified inside [QuickJS](https://github.com/quickjs-ng/quickjs),
with windowing and input from SDL2, rendering through OpenGL, and audio through
SoLoud. No NW.js, no Chromium, no Node.

The result is a small executable (a few megabytes instead of a couple hundred)
that starts instantly, uses far less memory than the stock NW.js player, and
runs on Linux and Windows from the same source tree.

## Features

- **Unmodified game code.** Drop in a standard RPG Maker MZ project directory;
  `rmmz_core.js`, plugins and `main.js` load exactly as shipped.
- **Browser API shim layer.** DOM, Canvas 2D, PIXI.js, Web Audio,
  XMLHttpRequest, `localStorage`/`localforage`, `document.fonts`, gamepads,
  fullscreen, timers and `requestAnimationFrame` are provided in JavaScript
  and backed by native C.
- **GPU rendering.** Sprite batching, render targets, blend modes, tilemap
  layers and PIXI filters (ColorMatrix, Blur, Alpha) on OpenGL 4.5.
- **Software Canvas 2D** for `Bitmap` operations (text, gradients, paths,
  transforms) with TrueType text via stb_truetype.
- **Audio** through SoLoud: OGG Vorbis and WAV, four independent buses
  (BGM, BGS, ME, SE), streaming for music, pitch and pan.
- **Movies** (title videos, the Play Movie command) through a built-in
  MPEG-1 decoder (pl_mpeg); the packaging tools transcode the game's
  WebM/MP4 files.
- **Plugin friendly.** Common plugin needs such as `document.currentScript`,
  `fetch()`, Steam/Greenworks stubs and PIXI filter stubs are covered.
- **Save data compatible.** Saves are written next to the game exactly where
  the NW.js player puts them.
- **Effekseer** particle effects through the native SDK (optional build).
- **Packaging tools** that decrypt assets, convert audio and bundle a
  self-contained distribution.

## How It Works

```
RPG Maker MZ game (unmodified JS)
        |
Browser API shims (src/shims/*.js)
        |
QuickJS C bindings (src/bindings/)
        |
Native backends: SDL2 + OpenGL, SoLoud, Effekseer
```

The runtime behaves like a purpose-built browser. Scripts are loaded in the
same order `index.html` would load them: shims, `pako`, the `rmmz_*` core
files, plugins, then `main.js`. Every browser object the engine touches is
either implemented in a shim or forwarded to a native function registered
under a `__native_*` name.

## Requirements

- CMake 3.20 or newer
- A C17 and C++ compiler: GCC, Clang, or Visual Studio 2022
- OpenGL 4.5 capable GPU and driver (direct state access is used throughout)
- Python 3.8+ for the packaging tools
- `ffmpeg` on `PATH` (or in `RMMZ_FFMPEG`), only if the game ships M4A
  audio or movies

SDL2, QuickJS-NG and SoLoud are fetched and built from source by CMake.
stb_image and stb_truetype are vendored under `third_party/stb`.

## Building

### Linux / macOS-style shells

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
```

### Windows (Visual Studio 2022)

Install the "Desktop development with C++" workload. Then either use Ninja from
a *x64 Native Tools Command Prompt for VS 2022*:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
```

or the Visual Studio generator from any prompt:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release
```

On Windows `outsider.exe` is a GUI application: no console window opens
when it is double-clicked. Output still reaches the parent console when
launched from a shell, follows redirection, and otherwise goes to
`console.log` next to `debug.log` in the game directory. Fatal startup errors
are shown in a message box. Pass `-DRMMZ_WIN32_CONSOLE=ON` to build a
console executable instead.

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Debug | Debug, Release, RelWithDebInfo, MinSizeRel |
| `RMMZ_BUILD_TESTS` | ON | Build the test suite |
| `RMMZ_DEFAULT_WIDTH` / `RMMZ_DEFAULT_HEIGHT` | 816 / 624 | Initial window size |
| `RMMZ_USE_EFFEKSEER` | OFF | Build the real Effekseer backend (stub otherwise) |
| `RMMZ_WIN32_CONSOLE` | OFF | Windows: console subsystem instead of GUI |
| `RMMZ_GAME_DIR` | (empty) | Game directory to bundle with `cmake --install` |
| `RMMZ_TEST_GAME_DIR` | (empty) | Real game used by the integration tests (optional) |

## Usage

```bash
# Run a game (positional or --game); shims default to src/shims
./build/outsider "/path/to/game"
./build/outsider --game "/path/to/game" --shims "/path/to/shims"

# Other options
./build/outsider --game "/path/to/game" --resolution 1280x720 --no-audio
```

The game directory is a normal RPG Maker MZ project: `js/`, `data/`, `img/`,
`audio/`, `fonts/`. With no arguments the runtime looks for a `game/` folder
next to the executable, and for shims in `game/shims/` or `shims/` beside it.
This is the layout produced by the packaging tool, so a packaged build runs
on double-click.

The window title shows `Outsider: <game title>` from `System.json`.
Logs are written to `debug.log` in the game directory. Saves go to
`save/` in the game directory, matching the stock player.

### Packaging a distribution

```bash
# Convert assets, build the runtime, and assemble dist/
python3 tools/build_game.py "/path/to/game" --output-dir dist --build-type Release

# Individual steps
python3 tools/resource_converter.py "/path/to/game" out/   # assets only
python3 tools/build_game.py "/path/to/game" --skip-compile  # assets + shims
python3 tools/build_game.py "/path/to/game" --skip-assets   # compile only
```

`resource_converter.py` decrypts `.png_`/`.ogg_` files using the key in
`System.json`, converts M4A to OGG and transcodes `movies/` to MPEG-1 when
`ffmpeg` is available, and drops NW.js
binaries, browser-only files and JS libraries that the runtime replaces
natively (PIXI, Effekseer WASM, vorbis decoder). `build_game.py` then copies
the shims into `dist/game/shims/`, builds the runtime and places the
executable at `dist/`.

## Architecture

```
src/
  main.c                  Entry point, argument parsing, subsystem init
  platform/
    platform_sdl2.c       Window, OpenGL context, events, gamepads
    win_console.c         Windows stdout/stderr handling and error dialogs
  engine/
    js_engine.c           QuickJS runtime, console, promise jobs, bytecode cache
    game_loop.c           Fixed-step loop: events -> timers -> rAF -> render
    script_loader.c       RPG Maker MZ script load order and plugin list
    error_handler.c       Structured logging, JS exception reporting
  io/file_io.c            Portable file system access rooted at the game dir
  rendering/
    gl_loader.c           Runtime OpenGL entry point loading
    renderer.c            Frame orchestration, render targets, blend state
    sprite_batch.c        Quad batching (VAO/VBO, texture sorting)
    canvas2d.c            Software Canvas 2D rasteriser
    font_manager.c        TrueType loading and glyph caching
    image_loader.c        PNG/JPEG decoding and texture cache
    tilemap.c             Tilemap layer rendering (autotiles, animation)
    filters.c             GLSL implementations of PIXI filters
  audio/audio_engine.cpp  SoLoud wrapper with four mix buses
  effects/effekseer_backend.cpp
  input/input_manager.c   SDL events -> DOM keyboard/mouse/gamepad state
  bindings/bind_*.c       QuickJS registrations, one module per subsystem
  shims/*.js              Browser API implementations loaded before the game
tests/                    Unit and integration tests (ctest)
tools/                    Packaging scripts
third_party/              Dependency fetching (CMake FetchContent) + vendored stb
```

### Shims

| File | Provides |
|------|----------|
| `dom_shim.js` | `window`, `document`, elements, events, timers, `requestAnimationFrame` |
| `canvas2d_shim.js` | `CanvasRenderingContext2D` backed by the native rasteriser |
| `pixi_shim.js` | PIXI application, containers, sprites, textures, render textures, renderer |
| `tilemap_shim.js` | Native `Tilemap` layer rendering (loaded after the core scripts) |
| `webaudio_shim.js` | `AudioContext`, buffers, gain nodes, `decodeAudioData` |
| `xhr_shim.js` | `XMLHttpRequest` over native file I/O |
| `storage_shim.js` | `localStorage`, `localforage`, `require("fs")`, `require("path")`, `process` |
| `font_shim.js` | `FontFace`, `document.fonts` |
| `navigator_shim.js` | `navigator`, `getGamepads()`, `screen` |
| `plugin_compat.js` | `fetch()`, PIXI filter stubs, Steam stubs, polyfills |
| `effekseer_shim.js` | `effekseer` context API over the native backend |

### Third-party dependencies

| Library | Purpose |
|---------|---------|
| SDL2 2.30 | Window, OpenGL context, input, audio device |
| QuickJS-NG 0.9 | JavaScript engine |
| SoLoud | Audio mixing, OGG/WAV decoding |
| Effekseer 1.70e | Particle effects (optional) |
| stb_image, stb_truetype | Image decoding, font rasterisation |

## Plugin Compatibility

Most plugins that only use the RPG Maker MZ API work as-is. The shim layer
additionally provides the browser features plugins commonly reach for:
`document.currentScript` for parameter parsing, `fetch()`, `Array.contains`,
PIXI filter classes and Greenworks/Steam objects that report Steam as
unavailable. Known limitations:

- PIXI filter stubs beyond ColorMatrix, Blur and Alpha accept their
  parameters but do not produce a visual effect.
- QuickJS rejects assignment to a static class field defined with a getter
  only; the affected plugin logs a "no setter for property" error and
  continues.
- M4A audio must be converted to OGG and movies to MPEG-1 (the packaging
  tool does both with `ffmpeg`). A movie with no `.mpg` next to it is
  skipped rather than played.

## Testing

```bash
ctest --test-dir build                      # all tests
ctest --test-dir build -R file_io -V        # one test, verbose
```

Tests use only fixtures under `tests/fixtures/` and a temporary directory.
Three tests can additionally exercise a real game (plugin loading, `pako`,
and an end-to-end acceptance pass); point `RMMZ_TEST_GAME_DIR` at an RPG
Maker MZ project at configure time to enable them, otherwise they skip.

## Authors and License

Outsider is developed by [General Arcade](https://generalarcade.com). Written by Gennadii Potapov;
see [AUTHORS](AUTHORS).

Dual-licensed: [GPLv2](LICENSES/GPL-2.0.txt) for free and open source use,
or a [commercial license](LICENSES/COMMERCIAL.md) from General Arcade for
proprietary and commercial projects (contact@generalarcade.com). See
[LICENSE](LICENSE).

Contributions are welcome under the terms in [CONTRIBUTING.md](CONTRIBUTING.md).
