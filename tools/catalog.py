#!/usr/bin/env python3
"""Export each fighter's moves and animation banks for the ultra editor.

  catalog.py [--seg1 data/seg1.bin] [--decomp ../ki1-decomp] [-o data/catalog.json]

Reads seg1 (the game's data segment: the editor unpacks it from the user's
U98 into its data folder, $KI1_DATA or data/) through ki1-decomp's
tools/moves.py (its copy in build/py without ki1-decomp) and writes one
JSON file:

  fighters[c] = {
    name, ultra_move,
    moves:  [{id, bank, script, kind, inputs}]       kind: normal | special | linker | ender | other
    banks:  {bank: {kind, moves, steps, hits}}       steps from the first move using it
    ultra:  [{index, bank, phase, hits, bytes}]      the stock ultra in kiultra.py's segments
    stock_finish_hits
    records: [{id, tag}]                             the stock ultra's hit records: light,
                                                     heavy (reaction 0x98) or lifts (launcher)
Moves also have label (e.g. "close MK", "crouch LP", "B+HP", "D DF F + LP"),
labels, group (standing, crouching, jumping, command, chain) and order.
Openers (opener: true) are the normals and specials an auto-double may follow
(its prev-move condition names them), i.e. what starts a combo: e.g.
Jago's Wind Kick and Laser Sword m50, jump-ins, B*+HP; not Tiger Fury, which
no auto-double follows. The combo-only ones among them are the linkers.
Linkers are the combo-only specials (combo>= or last-move) that an
auto-double may follow (its prev-move condition names them), e.g. Jago's
Laser Sword m87, Spinal's Soul Sword m88/m102; they are grouped by name.

Moves that crash the game when played in an ultra have crashes: [swing, ...]
(0 the move itself) and crashes_against: [opponent, ...], from
tools/crashes.json (tools/crashsweep.py: every piece previewed alone against
every opponent); the editor does not let them be added.

Each move with a script also has seg / seg_hits: the steps kiultra.py
copies for a {"from": "move"} segment; moves that switch banks also have
more_banks and whole_hits (with {"whole": true}). Moves whose button can be pressed
again (op 10) also have swings: [{frames: [first, last] | null, hits}]
in chain order and swing_cycle, the swing the chain returns to.
  }

A step is {"f": frame, "t": ticks} or {"hit": record, "args": [...]} (op 4).
Commands from all input modes count. Bank kind is "normal" when a
button-only move uses it, else "special" when a motion move does,
"ultra" when only the ultra script does. Sprites stay on the disk: a bank
number indexes the fighter slab the game loads from the HDD
(fighters/fighterNN), which is what the preview renders through the port.
"""
import argparse
import json
import os
import sys

from pathlib import Path


import movenames as MN  # noqa: E402  (tools/movenames.py)


def load_moves(recomp: Path):
    sys.path.insert(0, str(recomp / "tools"))
    sys.path.append(str(Path(__file__).resolve().parent.parent / "build" / "py"))
    import moves  # ki1-decomp/tools/moves.py
    return moves


def walk(M, seg, start, bank, limit=2000):
    """Split a script into per-bank segments: [(bank, phase, steps)]."""
    out, phase, cur = [], 0, None
    for a, _, _ in M.script(seg, start, limit=limit):
        f, x = seg.b(a), seg.b(a + 1)
        if f:
            if cur is None:
                cur = (bank, phase, [])
                out.append(cur)
            cur[2].append({"f": f, "t": x})
        elif x == 1 and seg.b(a + 2) == 0x34:
            bank, cur = seg.b(a + 3), None
        elif x == 74:
            phase, cur = phase + 1, None
        elif x == 4:
            if cur is None:
                cur = (bank, phase, [])
                out.append(cur)
            cur[2].append({"hit": seg.b(a + 3), "args": [seg.b(a + i) for i in range(2, 12)]})
    return out


def kind_of(toks):
    if not toks:
        return "other"
    motion = any(a < 0x40 for a, _, _ in toks)
    return "special" if motion else "normal"


# KI's high attacks are punches, low attacks kicks; quick/medium/fierce as L/M/H
SF = ["LP", "MP", "HP", "LK", "MK", "HK"]
STANCE = {0: "", 1: "mode1 ", 2: "crouch ", 3: "jump "}     # command list = func_88003774's mode
STANCE_ORDER = {0: 0, 2: 1, 3: 2, 1: 3}
FINISHERS = {0: "Ultra Combo", 1: "No Mercy", 2: "Humiliation", 3: "Ultimate (No Mercy)"}


