# -*- coding: utf-8 -*-
"""Cast a ray through the RENDER geometry of a level and report what it hits, in order.

`look_at` traces the COLLISION model (level.cform), which is a separate, simplified mesh -- a surface
that exists only in the render geometry is invisible to it, and the ray reports whatever lies behind.
This does the same trace against level.geom itself, so what it reports is what the player is looking
at, together with the hemi/sun the shader samples at the hit.

Usage: lvl_ray.py <level dir> camX camY camZ  hitX hitY hitZ  [--max N]
       (direction is taken as hit-cam, i.e. feed it the two points look_at printed)
"""
import os, struct, sys
import numpy as np
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from lvl_hemi import read_level, read_vbs, visual_info
from lvl_scan import read_ibs, ib_tris, USAGE_TEXCOORD

d = sys.argv[1]
cam = np.array([float(x) for x in sys.argv[2:5]])
aim = np.array([float(x) for x in sys.argv[5:8]])
maxn = int(sys.argv[sys.argv.index("--max")+1]) if "--max" in sys.argv else 8
dir_ = aim - cam
dir_ /= np.linalg.norm(dir_)

shaders, visuals = read_level(d)
vbdata, vb = read_vbs(d)
ibdata, ib = read_ibs(d)
_img = {}


def slab(bb):
    """does the ray cross this bbox?"""
    lo = np.array(bb[:3]); hi = np.array(bb[3:])
    t0, t1 = 0.0, 1e9
    for k in range(3):
        if abs(dir_[k]) < 1e-9:
            if cam[k] < lo[k] or cam[k] > hi[k]: return False
        else:
            a = (lo[k]-cam[k])/dir_[k]; b = (hi[k]-cam[k])/dir_[k]
            if a > b: a, b = b, a
            t0 = max(t0, a); t1 = min(t1, b)
            if t0 > t1: return False
    return True


hits = []
for idx, (cid, payload) in enumerate(visuals):
    v = visual_info(payload)
    if not v or v["vb"] is None or v["vb"] >= len(vb) or not slab(v["bb"]):
        continue
    B = vb[v["vb"]]
    pos_off = next((o for (o, t, u) in B["decl"] if u == 0), None)
    if pos_off is None:
        continue
    base = B["off"] + v["vbase"]*B["vsize"]
    P = np.frombuffer(vbdata, np.float32, v["vcount"]*B["vsize"]//4, base).reshape(v["vcount"], B["vsize"]//4)[:, pos_off//4:pos_off//4+3]
    for (i0, i1, i2) in ib_tris(ibdata, ib, payload):
        if max(i0, i1, i2) >= len(P):
            continue
        a, b, c = P[i0], P[i1], P[i2]                     # Moller-Trumbore
        e1, e2 = b-a, c-a
        h = np.cross(dir_, e2); det = e1.dot(h)
        if abs(det) < 1e-9: continue
        f = 1.0/det; s = cam-a
        u = f*s.dot(h)
        if u < 0 or u > 1: continue
        q = np.cross(s, e1); vv = f*dir_.dot(q)
        if vv < 0 or u+vv > 1: continue
        t = f*e2.dot(q)
        if t > 0.01:
            hits.append((t, idx, v, (i0, i1, i2), (1-u-vv, u, vv), B))

hits.sort(key=lambda r: r[0])
print("ray from (%.2f,%.2f,%.2f) dir (%.3f,%.3f,%.3f)" % (*cam, *dir_))
seen = set()
for t, idx, v, tri, bary, B in hits[:60]:
    if idx in seen: continue
    seen.add(idx)
    sh = shaders[v["shader_id"]-1]
    base = B["off"] + v["vbase"]*B["vsize"]
    texels = [(o, ty, u) for (o, ty, u) in B["decl"] if u == USAGE_TEXCOORD]
    info = ""
    if ",lmap#" in sh and len(texels) > 1:
        lm = sh.split(",")[-1].strip()
        if lm not in _img:
            _img[lm] = np.array(Image.open(os.path.join(d, lm+".dds")).convert("RGBA"))
        A = _img[lm]; H, W = A.shape[:2]
        off = texels[1][0]
        uv = []
        for k in tri:
            s_, t_ = struct.unpack_from("<hh", vbdata, base + k*B["vsize"] + off)
            uv.append((s_/32768.0, t_/32768.0))
        u_ = sum(w*p[0] for w, p in zip(bary, uv)); v_ = sum(w*p[1] for w, p in zip(bary, uv))
        x = int(np.clip(u_, 0, 1)*(W-1)); y = int(np.clip(v_, 0, 1)*(H-1))
        info = "hemi=%3d sun=%3d @texel(%d,%d) %s" % (A[y, x, 3], A[y, x, 1], x, y, lm)
    else:
        nrm = next(((o, ty) for (o, ty, u) in B["decl"] if u == 3), None)
        if nrm and nrm[1] == 4:
            info = "vertex hemi = %s" % ([vbdata[base + k*B["vsize"] + nrm[0] + 3] for k in tri],)
    p = cam + dir_*t
    print("  t=%6.2f m  #%-6d %-44s at (%7.2f,%6.2f,%7.2f)  %s" % (t, idx, sh.split("/")[-1][:44], p[0], p[1], p[2], info))
    if len(seen) >= maxn:
        break
