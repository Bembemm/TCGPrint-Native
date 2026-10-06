# TCGPrint Native

Native desktop rewrite of TCGPrint.

## Goals

- C++23
- Qt 6 / Qt Quick
- GPU-backed compositor
- local-first architecture
- SQLite persistence
- high-performance artwork pipeline
- lossless/high-fidelity PDF output
- Windows-first desktop application

## Source baseline

Behavioral reference:

Bembemm/TCGPrint
chatgpt/selection-picker-visual-rescue
7f2308764d3f4691d662e29d3ad0cc9ee8535a1b

## Initial scope exclusion

Plotter/cutter integration is deferred.

This includes:

- Silhouette
- .studio3
- DXF/SVG cutter output
- cutter-specific registration
- cutter-specific reserved zones

Printed PDF trim/cut guides, duplex and printer calibration remain in scope.

See `docs/migration/`.
