#!/usr/bin/env python3
"""Compile custom ultra combos for KI1 1.5d, for MAME and ../ki1-port.

  kiultra.py build PROJECT.json [--ultrap out.ultrap] [--u98 out.u98]
  kiultra.py stock CHAR [--hits N] > project.json   (starting point: the stock ultra)
  kiultra.py dump PROJECT.json                       (compiled scripts, readable)

A project names, per fighter, the pieces of a new script for that fighter's
ultra move (the move id in 0x88033F06[c], also used by the CPU):

  {"version": 1, "hit_cap": 32, "fighters": {"1": {
      "phase1": [SEGMENT, ...],          # build-up, ultra flag 1
      "finish": "stock" | [SEGMENT, ...] # after the second op 74 (flag 2)
      "keep_standing": true              # default: only the last hit knocks down
  }}}

  SEGMENT = {"from": "ultra", "index": i,            stock ultra segment i, verbatim
             "record": n | null}                     (all its hitboxes use record n)
          | {"from": "move", "move": id,             a move's frames in its bank
             "frames": [first, last],                inclusive range of steps (optional)
             "ticks": 1 | null,                      force every frame to N ticks
             "record": n | null,                     hit record for its hitboxes
                                                     (default: the stock ultra's)
             "loops": n,                             repeat (default 1)
             "align": gap,                           start at this gap from P2 (op 35;
                                                     default 20 after a cut piece with
                                                     keep_standing)
             "chain": true,                          loops chain as when the button is
                                                     pressed again (op 10; Spinal's
                                                     B+HP alternates two swings);
                                                     a cut piece ends at the last
                                                     re-press point
             "swing": i,                             start at the move's swing i (0: its
                                                     start; i > 0: what a re-press jumps
                                                     to, e.g. Spinal's B+HP second swing)
             "whole": true,                          follow the move through its bank
                                                     switches (default: stop at the
                                                     first; linkers need it)
             "fast": true,                           every other frame (for pieces that
                                                     morph, op 53: see morphs())
             "full": true}                           keep the follow-through after the
                                                     last hit (keep_standing cuts it
                                                     when another piece follows)

Compiled scripts go to SRAM 0x80020000 (never written by the game; checked
with the port's KI1_WRITE_MAP and in MAME), and the move table entry of each
ultra move in seg1 is pointed at them. The prologue (setup check, op 74) is
the stock one, relocated. Output:

  .ultrap KIPATCH1 records for ki1-port --patch (and the editor's preview)
  .u98    stock U98 with seg1 patched and the scripts as an extra packed
          segment; MAME reports a checksum mismatch and runs it
"""
import argparse
import json
import struct
import sys
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
DECOMP = HERE.parent / "ki1-decomp"

SCRIPT_BASE = 0x80020000
SCRIPT_LIMIT = 0x8002C000          # 0x80000200-0x8002CE00 is unused SRAM
SEG1_VRAM = 0x88033900
STOCK_U98_SHA1 = "607394d4ba1713f38c2cb5159303cace9cde991e"     # ki-l15d.u98, v1.5d
DEFAULT_CAP = 32

sys.path.insert(0, str(DECOMP / "tools"))
sys.path.append(str(HERE / "build" / "py"))     # copies made by make, without ki1-decomp
import moves as M  # noqa: E402


def data_dir():
    """The game data: $KI1_DATA (the editor's data folder, unpacked from
    the user's kinst.zip and kinst.chd), else ki1-decomp's extracted files."""
    return Path(os.environ["KI1_DATA"]) if os.environ.get("KI1_DATA") else DECOMP / "build" / "extract"

# ops copied out of ordinary moves: everything except control flow, cancel
# windows, combo/finisher/ultra state and the bank switch the compiler emits
DROP = {0, 3, 20, 5, 10, 13, 24, 25, 26, 42, 49, 50, 58, 59, 76, 83, 7, 12,
        23, 41, 57, 73, 74, 75}


class Seg1:
    def __init__(self, path):
        self.s = M.Seg(path)
        self.d = bytearray(self.s.d)

    def raw(self, a, n):
        return bytes(self.d[a - SEG1_VRAM:a - SEG1_VRAM + n])


def op_len(seg, a):
    f, x = seg.b(a), seg.b(a + 1)
    if f:
        return 2, None
    kind, n = M.OPS.get(x, ("?", 2))
    if kind == "?":
        raise ValueError(f"unknown op {x} at {a:08X}")
    if kind in ("goto", "br8"):
        return align4(a + 8) - a, kind
    if kind == "br10":
        return align4(a + 10) - a, kind
    return n, kind


