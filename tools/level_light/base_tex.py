# -*- coding: utf-8 -*-
"""What colour is the BASE texture at the point a ray hits? (albedo, not lighting)

If the shader's lighting inputs at a spot look healthy but the pixel is black on screen, the next
suspect is the albedo itself -- the first TEXCOORD, sampled from the shader's first texture.
"""
import os, struct, sys
import numpy as np
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from lvl_hemi import read_level, read_vbs, visual_info
from lvl_scan import read_ibs, ib_tris, USAGE_TEXCOORD

d, vid = sys.argv[1], int(sys.argv[2])
TEXROOTS = [r"I:\SteamLibrary\steamapps\common\STALKER Clear Sky\gamedata\textures",
            r"I:\SteamLibrary\steamapps\common\STALKER Clear Sky Vanilla\resources\gamedata\textures",
            r"I:\SteamLibrary\steamapps\common\STALKER Clear Sky Vanilla\patches\gamedata\textures"]
shaders, visuals = read_level(d)
vbdata, vb = read_vbs(d); ibdata, ib = read_ibs(d)
v = visual_info(visuals[vid][1]); sh = shaders[v["shader_id"]-1]
tex = sh.split("/")[1].split(",")[0]
print("visual #%d, base texture: %s" % (vid, tex))
path = None
for r in TEXROOTS:
    p = os.path.join(r, tex.replace("\\", os.sep) + ".dds")
    if os.path.exists(p): path = p; break
print("  file:", path or "NOT FOUND")
if not path: raise SystemExit
img = np.array(Image.open(path).convert("RGB"))
H, W = img.shape[:2]
print("  size %dx%d" % (W, H))

B = vb[v["vb"]]; base = B["off"] + v["vbase"]*B["vsize"]
tc = [(o, t, u) for (o, t, u) in B["decl"] if u == USAGE_TEXCOORD][0]
pos_off = next(o for (o, t, u) in B["decl"] if u == 0)
P = np.frombuffer(vbdata, np.float32, v["vcount"]*B["vsize"]//4, base).reshape(v["vcount"], B["vsize"]//4)[:, pos_off//4:pos_off//4+3]
# base tc is SHORT2 scaled by 1/32 (unpack_tc_base: tc*(1/32) * dt scale) -- read raw and try both
rows = []
for (a, b, c) in ib_tris(ibdata, ib, visuals[vid][1]):
    if max(a, b, c) >= v["vcount"]: continue
    uvs = []
    for k in (a, b, c):
        s, t = struct.unpack_from("<hh", vbdata, base + k*B["vsize"] + tc[0])
        uvs.append((s/32.0, t/32.0))
    u_ = sum(p[0] for p in uvs)/3; v_ = sum(p[1] for p in uvs)/3
    x = int((u_ % 1.0)*(W-1)); y = int((v_ % 1.0)*(H-1))
    wc = (P[a]+P[b]+P[c])/3
    rows.append((int(img[y, x].mean()), wc, (x, y), (u_, v_)))
rows.sort(key=lambda r: r[0])
lum = np.array([r[0] for r in rows])
print("  albedo luminance over %d tris: min=%d p10=%d p50=%d max=%d" % (len(rows), lum.min(), np.percentile(lum, 10), np.percentile(lum, 50), lum.max()))
for l, wc, tx, uv in rows[:8]:
    print("    lum=%3d at world (%7.2f,%6.2f,%7.2f)  texel (%4d,%4d)  uv (%.2f, %.2f)" % (l, wc[0], wc[1], wc[2], tx[0], tx[1], uv[0], uv[1]))
