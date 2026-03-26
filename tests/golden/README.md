# Golden Reference Images

This directory contains standalone filter-chain golden PNGs for diagnostics-heavy and intermediate-pass verification.

## Tracked files

- `runtime_format_pass0.png` / `runtime_format_pass1.png` - intermediate pass captures for the runtime format preset
- `runtime_history_frame1.png` / `runtime_history_frame3.png` - temporal final-output captures for the runtime history preset
- `runtime_history_pass0_frame1.png` / `runtime_history_pass0_frame3.png` - temporal intermediate captures for the runtime history preset

## Upstream presets

The `upstream/` directory contains golden images for 14 curated RetroArch presets, generated on lavapipe at 320×240 from `assets/test_pattern_240p.png`.

Regenerate with: `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json pixi run update-golden -p test`

### Naming convention

- `final_frame{N}.png` — final output for frame N (N = 0, 1, 2)
- `pass_{ordinal}_frame{N}.png` — intermediate pass output

## Ownership

These images belong to standalone filter-chain verification. Goggles host-side visual tests no longer own or consume them.
