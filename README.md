# ki-custom-ultra

Editor for custom Killer Instinct (1.5d) ultra combos: pick a fighter, build
a sequence from that fighter's normal and special animations, keep it under
the hit cap (default 32), and preview it in the real game.

This is a mostly **vibe coded** project from someone with **beginner-intermediate** experience making software and Claude implementing functionality. If you are not comfortable using this type of AI created software or encountering bugs, do **NOT** try it (if you like the concept, you are totally welcome to create your own version).
Works in both **MAME** and **a KI recomp project if you implement .ultrap file support** from the same compiled bytes: a
30-hit Jago ultra ran frame-identically in both (405 frames compared:
bank, frame, ultra flag, hit counter).

## Download

The [releases](https://github.com/CSword123/ki-custom-ultra/releases) have
a zip per system. Each one is a `ki-custom-ultra/` folder that runs where it
is extracted. It needs neither ki1-decomp nor ki1-port.

| zip | has | needs |
|---|---|---|
| `ki-custom-ultra-VERSION-windows-x64.zip` | `ki-custom-ultra.exe`, `SDL3.dll`, `python/` (embeddable Python 3.14), `mingw64/` (the part of MinGW-w64 gcc 16 that compiles the game code) | Windows 10 or 11, 64-bit |
| `ki-custom-ultra-VERSION-linux-x64.zip` | `ki-custom-ultra`, `lib/libSDL3.so.0` | x86-64, a recent glibc, `python3` and `gcc` (or `$KI_CC`, e.g. clang) |

1. Extract the zip.
2. Put your own MAME `kinst` set in `ki-custom-ultra/roms/`: `kinst.zip` and
   `kinst.chd` (or `kinst/kinst.chd`). See `roms/README.txt`.
3. Start `ki-custom-ultra.exe` (Windows) or `./ki-custom-ultra` (Linux). It
   can be started from anywhere: it goes to its own folder.
4. On the first start, press **check and unpack**. The editor checks the
   files against the original dumps and unpacks them into `data/` (~140 MB).
   Then it recompiles the game code from your U98 and compiles it with the
   bundled gcc (Windows) or yours (Linux), which takes a few seconds. This
   happens once.

The zips contain no game code or data. Everything from the game is made on
your machine from your own files (see [Building](#building) below). The
licenses of the bundled parts are in `THIRD-PARTY.txt`.

## What the editor does

**Fighter and project**
- The **fighter** drop-down covers the ten fighters with an ultra (Eyedol
  has none). **reset to stock** puts back the fighter's original ultra.
- The **hit cap** (default 32, `-` / `+`) and a bar show the hitboxes the
  ultra has against the cap.
- Projects are JSON files (`projects/custom.json` by default). The bottom
  bar has **browse...**, **save** and **load**. One project holds a custom
  ultra for any number of fighters.

**The move palette** (left)
- The tabs are **Stock** (the pieces of the stock ultra), **Openers**,
  **Normals**, **Specials**, **Linkers**, **Enders** and **Others** (see
  [Move names](#move-names)). Moves are named from their commands, and the
  specials from the MKKomplete move list.
- The filter matches the bank, move name or input.
- Double-click a move to add it to the **build-up** or the **finish**,
  whichever radio button is selected.
- Moves with a re-press window list their other swings under them
  ("+ swing B").
- **[shadow]** marks the after-image versions.
- **[crashes]** marks the 50 pieces known to crash the game. They are greyed
  out and cannot be added (see [Pieces that crash the
  game](#pieces-that-crash-the-game)).

**The ultra** (middle)
- There are two lists: **build-up** (ultra flag 1, after the stock prologue)
  and **finish** (ultra flag 2). Or tick **stock finish** to keep the
  fighter's own finish.
- Each piece is a row with its hit count and these controls:
  - **record**: the hit record its hits use (default, the move's own, or
    one of the stock ultra's light / heavy / lifting records)
  - **loops**: how many times it plays
  - **1 tick**: one tick per frame, as the stock ultras play
  - **chain**: loops follow the re-press window, as when the button is
    pressed again
  - **full**: keep the follow-through after the last hit
  - **banks**: follow the move through its bank switches
  - **2x**: every other frame, which is on by default for morph finishes
  - the swing to start at, then **up** / **dn** to reorder and **x** to
    remove
  - **clear** empties a whole list.
- **keep P2 standing until the last hits** (on by default) cuts
  follow-throughs, swaps lifting records for light ones and drops hold
  releases. The goal is that only the last segment knocks P2 down (see
  [Keeping P2 standing](#keeping-p2-standing)).
- **compiled script** shows the script the project compiles to, readable.

**Building**
- **compile** writes `build/custom.ultrap`, the patch for ki1-port
  (`--patch`). Its format is in
  [docs/ultrap-format.md](docs/ultrap-format.md).
- **build U98 (MAME)** writes `build/ki-l15d-ultra.u98`: your stock U98 with
  the ultra in it, for MAME.
- Neither builds while a project has a crashing piece in it.

**The preview** (right)
- **preview** compiles the project and plays the current fighter's ultra in
  the real game. **preview stock** plays the original.
- **vs** picks P2 (default Fulgore). This matters for finishes that morph
  into the opponent.
- The controls are play / pause, `|<`, step `<` `>`, 0.25x-2x, **loop**,
  **mute** and **stop**. The timeline can be scrubbed. Sound plays at 1x.
- The timeline is coloured by phase (blue build-up, orange finish), with a
  tick per hit and a gold mark at the "Ultra Combo!" call. Red marks where
  P2 is off the ground or where the combo dropped.
- The status line shows the ultra flag, P1's bank and frame, the hit
  counter and whether P2 is in the air. Under it the editor lists when P2
  was lifted, when the combo dropped or ended, and when the call came.
- The first preview of a fighter (or fighter/opponent pair) takes ~4-6 s.
  Later ones stream in about 3 s, and playback starts at once.
- If a piece crashes the game, the editor says at which frame and why. The
  next preview starts the game again.

**Loading a patch made elsewhere**
- **load patch** (or `--load FILE`) previews an `.ultrap`, a U98 built for
  MAME, or a `kinst.zip` that has one.
- It tells a stock U98 (green) from a modified one (orange), lists the
  fighters whose ultra changed with their hit counts, and has a
  **preview NAME** button for each (see [Loading a patch made
  elsewhere](#loading-a-patch-made-elsewhere)).

**Command-line options**
- `--version`
- `--setup`: check and unpack `roms/` without the GUI
- `--load FILE`
- `--project P`, `--fighter N`, `--opponent N`, `--tab NAME`: start with
  these selected
- `--shot FILE.bmp [--preview-frame N]`: render, save a screenshot and exit
- Environment: `KI1_DATA` (the data folder), `KI_PYTHON`, `KI_CC`

## Building
The project is made using a **personal** PC decomp and recomp of KI1 arcade which I would not like to provide currently.
```
make -C ../ki1-decomp disasm        # building only: asm/seg0, for seg0's function starts
make run                            # the editor (Dear ImGui + SDL3)
make dist                           # dist/ki-custom-ultra/: what can be handed out
make release                        # dist/ki-custom-ultra-VERSION-{windows,linux}-x64.zip
```
Building needs ki1-port and ki1-decomp next to this folder. A built copy
does not: `make dist` and `make release` copy what it uses from them.
The program has **no game code or data in it**: `make` links ki1-port's
runtime alone (`make -C ../ki1-port lib`, `libki1.a`: the hardware, the
preview server, the DCS interpreter). The game code is recompiled and
compiled on the player's machine from the user's own U98, at setup:

1. ki1-port's recompiler (`build/recomp/recomp.py`, with seg0's function
   starts as a list of addresses, `functions.txt`, from ki1-decomp's asm,
   and ki1-port's entries, hooks and probes) turns the unpacked `seg0.bin`
   into C in `data/gen/` (0.4 s).
2. The player's C compiler builds it into the game library,
   `data/gen/game.dll` (Windows) or `game.so`: **gcc from MinGW-w64 on
   Windows** (the release's `mingw64/`, winlibs.com, or MSYS2's `mingw-w64-ucrt-x86_64-gcc`; its
   `bin` folder on PATH), gcc on Linux, or whatever `$KI_CC` names (clang
   works). One compiler process per CPU core, ki1-port's flags for its
   generated code; ~2 s on 16 cores, ~10 s on one. Without a compiler the
   setup screen says what to install.
3. `build/recomp/glue.c` (`src/gameglue.c`) is compiled in: a DLL cannot
   call into the program that loads it, so the editor hands the runtime
   functions the code calls (`ki1_io_read`, `ki1_hook`, ...) over in a
   table (`src/kcu_host.h`); the library exports only `kcu_game`, which
   takes the table and returns the entry table (`ki1_code`).

The preview server (the program started again with `--preview-server`)
loads the library. The DCS sound program runs on ki1-port's interpreter,
from the user's sound ROMs: its recompiled form is one function that takes
gcc ~40 s and 1 GB to compile, and the interpreter is fast enough (the game
runs ~390 frames/s headless). The library and C are made again when the
recompiler, its config or the headers change. Previews are pixel-identical
to ki1-port's own build (checked with gcc and clang libraries); the first
preview of a fighter takes ~6 s.

**Game data**: players put their own MAME `kinst` set in `roms/`:
`kinst.zip` and `kinst.chd` (or `kinst/kinst.chd`). On the first start the
editor shows what is missing; **check and unpack** checks every file
against the original dumps (SHA-1 of each ROM, the stock v1.5d
`ki-l15d.u98`, the CHD's blocks and the whole disk) and unpacks them into
`data/` (`$KI1_DATA` instead; ~140 MB): `hdd/kinst.img`,
`sound/dcs_rom.bin`, `ki-l15d.u98`, then the game segments `seg0-2.bin`
unpacked from that U98 (`build/py/u98pack.py`), the game code recompiled
and compiled from them (`gen/`, above) and
`catalog.json` made from `seg1.bin` (`tools/catalog.py`; remade when the
tools change). Without
them the editor does nothing else, and the preview server refuses to start.
`ki-custom-ultra --setup` does the same without the GUI. A zip whose U98 is
modified (a custom ultra built for MAME) is refused as the base; it can be
previewed with **load patch** once the editor runs.

`make dist` copies only the program, the Python tools (`tools/`, and
ki1-decomp's `moves.py`, `u98pack.py`, `layout.py` in `build/py/`),
`build/recomp/`, the README and `roms/README.txt`: no data folder,
generated code, library, catalog, `.ultrap` or U98. None of the ROM, disk or
segment bytes are in the program (checked by searching it for windows of
each) and none of the recompiled functions (`f_88...`; ki1-port's binary
has all 631). Players need Python 3 (setup, the compiler) and a C
compiler (gcc / MinGW-w64).

`make release` (`tools/release.sh windows|linux|all`) makes the release
zips from `make dist`. It adds the parts that make them stand alone:
- **Windows**: `SDL3.dll`, python.org's embeddable Python in `python/`
  (with `tools/` and `build/py/` in its `._pth`, since it does not add a
  script's folder to `sys.path`), and `mingw64/`. That folder holds only
  the part of a winlibs.com MinGW-w64 gcc (UCRT) that compiles the game
  code: gcc, cc1, collect2, as and ld with their DLLs, libgcc, the C headers
  the standard C headers pull in, and the CRT and import libraries that
  gcc's link spec names for a DLL. It is ~80 MB, against ~1 GB for the
  whole toolchain.
- **Linux**: `lib/libSDL3.so.0`, found through the program's
  `$ORIGIN/lib` rpath. The C++ runtime is linked in.

The editor uses `python\python.exe` and `mingw64\bin\gcc.exe` when they
are there and `KI_PYTHON` / `KI_CC` are not set. When started from another
folder, it changes to its own. Downloads are kept in
`build/release-cache/` and checked against pinned SHA-256s. The script
refuses to zip a folder that has a data folder, generated code, a game
library, a catalog, a `.bin`, an `.ultrap` or a U98 in it.

**Windows**: `make windows SDL3_DIR=/path/SDL3-3.x.y/x86_64-w64-mingw32`
cross-builds `build-win/ki-custom-ultra.exe` (+ SDL3.dll) with MinGW-w64 or
llvm-mingw (`g++` and `gcc`; ki1-port's library is cross-built with it;
a game.dll cross-built with MinGW gcc exports only `kcu_game`; the
release zip, run under wine 10 from a fresh folder, set up from the ROMs,
compiled game.dll with its own gcc and previewed Jago's ultra with the
same frames, hits and call as the Linux build);
in an MSYS2 UCRT64 shell plain `make` does. Run it from this folder (it
uses `build/`, `projects/`, `roms/`, `data/` and `tools/`); it runs
`tools/kiultra.py` with `$KI_PYTHON`, else `python\python.exe` from the
release, else `python`. The live preview works
there too (ki1-port's preview server keeps in-memory snapshots, no fork).

Command line, same thing without the GUI:

```
python3 tools/kiultra.py stock 1 > projects/jago.json      # start from the stock ultra
python3 tools/kiultra.py build projects/jago.json --ultrap build/custom.ultrap --u98 build/ki-l15d-ultra.u98
python3 tools/kiultra.py dump projects/jago.json           # compiled script, readable

../ki1-port/ki1-port --patch build/custom.ultrap              # port
mkdir -p build/mame/kinst && cp build/ki-l15d-ultra.u98 build/mame/kinst/ki-l15d.u98
mame kinst -bios v1.5d -rompath "build/mame;$HOME/Games/MAME/roms"   # MAME (checksum warning is expected)
python3 tools/kiultra.py import build/ki-l15d-ultra.u98 --dump   # what an .ultrap / U98 / zip changes
```

(`roms/` is only for the original files the editor starts from.) The U98
export patches the user's stock U98 from `data/`.

`mame/ultrabot.lua` (MAME) and `KI1_ULTRA_BOT=1` (port) start a 2P game,
put P2 in ultra range and enter Jago's ultra, logging every frame.

## Preview

The editor starts itself as the preview server (`--preview-server`,
ki1-port's server linked in; log: `build/preview.log`) in the
background and shows the real game in the right
pane: **preview** compiles the project and plays the current fighter's
ultra, **preview stock** plays the original. Play/pause, step, scrub,
0.25x-2x, loop; sound at 1x. The timeline is coloured by ultra phase (blue
build-up, orange finish) with a tick per hit; the line under it shows the
ultra flag, P1's animation bank and frame, and the hit counter.

How the server works (`ki1-port/src/preview.c`): snapshots are processes.
It boots the game once and stops before character select; the first
preview of a fighter forks a process that selects that fighter for P1
(P2 is an idle Fulgore), waits for the round and walks P1 in, and stops
there. Each preview forks that snapshot, loads the `.ultrap`, puts P2 on its
last bar and starts P1's ultra through a hook on the command matcher
(`0x8800381C`), so no inputs or conditions are involved. First preview of a
fighter ~4 s (character select and round intro), later ones only stream
(~3 s for a whole ultra; playback starts immediately).

**vs** picks P2 (default Fulgore; `preview <c> <ultrap> <frames> <opponent>`
to the server, which moves P2's cursor at character select; one snapshot
per pair). It matters for finishes that morph into the opponent (Spinal's
Skeleport enders, op 53: P1 becomes the opponent and plays its move from
the table at `0x880340AA[opponent][X]`),. The "Ultra Combo!" call is
track 0x04F7 against Fulgore (speech event 0x78 from his KO reactions) and
0x008B against everyone else; the preview recognises both (DCS channel 1,
together with the victim's KO cry on channel 2).

Why a morph finish can drop: an ender started by its command freezes the
victim for the command's extra byte (40 for Skeleports): the command
code (`0x88003834`) stores it at the opponent's `+0x47`, and the script
interpreter skips the victim's script while it counts down. In an ultra
the finish is part of the script, nothing sets it, and the victim
recovers ~27 frames after the last hit. The morph move's first hit must
land before that (Jago's Tiger Fury m82 needs 12 ticks after the
teleport; Fulgore's m103, 5). Pieces that morph get a **2x** checkbox, on by default
(`"fast": true`: every other frame, the last kept): the Skeleport's
teleport-out drops from 11 frames to 6 and the finish connects against all
ten opponents (custom-spinal1: 14-15 hits each).

The timeline also shows, in red, where P2 is off the ground (lifted or
knocked down: `+0x98` set or height `+0x10` above `0x180`, as the game's
command code tests it) and where the combo dropped (the hit counter went
back to 0 while the ultra was still running); both are listed under it.

The hit counter in the preview is what the game counts. It can differ
from the hitbox count the editor caps (Fulgore's stock ultra: 14 hitboxes,
18 hits), so check the preview against the cap.

## Pieces that crash the game

50 moves crash the game when played in an ultra (the CPU takes an
exception, e.g. a jump to the exception vector at `0x80000180`): all in
the **Others** tab, no-command moves (follow-ups, reactions, throws) and
three danger moves (Thunder's and Glacius' No Mercy, Orchid's
Humiliation), for Jago, Spinal, Thunder, Glacius, Fulgore, Cinder and
Orchid. They are listed in `tools/crashes.json` (fighter, move, swing and
the opponents each crashed against; ids only), `tools/catalog.py` marks
them (`crashes`), and the editor shows them greyed out with **[crashes]**,
does not let them be added, marks one already in a project red and builds
nothing (compile, preview, U98) until it is removed.

`tools/crashsweep.py` made the list: every piece the palette offers (each
move as the palette adds it, and each of its swings) compiled as a
one-piece build-up with the stock finish and previewed through the preview
server, one server per fighter in parallel, against every opponent (11043
previews: all 1588 pieces against Fulgore, Jago and Riptor, the Others
pieces against all ten; nothing outside Others crashed). Whether a move
crashes depends on the opponent (Jago's crash against eight, not Thunder or
Riptor), so it is listed when it crashes against any. Run it again when the
way `kiultra.py` compiles move pieces changes (~2 hours for everything
against everyone on 16 cores).

After a crash the preview server exits (it used to hang in `exit()`, behind
its stdin thread) and the editor says so ("the game crashed at frame N"
with the runtime's reason, from `build/preview.log`); the next preview
starts the server again.

## Loading a patch made elsewhere

**load patch** (or `--load FILE`) takes an `.ultrap`, a U98 built for MAME, or
a zip with one (a MAME `kinst.zip` whose U98 was replaced).
`kiultra.py import` reads it: for a U98 it says whether it is the stock
v1.5d dump (SHA-1 matches; green in the editor) or modified (orange),
unpacks it and turns what differs from the user's stock seg1/seg2, plus any
extra segment (the scripts at `0x80020000`), into patch records
(`build/loaded.ultrap`). It lists the fighters whose ultra move entry points
elsewhere, with their hit counts, shows their scripts under **compiled
script**, and previews the current fighter's (else the first changed one's)
ultra; a **preview NAME** button per changed fighter switches. A changed
seg0 (game code) is reported but cannot be run, since the code is the
stock U98's, recompiled. The patch is previewed as it is: it is not turned
back into an editable project.

## How a custom ultra reaches the game

- **Where the script lives:** SRAM `0x80020000-0x8002C000`. The game never
  writes `0x80000200-0x8002CE00` (port write map, `KI1_WRITE_MAP` in a
  `make WATCH=1` build, over attract, CPU fights and the ultra bot), and in
  MAME a marker segment unpacked there by the boot code stayed intact. DRAM
  has no such gap: the largest fighter slab fills its slot to `0x88330C00`.
- **Hooking it up:** the fighter's ultra move entry in the move table
  (`0x88033AE0[c] + 8 × move`, seg1) points at the new script. The CPU
  uses the same move, so it does the custom ultra too.
- **Port:** `--patch FILE.ultrap` (`KIPATCH1`: address, length, bytes records)
  writes the script and the pointer after loading the segments.
  `.ultrap` was `.kip` before 0.01; the bytes are the same, so either
  extension loads. The format, for other ports and tools that want to load
  these files: [docs/ultrap-format.md](docs/ultrap-format.md).
- **MAME:** `kiultra.py --u98` unpacks the stock U98 with ki1-decomp's
  `tools/u98pack.py`, patches seg1, adds the scripts as a fourth packed
  segment at `0x80020000`, and splices the stream back in.

## Project format

See `tools/kiultra.py`. Per fighter: `phase1` (build-up, ultra flag 1)
and `finish` (`"stock"` or segments, flag 2). A segment is a stock ultra
piece (`{"from": "ultra", "index": i}`, verbatim) or a move's frames in
its own bank (`{"from": "move", "move": id, "ticks": 1, "loops": n,
"record": r}`). From moves the compiler keeps frames, hitboxes, sounds and
movement ops and drops branches, cancel windows, combo-over, finisher and
ultra ops; hitboxes get the stock ultra's first hit record unless
`record` says otherwise. The stock prologue (setup check, op 74) is
always used, relocated. The hit cap (default 32) counts hitbox ops.

## What an ultra is (from ../ki1-decomp/analysis/moves.md)

A stock ultra is one move script: op 74 (ultra flag 1), then segments
`op 1 0x34 <bank>` + frames `[frame, ticks]` + hitboxes (op 4, 12 bytes:
record + 4 box bytes), op 74 again (flag 2), a short finish, end. Verified
frame by frame for Jago in the port (`KI1_ULTRA_BOT=1`): banks
18, 74, 18, 74, 81×8 hits, 32, 30, 79, 18 hits in 251 frames.

A custom ultra is therefore a **new script for the fighter's ultra move**,
built from segments the editor copies out of existing move scripts:

| Editor concept | Game data |
|---|---|
| fighter drop-down | char id 1-10 (Eyedol has no ultra move) |
| animation list (normals / specials) | `catalog.json` banks, kind `normal` / `special` / `ultra` |
| segment | bank + frame range + ticks + its hitboxes, optionally looped |
| hit counter / cap | number of op 4 hitboxes in the script |
| phase split | where the second op 74 goes (finisher part) |

## Keeping P2 standing

What drops the opponent, found by comparing hit records with the preview's
airborne bit: a hit record (32 bytes, table `0x88033BE8[c]`) with a
vertical launch (byte 6) or the knockdown bit (byte `0x17` bit 7) lifts
P2; once in the air the next hits juggle and P2 lands knocked down
(`0x13` bit 7 knocks down an opponent already in the air). And the stock
finishes end the hold set by the prologue (`op 18 F9 FA`, P1 `+0xF9`)
with `op 18 F9 00` after their last hit.

Several stock ultras lift P2 before their last hit on purpose (Jago at hit
14, Combo and Fulgore at 17: the last hits land on a falling opponent).
That is left as it is. "Keep P2 standing" (on by default, per fighter)
applies to pieces placed out of their stock order: their follow-through
after the last hit is cut (as Rare cut the moves in the stock ultras;
the **full** column, `"full": true`, keeps it for one piece, which still
gets the other changes below; with **chain**, `"chain": true`, the
loops of a move play as when its button is pressed again, following its
re-press window, op 10, e.g. Spinal's B+HP alternating two swings, and a
cut piece ends at the last re-press point). Moves with a re-press window list
their other swings under them in the move list ("+ swing B"), and a
piece's swing can be switched in its row (`"swing": i`); alone, a swing
plays with its own recovery, with chain the loops go on from it, lifting records become the stock ultra's
first (light) record, and hold releases are dropped, so only the last
segment knocks P2 down. Records chosen in the editor are kept. Stock
ultras compile byte-identical with it on or off. It cannot fix spacing: a
piece can still whiff because P2 was pushed out of its range; the
preview marks where the combo drops.

Move pieces start with the hit-spark type reset (`op 18 8E 00`; `+0x8E`,
read by the hit code at `0x8800A34C` unless the hit record picks a spark):
Soul Sword sets fire (9) and no move clears it, so every later hit with the
default record threw fire sparks. Likewise the overlay sprite bank
(`+0x94`, drawn over the fighter at its current frame by `func_88001B90`):
Soul Sword turns on its glowing-sword banks (`op 33 56` / `55`) and the
return to stance turns them off; in an ultra nothing did, so every later
swing drew a ghost cutlass. Move pieces start with `op 34` (overlay off)
when an earlier piece may have turned it on.

Pieces out of stock order are also kept in reach and on the ground:
they start at a fixed gap from P2 (`op 35`, default gap 20: Fulgore 56
units away, as in Jago's stock ultra; per segment `"align"`), and their
jump is dropped (`op 14` sets P1's upward speed), so the landing wait
(`op 11`, which repeats the frames before it until P1 lands) passes at
once; the frames an `op 11` jumps back over are never cut.

The "Ultra Combo!" call (speech event 0x78, sound 0x04F7 in each
fighter's speech table) comes at the end of every ultra, unless the KO
happens past a ledge (rooftop stage, type 6): then the game plays the
fall-off (sound 0x0525) instead. An ultra pushes P2 back the whole time
(Jago's stock ~230 units, 30 hits ~400), so the preview starts with room
behind P2: both fighters are moved 220 units toward the side behind P1,
and P2 is placed 70 px in front of P1 (where Jago's stock ultra connects;
on narrow stages the move hits the wall, the game clamps both fighters
onto it, and P2 is put back in front). The call comes when the combo
counter resets after the ultra, which after a long knockdown slide can be
300+ frames after the last hit (Thunder: frame 515); the preview runs
until the call (gold mark on the timeline) plus 90 frames. In real play a long
ultra near that ledge still ends with the fall, as the stock ones do.

Hit records can be chosen per segment (the stock ultra's records, tagged
light, heavy = reaction 0x98, lifts).

## Move names

Normals are named from their commands: KI's high attacks are punches and
low attacks kicks, quick / medium / fierce = L / M / H (HQ = LP, LF = HK);
the command list the game picks (`func_88003774`: 0 standing, 2 crouching,
3 jumping) gives the stance; a distance limit makes it "close" and the
same button without one "far"; one direction is a command normal ("B+HK").
Specials are named from the MKKomplete Killer Instinct (1994) move list
(`tools/movenames.py`, matched by motion and button) and grouped by name.
Openers: the moves an auto-double may follow that are not combo-only,
i.e. what starts a combo (jump-ins, command normals like B*+HP, and specials
such as Jago's Wind Kick and Laser Sword m50; not Tiger Fury, which no
auto-double follows); the **Openers** tab lists them, and they stay in
Normals / Specials too. Linkers: combo-only specials (`combo>=` or
`last-move`) that an auto-double may follow. Auto-double commands name the moves they follow
(`prev-move`, either of two ids against `+0x1D`, `func_88005D10` at
`0x880061E4`), e.g. Jago's Laser Sword m87, Spinal's Soul Sword m88/m102,
Combo's Rollercoaster m36. Many continue in other banks (Rollercoaster
53, 9, 53), so they are added with **banks** (`"whole": true`: follow the
bank switches instead of stopping at the first) and **own** records
(`"record": null`). Linkers without hitboxes (Fulgore's Eye Laser
projectile, Glacius' teleport m58, which freezes the ultra) stay in Others.
**[shadow]** marks the shadow versions: moves that leave an after-image
trail (`op 18 F9 n` sets `+0xF9`, which counts down while
`func_88002424` draws an after-image every other frame). Most set it behind
`op 58 BB 00`, i.e. only while `+0xBB`, Spinal's skull count (op 52), is
nonzero: they are the moves his Skeleport morph plays (`0x880340AA`), and
trail when a morphed Spinal has skulls (the fighter doing them himself
never does). Fulgore m105, Sabrewulf m84 and Orchid m87 always trail. The
compiler drops op 58, so in a custom ultra these pieces always trail.
Enders: the other combo-only specials, grouped by name and added the same
way (banks, own records). Others: auto-doubles (normals that need a
previous move or a combo), finishers (the danger condition: 0 is
the Ultra Combo; 1 No Mercy, 2 Humiliation and 3 Ultimate are inferred
from their conditions), and moves with no command (follow-ups, reactions).

## Data

`tools/catalog.py` writes `data/catalog.json` for the editor (from the
user's `seg1.bin`; `make catalog` remakes it): every
move (bank, kind, inputs, the steps and hit count a move segment compiles
to) and the stock ultra in the compiler's segments.

## Next
- Bug fixes