def align4(a):
    return a & ~3


class Script:
    """Script being assembled at a fixed address. Branch ops store their
    target in the word at align4(a + len) - 4, so they are re-laid out."""

    def __init__(self, base):
        self.base, self.b = base, bytearray()
        self.hits = 0
        self.hitboxes = []      # (offset of op 4 in self.b, segment number)
        self.segment = 0
        self.releases = []      # (offset of op 18 F9 00, segment): stops the after-images
        self.spark = None       # P1 +0x8E as left by the script so far (None: unknown)
        self.overlay = None     # P1 +0x94 likewise (op 33 sets, op 34 clears)
        self.loose = set()      # segments out of their stock context (keep_standing fixes them)

    @property
    def pc(self):
        return self.base + len(self.b)

    def raw(self, data):
        self.b += data

    def frame(self, f, t):
        self.b += bytes((f, t))

    def op(self, x, *args):
        self.b += bytes((0, x) + args)

    def branch(self, x, body, target, kind):
        """body = bytes after [0, x] up to the pointer, from the original"""
        a = self.pc
        n = (align4(a + 8) if kind in ("goto", "br8") else align4(a + 10)) - a
        out = bytearray((0, x)) + body
        out = out[:n - 4] + bytes(max(0, n - 4 - len(out)))
        self.b += out + struct.pack("<I", target)

    def copy(self, seg, a, end, reloc=None, filt=None, ticks=None, record=None, cut_after=None,
             explicit=False):
        """Copy steps [a, end) of a script. reloc(target) -> new target or
        None to drop the branch; filt(op) -> keep?; cut_after: skip the
        frames after this address (the follow-through after the last hit)"""
        keep = set()
        if cut_after is not None:
            # op 11 [back, threshold] repeats the steps `back` bytes before it
            # until the fighter lands: those frames must stay in front of it
            b = a
            while b < end:
                if seg.b(b) == 0 and seg.b(b + 1) == 11:
                    keep.update(range(b - seg.b(b + 2), b))
                b += op_len(seg, b)[0]
        while a < end:
            n, kind = op_len(seg, a)
            f, x = seg.b(a), seg.b(a + 1)
            if f and cut_after is not None and a > cut_after and a not in keep:
                pass
            elif f:
                self.frame(f, ticks or seg.b(a + 1))
            elif filt and not filt(x):
                pass
            elif kind in ("goto", "br8", "br10"):
                t = seg.w(a + n - 4)
                t2 = reloc(t) if reloc else None
                if t2 is not None:
                    self.branch(x, bytes(seg.b(a + i) for i in range(2, n - 4)), t2, kind)
            else:
                data = bytearray(seg.b(a + i) for i in range(n))
                if x == 18 and data[2] == 0xF9 and data[3] == 0:
                    self.releases.append((len(self.b), self.segment))
                if x == 18 and data[2] == 0x8E:
                    self.spark = data[3]
                if x == 33:
                    self.overlay = data[2]
                if x == 34:
                    self.overlay = 0
                if x == 4:
                    self.hits += 1
                    self.hitboxes.append((len(self.b), self.segment if not explicit else -1))
                    if record is not None:
                        data[3] = record
                self.raw(bytes(data))
            a += n


