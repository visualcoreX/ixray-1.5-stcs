# -*- coding: utf-8 -*-
"""How is the vertex hemi distributed inside one visual? (alpha of the packed normal)"""
import sys, os
import numpy as np
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lvl_hemi import read_level, read_vbs, visual_info
d, vid = sys.argv[1], int(sys.argv[2])
shaders, visuals = read_level(d)
vbdata, vb = read_vbs(d)
v = visual_info(visuals[vid][1])
B = vb[v["vb"]]
nrm = next((o for (o, t, u) in B["decl"] if u == 3 and t == 4), None)
base = B["off"] + v["vbase"] * B["vsize"]
h = np.array([vbdata[base + i*B["vsize"] + nrm + 3] for i in range(v["vcount"])])
print("#%d %s  vb=%d @%d  verts=%d" % (vid, shaders[v["shader_id"]-1], v["vb"], v["vbase"], v["vcount"]))
print("  hemi: zero=%d (%.1f%%)  1..15=%d  >15: min=%d p25=%d p50=%d p75=%d max=%d"
      % ((h == 0).sum(), 100.0*(h == 0).mean(), ((h > 0) & (h <= 15)).sum(),
         h[h > 15].min() if (h > 15).any() else -1,
         np.percentile(h[h > 15], 25) if (h > 15).any() else -1,
         np.percentile(h[h > 15], 50) if (h > 15).any() else -1,
         np.percentile(h[h > 15], 75) if (h > 15).any() else -1, h.max()))