def command_label(M, mode, mask, toks, c=0):
    """Readable name of one command, e.g. "close MK", "crouch LP", "B+HP",
    "Tiger Fury (F D DF + HP)"; its kind (normal, command, special); and
    its group among the chained/conditional moves (None, "auto-double",
    "ender", "finisher")."""
    btn = "+".join(SF[i] for i in range(6) if mask & (1 << i)) or "?"
    dirs, close, far, prev, combo, danger = [], False, False, False, False, None
    for a, b, x in toks:
        if a == 0xE0:
            close = True
        elif a == 0xEA:
            far = True
        elif a in (0xE3, 0xE6):                 # prev-move, last-move
            prev = True
        elif a == 0xE2:                         # combo>=
            combo = True
        elif a == 0xE9:                         # danger(n): finishers, ultra
            danger = b
        elif a < 0x40:
            dirs.append(M.DIRS[a] if a < len(M.DIRS) else f"{M.DIRS[a - 10]}*" if a - 10 < len(M.DIRS) else f"d{a}")
        elif 0x40 <= a < 0xE0:                  # buttons inside the motion (e.g. HQ ... HQ)
            dirs.append("+".join(SF[i] for i in range(6) if a & (1 << i)) or f"b{a:02X}")
    stance = STANCE[mode] if mode != 1 else ""   # list 1 repeats list 0
    group = "finisher" if danger is not None else None
    if danger is not None:
        # danger(0) is the ultra (0x88033F06); 1-3 inferred from the
        # conditions: 1 the No Mercy, 3 the same move after a 4+ hit
        # combo (Ultimate), 2 without distance limits (Humiliation)
        fin = FINISHERS.get(danger, f"finisher {danger}")
        motion = " ".join(dirs)
        return f"{fin} ({motion + ' + ' if motion else ''}{btn})", "special", group, close
    if len(dirs) >= 2:
        motion = " ".join(dirs)
        name = MN.special_name(c, motion, btn)
        text = f"{motion} + {btn}"
        label = f"{stance}{name} ({text})" if name else f"{stance}{text}"
        if not group and (combo or prev):
            group = "ender"                     # combo enders / linkers
        return label, "special", group, close
    if dirs:
        label = f"{stance}{dirs[0]}+{btn}"
        if not group and (combo or prev):
            group = "auto-double"
        return label, "command", group, close
    if not group and (combo or prev):
        group = "auto-double"                   # normals that follow an opener
    return f"{stance}{'close ' if close else 'far ' if far else ''}{btn}", "normal", group, close


def hits_whole(M, seg, start):
    """hitboxes in a script through its bank switches (linkers without any,
    e.g. Glacius' teleport m58 or Fulgore's Eye Laser projectile, can't do
    anything in an ultra and stay in "other")"""
    return sum(1 for a, _, _ in M.script(seg, start, limit=600) if not seg.b(a) and seg.b(a + 1) == 4)