def stock_layout(seg, c):
    """Stock ultra: script range, prologue end, segment boundaries."""
    mv = M.moves(seg, c)
    uid = seg.b(0x88033F06 + c)
    start = mv[uid]["script"]
    steps = [a for a, _, _ in M.script(seg, start, limit=2000)]
    end = steps[-1] + op_len(seg, steps[-1])[0]
    ultras = [a for a in steps if seg.b(a) == 0 and seg.b(a + 1) == 74]
    # the prologue runs to the first frame, hitbox or bank switch after the
    # first op 74; segments start there, at each later bank switch (op 1
    # 0x34) and at the second op 74 (the finish, mid-bank in some ultras)
    def is_bank(a):
        return seg.b(a) == 0 and seg.b(a + 1) == 1 and seg.b(a + 2) == 0x34
    pro_end = next((a for a in steps if a > ultras[0] and
                    (seg.b(a) or seg.b(a + 1) == 4 or is_bank(a))), end)
    cuts = sorted({pro_end} | {a for a in steps if a > pro_end and is_bank(a)} | set(ultras[1:2]))
    cuts = [a for a in cuts if a < end]
    segs, bounds, bank = [], cuts + [end], mv_bank(seg, c, uid)
    for a in steps:
        if a >= pro_end:
            break
        if seg.b(a) == 0 and seg.b(a + 1) == 1 and seg.b(a + 2) == 0x34:
            bank = seg.b(a + 3)
    for i in range(len(cuts)):
        a, b = bounds[i], bounds[i + 1]
        if seg.b(a) == 0 and seg.b(a + 1) == 1 and seg.b(a + 2) == 0x34:
            bank = seg.b(a + 3)
        hits = sum(1 for s in steps if a <= s < b and seg.b(s) == 0 and seg.b(s + 1) == 4)
        phase = 2 if len(ultras) > 1 and a >= ultras[1] else 1
        segs.append({"start": a, "end": b, "bank": bank, "hits": hits, "phase": phase})
    records = [seg.b(s + 3) for s in steps if seg.b(s) == 0 and seg.b(s + 1) == 4]
    return {"move": uid, "entry": M.moves(seg, c), "start": start, "end": end,
            "pro_end": pro_end, "ultras": ultras, "segs": segs, "records": records}


def mv_bank(seg, c, move_id):
    return M.moves(seg, c)[move_id]["anim"]


def move_segment(seg, c, move_id, whole=False):
    """[(addr, len)] of a move's steps in its own bank, up to the first
    bank switch or end of the script; whole: through its bank switches
    (op 1 0x34, copied as they are) to the end, as linkers play (Combo's
    Rollercoaster: banks 53, 9, 53)."""
    mv = M.moves(seg, c)[move_id]
    out = []
    for a, _, _ in M.script(seg, mv["script"], limit=600):
        f, x = seg.b(a), seg.b(a + 1)
        if not f and x == 1 and seg.b(a + 2) == 0x34 and not whole:
            break
        try:
            n, kind = op_len(seg, a)
        except ValueError:          # unknown op: data after the script
            break
        if not f and kind == "end":
            break
        out.append(a)
    return mv["anim"], out


def move_swings(seg, c, move_id):
    """A move as the game chains it when its button is pressed again: op 10
    [mask, target] opens a re-press window (func_88006670 at 0x88006B8C
    stores the mask in +0x60 and the target in +0x64), and pressing the
    button jumps to the target, skipping the recovery after the op. Spinal's
    B+HP chains swing A (frames 7-13) into swing B (15-21) and B back into A,
    as in his stock ultra. Returns bank, [(body, tail)] in chain order and
    the index the chain cycles back to; tail = the steps after the op 10,
    played only when the chain stops there."""
    mv = M.moves(seg, c)[move_id]
    swings, starts, a0 = [], [mv["script"]], mv["script"]
    while True:
        body, tail, nxt = [], [], None
        for a, _, _ in M.script(seg, a0, limit=600):
            f, x = seg.b(a), seg.b(a + 1)
            if not f and x == 1 and seg.b(a + 2) == 0x34:
                break
            try:
                n, kind = op_len(seg, a)
            except ValueError:
                break
            if not f and kind == "end":
                break
            if not f and x == 10 and nxt is None:
                nxt = seg.w(a + n - 4)
                continue
            (body if nxt is None else tail).append(a)
        swings.append((body, tail))
        if nxt is None:
            return mv["anim"], swings, len(swings) - 1
        if nxt in starts:
            return mv["anim"], swings, starts.index(nxt)
        starts.append(nxt)
        a0 = nxt


def swing_order(sw, cyc, first, n):
    """n swings chained from swing `first` (move_swings)"""
    if not 0 <= first < len(sw):
        raise ValueError(f"swing {first}: the move has {len(sw)}")
    out, j = [], first
    for _ in range(n):
        out.append(j)
        j = j + 1 if j + 1 < len(sw) else cyc
    return out


def morphs(seg, c, move_id):
    """X of a move that morphs into the opponent (op 53 X, func_88007878:
    P1 takes the opponent's character and plays its move
    0x880340AA[opponent][X] when the script ends), else None. In an ultra
    such a finish can drop: started by its command, an ender freezes the
    victim for the command's extra byte (stored at the opponent's +0x47 at
    0x88003834; the interpreter skips the victim's script while it counts
    down), but inside the ultra script nothing sets it, so the morph move
    must hit within ~27 frames of the last hit. Jago's Tiger Fury m82
    (Skeleport D N D + MP vs Jago) needs 12 ticks after the 11-frame
    teleport and drops; "fast" halves the teleport and it connects."""
    _, steps = move_segment(seg, c, move_id, whole=True)
    for a in steps:
        if not seg.b(a) and seg.b(a + 1) == 53:
            return seg.b(a + 2)
    return None


