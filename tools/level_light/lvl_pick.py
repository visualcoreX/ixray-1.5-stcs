# -*- coding: utf-8 -*-
"""Which TRIANGLE is under a world point, and what does the shader read there?

bbox containment is useless for picking: a merged batch's box spans half the factory. This walks the
actual triangles of every candidate visual, finds the nearest one to the point, and then reads the
hemi/sun the shader would sample AT THAT SPOT -- the lightmap texel the hit's barycentric UV lands on
(or the vertex hemi for a vertex-lit surface).

Usage: lvl_pick.py <level dir> X Y Z [X Y Z ...] [--margin M]
"""
import os, struct, sys
import numpy as np
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from lvl_hemi import read_level, read_vbs, visual_info
from lvl_scan import read_ibs, ib_tris, USAGE_TEXCOORD

USAGE_POSITION, USAGE_NORMAL = 0, 3
_img = {}


def img_of(d, name):
    if name not in _img:
        p = os.path.join(d, name + ".dds")
        _img[name] = np.array(Image.open(p).convert("RGBA")) if os.path.exists(p) else None
    return _img[name]


def closest_on_tri(p, a, b, c):
    """distance from p to triangle abc, and the barycentric coords of the closest point"""
    ab, ac, ap = b - a, c - a, p - a
    d1, d2 = ab.dot(ap), ac.dot(ap)
    if d1 <= 0 and d2 <= 0:  return np.linalg.norm(p - a), (1., 0., 0.)
    bp = p - b; d3, d4 = ab.dot(bp), ac.dot(bp)
    if d3 >= 0 and d4 <= d3: return np.linalg.norm(p - b), (0., 1., 0.)
    vc = d1*d4 - d3*d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3); q = a + v*ab
        return np.linalg.norm(p - q), (1-v, v, 0.)
    cp = p - c; d5, d6 = ab.dot(cp), ac.dot(cp)
    if d6 >= 0 and d5 <= d6: return np.linalg.norm(p - c), (0., 0., 1.)
    vb = d5*d2 - d1*d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2 / (d2 - d6); q = a + w*ac
        return np.linalg.norm(p - q), (1-w, 0., w)
    va = d3*d6 - d5*d4
    if va <= 0 and (d4-d3) >= 0 and (d5-d6) >= 0:
        w = (d4-d3) / ((d4-d3) + (d5-d6)); q = b + w*(c-b)
        return np.linalg.norm(p - q), (0., 1-w, w)
    den = 1. / (va + vb + vc); v = vb*den; w = vc*den
    q = a + ab*v + ac*w
    return np.linalg.norm(p - q), (1-v-w, v, w)


def main():
    d = sys.argv[1]
    args = sys.argv[2:]
    margin = 1.0
    if "--margin" in args:
        i = args.index("--margin"); margin = float(args[i+1]); args = args[:i] + args[i+2:]
    pts = [np.array([float(args[i]), float(args[i+1]), float(args[i+2])]) for i in range(0, len(args)-2, 3)]

    shaders, visuals = read_level(d)
    vbdata, vb = read_vbs(d)
    ibdata, ib = read_ibs(d)

    for p in pts:
        best = []
        for idx, (cid, payload) in enumerate(visuals):
            v = visual_info(payload)
            if not v or v["vb"] is None or v["vb"] >= len(vb):
                continue
            bb = v["bb"]
            if not all(bb[k]-margin <= p[k] <= bb[k+3]+margin for k in range(3)):
                continue
            B = vb[v["vb"]]
            pos_off = next((o for (o, t, u) in B["decl"] if u == USAGE_POSITION), None)
            if pos_off is None:
                continue
            base = B["off"] + v["vbase"] * B["vsize"]
            P = np.frombuffer(vbdata, dtype=np.float32, count=v["vcount"]*B["vsize"]//4,
                              offset=base).reshape(v["vcount"], B["vsize"]//4)[:, pos_off//4:pos_off//4+3]
            tris = ib_tris(ibdata, ib, payload)
            for (x, y, z) in tris:
                if max(x, y, z) >= len(P):
                    continue
                if min(P[x].min(), P[y].min(), P[z].min()) > max(p) + margin:
                    pass
                dist, bary = closest_on_tri(p, P[x], P[y], P[z])
                if dist < margin:
                    best.append((dist, idx, v, (x, y, z), bary, B))
        best.sort(key=lambda r: r[0])
        print("\n=== point (%.2f, %.2f, %.2f)" % tuple(p))
        seen = set()
        for dist, idx, v, tri, bary, B in best[:8]:
            if idx in seen:
                continue
            seen.add(idx)
            sh = shaders[v["shader_id"]-1]
            base = B["off"] + v["vbase"] * B["vsize"]
            info = ""
            texels = [(o, t, u) for (o, t, u) in B["decl"] if u == USAGE_TEXCOORD]
            if ",lmap#" in sh and len(texels) > 1:
                A = img_of(d, sh.split(",")[-1].strip())
                off = texels[1][0]
                uv = []
                for k in tri:
                    s, t = struct.unpack_from("<hh", vbdata, base + k*B["vsize"] + off)
                    uv.append((s/32768.0, t/32768.0))
                u_ = sum(b*c[0] for b, c in zip(bary, uv)); t_ = sum(b*c[1] for b, c in zip(bary, uv))
                H, W = A.shape[:2]
                x = int(np.clip(u_, 0, 1)*(W-1)); y = int(np.clip(t_, 0, 1)*(H-1))
                info = "hemi(a)=%3d sun(g)=%3d @ texel (%d,%d) of %s" % (A[y, x, 3], A[y, x, 1], x, y, sh.split(",")[-1])
            else:
                nrm = next(((o, t) for (o, t, u) in B["decl"] if u == USAGE_NORMAL), None)
                if nrm and nrm[1] == 4:
                    vals = [vbdata[base + k*B["vsize"] + nrm[0] + 3] for k in tri]
                    info = "vertex hemi = %s" % (vals,)
            print("  d=%.3f m  #%-6d %-46s %s" % (dist, idx, sh.split("/")[-1][:46], info))


if __name__ == "__main__":
    main()
