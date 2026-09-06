# -*- coding: utf-8 -*-
"""Trace the render geometry and report the ALBEDO at the hit, next to the lighting values.

The last question in a black-surface hunt: is the pixel dark because the light is zero, or because
the texture itself is black there? This samples the shader's first texture at the hit's barycentric
base UV.
"""
import os, struct, sys
import numpy as np
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from lvl_hemi import read_level, read_vbs, visual_info
from lvl_scan import read_ibs, ib_tris, USAGE_TEXCOORD

TEXROOTS = [r"I:\SteamLibrary\steamapps\common\STALKER Clear Sky\gamedata\textures",
            r"I:\SteamLibrary\steamapps\common\STALKER Clear Sky Vanilla\resources\gamedata\textures"]
d = sys.argv[1]
cam = np.array([float(x) for x in sys.argv[2:5]])
aim = np.array([float(x) for x in sys.argv[5:8]])
dir_ = aim - cam; dir_ /= np.linalg.norm(dir_)

shaders, visuals = read_level(d)
vbdata, vb = read_vbs(d); ibdata, ib = read_ibs(d)
best = None
for idx, (cid, payload) in enumerate(visuals):
    v = visual_info(payload)
    if not v or v["vb"] is None or v["vb"] >= len(vb):
        continue
    bb = v["bb"]
    lo, hi = np.array(bb[:3]), np.array(bb[3:])
    t0, t1, ok = 0.0, 1e9, True
    for k in range(3):
        if abs(dir_[k]) < 1e-9:
            if cam[k] < lo[k] or cam[k] > hi[k]: ok = False; break
        else:
            a_ = (lo[k]-cam[k])/dir_[k]; b_ = (hi[k]-cam[k])/dir_[k]
            if a_ > b_: a_, b_ = b_, a_
            t0 = max(t0, a_); t1 = min(t1, b_)
            if t0 > t1: ok = False; break
    if not ok: continue
    B = vb[v["vb"]]
    pos_off = next((o for (o, t, u) in B["decl"] if u == 0), None)
    if pos_off is None: continue
    base = B["off"] + v["vbase"]*B["vsize"]
    P = np.frombuffer(vbdata, np.float32, v["vcount"]*B["vsize"]//4, base).reshape(v["vcount"], B["vsize"]//4)[:, pos_off//4:pos_off//4+3]
    for (i0, i1, i2) in ib_tris(ibdata, ib, payload):
        if max(i0, i1, i2) >= len(P): continue
        a, b, c = P[i0], P[i1], P[i2]
        e1, e2 = b-a, c-a
        h = np.cross(dir_, e2); det = e1.dot(h)
        if abs(det) < 1e-9: continue
        f = 1.0/det; s = cam-a
        u = f*s.dot(h)
        if u < 0 or u > 1: continue
        q = np.cross(s, e1); vv = f*dir_.dot(q)
        if vv < 0 or u+vv > 1: continue
        t = f*e2.dot(q)
        if t > 0.01 and (best is None or t < best[0]):
            best = (t, idx, v, (i0, i1, i2), (1-u-vv, u, vv), B)

if not best:
    print("ray hits nothing"); raise SystemExit
t, idx, v, tri, bary, B = best
sh = shaders[v["shader_id"]-1]
base = B["off"] + v["vbase"]*B["vsize"]
tc0 = [(o, ty, u) for (o, ty, u) in B["decl"] if u == USAGE_TEXCOORD][0]
# unpack_tc_base (shaders/shared/common.h): (tc + (du,dv)) * 32/32768, i.e. /1024 -- NOT /32.
# du/dv are the ALPHA of the packed tangent/binormal D3DCOLORs (usage TANGENT=6 / BINORMAL=7).
tan_off = next((o for (o, ty, u) in B["decl"] if u == 6), None)
bin_off = next((o for (o, ty, u) in B["decl"] if u == 7), None)
uv = []
for k in tri:
    s_, t_ = struct.unpack_from("<hh", vbdata, base + k*B["vsize"] + tc0[0])
    du = vbdata[base + k*B["vsize"] + tan_off + 3]/255.0 if tan_off is not None else 0.0
    dv = vbdata[base + k*B["vsize"] + bin_off + 3]/255.0 if bin_off is not None else 0.0
    uv.append(((s_ + du)*32.0/32768.0, (t_ + dv)*32.0/32768.0))
u_ = sum(w*p[0] for w, p in zip(bary, uv)); v_ = sum(w*p[1] for w, p in zip(bary, uv))
tex = sh.split("/")[1].split(",")[0]
path = next((os.path.join(r, tex.replace("\\", os.sep)+".dds") for r in TEXROOTS
             if os.path.exists(os.path.join(r, tex.replace("\\", os.sep)+".dds"))), None)
print("hit at t=%.2f m, visual #%d, %s" % (t, idx, sh))
print("  base uv (%.3f, %.3f) -> wrapped (%.3f, %.3f)" % (u_, v_, u_ % 1.0, v_ % 1.0))
if path:
    im = np.array(Image.open(path).convert("RGB")); H, W = im.shape[:2]
    x = int((u_ % 1.0)*(W-1)); y = int((v_ % 1.0)*(H-1))
    px = im[y, x]
    print("  albedo at texel (%d,%d) of %dx%d: RGB %s  luminance %d" % (x, y, W, H, tuple(int(c) for c in px), int(px.mean())))
    Image.fromarray(im[max(0,y-24):y+24, max(0,x-24):x+24]).resize((384,384), Image.NEAREST).save("albedo_hit.png")
    print("  crop saved: albedo_hit.png (centre = the hit)")