ALIGN_GAP = 20          # op 35 gap for a piece that follows a cut one


def align(s, gap):
    """Place P1 at a fixed distance from P2 (op 35, func_88007284 at
    0x88007438: position = P2's + the unit vector between them x (P2's
    width from 0x88033E96 + gap); speed zeroed). P2 was pushed back by the
    hits before, so a piece placed out of its stock order would otherwise
    start out of reach. Rare's scripts use gaps of -127..127; 20 puts
    Fulgore (width 36) 56 units away, as during Jago's stock ultra (57-87)."""
    s.op(35, gap & 0xFF, 0)


def compile_fighter(seg, c, spec, base, cap):
    L = stock_layout(seg, c)
    s = Script(base)

    def reloc_stock(t):
        return base + (t - L["start"]) if L["start"] <= t < L["pro_end"] else (
            t if not (L["start"] <= t < L["end"]) else None)

    s.copy(seg, L["start"], L["pro_end"], reloc=reloc_stock)
    s.hits = 0
    default_record = L["records"][0] if L["records"] else None

    standing = spec.get("keep_standing", True)
    last_p1 = max((i for i, sg in enumerate(L["segs"]) if sg["phase"] == 1), default=-1)

    def last_hit(addrs):
        hits = [a for a in addrs if seg.b(a) == 0 and seg.b(a + 1) == 4]
        return hits[-1] if hits else None

    def natural(sg, nxt):
        """sg followed by nxt as in the stock ultra (then its follow-through stays)"""
        if sg["from"] != "ultra" or sg.get("record") is not None:
            return False
        if nxt == "stock-finish":
            return sg["index"] == last_p1
        return (nxt is not None and nxt.get("from") == "ultra" and nxt.get("index") == sg["index"] + 1
                and nxt.get("record") is None)

    def emit(sg_list, after):
        """after: what follows the list (None, "stock-finish" or a segment)"""
        for i, sg in enumerate(sg_list):
            reps = int(sg.get("loops", 1))
            # "chain": the loops are one piece, the move re-pressed (move_swings)
            chain = sg["from"] == "move" and sg.get("chain")
            loops = 1 if chain else reps
            for k in range(loops):
                nxt = sg if k + 1 < loops else (sg_list[i + 1] if i + 1 < len(sg_list) else after)
                # keep_standing: cut the follow-through after the last hit when
                # another segment follows that is not this one's stock successor
                cut = standing and nxt is not None and not (k + 1 == loops and natural(sg, nxt))
                # "full": the piece still counts as cut (no jump, P2 kept up,
                # the next piece re-aligned) but plays to its last frame
                tail_cut = cut and not sg.get("full")
                s.segment += 1
                if cut:
                    s.loose.add(s.segment)
                prev_loose = s.segment - 1 in s.loose
                gap = sg.get("align", ALIGN_GAP if standing and prev_loose else None)
                if sg["from"] == "ultra":
                    st = L["segs"][sg["index"]]
                    steps = [a for a, _, _ in M.script(seg, st["start"], limit=2000) if a < st["end"]]
                    if gap is not None:
                        align(s, gap)
                    s.copy(seg, st["start"], st["end"],
                           reloc=lambda t: t if not (L["start"] <= t < L["end"]) else None,
                           filt=(lambda x: x not in (74, 14)) if cut else (lambda x: x != 74),
                           record=sg.get("record"),
                           cut_after=last_hit(steps) if tail_cut else None,
                           explicit=sg.get("record") is not None)
                elif sg["from"] == "move":
                    swing = int(sg.get("swing", 0))
                    if chain:
                        bank, sw, cyc = move_swings(seg, c, sg["move"])
                        order = swing_order(sw, cyc, swing, reps)
                        # when cut, stop at the last re-press window, where
                        # the next piece follows as the next press would
                        steps = [a for j in order for a in sw[j][0]]
                        if not tail_cut:
                            steps += sw[order[-1]][1]
                    elif swing:
                        bank, sw, _ = move_swings(seg, c, sg["move"])
                        steps = sw[swing][0] + sw[swing][1]
                    else:
                        bank, steps = move_segment(seg, c, sg["move"], whole=bool(sg.get("whole")))
                    lo, hi = sg.get("frames") or [0, len(steps) - 1]
                    steps = steps[lo:hi + 1]
                    tail = last_hit(steps) if tail_cut and not chain else None
                    if sg.get("fast"):
                        # every other frame (the last one kept): a morph
                        # finish must hit before P2 recovers, see morphs()
                        fr = [a for a in steps if seg.b(a)]
                        keep = set(fr[::2]) | {fr[-1]} if fr else set()
                        steps = [a for a in steps if not seg.b(a) or a in keep]
                    s.op(1, 0x34, bank)
                    if s.spark != 0:
                        # hit-spark type (+0x8E, func_88008CBC at 0x8800A34C,
                        # unless the hit record picks one): Soul Sword sets
                        # fire (9) and no move resets it, so every later hit
                        # with a record that doesn't override it threw fire
                        s.op(18, 0x8E, 0)
                        s.spark = 0
                    if s.overlay:
                        # overlay sprite bank (+0x94, drawn over P1 at its
                        # frame by func_88001B90): Soul Sword turns on its
                        # glowing-sword banks (op 33 56 / 55) and the return
                        # to stance turns them off; in an ultra nothing did,
                        # and later swings drew ghost cutlasses
                        s.op(34)
                        s.overlay = 0
                    if gap is not None:
                        align(s, gap)
                    for a in steps:
                        n, _ = op_len(seg, a)
                        s.copy(seg, a, a + n, filt=(lambda x: x not in DROP and x != 14) if cut
                               else (lambda x: x not in DROP),
                               ticks=sg.get("ticks"), record=sg.get("record", default_record),
                               cut_after=tail, explicit=sg.get("record") is not None)
                else:
                    raise ValueError(f"segment source {sg['from']!r}")

    fin = spec.get("finish", "stock")
    fin_first = "stock-finish" if fin == "stock" else (fin[0] if fin else None)
    emit(spec.get("phase1", []), fin_first)
    if fin == "stock":
        u2 = L["ultras"][1] if len(L["ultras"]) > 1 else None
        if u2 is None:
            s.op(74)
            s.op(0)
        else:
            # the stock ending, plus the stock ultra's last build-up segment
            # when it comes right before (keep_standing leaves both alone)
            p1 = spec.get("phase1", [])
            last_p1 = max((i for i, sg in enumerate(L["segs"]) if sg["phase"] == 1), default=-1)
            if not (p1 and p1[-1].get("from") == "ultra" and p1[-1].get("index") == last_p1
                    and int(p1[-1].get("loops", 1)) == 1):
                s.segment += 1
            here = s.pc
            s.copy(seg, u2, L["end"],
                   reloc=lambda t: here + (t - u2) if u2 <= t < L["end"] else
                   (t if not (L["start"] <= t < L["end"]) else None))
    else:
        s.op(18, 0xF9, 0x00)
        s.op(74)
        emit(fin, None)
        s.op(0)
    if spec.get("keep_standing", True):
        keep_standing(seg, c, L, s)
    if s.hits > cap:
        raise ValueError(f"{M.NAMES[c]}: {s.hits} hits, cap is {cap}")
    return L, s


