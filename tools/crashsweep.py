#!/usr/bin/env python3
"""Find the palette pieces that crash the game when played in an ultra, and
write tools/crashes.json (the editor greys them out; tools/catalog.py marks
them "crashes").

Every piece the editor's palette offers (each move, as the palette adds it,
and each of its swings) is compiled as a one-piece build-up with the stock
finish and previewed through the editor's preview server (the program run
with --preview-server), one server per fighter in parallel; a piece crashes
when the server dies (the game took a CPU exception). Whether one does
depends on the opponent (e.g. Jago's crash against eight, not Thunder or Riptor), so each is
tried against every opponent asked for, and listed when it crashes against
any. The list depends on the compiler (tools/kiultra.py): run this again
when the way move pieces are compiled changes.

  crashsweep.py [--opponents 1-10] [--kinds normal,special,...] [--merge OLD.json ...]

All pieces against all ten opponents take about 2 hours on 16 cores. Needs
the editor built and set up (data/). --merge adds the results of earlier
runs (this script's --results files) to the list.
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--opponents", default="1-10", help="P2s to try, e.g. 1-10 or 1,6,10")
ap.add_argument("--kinds", help="only these catalog kinds (normal, special, linker, ender, other)")
ap.add_argument("--fighters", default="1-10")
ap.add_argument("--results", type=Path, help="also write every preview's result here")
ap.add_argument("--merge", type=Path, nargs="*", default=[], help="earlier --results files to include")
ap.add_argument("--no-run", action="store_true", help="only merge (with --merge)")
ap.add_argument("-o", "--out", type=Path)
args = ap.parse_args()


def numbers(s):
    out = []
    for part in s.split(","):
        lo, _, hi = part.partition("-")
        out += range(int(lo), int(hi or lo) + 1)
    return out


ed = Path(__file__).resolve().parent.parent
out = args.out or ed / "tools" / "crashes.json"
fighters = numbers(args.fighters)
os.environ["KI1_DATA"] = os.environ.get("KI1_DATA") or str(ed / "data")
data = Path(os.environ["KI1_DATA"])
sys.path[:0] = [str(ed / "tools"), str(ed / "build" / "py")]
import kiultra as K  # noqa: E402

cat = json.loads((data / "catalog.json").read_text())
seg1 = K.Seg1(data / "seg1.bin")
work = ed / "build" / "crashsweep"
work.mkdir(parents=True, exist_ok=True)


KINDS = args.kinds.split(",") if args.kinds else None


def pieces(c):
    """the pieces the palette offers for fighter c, as it adds them"""
    for m in cat["fighters"][str(c)]["moves"]:
        if not m.get("seg") or (KINDS and m["kind"] not in KINDS):
            continue
        base = {"from": "move", "move": m["id"], "ticks": 1}
        if m["kind"] in ("linker", "ender"):
            base["record"] = None
            if "more_banks" in m:
                base["whole"] = True
        if "morph" in m:
            base["fast"] = True
        yield m, 0, base
        for i, w in enumerate(m.get("swings", [])[1:], 1):
            if w.get("frames") is not None:
                yield m, i, dict(base, swing=i)


class Server:
    def __init__(self):
        exe = ed / ("ki-custom-ultra.exe" if os.name == "nt" else "ki-custom-ultra")
        self.p = subprocess.Popen([str(exe), "--preview-server", "--data", str(data)],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=ed)
        self.texts, self.frames, self.dead = [], 0, False
        self.err = []
        threading.Thread(target=self.read, daemon=True).start()
        threading.Thread(target=lambda: self.err.extend(l.decode(errors="replace") for l in self.p.stderr),
                         daemon=True).start()
        self.wait(lambda: "booted" in self.texts, 60)

    def read(self):
        while True:
            h = self.p.stdout.read(12)
            if len(h) < 12:
                self.dead = True
                return
            _, typ, n = struct.unpack("<III", h)
            body = self.p.stdout.read(n)
            if typ == 2:
                self.texts.append(body.decode())
            else:
                self.frames += 1

    def wait(self, pred, timeout):
        end = time.time() + timeout
        while time.time() < end and not pred() and not self.dead:
            time.sleep(0.02)
        return pred()

    def preview(self, c, ultrap, opp):
        n = len(self.texts)
        self.frames = 0
        self.p.stdin.write(f"preview {c} {ultrap} 1500 {opp}\n".encode())
        self.p.stdin.flush()
        ok = self.wait(lambda: any(t.startswith("done") for t in self.texts[n:]), 60)
        new = self.texts[n:]
        if self.dead:
            return "crash", [l.strip() for l in self.err if "jump outside" in l or "exception" in l.lower()][-1:]
        if not ok:
            return "hang", []
        return ("no-start" if any("did not start" in t for t in new) else "ok"), []

    def close(self):
        try:
            self.p.stdin.write(b"quit\n"); self.p.stdin.flush(); self.p.wait(5)
        except Exception:
            self.p.kill()


def sweep(job):
    c, opp = job
    res, srv = [], Server()
    for m, swing, spec in pieces(c):
        proj = {"version": 1, "hit_cap": 999, "fighters": {str(c): {"phase1": [spec], "finish": "stock"}}}
        try:
            blob, patches, _ = K.build(seg1, proj)
        except Exception as e:
            res.append({"fighter": c, "opponent": opp, "move": m["id"], "swing": swing, "kind": m["kind"],
                        "result": "compile-error", "why": str(e)})
            continue
        ultrap = work / f"c{c}m{m['id']}s{swing}o{opp}.ultrap"
        K.write_ultrap(ultrap, blob, patches)
        r, why = srv.preview(c, ultrap, opp)
        res.append({"fighter": c, "opponent": opp, "move": m["id"], "swing": swing, "kind": m["kind"],
                    "label": m.get("label"), "result": r, "why": why, "frames": srv.frames})
        if r in ("crash", "hang"):
            if not srv.dead:
                srv.close()
            srv = Server()
    srv.close()
    return res


results = []
for p in args.merge:
    results += json.loads(p.read_text())
if not args.no_run:
    t0 = time.time()
    jobs = [(c, o) for o in numbers(args.opponents) for c in fighters]
    with ThreadPoolExecutor(min(len(jobs), max(1, (os.cpu_count() or 4) // 2))) as ex:
        for r in ex.map(sweep, jobs):
            results += r
    print(f"{time.time() - t0:.0f} s", file=sys.stderr)
if args.results:
    args.results.write_text(json.dumps(results, indent=1))
crash = {}
for x in results:
    if x["result"] in ("crash", "hang"):
        e = crash.setdefault(str(x["fighter"]), {}).setdefault(str(x["move"]), {"swings": [], "against": []})
        if x["swing"] not in e["swings"]:
            e["swings"].append(x["swing"])
        if x["opponent"] not in e["against"]:
            e["against"].append(x["opponent"])
tried = sorted({x["opponent"] for x in results})
lst = {"note": "pieces that crash the game when played in an ultra, by fighter and move: the swings "
               "(0 the move itself) and the opponents they crashed against; made by tools/crashsweep.py",
       "opponents_tried": tried,
       "fighters": {c: {m: {"swings": sorted(e["swings"]), "against": sorted(e["against"])}
                        for m, e in sorted(ms.items(), key=lambda kv: int(kv[0]))}
                    for c, ms in sorted(crash.items(), key=lambda kv: int(kv[0]))}}
out.write_text(json.dumps(lst, indent=1) + "\n")
for c in sorted(crash, key=int):
    name = cat["fighters"][c]["name"]
    print(f"{name:<10} " + ", ".join(f"m{m}" for m in sorted(crash[c], key=int)))
print(f"wrote {out}: {sum(len(v) for v in crash.values())} moves, opponents tried {tried}")
