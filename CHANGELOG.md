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