def hit_record(seg, c, r):
    """32-byte hit record r of fighter c (table 0x88033BE8[c], func_88008CBC)"""
    base = seg.w(0x88033BE8 + 4 * c)
    return bytes(seg.b(base + 32 * r + k) for k in range(32))


def launches(rec):
    """Hit that lifts or knocks down the opponent: a vertical launch in
    byte 6 or the knockdown bit 0x17.7. Checked with the preview's airborne
    bit: Jago's 0x60 (byte 6 = 2) lifts at hit 14 of his stock ultra, 0x50
    (5) and Combo's 0x65 (0x18) lift; 0x5B (0, byte 7 = 0x10) does not.
    0x13.7 knocks down an opponent already in the air (func_88008CBC at
    0x88009554), so it is harmless while nothing lifts."""
    return bool(rec[6] or rec[0x17] & 0x80)


def keep_standing(seg, c, L, s):
    """Keep P2 on its feet until the last segment with hits. (At copy time,
    such pieces also lose their follow-through, their jump - op 14 sets the
    upward speed +0x10, so P1 stays on the ground next to P2 - and start at
    a fixed gap from P2, see align.) Segments placed
    out of their stock context (s.loose: followed by something other than
    their stock successor) were copied without their follow-through; here
    they also lose what drops P2: releases of the hold (op 18 F9 00 ->
    F9 FA) and records that lift or knock down (-> the stock ultra's first
    record, a light hit that keeps every opponent standing). Segments in
    stock order keep their designed behaviour, so stock ultras compile
    unchanged. If nothing in the last segment knocks down, its last hit gets
    the stock ultra's final record. Records chosen in the project are kept."""
    if not s.hitboxes or not L["records"]:
        return
    first, final = L["records"][0], L["records"][-1]
    last_seg = s.hitboxes[-1][1]       # -1 if the last hit has a chosen record
    for off, sgn in s.releases:
        if sgn in s.loose and sgn != last_seg:
            s.b[off + 3] = 0xFA
    for off, sgn in s.hitboxes:
        if sgn in s.loose and sgn != last_seg and launches(hit_record(seg, c, s.b[off + 3])):
            s.b[off + 3] = first
    if last_seg >= 0 and not any(launches(hit_record(seg, c, s.b[off + 3]))
                                 for off, sgn in s.hitboxes if sgn == last_seg):
        off = s.hitboxes[-1][0]
        if launches(hit_record(seg, c, final)):
            s.b[off + 3] = final