def fighter(M, seg, c):
    mv = M.moves(seg, c)
    cmds = {}
    entries = []
    for mode, mask, r, toks, m, extra in M.commands(seg, c):
        d = M.describe(toks, mask)
        k = kind_of([t for t in toks if t[0] < 0xE0] or [(0x40, 0, 0)])
        e = cmds.setdefault(m, {"kind": k, "inputs": [], "labels": []})
        if d not in e["inputs"]:
            e["inputs"].append(d)
        if k == "special":
            e["kind"] = "special"
        label, lk, chain, close = command_label(M, mode, mask, toks, c)
        entries.append((m, mode, mask, label, lk, chain, close))
    # "far": the plain version of a button that also has a close version
    has_close = {(mode, mask) for m, mode, mask, label, lk, chain, close in entries
                 if lk == "normal" and close and not chain}
    for m, mode, mask, label, lk, chain, close in entries:
        if lk == "normal" and not close and (mode, mask) in has_close and "far " not in label:
            label = label.replace(STANCE[mode], STANCE[mode] + "far ", 1) if STANCE[mode] else "far " + label
        e = cmds[m]
        if (label, lk, chain, mode, mask) not in e["labels"]:
            e["labels"].append((label, lk, chain, mode, mask))
    ultra_id = seg.b(0x88033F06 + c) if c != 11 else None
    # moves an auto-double may follow: prev-move (0xE3) passes when the
    # previous move (+0x1D) is either of its two ids (func_88005D10 at
    # 0x880061E4). Combo-only specials among them are the linkers.
    follow = {v for _, _, _, toks, _, _ in M.commands(seg, c) for a, b, x in toks if a == 0xE3 for v in (b, x)}
    moves, banks = [], {}
    for e in mv:
        info = cmds.get(e["id"], {"kind": "other", "inputs": [], "labels": []})
        # the move's name: its first command without a chain condition,
        # standing before crouching before jumping
        labels = sorted(info["labels"], key=lambda t: (t[2] is not None, STANCE_ORDER.get(t[3], 9), t[4]))
        entry = {"id": e["id"], "bank": e["anim"], "script": e["script"],
                 "kind": info["kind"], "inputs": info["inputs"],
                 "labels": list(dict.fromkeys(t[0] for t in labels))}
        if labels:
            label, lk, chain, mode, mask = labels[0]
            entry["label"] = label
            # editor tabs: normals (incl. command normals like B+HP), specials,
            # others (auto-doubles, combo enders/linkers, finishers, the rest)
            entry["kind"] = "other" if chain else "special" if lk == "special" else "normal"
            bare = label.split(" (")[0].replace("crouch ", "").replace("jump ", "")
            entry["group"] = chain if chain else "command" if lk == "command" else \
                ((bare if "(" in label else "unnamed specials") if lk == "special" else
                 {0: "standing", 1: "standing", 2: "crouching", 3: "jumping"}.get(mode, "other"))
            if e["id"] in follow and entry["kind"] in ("normal", "special"):
                entry["opener"] = True
            if chain == "ender" and e["id"] in follow and hits_whole(M, seg, e["script"]):
                entry["kind"], entry["group"] = "linker", bare
            elif chain == "ender":
                # the rest of the combo-only specials end a combo
                entry["kind"], entry["group"] = "ender", bare
            btn = min((i for i in range(6) if mask & (1 << i)), default=9)
            entry["order"] = [{"standing": 0, "crouching": 1, "jumping": 2, "command": 3,
                               "auto-double": 5, "ender": 6, "finisher": 7}.get(entry["group"],
                                                                                 4 if lk == "special" or
                                                                                 entry["kind"] in ("linker", "ender")
                                                                                 else 8),
                              label if lk == "special" else "", btn, 0 if "close" in label else 1, e["id"]]
        else:
            entry["label"] = f"move {e['id']}"
            entry["group"] = "no command (follow-ups, reactions, throws)"
            entry["order"] = [9, "", 9, 9, e["id"]]
        moves.append(entry)
        info = {"kind": entry["kind"]}
        if not e["script"] or e["id"] == ultra_id:
            continue
        b = banks.setdefault(e["anim"], {"kind": "other", "moves": [], "steps": None, "hits": 0})
        b["moves"].append(e["id"])
        # a normal's bank stays "normal" even when combo specials reuse it
        if info["kind"] == "normal" or (info["kind"] == "special" and b["kind"] == "other"):
            b["kind"] = info["kind"]
        if b["steps"] is None:
            segs = [s for s in walk(M, seg, e["script"], e["anim"], 400) if s[0] == e["anim"]]
            if segs:
                b["steps"] = segs[0][2]
                b["hits"] = sum("hit" in s for s in segs[0][2])
    # what kiultra.py compiles for {"from": "move"}: the move's steps in its
    # own bank (frames, hitboxes and the ops it keeps)
    for m in moves:
        if not m["script"]:
            continue
        _, steps = K.move_segment(seg, c, m["id"])
        m["seg"] = [{"f": seg.b(a), "t": seg.b(a + 1)} if seg.b(a) else
                    {"op": seg.b(a + 1), **({"hit": seg.b(a + 3)} if seg.b(a + 1) == 4 else {})}
                    for a in steps]
        m["seg_hits"] = sum(1 for x in m["seg"] if "hit" in x)
        # the whole move through its bank switches ({"whole": true})
        _, whole = K.move_segment(seg, c, m["id"], whole=True)
        more = [seg.b(a + 3) for a in whole if not seg.b(a) and seg.b(a + 1) == 1 and seg.b(a + 2) == 0x34]
        if more:
            m["more_banks"] = more
            m["whole_hits"] = sum(1 for a in whole if not seg.b(a) and seg.b(a + 1) == 4)
        # shadow version: sets the after-image timer (op 18 F9 n: +0xF9 counts
        # down, func_88002424 draws an after-image every other frame). Most
        # are behind op 58 BB 00, i.e. only when +0xBB, Spinal's skull count
        # (op 52), is set: the moves his Skeleport morph plays (0x880340AA),
        # which trail when a morphed Spinal has skulls. kiultra drops op 58,
        # so in a custom ultra the trail always plays.
        prev = None
        for a in whole if m["id"] != ultra_id else []:     # (every ultra's prologue has it)
            if not seg.b(a) and seg.b(a + 1) == 18 and seg.b(a + 2) == 0xF9 and seg.b(a + 3):
                m["shadow"] = "skulls" if prev is not None and seg.b(prev + 1) == 58 and \
                    seg.b(prev + 2) == 0xBB else "always"
                break
            if not seg.b(a):
                prev = a
        # morphs into the opponent at its end (op 53 X; "fast" in the editor)
        x = K.morphs(seg, c, m["id"])
        if x is not None:
            m["morph"] = x
        # swings reached by pressing the button again (op 10, kiultra.move_swings)
        _, sw, cyc = K.move_swings(seg, c, m["id"])
        if len(sw) > 1:
            m["swing_cycle"] = cyc
            m["swings"] = []
            for body, tail in sw:
                fr = [seg.b(a) for a in body if seg.b(a)]
                m["swings"].append({"frames": [fr[0], fr[-1]] if fr else None,
                                    "hits": sum(1 for a in body + tail if not seg.b(a) and seg.b(a + 1) == 4)})
    # the stock ultra in kiultra.py's segments ({"from": "ultra", "index": i})
    ultra, finish_hits, records = [], 0, []
    if ultra_id is not None:
        L = K.stock_layout(seg, c)
        for i, sg in enumerate(L["segs"]):
            ultra.append({"index": i, "bank": sg["bank"], "phase": sg["phase"], "hits": sg["hits"],
                          "bytes": sg["end"] - sg["start"]})
            if sg["phase"] == 2:
                finish_hits += sg["hits"]
            if sg["bank"] not in banks:
                banks[sg["bank"]] = {"kind": "ultra", "moves": [], "steps": None, "hits": sg["hits"]}
        # the stock ultra's hit records, for the editor's record choice
        records = []
        for r in dict.fromkeys(L["records"]):
            q = K.hit_record(seg, c, r)
            records.append({"id": r, "tag": "lifts" if K.launches(q) else "heavy" if q[8] == 0x98 else "light"})
    return {"name": M.NAMES[c], "ultra_move": ultra_id, "moves": moves,
            "banks": {str(k): banks[k] for k in sorted(banks)}, "ultra": ultra,
            "ultra_hits": sum(s["hits"] for s in ultra), "stock_finish_hits": finish_hits,
            "records": records}


