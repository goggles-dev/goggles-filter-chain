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

`goggles-chain-cli` applies RetroArch shader presets to images offline and provides structured diagnostic tooling for CI. Built automatically with all standalone presets except `release`:

```bash
pixi run build -p debug
./build/debug/tools/cli/goggles-chain-cli --preset shaders/crt.slangp --output out/ input.png
```

### Process (default)

Applies a preset to input images and writes the results as PNGs.

```
goggles-chain-cli [process] --preset <path.slangp> --output <dir> [options] <input...>

Options:
  --frames <N>            Render N frames per image (default: 1)
  --scale <factor>        Output scale factor (e.g., 2 for 2x upscale)
  --output-size <WxH>     Explicit output dimensions (overrides --scale)
  --param <name=value>    Control override (repeatable)
  --verbose               Enable diagnostic logging
```

### Validate

Compiles and reflects a preset without rendering. Reports authoring findings and exits with a meaningful code.

```
goggles-chain-cli validate <preset.slangp> [--json] [--verbose]

Exit codes:
  0  pass      No errors or warnings
  1  degraded  Warnings or fallbacks present
  2  fail      Errors that prevent correct rendering
```

With `--json`, emits a full `DiagnosticReport` JSON to stdout instead of text.

### Diagnose

Runs the full render pipeline in forensic capture mode and writes a diagnostic bundle.

```
goggles-chain-cli diagnose <preset.slangp> <input-images...> --output <dir>
                           [--frames <N>] [--json-only] [--verbose]

Output bundle:
  <dir>/report.json              Full forensic DiagnosticReport
  <dir>/pass_0_frame0.png        Intermediate output per pass
  <dir>/final_frame0.png         Final chain output
```

With `--json-only`, skips image export and only writes `report.json`.

### Assert Commands

Exit-code-based assertions for CI scripts. Each checks one thing, prints a one-line result, and exits 0 (pass) or 1 (fail).

```bash
# Image matches golden within tolerance
goggles-chain-cli assert-image actual.png golden.png --tolerance 0.02

# Preset compiles with no errors or warnings
goggles-chain-cli assert-clean <preset.slangp>

# No degradation (fallbacks, unresolved semantics) detected
goggles-chain-cli assert-no-degradation <preset.slangp>
```

Typical CI usage:

```bash
goggles-chain-cli diagnose preset.slangp input.png --output diag/
goggles-chain-cli assert-clean preset.slangp
goggles-chain-cli assert-no-degradation preset.slangp
goggles-chain-cli assert-image diag/final_frame0.png golden/final_frame0.png --tolerance 0.02
```

### Capture (RenderDoc)

Triggers a RenderDoc in-application frame capture alongside diagnostic export, producing a paired `.rdc` + `.json` bundle. Requires building with `-DGOGGLES_FILTER_CHAIN_ENABLE_RENDERDOC_CAPTURE=ON`.

```
goggles-chain-cli capture <preset.slangp> <input.png> --output <dir> [--verbose]
```

Works without RenderDoc installed (graceful no-op for the `.rdc` portion).

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