def stock_project(seg, c, hits=None):
    L = stock_layout(seg, c)
    p1 = [{"from": "ultra", "index": i} for i, sg in enumerate(L["segs"]) if sg["phase"] == 1]
    return {"version": 1, "hit_cap": DEFAULT_CAP, "fighters": {str(c): {"phase1": p1, "finish": "stock"}}}


def build(seg1, project):
    cap = int(project.get("hit_cap", DEFAULT_CAP))
    a = SCRIPT_BASE
    blob = bytearray()
    patches = []
    report = []
    for key, spec in sorted(project["fighters"].items(), key=lambda kv: int(kv[0])):
        c = int(key)
        if not 1 <= c <= 10:
            raise ValueError(f"fighter {c}: only 1-10 have an ultra move")
        L, s = compile_fighter(seg1.s, c, spec, a, cap)
        blob += s.b
        blob += bytes(-len(blob) % 4)
        entry = seg1.s.w(0x88033AE0 + 4 * c) + 8 * L["move"]
        patches.append((entry, struct.pack("<I", a)))
        report.append((c, L["move"], a, len(s.b), s.hits))
        a = SCRIPT_BASE + len(blob)
    if SCRIPT_BASE + len(blob) > SCRIPT_LIMIT:
        raise ValueError(f"scripts are 0x{len(blob):x} bytes, over the SRAM area")
    return bytes(blob), patches, report


def write_ultrap(path, blob, patches):
    out = bytearray(b"KIPATCH1")
    for addr, data in [(SCRIPT_BASE, blob)] + patches:
        out += struct.pack("<II", addr, len(data)) + data + bytes(-len(data) % 4)
    Path(path).write_bytes(out)


def write_u98(path, blob, patches):
    """The stock U98 with seg1 patched and the scripts as a fourth segment,
    packed with ki1-decomp's u98pack."""
    import u98pack
    from layout import U98_REGIONS, U98_SIZE
    # the user's stock U98 (checked when unpacked), else ki1-decomp's
    stock = data_dir() / "ki-l15d.u98"
    if not stock.exists():
        stock = DECOMP / "original" / "ki-l15d.u98"
    rom = bytearray(stock.read_bytes())
    tables, stream = u98pack.rom_parts(rom)
    segs = u98pack.unpack(stream, tables)
    seg1_addr, seg1 = segs[1]
    seg1 = bytearray(seg1)
    for addr, data in patches:
        seg1[addr - SEG1_VRAM:addr - SEG1_VRAM + len(data)] = data
    segs[1] = (seg1_addr, bytes(seg1))
    segs.append((SCRIPT_BASE, bytes(blob) + bytes(-len(blob) % 4)))
    stream = u98pack.pack(segs, tables)
    r = {name: (s, e) for name, s, e, _ in U98_REGIONS}
    ss, se = r["stream"]
    if ss + len(stream) > U98_SIZE:
        raise ValueError("packed game does not fit in U98")
    rom[ss:se] = bytes(se - ss)
    rom[ss:ss + len(stream)] = stream
    Path(path).write_bytes(rom)
    return U98_SIZE - ss - len(stream)


