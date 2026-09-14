# -*- coding: utf-8 -*-
"""Compare freshly compiled levels against the originals, on the files that actually have to ship.

Two questions, and they are different:

  1. IS THE BUILD COMPLETE AND SELF-CONSISTENT?  The render files index each other -- visuals in
     `level` point into `level.geom` / `level.geomx`, and the shader table names the lightmap pages.
     A page named in `level` but absent on disk, or a missing .geomx, is a crash or a black surface,
     and it does not depend on the original at all.

  2. DOES IT STILL MATCH THE ORIGINAL where it must?  The sector graph and portal topology come from
     the source scene; if they differ, the scene lost something on the way into the compiler.

What a compile has to hand over (see the level loader): level, level.geom, level.geomx, every lmap
page the shader table names, level_lods{,_nm}.dds, level.details + build_details.dds, terrain\\.
Everything else in a compiler output directory (build.*, level.ai, level.game, level.spawn,
level_stat.*) is either a build input or gameplay data tied to the game graph -- not ours to ship.

A file counts as missing only when THE ORIGINAL HAS IT: an underground level legitimately ships no
grass and no terrain, and a fixed checklist calls that a fault when it is not one.

Usage: lvl_compare.py <compiled root> <original root>
"""
import os
import re
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")

LMAP = re.compile(r"lmap#\d+_\d")
SHIPPED = ["level", "level.geom", "level.geomx", "level_lods.dds", "level_lods_nm.dds",
           "level.details", "build_details.dds", "level.hom", "level.som"]


def chunks(buf):
    out, off = [], 0
    while off + 8 <= len(buf):
        cid, size = struct.unpack_from("<II", buf, off)
        off += 8
        if size > len(buf) - off:
            break
        out.append((cid & 0x7FFFFFFF, buf[off:off + size]))
        off += size
    return out


def read_level(path):
    f = os.path.join(path, "level")
    if not os.path.isfile(f):
        return None
    top = dict(chunks(open(f, "rb").read()))
    ver, qual = struct.unpack_from("<HH", top[1], 0) if 1 in top else (0, 0)
    raw = top.get(2, b"")
    sh = []
    if raw:
        cnt = struct.unpack_from("<I", raw, 0)[0]
        off = 4
        for _ in range(cnt):
            e = raw.index(b"\0", off)
            sh.append(raw[off:e].decode("latin-1"))
            off = e + 1
    port = top.get(4, b"")
    secs = chunks(top[8]) if 8 in top else []
    roots, sec_portals = [], []
    for _cid, payload in secs:
        sub = dict(chunks(payload))
        roots.append(struct.unpack_from("<I", sub[2], 0)[0] if 2 in sub and len(sub[2]) >= 4 else None)
        sec_portals.append(len(sub.get(1, b"")) // 2)
    return {
        "quality": qual, "xrlc": ver,
        "visuals": len(chunks(top[3])) if 3 in top else 0,
        "shaders": len(sh),
        "lmaps": sorted(set(m for s in sh for m in LMAP.findall(s))),
        "portals": len(port) // 80,
        "pairs": [struct.unpack_from("<HH", port, i * 80) for i in range(len(port) // 80)],
        "sectors": len(secs), "roots": roots, "sec_portals": sec_portals,
    }


QUALITY = {0: "Draft", 1: "High", 2: "Custom"}


def main(cdir, vdir):
    levels = sorted(d for d in os.listdir(cdir) if os.path.isdir(os.path.join(cdir, d)))
    print("%-22s %11s %13s %13s %9s  %s" %
          ("level", "sect/port", "visuals", "lmap pages", "quality", "state"))
    print("%-22s %11s %13s %13s %9s" % ("", "new / orig", "new / orig", "new / orig", "new/orig"))
    print("-" * 104)
    problems = []
    for lvl in levels:
        c, v = os.path.join(cdir, lvl), os.path.join(vdir, lvl)
        ci, vi = read_level(c), read_level(v)
        if ci is None:
            print("%-22s  -- no `level` file in the compiled output" % lvl)
            problems.append((lvl, "no level file"))
            continue

        notes = []
        # --- completeness: only what the original also ships
        missing = []
        for f in SHIPPED:
            if os.path.isfile(os.path.join(v, f)) and not os.path.isfile(os.path.join(c, f)):
                missing.append(f)
        if os.path.isdir(os.path.join(v, "terrain")) and not os.path.isdir(os.path.join(c, "terrain")):
            missing.append("terrain/")
        # --- self-consistency: every page the level names must be on disk
        for page in ci["lmaps"]:
            if not os.path.isfile(os.path.join(c, page + ".dds")):
                missing.append(page + ".dds  <- NAMED BY THE LEVEL")
        if missing:
            notes.append("MISSING: " + ", ".join(missing))

        extra = []
        for f in SHIPPED:
            if os.path.isfile(os.path.join(c, f)) and not os.path.isfile(os.path.join(v, f)):
                extra.append(f)
        if extra:
            notes.append("new file not in the original: " + ", ".join(extra))

        on_disk = set(f[:-4] for f in os.listdir(c) if f.startswith("lmap#") and f.endswith(".dds"))
        unused = sorted(on_disk - set(ci["lmaps"]))
        if unused:
            notes.append("%d lmap pages on disk are never referenced" % len(unused))

        # --- parity with the original
        if vi is None:
            notes.append("no original to compare against")
            vs = vp = vv = vl = vq = 0
        else:
            vs, vp, vv, vl, vq = vi["sectors"], vi["portals"], vi["visuals"], len(vi["lmaps"]), vi["quality"]
            if ci["sectors"] != vs or ci["portals"] != vp:
                notes.append("SECTOR GRAPH DIFFERS")
            else:
                if ci["pairs"] != vi["pairs"]:
                    notes.append("PORTAL SECTOR PAIRS DIFFER")
                if ci["sec_portals"] != vi["sec_portals"]:
                    notes.append("PORTALS-PER-SECTOR DIFFERS")

        print("%-22s %5d/%-5d %6d/%-6d %6d/%-6d %4s/%-4s  %s" %
              (lvl, ci["sectors"], ci["portals"], ci["visuals"], vv,
               len(ci["lmaps"]), vl,
               QUALITY.get(ci["quality"], ci["quality"]), QUALITY.get(vq, vq),
               "; ".join(notes) if notes else "ok"))
        if any(n.startswith(("MISSING", "SECTOR", "PORTAL")) for n in notes):
            problems.append((lvl, "; ".join(notes)))

    print()
    if problems:
        print("NEEDS ATTENTION:")
        for lvl, why in problems:
            print("  %-22s %s" % (lvl, why))
    else:
        print("every level is complete, self-consistent, and keeps the original's sector graph")


main(sys.argv[1], sys.argv[2])
