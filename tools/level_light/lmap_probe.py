# -*- coding: utf-8 -*-
"""Sample BOTH lightmap channels a surface uses: .a = hemi, .g = static sun occlusion (get_sun).

Sampling is done at triangle centroids -- vertex UVs sit on island edges where the padding is black.
"""
import os, struct, sys
import numpy as np
sys.stdout.reconfigure(encoding="utf-8")
from PIL import Image
from lvl_hemi import read_level, read_vbs, visual_info
from lvl_scan import read_ibs, ib_tris, USAGE_TEXCOORD

d = sys.argv[1]
ids = [int(x) for x in sys.argv[2:]]
shaders, visuals = read_level(d)
vbdata, vb = read_vbs(d)
ibdata, ib = read_ibs(d)
cache = {}
for vid in ids:
    v = visual_info(visuals[vid][1])
    sh = shaders[v["shader_id"]-1]
    lm = sh.split(",")[-1].strip()
    if lm not in cache:
        cache[lm] = np.array(Image.open(os.path.join(d, lm + ".dds")).convert("RGBA"))
    IMG = cache[lm]
    H, W = IMG.shape[:2]
    B = vb[v["vb"]]
    texels = [(o, t, u) for (o, t, u) in B["decl"] if u == USAGE_TEXCOORD]
    off = texels[1][0]
    base = B["off"] + v["vbase"] * B["vsize"]
    uv = [struct.unpack_from("<hh", vbdata, base + i*B["vsize"] + off) for i in range(v["vcount"])]
    uv = [(s/32768.0, t/32768.0) for s, t in uv]
    tris = ib_tris(ibdata, ib, visuals[vid][1])
    pts = [((uv[a][0]+uv[b][0]+uv[c][0])/3, (uv[a][1]+uv[b][1]+uv[c][1])/3) for a, b, c in tris if max(a,b,c) < len(uv)]
    A = [int(IMG[int(np.clip(t,0,1)*(H-1)), int(np.clip(u,0,1)*(W-1)), 3]) for u, t in pts]
    G = [int(IMG[int(np.clip(t,0,1)*(H-1)), int(np.clip(u,0,1)*(W-1)), 1]) for u, t in pts]
    us = [p[0] for p in pts]; vs = [p[1] for p in pts]
    print("#%-6d %-52s tris=%-5d hemi(a) avg=%6.1f max=%3d | sun(g) avg=%6.1f max=%3d | uv %.3f..%.3f / %.3f..%.3f  %s"
          % (vid, sh.split("/")[-1], len(pts), sum(A)/len(A), max(A), sum(G)/len(G), max(G),
             min(us), max(us), min(vs), max(vs), lm))
