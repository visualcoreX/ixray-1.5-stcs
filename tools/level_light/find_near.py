# -*- coding: utf-8 -*-
"""Every visual with a TRIANGLE inside a box around a point (not the bbox test -- real triangles)."""
import os, struct, sys
import numpy as np
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lvl_hemi import read_level, read_vbs, visual_info
from lvl_scan import read_ibs, ib_tris

d = sys.argv[1]
p = np.array([float(x) for x in sys.argv[2:5]])
R = float(sys.argv[5]) if len(sys.argv) > 5 else 2.0
shaders, visuals = read_level(d)
vbdata, vb = read_vbs(d); ibdata, ib = read_ibs(d)
out = []
for idx, (cid, payload) in enumerate(visuals):
    v = visual_info(payload)
    if not v or v["vb"] is None or v["vb"] >= len(vb): continue
    bb = v["bb"]
    if not all(bb[k]-R <= p[k] <= bb[k+3]+R for k in range(3)): continue
    B = vb[v["vb"]]
    pos_off = next((o for (o, t, u) in B["decl"] if u == 0), None)
    if pos_off is None: continue
    base = B["off"] + v["vbase"]*B["vsize"]
    P = np.frombuffer(vbdata, np.float32, v["vcount"]*B["vsize"]//4, base).reshape(v["vcount"], B["vsize"]//4)[:, pos_off//4:pos_off//4+3]
    dmin = np.linalg.norm(P - p, axis=1).min()
    if dmin <= R:
        out.append((dmin, idx, v["vcount"], shaders[v["shader_id"]-1]))
out.sort()
print("visuals with vertices within %.1f m of (%.2f, %.2f, %.2f):" % (R, *p))
for dmin, idx, n, sh in out:
    print("   %5.2f m  #%-6d verts=%-5d %s" % (dmin, idx, n, sh))
