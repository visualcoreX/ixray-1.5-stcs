# -*- coding: utf-8 -*-
"""Per-triangle map of one visual: world centroid + the hemi/sun the shader reads there.

A visual is one shader+texture batch and can span a whole sector, so "the surface is bright on
average" says nothing about the black patch on it. This prints the distribution and the darkest
triangles with their world positions, which is what a patch has to target.
"""
import os, struct, sys
import numpy as np
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from lvl_hemi import read_level, read_vbs, visual_info
from lvl_scan import read_ibs, ib_tris, USAGE_TEXCOORD

d, vid = sys.argv[1], int(sys.argv[2])
top = int(sys.argv[3]) if len(sys.argv) > 3 else 15
shaders, visuals = read_level(d)
vbdata, vb = read_vbs(d); ibdata, ib = read_ibs(d)
v = visual_info(visuals[vid][1]); sh = shaders[v["shader_id"]-1]
B = vb[v["vb"]]; base = B["off"] + v["vbase"]*B["vsize"]
pos_off = next(o for (o, t, u) in B["decl"] if u == 0)
P = np.frombuffer(vbdata, np.float32, v["vcount"]*B["vsize"]//4, base).reshape(v["vcount"], B["vsize"]//4)[:, pos_off//4:pos_off//4+3]
tris = ib_tris(ibdata, ib, visuals[vid][1])
print("#%d %s   %d verts, %d tris" % (vid, sh, v["vcount"], len(tris)))

rows = []
if ",lmap#" in sh:
    lm = sh.split(",")[-1].strip()
    A = np.array(Image.open(os.path.join(d, lm + ".dds")).convert("RGBA"))
    H, W = A.shape[:2]
    off = [o for (o, t, u) in B["decl"] if u == USAGE_TEXCOORD][1]
    uv = [struct.unpack_from("<hh", vbdata, base + i*B["vsize"] + off) for i in range(v["vcount"])]
    uv = [(s/32768.0, t/32768.0) for s, t in uv]
    for (a, b, c) in tris:
        if max(a, b, c) >= len(uv): continue
        u_ = (uv[a][0]+uv[b][0]+uv[c][0])/3; t_ = (uv[a][1]+uv[b][1]+uv[c][1])/3
        x = int(np.clip(u_, 0, 1)*(W-1)); y = int(np.clip(t_, 0, 1)*(H-1))
        wc = (P[a]+P[b]+P[c])/3
        rows.append((int(A[y, x, 3]), int(A[y, x, 1]), wc, (x, y)))
    vals = np.array([r[0] for r in rows])
    print("hemi over %d tris: min=%d p10=%d p50=%d p90=%d max=%d   zero tris=%d"
          % (len(vals), vals.min(), np.percentile(vals, 10), np.percentile(vals, 50),
             np.percentile(vals, 90), vals.max(), (vals == 0).sum()))
    rows.sort(key=lambda r: r[0])
    for h, g, wc, tx in rows[:top]:
        print("   hemi=%3d sun=%3d  world (%7.2f,%6.2f,%7.2f)  texel (%4d,%4d)" % (h, g, wc[0], wc[1], wc[2], tx[0], tx[1]))
else:
    nrm = next(o for (o, t, u) in B["decl"] if u == 3)
    h = np.array([vbdata[base + i*B["vsize"] + nrm + 3] for i in range(v["vcount"])])
    print("vertex hemi: min=%d p10=%d p50=%d p90=%d max=%d  zeros=%d" %
          (h.min(), np.percentile(h, 10), np.percentile(h, 50), np.percentile(h, 90), h.max(), (h == 0).sum()))
    order = np.argsort(h)[:top]
    for i in order:
        print("   hemi=%3d  world (%7.2f,%6.2f,%7.2f)" % (h[i], P[i][0], P[i][1], P[i][2]))
