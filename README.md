# Goggles Filter Chain

`goggles-filter-chain` is the standalone Vulkan filter-chain runtime extracted from Goggles. Version `0.1.0` packages preset loading, shader compilation/reflection, embedded assets, control enumeration/mutation, record-time rendering, and bounded passive diagnostics for external consumers.

## License

This project is licensed under the `MIT` license. See `LICENSE`.

## Build From Source

Install the standalone workspace dependencies and build with Pixi:

```bash
pixi install
pixi run build -p asan
```

Direct CMake preset usage is also supported:

```bash
cmake --preset asan
cmake --build --preset asan
```

Available presets include `debug`, `release`, `asan`, `quality`, and `test`.

## Run Tests

- Contract and visual verification: `pixi run test -p test`
- ASAN contract and golden verification: `ctest --preset asan --output-on-failure`
- Installed consumer validation: `pixi run consumer-validation -p test`
- Static analysis lane: `pixi run static-analysis`

Standalone FC owns the diagnostics-heavy and `intermediate-pass` golden coverage in `tests/visual/` and `tests/golden/`. Goggles keeps only host integration and end-to-end application verification.

## Consume The Library

Use the exported CMake target when the project is installed:

```cmake
find_package(GogglesFilterChain CONFIG REQUIRED)

target_link_libraries(your_target PRIVATE
    GogglesFilterChain::goggles-filter-chain
)
```

The repository also supports in-tree consumption:

```cmake
add_subdirectory(path/to/goggles-filter-chain)

target_link_libraries(your_target PRIVATE
    GogglesFilterChain::goggles-filter-chain
)
```

For installed-package validation examples, see `tests/consumer/` and `scripts/validate-installed-consumers.sh`.

## CLI Tool

`goggles-chain-cli` applies RetroArch shader presets to images offline. It is built automatically with all standalone presets except `release`:

```bash
pixi run build -p debug
./build/debug/tools/cli/goggles-chain-cli --preset shaders/crt.slangp --output out/ input.png
```

```
goggles-chain-cli --preset <path.slangp> --output <dir> [options] <input...>

Positional:
  <input...>              One or more image files, or a directory (PNG/JPG)

Required:
  --preset <path>         RetroArch preset file (.slangp)
  --output <dir>          Output directory (created if missing)

Optional:
  --frames <N>            Render N frames per image (default: 1)
  --scale <factor>        Output scale factor (e.g., 2 for 2x upscale)
  --output-size <WxH>     Explicit output dimensions (overrides --scale)
  --param <name=value>    Control override (repeatable)
  --verbose               Enable diagnostic logging
  --help                  Show usage
  --version               Show version
```

## Local Goggles Override

Goggles keeps `filter-chain/` as the default integration path, but co-development uses the documented local override:

```bash
cmake --preset asan \
  -DGOGGLES_FILTER_CHAIN_SOURCE_DIR=/abs/path/to/goggles-filter-chain
```

That lets a Goggles checkout consume a local FC checkout without editing `.gitmodules` or committed CMake files.

## Ownership Boundary

`goggles-filter-chain` owns:

- preset parsing and import resolution
- shader compilation/reflection and embedded assets
- executable pass graph, frame history, and runtime control state
- record-time rendering, reports, and stable consumer packaging
- library-level diagnostics verification and `intermediate-pass` golden tests

Goggles owns:

- Vulkan host orchestration, swapchain/import/presentation, and app/UI policy
- translation of host state into FC inputs
- end-to-end host integration verification

## Diagnostics Policy

The stable diagnostics boundary is intentionally narrow:

- diagnostics `mode` is the only stable caller-facing policy knob described by this extraction
- stable diagnostics access is passive summary/report metadata queried from chain state
- public diagnostics-session lifecycle and pass capture are not part of the durable consumer boundary
- capture-oriented seams remain standalone test support or transitional/internal surface, not promised package API

## Repository Support

Repository automation is split into `format-check`, `build-and-test`, `consumer-validation`, and `static-analysis` lanes. Use the issue templates in `.github/ISSUE_TEMPLATE/` to report runtime, packaging, consumer, or boundary issues.
