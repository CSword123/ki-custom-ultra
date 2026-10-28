# The `.ultrap` format (KIPATCH1)

An `.ultrap` file is a **memory patch** for Killer Instinct v1.5d (MAME set
`kinst`, BIOS `ki-l15d.u98`): a list of byte ranges to write into the game's
RAM after the game's segments are loaded. ki-custom-ultra uses it for custom
ultra combos, but the file itself says nothing about ultras. A program that
runs the original game code (a recompilation like ki1-port, an emulator, an
interpreter) only has to copy bytes to addresses; the game's own move-script
interpreter does the rest.

Files ki-custom-ultra wrote before 0.01 are named `.kip`. The bytes are the
same; readers should accept both extensions.

Reference implementations:

| What | Where |
|---|---|
| Writer | `tools/kiultra.py`, `write_ultrap()` |
| Reader (Python) | `tools/kiultra.py`, `read_ultrap()` |
| Reader + live apply / undo (C) | `../ki1-port/src/patch.c` |

## File layout

All integers are **little endian**, unsigned 32-bit.

```
offset  size  field
0       8     magic: the ASCII bytes "KIPATCH1" (4B 49 50 41 54 43 48 31)
8       ...   records, one after another, to the end of the file

record:
0       4     address   where the bytes go (MIPS virtual address, see below)
4       4     length    number of data bytes, n
8       n     data      the bytes, written as they are
8+n     p     padding   zero bytes, p = (4 - n % 4) % 4, so the next record
                        starts 4-byte aligned
```

There is no header beyond the magic, no record count and no checksum: a
reader loops while at least 8 bytes (one record header) remain. Fewer than 8
trailing bytes are ignored.

The `1` in the magic is the format version. A change to the layout gets a new
magic (`KIPATCH2`, ...); readers must refuse a magic they do not know.

## Addresses

Addresses are R4600 virtual addresses as the game's code uses them, normally
KSEG0 (`0x8xxxxxxx`). Take the physical address with `address & 0x1FFFFFFF`
(so KSEG1 `0xAxxxxxxx` aliases work as well) and map it to the board's RAM:

| Physical | Size | Memory | KSEG0 addresses |
|---|---|---|---|
| `0x00000000` | `0x80000` (512 KB) | SRAM | `0x80000000-0x8007FFFF` |
| `0x08000000` | `0x800000` (8 MB) | DRAM | `0x88000000-0x887FFFFF` |

A record must lie entirely inside one of the two. Anything else (ROM, I/O,
a record that runs off the end of a RAM) makes the file invalid.

## Rules for readers

- **All or nothing.** Read and check the whole file before writing anything:
  wrong magic, a record outside RAM or a record whose data is cut short by the
  end of the file rejects the file, and game memory stays as it was.
- **In order.** Apply records in file order. If two overlap, the later one
  wins.
- **Zero-length records** are valid and do nothing.
- **When to apply:** after the game's segments are in RAM (seg0 code, seg1
  and seg2 data unpacked from the U98 to `0x88000000`, `0x88033900` and
  `0x88033880`), before the game runs. On a reset or reboot the RAM is
  rebuilt, so apply the patch again.
- **Applying while the game runs** works (ki1-port's ULTRAS menu, F1, does
  it between frames, mid-fight included). Keep the bytes each
  record overwrites so the patch can be taken out again (restore them last
  record first). Do not swap patches while an ultra is running: the halfword
  at `0x88089F12` (the ultra flag) is non-zero then, and the script being
  played may be one the swap replaces.
- **Accept only v1.5d.** The addresses only mean something for the v1.5d
  game data (U98 SHA-1 `607394d4ba1713f38c2cb5159303cace9cde991e`). The
  file does not name the game version, so a reader for another version must
  not apply it.

## What ki-custom-ultra puts in a file

A file built by `kiultra.py build` (and the editor's **preview** /
**build**) has:

1. **One record at `0x80020000`**: the compiled ultra scripts of every
   fighter in the project, one after another, each 4-byte aligned. They live
   in SRAM `0x80020000-0x8002C000` (at most 48 KB), a part of SRAM the game
   never writes (`0x80000200-0x8002CE00` is unused; checked in ki1-port with
   a write map and in MAME). The scripts hold absolute addresses (jumps
   inside the script, pointers into the stock move data), so they only work
   at the address they were built for.
2. **One 4-byte record per changed fighter**: the first word of that
   fighter's ultra move entry in the move table, pointed at its new script.
   For fighter `c` (1-10) the entry is at
   `word(0x88033AE0 + 4*c) + 8 * byte(0x88033F06 + c)`: the table of
   per-fighter move tables, and the fighter's ultra move id. All of these
   are in seg1, in DRAM.

The CPU-controlled fighter uses the same move entry, so it plays the custom
ultra too.

