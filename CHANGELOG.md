# Changelog

## 0.03 (2026-10-30)

Move tabs checked, move names coloured.

- Move tabs checked for every fighter against the game's commands:
  - Fulgore's Eye Laser m96 (the combo version) is in **Linkers**. Its hit
    is the projectile, so it was left out for having no hitbox.
  - Spinal's F* B + HK m103 (after Bone Shaker) is in **Linkers**.
  - Combo's Rollercoaster m36 is in **Enders**: only his Flying Knee
    follow-up follows it, not an auto-double.
  - **Openers** counts only auto-doubles (button-only commands), not
    special follow-ups: Fulgore's Reflect, Sabrewulf's Howl, Combo's B+HK
    and B+HP and a few normals are no longer listed there.
- Combo breakers are labelled **[Combo Breaker]**.
- Buttons in move names are coloured by strength: quick light blue, medium
  yellow, fierce red.
- The **record** column is now **reaction**.
- A patch zip, `ki-custom-ultra-0.03-patch.zip`, updates a 0.02 folder
  (Windows or Linux) in place: extract it over the folder. The move
  catalog is remade on the next start.

## 0.02 (2026-10-29)

Bug fix.

- **build** and the dump of a project failed when the project's path had a
  space in it (`C:\Users\...\Saved Games\...`): the path went to
  `kiultra.py` unquoted, was split in two, and `kiultra.py` stopped with
  "unrecognized arguments". It is now quoted, as the editor's other paths.
- A patch zip, `ki-custom-ultra-0.02-patch.zip`, updates a 0.01 folder
  (Windows or Linux) in place: extract it over the folder (`make patch`).
- Linux: the release is built in Debian 13 (trixie), with SDL3 built there
  from source, so it runs on glibc 2.41 and newer. 0.01 needed the build
  machine's glibc 2.43.

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
