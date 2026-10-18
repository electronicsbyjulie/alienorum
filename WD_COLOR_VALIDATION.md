# White dwarf color delivery and validation

The implemented stages are committed on `wd-colors`:

- `0654705`: spectral integration tooling and a 40-node hydrogen gravity grid.
- `dd39bbb`: twelve angular color profiles derived from published figures,
  shared by the CPU and GPU sphere renderers.
- Final hardening: indexed published-color lookup with ambiguous identities
  rejected, plus downloaded spectrum parameter verification.

The earlier catalog work supplies 239 published color records, with 140
associations in the current generated catalog and 95 selectable records absent
from it. Importing those absent stars remains a separate follow-up, as requested.

## Checks performed on 2026-10-18

- Native build succeeded. Star tests: 44 passed; color tests: 11 passed;
  serialization/search tests: 35 passed.
- Eight selected catalog regressions passed: WD merging/reload, companion
  photometry, Tau Ophiuchi B, Taurus constellation identification, host caching,
  Gaia updates, and Atlas orbital planes.
- Two Python integration tests passed, covering exposure invariance and invalid
  spectral input rejection.
- AddressSanitizer star suite: all 44 passed, with leak detection disabled.
- MinGW syntax checks passed for the modified star code and rendering paths.
- Both complete sphere shader stages passed `glslangValidator`.
- A fresh catalog was generated in an isolated application directory. The app
  opened the WD1330+6435 system scene, saved a green-star snapshot, and exited
  successfully. The existing project catalog was preserved.
- AddressSanitizer startup from that generated catalog also opened the system
  scene, saved a snapshot, and exited successfully without sanitizer errors
  (leak detection disabled).
- Both new runtime data files appear in the Windows CMake installation list and
  installer script guards.

## Limits of this delivery

The hydrogen gravity correction is an approximation anchored to the published
reference track. Angular profiles are fitted to digitized figure samples, not
the authors' original atmosphere intensities. Unsupported compositions retain
the documented fallback; no abundance or spectrum is invented.

This host's OpenGL driver rejects the renderer's existing FP64 fragment
extension. CPU scene rendering and offline shader compilation were checked;
GPU execution and frame-rate comparison await testing on supported hardware.
See [WD_COLOR_RENDERING.md](WD_COLOR_RENDERING.md) for data coverage, provenance,
normalization, and the published green-star figure/prose discrepancy.
