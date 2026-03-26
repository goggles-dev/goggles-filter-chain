# Debug Ledger

Guide to debugging shader preset failures with the filter-chain diagnostics system. Two categories of failure exist — compile-time and runtime — and each has a different diagnostic path.

## Setup

The diagnostics CLI works with any build preset. A debug build is fine for most investigations:

```bash
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
pixi run build -p debug
CLI=./build/debug/tools/cli/goggles-chain-cli
```

## Compile-time failures

Use `validate` to triage. It compiles and reflects a preset without rendering. Exit codes: 0 = pass, 1 = degraded, 2 = fail.

```bash
$CLI validate <preset.slangp> --json --verbose > validate.json 2>validate.log
```

`--verbose` routes log messages to stderr; `--json` writes a structured `DiagnosticReport` to stdout. Separate them so the JSON stays parseable.

### Reading the output

**validate.log** shows the error chain as it propagates — look for `[ERROR]` lines that name the failing component and the error message from Slang or the runtime.

**validate.json** gives machine-readable localization:

```bash
# Verdict and error counts
jq '.verdict, .error_counts' validate.json

# Which passes failed compilation
jq '.compile_summaries[] | select(.success == false)' validate.json

# Unresolved bindings or semantics
jq '.binding_coverage[] | select(.unresolved > 0)' validate.json
jq '.semantic_coverage[] | select(.unresolved > 0)' validate.json
```

### Iterating

Compile-time failures can stack — fixing one may reveal the next. `validate` is cheap (no GPU rendering), so iterate: fix, re-run, read the next error.

Common categories:

- **Path resolution errors** ("Failed to open relative import") — the runtime could not find a shader or texture file. Check whether the path in the error makes sense. Doubled or malformed paths indicate a bug in preset parsing or dependency validation.
- **Slang compilation errors** — the Slang compiler rejected a GLSL construct. Read the Slang diagnostic (file, line, message) and check whether the construct is valid GLSL that Slang doesn't support. If so, the fix belongs in the preprocessor's Slang compat pipeline (`fix_slang_compat` in `retroarch_preprocessor.cpp`).
- **Reflection/binding errors** — the compiled shader's resource layout doesn't match what the runtime expected. Check `compile_summaries` and `binding_coverage` in the JSON report.

### Confirming the fix

Once `validate` exits 0, run `diagnose` to exercise the full pipeline:

```bash
$CLI diagnose <preset.slangp> <input.png> --output /tmp/diag --frames 1 --verbose
```

This produces `report.json` plus per-pass intermediate PNGs. Inspect the report for degradation:

```bash
jq '.degradation | length' /tmp/diag/report.json
```

---

## Runtime failures

When `validate` passes (exit code 0) but the preset crashes or produces wrong output during rendering, the bug is in chain execution — not shader authoring.

### Reproducing with `diagnose`

`diagnose` runs the full forensic render pipeline:

```bash
$CLI diagnose <preset.slangp> <input.png> --output /tmp/diag --frames 1 --verbose
```

If the preset produces wrong output without crashing, compare the intermediate PNGs in the output directory to identify which pass diverges.

If the preset crashes, rebuild with ASAN to get precise memory diagnostics:

```bash
pixi run build -p asan
export ASAN_OPTIONS=detect_leaks=0
./build/asan/tools/cli/goggles-chain-cli diagnose <preset.slangp> <input.png> \
    --output /tmp/diag --frames 1 --verbose
```

ASAN reports tell you:

1. **Error type** — heap-buffer-overflow, use-after-free, stack-buffer-overflow, etc.
2. **Crash site** — the function and line where the bad access happened
3. **Allocation site** — where the buffer that overflowed was originally created
4. **Access size vs. buffer size** — how far past the end the access went

### Interpreting the crash

The access size and buffer size often reveal the category immediately:

- **2x overflow** (e.g., write of 614,400 into 307,200) — format mismatch. The code assumed 4 bytes per pixel but the image uses 8 (R16G16B16A16Sfloat) or 16 (R32G32B32A32Sfloat). Check whether `float_framebuffer` or `srgb_framebuffer` is set in the preset.
- **Small overflow** (a few bytes past end) — off-by-one in an index or count. Check loop bounds in the executor or resource allocator.
- **Crash in lavapipe / mesa internals** — the filter-chain issued an invalid Vulkan command. Trace back from the lavapipe frame to find the `vkCmd*` call in FC code.

If a `report.json` was produced before the crash, inspect it:

```bash
jq '.execution_trace[] | select(.type == "pass_start")' /tmp/diag/report.json
```

The execution trace shows which pass was running when the crash happened.

### Confirming the fix

Re-run `diagnose` with a debug build — expect exit code 0 and a complete set of intermediate PNGs. Then verify the output is visually correct via golden comparison.

---

## Golden verification

After fixing a preset, regenerate golden images and run the full comparison:

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json pixi run update-golden -p test
ctest --test-dir build/test -R '^fc_test_upstream_golden$' --output-on-failure
```

The golden test renders each preset on lavapipe at 320x240, captures 3 frames with all intermediate passes, and compares against committed reference PNGs at 5% tolerance. A passing suite confirms the fix produces correct visual output, not just absence of crashes.
