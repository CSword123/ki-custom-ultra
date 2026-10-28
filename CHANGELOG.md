# Changelog

## 0.01 (2026-10-28)

First release.

- Patch files are now `.ultrap` (were `.kip`). The format is unchanged
  (`KIPATCH1` records), so old `.kip` files still load (**load patch**,
  `kiultra.py import`) and ki1-port's `--patch` takes either.
- The format is written down in `docs/ultrap-format.md` (layout, address
  map, rules for readers, reference reader and writer in Python and C).
- `kiultra.py build/import --ultrap FILE` (`--kip` still works as an alias).
- `ki-custom-ultra --version`; the version is in `VERSION` and the window title.
- Release zips for Windows and Linux (`make release`, `tools/release.sh`).
  Each one stands alone, without ki1-decomp or ki1-port. The Windows zip
  bundles SDL3, an embeddable Python and the part of MinGW-w64 gcc that
  compiles the game code. The Linux zip bundles libSDL3 and uses the
  system's python3 and gcc.
- The editor changes to its own folder when started from elsewhere, and
  uses the bundled Python and gcc when they are there.
- Windows: Python and gcc run without a console window each (**check and
  unpack**, **build**), instead of opening one window per tool.