def load_crashes():
    """{(fighter, move): {swings, against}} from tools/crashes.json"""
    p = Path(__file__).resolve().parent / "crashes.json"
    if not p.exists():
        return {}
    d = json.loads(p.read_text())
    return {(int(c), int(m)): e for c, moves in d["fighters"].items() for m, e in moves.items()}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    here = Path(__file__).resolve().parent.parent
    ap.add_argument("--decomp", type=Path, default=here.parent / "ki1-decomp")
    data = Path(os.environ.get("KI1_DATA") or here / "data")
    ap.add_argument("--seg1", type=Path, default=data / "seg1.bin")
    ap.add_argument("-o", "--out", type=Path, default=data / "catalog.json")
    args = ap.parse_args()
    global K
    M = load_moves(args.decomp)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import kiultra as K
    seg = M.Seg(args.seg1)
    data = {"source": str(args.seg1),
            "fighters": {str(c): fighter(M, seg, c) for c in range(1, 12)}}
    for (c, mid), e in load_crashes().items():
        for m in data["fighters"][str(c)]["moves"]:
            if m["id"] == mid:
                m["crashes"], m["crashes_against"] = e["swings"], e["against"]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(data, indent=1))
    for c, f in data["fighters"].items():
        kinds = {}
        for b in f["banks"].values():
            kinds[b["kind"]] = kinds.get(b["kind"], 0) + 1
        print(f"{c:>2} {f['name']:<10} moves {len(f['moves']):3d} banks {len(f['banks']):3d} "
              f"{kinds}  stock ultra {f['ultra_hits']} hits")


if __name__ == "__main__":
    main()