# --- importing a patch made elsewhere: an .ultrap or a patched U98 ------------

class Image:
    """The game's memory as a patch leaves it: stock seg1 with the patch's
    records written over it, and the records outside seg1 as regions of
    their own (KSEG0 addresses, as the scripts use them)."""

    def __init__(self, seg1, records):
        self.regions = [(SEG1_VRAM, bytearray(seg1.d))]
        for addr, data in records:
            a = 0x80000000 | (addr & 0x1FFFFFFF)
            for base, d in self.regions:
                if base <= a and a + len(data) <= base + len(d):
                    d[a - base:a - base + len(data)] = data
                    break
            else:
                self.regions.append((a, bytearray(data)))

    def _at(self, a):
        # outside every region (the disassembler's look past a script's
        # last op): zeros; ok() is what bounds the walk
        for base, d in self.regions:
            if base <= a and a + 4 <= base + len(d):
                return d, a - base
        return bytes(4), 0

    def ok(self, a):
        return any(base <= a < base + len(d) for base, d in self.regions)

    def b(self, a):
        d, o = self._at(a)
        return d[o]

    def sb(self, a):
        v = self.b(a)
        return v - 256 if v >= 128 else v

    def h(self, a):
        d, o = self._at(a)
        return struct.unpack_from("<H", d, o)[0]

    def sh(self, a):
        d, o = self._at(a)
        return struct.unpack_from("<h", d, o)[0]

    def w(self, a):
        d, o = self._at(a)
        return struct.unpack_from("<I", d, o)[0]


def read_ultrap(path):
    d = Path(path).read_bytes()
    if d[:8] != b"KIPATCH1":
        raise ValueError(f"{path} is not a KIPATCH1 file")
    records, p = [], 8
    while p + 8 <= len(d):
        addr, n = struct.unpack_from("<II", d, p)
        if p + 8 + n > len(d):
            raise ValueError(f"{path}: record at 0x{addr:08X} is cut short")
        records.append((addr, d[p + 8:p + 8 + n]))
        p += 8 + n + (-n % 4)
    return records


def u98_records(path, seg1):
    """A patched U98 (or a zip with one, e.g. a MAME set) as patch records:
    the bytes of seg1 and seg2 that differ from stock, and every segment the
    stock U98 does not have. Code changes (seg0) cannot be run: the game
    code is recompiled from the stock U98."""
    import u98pack
    path = Path(path)
    if path.suffix.lower() == ".zip":
        import zipfile
        with zipfile.ZipFile(path) as z:
            name = next((n for n in z.namelist() if n.lower().rsplit("/", 1)[-1] == "ki-l15d.u98"), None)
            if not name:
                raise ValueError(f"{path} has no ki-l15d.u98")
            rom = z.read(name)
    else:
        rom = path.read_bytes()
    import hashlib
    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 == STOCK_U98_SHA1:
        state = "stock v1.5d (SHA-1 matches the original dump): no custom ultra in it"
    else:
        state = f"modified (SHA-1 {sha1}, the stock v1.5d dump is {STOCK_U98_SHA1})"
    notes = [f"ki-l15d.u98: {state}"]
    if len(rom) != 0x80000:
        raise ValueError(f"{path}: {len(rom)} bytes, a U98 is 0x80000")
    tables, stream = u98pack.rom_parts(rom)
    try:
        segs = u98pack.unpack(stream, tables)
    except Exception as e:
        raise ValueError(f"{path}: not a KI U98 this tool can unpack ({e})")
    stock = {SEG1_VRAM: seg1.d, 0x88033880: (data_dir() / "seg2.bin").read_bytes(),
             0x88000000: (data_dir() / "seg0.bin").read_bytes()}
    records = []
    for addr, data in segs:
        old = stock.get(addr)
        if old is None:
            records.append((addr, bytes(data)))
            continue
        if addr == 0x88000000:
            if bytes(data) != old:
                notes.append("note: the U98's game code (seg0) differs from stock; the editor runs the "
                             "stock code (it is recompiled), only data changes are previewed")
            continue
        i = 0
        while i < min(len(data), len(old)):
            if data[i] == old[i]:
                i += 1
                continue
            j = i
            while j < len(data) and (j >= len(old) or data[j] != old[j]):
                j += 1
            s, e = i & ~3, (j + 3) & ~3
            records.append((addr + s, bytes(data[s:e])))
            i = e
        if len(data) > len(old):
            records.append((addr + len(old), bytes(data[len(old):])))
    return records, notes


