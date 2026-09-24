Prebuilt runtimes for Windows, macOS and Linux (x64 and ARM64). Each archive holds the
`outsider` executable and the `shims/` directory it loads before the game;
keep them together.

| Download | Runs on | Graphics |
|---|---|---|
| `outsider-*-windows-x64.zip` | Windows 10/11 x64 | D3D12 |
| `outsider-*-macos-universal.tar.gz` | macOS 11+, Apple Silicon and Intel | Metal |
| `outsider-*-linux-x64.tar.gz` | glibc 2.35+ (Ubuntu 22.04 and newer) | Vulkan |
| `outsider-*-linux-arm64.tar.gz` | glibc 2.35+ on ARM64 (Raspberry Pi 5, Ampere, Apple Silicon VMs) | Vulkan |

## Running a game

```bash
./outsider "/path/to/RPG Maker MZ project"
```

The project directory is the usual one: `js/`, `data/`, `img/`, `audio/`,
`fonts/`. With no argument the runtime looks for a `game/` folder next to the
executable.

A game with encrypted assets, M4A audio or movies needs converting first,
which is `tools/resource_converter.py` from the source tree and needs Python:

```bash
python3 tools/resource_converter.py "/path/to/game" converted/
./outsider converted/
```

## These binaries are not code-signed

Nothing here is signed with a Microsoft or Apple certificate, so both systems
will object the first time.

- **Windows** shows a SmartScreen warning. *More info* → *Run anyway*.
- **macOS** quarantines the download. Either clear it,

  ```bash
  xattr -dr com.apple.quarantine outsider
  ```

  or right-click the executable and choose *Open*, which offers to run it once.
- **Linux** needs a Vulkan driver for the GPU it is running on
  (`mesa-vulkan-drivers` covers AMD and Intel; NVIDIA ships its own).

## Known limitations

- PIXI filters beyond ColorMatrix, Blur and Alpha accept their parameters but
  produce no visual effect. GLSL a plugin supplies is compiled at run time and
  does work.
- M4A audio must be converted to OGG and movies to MPEG-1; the packaging tool
  does both with `ffmpeg`.
- QuickJS rejects assignment to a static class field that defines only a
  getter; the plugin logs an error and carries on.

See the [README](https://github.com/General-Arcade/outsider#readme) for
building from source, packaging a distribution and the full compatibility
notes.
