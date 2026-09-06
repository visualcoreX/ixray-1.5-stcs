# -*- coding: utf-8 -*-
"""Group a level's near-black surfaces into spatial clusters, so a whole broken structure shows up
as one entry instead of forty. Threshold is on the hemi the surface actually samples (see lvl_scan).

Usage: lvl_dark.py <level dir> [--max A] [--cell M] [--min-verts N]
"""
import os, struct, sys
import numpy as np
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lvl_hemi import read_level, read_vbs, visual_info
from lvl_scan import read_ibs, ib_tris, lmap_alpha, USAGE_NORMAL, USAGE_TEXCOORD


def measure(d):
    shaders, visuals = read_level(d)
    vbdata, vb = read_vbs(d)
    ibdata, ib = read_ibs(d)
    out = []
    for idx, (cid, payload) in enumerate(visuals):
        v = visual_info(payload)
        if not v or v["vb"] is None or v["vb"] >= len(vb):
            continue
        sh = shaders[v["shader_id"]-1] if 0 < v["shader_id"] <= len(shaders) else "?"
        B = vb[v["vb"]]
        base = B["off"] + v["vbase"] * B["vsize"]
        texels = [(o, t, u) for (o, t, u) in B["decl"] if u == USAGE_TEXCOORD]
        nrm = next(((o, t) for (o, t, u) in B["decl"] if u == USAGE_NORMAL), None)
        if ",lmap#" in sh and len(texels) > 1:
            A = lmap_alpha(d, sh.split(",")[-1].strip())
            if A is None:
                continue
            H, W = A.shape
            off = texels[1][0]
            uv = [struct.unpack_from("<hh", vbdata, base + i * B["vsize"] + off) for i in range(v["vcount"])]
            uv = [(s / 32768.0, t / 32768.0) for s, t in uv]
            tris = ib_tris(ibdata, ib, payload)
            pts = ([((uv[a][0]+uv[b][0]+uv[c][0])/3, (uv[a][1]+uv[b][1]+uv[c][1])/3)
                    for a, b, c in tris if max(a, b, c) < len(uv)] or uv)
            vals = [int(A[int(np.clip(t_, 0, 1)*(H-1)), int(np.clip(u_, 0, 1)*(W-1))]) for u_, t_ in pts]
            kind = "lmap"
        elif nrm and nrm[1] == 4:
            vals = [vbdata[base + i * B["vsize"] + nrm[0] + 3] for i in range(v["vcount"])]
            kind = "vert"
        else:
            continue
        bb = v["bb"]
        out.append({"idx": idx, "sh": sh, "kind": kind, "avg": sum(vals)/len(vals), "max": max(vals),
                    "verts": v["vcount"], "bb": bb,
                    "c": ((bb[0]+bb[3])/2, (bb[1]+bb[4])/2, (bb[2]+bb[5])/2)})
    return out


def main():
    d = sys.argv[1]
    amax = float(sys.argv[sys.argv.index("--max")+1]) if "--max" in sys.argv else 6.0
    cell = float(sys.argv[sys.argv.index("--cell")+1]) if "--cell" in sys.argv else 25.0
    minv = int(sys.argv[sys.argv.index("--min-verts")+1]) if "--min-verts" in sys.argv else 0
    rows = [r for r in measure(d) if r["avg"] <= amax and r["verts"] >= minv
            and not r["sh"].startswith("effects\\wallmark")]
    print("surfaces at or below hemi %.0f: %d" % (amax, len(rows)))
    buckets = {}
    for r in rows:
        key = (int(r["c"][0]//cell), int(r["c"][1]//cell), int(r["c"][2]//cell))
        buckets.setdefault(key, []).append(r)
    for key, items in sorted(buckets.items(), key=lambda kv: -sum(i["verts"] for i in kv[1])):
        xs = [i["c"][0] for i in items]; ys = [i["c"][1] for i in items]; zs = [i["c"][2] for i in items]
        print("\ncluster @ (%.0f, %.0f, %.0f)   %d surfaces, %d verts"
              % (sum(xs)/len(xs), sum(ys)/len(ys), sum(zs)/len(zs), len(items), sum(i["verts"] for i in items)))
        for i in sorted(items, key=lambda r: -r["verts"])[:12]:
            print("    #%-6d %-4s avg=%5.1f max=%3d verts=%-5d  %s" % (i["idx"], i["kind"], i["avg"], i["max"], i["verts"], i["sh"]))


if __name__ == "__main__":
    main()
