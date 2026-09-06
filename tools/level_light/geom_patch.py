# -*- coding: utf-8 -*-
"""Put a floor under the baked vertex hemi of chosen visuals, in level.geom.

For a vertex-lit surface the shader reads hemi from the ALPHA of the packed normal
(deffer_base_flat.vs: O.position = float4(Pe, I.Nh.w)), so a vertex baked to 0 gets no ambient at
all and renders pitch black -- while a flashlight still lights it, which is how these get reported.

This raises only the vertices of the named visuals, only where they are below the floor, and writes
the whole level.geom out for gamedata\\levels\\<level>\\, where a loose file overrides the archive.
No recompile; deleting the file reverts.

Usage: geom_patch.py <level dir> <out dir> <visual ids...> [--floor V] [--dry]
"""
import os, shutil, struct, sys
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lvl_hemi import read_level, read_vbs, visual_info


def main():
    d, out = sys.argv[1], sys.argv[2]
    ids, floor, dry = [], 20, "--dry" in sys.argv
    a = sys.argv[3:]
    i = 0
    while i < len(a):
        if a[i] == "--floor":  floor = int(a[i+1]); i += 2
        elif a[i] == "--dry":  i += 1
        else:                  ids.append(int(a[i])); i += 1

    shaders, visuals = read_level(d)
    vbdata, vb = read_vbs(d)
    buf = bytearray(vbdata)            # the fsL_VB chunk payload, patched in place
    changed = 0

    for vid in ids:
        v = visual_info(visuals[vid][1])
        B = vb[v["vb"]]
        nrm = next(((o, t) for (o, t, u) in B["decl"] if u == 3), None)
        if not nrm or nrm[1] != 4:
            print("#%d: no D3DCOLOR normal -- skipped" % vid); continue
        base = B["off"] + v["vbase"] * B["vsize"]
        lifted = 0
        for k in range(v["vcount"]):
            o = base + k * B["vsize"] + nrm[0] + 3
            if buf[o] < floor:
                buf[o] = floor; lifted += 1
        changed += lifted
        print("#%-6d %-46s verts=%-5d lifted %d to %d" % (vid, shaders[v["shader_id"]-1].split("/")[-1], v["vcount"], lifted, floor))

    if dry or not changed:
        print("dry run / nothing to do"); return

    src = os.path.join(d, "level.geom")
    raw = bytearray(open(src, "rb").read())
    # find the fsL_VB chunk (9) and splice the patched payload back in
    off = 0
    while off + 8 <= len(raw):
        cid, size = struct.unpack_from("<II", raw, off)
        if (cid & 0x7FFFFFFF) == 9:
            assert size == len(buf), (size, len(buf))
            raw[off+8:off+8+size] = buf
            break
        off += 8 + size
    else:
        raise SystemExit("fsL_VB chunk not found")

    os.makedirs(out, exist_ok=True)
    dst = os.path.join(out, "level.geom")
    open(dst, "wb").write(raw)
    print("wrote %s (%d bytes, %d vertices lifted)" % (dst, len(raw), changed))


if __name__ == "__main__":
    main()