`kiultra.py import` makes `.ultrap` files out of other patches too (a U98
built for MAME, or a zip with one): it diffs the U98's seg1 and seg2 against
the stock ones and adds every segment the stock U98 does not have. Such a
file can have more records, and records anywhere in RAM. **A reader must not
assume the two-part layout above;** it is what the editor writes, not part of
the format.

To find which fighters a patch changes, apply it to a copy of stock seg1 and
compare each fighter's ultra entry word with the stock one
(`patched_ultras()` in `tools/kiultra.py`).

## Example

A file with two records: 6 bytes at SRAM `0x80020000` and the word
`0x80020000` at `0x880457A8` (Jago's ultra entry). The numbers are made up
for the example, apart from the addresses.

```
00000000  4b 49 50 41 54 43 48 31                           KIPATCH1
00000008  00 00 02 80  06 00 00 00                          addr 0x80020000, len 6
00000010  01 02 03 04 05 06  00 00                          data, 2 bytes padding
00000018  a8 57 04 88  04 00 00 00                          addr 0x880457A8, len 4
00000020  00 00 02 80                                       data (0x80020000)
```

36 bytes. `python3 tools/kiultra.py import FILE --dump` reads any `.ultrap`,
lists the fighters whose ultra it changes and disassembles their scripts.

## Reference code

A reader in Python, with the checks above:

```python
import struct

SRAM = (0x00000000, 0x80000)
DRAM = (0x08000000, 0x800000)

def read_ultrap(data: bytes):
    """[(physical address, memory name, bytes)]; raises ValueError."""
    if data[:8] != b"KIPATCH1":
        raise ValueError("not a KIPATCH1 file")
    out, p = [], 8
    while p + 8 <= len(data):
        addr, n = struct.unpack_from("<II", data, p)
        if p + 8 + n > len(data):
            raise ValueError(f"record at 0x{addr:08X} is cut short")
        pa = addr & 0x1FFFFFFF
        for name, (base, size) in (("sram", SRAM), ("dram", DRAM)):
            if base <= pa and pa - base + n <= size:
                out.append((pa, name, data[p + 8:p + 8 + n]))
                break
        else:
            raise ValueError(f"record at 0x{addr:08X} (+0x{n:X}) is outside RAM")
        p += 8 + n + (-n % 4)
    return out
```

And a writer:

```python
def write_ultrap(records):
    """records: [(KSEG0 address, bytes)] -> file contents"""
    out = bytearray(b"KIPATCH1")
    for addr, data in records:
        out += struct.pack("<II", addr, len(data)) + data + bytes(-len(data) % 4)
    return bytes(out)
```

In C, applying a file to a machine whose SRAM and DRAM are byte arrays (as
`ki1-port/src/patch.c` does, which also keeps the overwritten bytes for
undo):

```c
/* 0 = applied; -1 = not a valid file, memory unchanged */
int ultrap_apply(const uint8_t *f, size_t size, uint8_t *sram, uint8_t *dram)
{
    struct ultrap_rec { uint8_t *dst; const uint8_t *src; uint32_t len; } rec[1024];
    size_t p = 8;
    int n = 0;
    if (size < 8 || memcmp(f, "KIPATCH1", 8))
        return -1;
    while (p + 8 <= size) {
        uint32_t a = f[p] | f[p + 1] << 8 | f[p + 2] << 16 | (uint32_t)f[p + 3] << 24;
        uint32_t len = f[p + 4] | f[p + 5] << 8 | f[p + 6] << 16 | (uint32_t)f[p + 7] << 24;
        uint32_t pa = a & 0x1FFFFFFF;
        uint8_t *dst = NULL;
        if (pa >= 0x08000000 && pa - 0x08000000 <= 0x800000 && len <= 0x800000 - (pa - 0x08000000))
            dst = dram + (pa - 0x08000000);
        else if (pa < 0x80000 && len <= 0x80000 - pa)
            dst = sram + pa;
        if (!dst || len > size - p - 8 || n == 1024)
            return -1;
        rec[n++] = (struct ultrap_rec){dst, f + p + 8, len};
        p += 8 + len + (4 - len % 4) % 4;
    }
    for (int i = 0; i < n; i++)         /* checked: now write, in order */
        memcpy(rec[i].dst, rec[i].src, rec[i].len);
    return 0;
}
```

(1024 records is this sample's limit, not the format's; files from the editor
have 1 + one per fighter. SRAM and DRAM here are the game's view of them: the
bytes as the R4600 sees them in little-endian mode, which is how the KI board
runs.)

## Running a patch elsewhere

- **ki1-port:** `ki1-port --patch FILE.ultrap`, or the F1 menu's ULTRAS page
  (lists `.ultrap` and `.kip` files in a folder you pick).
- **MAME:** MAME cannot load an `.ultrap`; `kiultra.py build PROJECT --u98
  OUT.u98` puts the same bytes into a repacked U98 instead (the scripts as an
  extra segment unpacked to `0x80020000` by the game's boot code, the move
  table words patched in seg1). Both run the same bytes, frame for frame.