def patched_ultras(seg1, img):
    """[(fighter, ultra move, script address)] whose ultra script moved."""
    out = []
    for c in range(1, 11):
        uid = seg1.s.b(0x88033F06 + c)
        entry = seg1.s.w(0x88033AE0 + 4 * c) + 8 * uid
        if img.w(entry) != seg1.s.w(entry):
            out.append((c, uid, img.w(entry)))
    return out


def import_patch(seg1, path):
    """(records, report lines, ultras, image) for an .ultrap or a patched U98."""
    p = Path(path)
    if p.suffix.lower() in (".u98", ".zip"):
        records, notes = u98_records(p, seg1)
    else:
        records, notes = read_ultrap(p), []
    img = Image(seg1, records)
    ultras = patched_ultras(seg1, img)
    lines = list(notes)
    for c, uid, a in ultras:
        hits = sum(1 for x, _, _ in M.script(img, a, limit=2000) if img.b(x) == 0 and img.b(x + 1) == 4)
        lines.append(f"{M.NAMES[c]:<10} move {uid:3d} -> {a:08X}  {hits} hits")
    if not ultras:
        lines.append("no fighter's ultra is changed by this patch")
    return records, lines, ultras, img


def dump_script(img, a, title):
    print(f"===== {title} at {a:08X}")
    for x, txt, _ in M.script(img, a, limit=2000):
        print(f"      {x:08X} {txt}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("project", type=Path)
    b.add_argument("--ultrap", "--kip", type=Path, dest="ultrap")
    b.add_argument("--u98", type=Path)
    st = sub.add_parser("stock")
    st.add_argument("char", type=int)
    d = sub.add_parser("dump")
    d.add_argument("project", type=Path)
    im = sub.add_parser("import", help="an .ultrap or a patched U98 (or a zip with one)")
    im.add_argument("patch", type=Path)
    im.add_argument("--ultrap", "--kip", type=Path, dest="ultrap", help="write it as an .ultrap (for the preview)")
    im.add_argument("--dump", action="store_true", help="print the changed ultra scripts")
    args = ap.parse_args()
    seg1 = Seg1(data_dir() / "seg1.bin")

    if args.cmd == "import":
        records, lines, ultras, img = import_patch(seg1, args.patch)
        for line in lines:
            print(line, file=sys.stderr)
        # first line for the editor: the fighters whose ultra changed
        print("fighters " + " ".join(str(c) for c, _, _ in ultras), file=sys.stderr)
        if args.dump:
            for c, _, a in ultras:
                dump_script(img, a, M.NAMES[c])
        if args.ultrap:
            out = bytearray(b"KIPATCH1")
            for addr, data in records:
                out += struct.pack("<II", addr, len(data)) + data + bytes(-len(data) % 4)
            args.ultrap.write_bytes(out)
            print(f"wrote {args.ultrap} ({len(records)} records)", file=sys.stderr)
        return

    if args.cmd == "stock":
        print(json.dumps(stock_project(seg1.s, args.char), indent=1))
        return
    project = json.loads(args.project.read_text())
    blob, patches, report = build(seg1, project)
    for c, mid, a, n, hits in report:
        print(f"{M.NAMES[c]:<10} move {mid:3d} -> {a:08X}  {n:4d} bytes  {hits} hits", file=sys.stderr)
    if args.cmd == "dump":
        # disassemble the compiled scripts through moves.py's walker
        for c, mid, a, n, hits in report:
            print(f"===== {M.NAMES[c]} at {a:08X}")
            base = a

            class S(M.Seg):
                def ok(self, x):
                    return base <= x < base + n

                def b(self, x):
                    return blob[x - SCRIPT_BASE]

                def w(self, x):
                    return struct.unpack_from("<I", blob, x - SCRIPT_BASE)[0]
            s = S.__new__(S)
            for x, txt, _ in M.script(s, a, limit=2000):
                print(f"      {x:08X} {txt}")
        return
    if args.ultrap:
        write_ultrap(args.ultrap, blob, patches)
        print(f"wrote {args.ultrap}", file=sys.stderr)
    if args.u98:
        free = write_u98(args.u98, blob, patches)
        print(f"wrote {args.u98} (0x{free:x} bytes of ROM free)", file=sys.stderr)


if __name__ == "__main__":
    main()
