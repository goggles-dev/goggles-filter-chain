# Contributing to Goggles Filter Chain

## Scope

This repository owns the standalone filter-chain runtime and its reusable consumer boundary. Keep changes aligned with the extracted ownership split:

- FC owns runtime behavior, packaging, diagnostics-heavy verification, and `intermediate-pass` golden coverage.
- Goggles owns host orchestration and end-to-end host integration behavior.

Host-only problems should be filed in the Goggles repository, not here.

## Local Development

```bash
pixi install
pixi run build -p asan
pixi run test -p test
pixi run consumer-validation -p test
pixi run static-analysis
```

Use `find_package(GogglesFilterChain CONFIG REQUIRED)` as the consumer contract when validating installed usage. The standalone CI lanes are `format-check`, `build-and-test`, `consumer-validation`, and `static-analysis`.

## Goggles Co-Development Override

When testing a local FC checkout from a Goggles checkout, configure Goggles with:

```bash
cmake --preset asan \
  -DGOGGLES_FILTER_CHAIN_SOURCE_DIR=/abs/path/to/goggles-filter-chain
```

That override is the supported local workflow; do not require committed path rewrites.

## Boundary Rules

- Keep the stable diagnostics surface minimal: diagnostics `mode` plus passive summary/report queries.
- Do not document diagnostics-session lifecycle or pass capture as durable public API without an approved spec change.
- Keep standalone tests responsible for diagnostics-heavy and `intermediate-pass` verification.
- Keep consumer-facing guidance accurate for both installed packages and `add_subdirectory()` use.

## Filing Issues

Use the issue templates under `.github/ISSUE_TEMPLATE/` and classify whether the problem is runtime, packaging, consumer-validation, diagnostics, or ownership-boundary related.
