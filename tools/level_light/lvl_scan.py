# -*- coding: utf-8 -*-
"""Rank a level's static surfaces by the hemi they ACTUALLY sample, and say where each one is.

Two families, two storages -- measuring the wrong one gives nonsense (my first pass did):
  * shader string "default/<tex>,lmap#N_1,lmap#N_2"  -> lightmapped: hemi = ALPHA of lmap#N_2 under
    the surface's lightmap UVs (s_hemi = L_textures[2]).
  * shader string "def_shaders\\def_vertex/<tex>" and friends -> vertex-lit: hemi = ALPHA of the
    packed normal in each vertex (deffer_base_flat.vs: O.position = float4(Pe, I.Nh.w)).

Usage: lvl_scan.py <level dir> [--top N] [--tex substring] [--near X Y Z R]
"""
import os, struct, sys
import numpy as np
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lvl_hemi import chunks, read_level, read_vbs, visual_info

USAGE_NORMAL, USAGE_TEXCOORD = 3, 5
_lmap_cache = {}
_ib_cache = {}


def read_ibs(dirpath):
    if dirpath not in _ib_cache:
        geom = dict((c & 0x7FFFFFFF, p) for c, p in chunks(open(os.path.join(dirpath, "level.geom"), "rb").read()))
        ib, off = geom[10], 0
        count = struct.unpack_from("<I", ib, off)[0]; off += 4
        out = []
        for _ in range(count):
            icount = struct.unpack_from("<I", ib, off)[0]; off += 4
            out.append({"icount": icount, "off": off}); off += icount * 2
        _ib_cache[dirpath] = (ib, out)
    return _ib_cache[dirpath]


def ib_tris(ibdata, ib, payload):
    """the visual's triangles as vertex indices RELATIVE to its vbase"""
    sub = dict((c & 0x7FFFFFFF, p) for c, p in chunks(payload))
    if 21 in sub:
        _, _, _, ib_id, ibase, icount = struct.unpack_from("<IIIIII", sub[21], 0)
    elif 8 in sub:
        ib_id, ibase, icount = struct.unpack_from("<III", sub[8], 0)
    else:
        return []
    if ib_id >= len(ib):
        return []
    off = ib[ib_id]["off"] + ibase * 2
    idx = struct.unpack_from("<%dH" % icount, ibdata, off)
    return [(idx[i], idx[i+1], idx[i+2]) for i in range(0, icount - 2, 3)]


def lmap_alpha(dirpath, name):
    if name not in _lmap_cache:
        from PIL import Image
        p = os.path.join(dirpath, name + ".dds")
        _lmap_cache[name] = np.array(Image.open(p).convert("RGBA"))[:, :, 3] if os.path.exists(p) else None
    return _lmap_cache[name]


def main():
    d = sys.argv[1]
    top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 30
    tex = sys.argv[sys.argv.index("--tex") + 1].lower() if "--tex" in sys.argv else None
    near = None
    if "--near" in sys.argv:
        i = sys.argv.index("--near"); near = tuple(float(x) for x in sys.argv[i+1:i+5])

    shaders, visuals = read_level(d)
    vbdata, vb = read_vbs(d)
    ibdata, ib = read_ibs(d)
    rows = []
    for idx, (cid, payload) in enumerate(visuals):
        v = visual_info(payload)
        if not v or v["vb"] is None or v["vb"] >= len(vb):
            continue
        sh = shaders[v["shader_id"]-1] if 0 < v["shader_id"] <= len(shaders) else "?"
        if tex and tex not in sh.lower():
            continue
        bb = v["bb"]
        c = ((bb[0]+bb[3])/2, (bb[1]+bb[4])/2, (bb[2]+bb[5])/2)
        if near:
            dx, dy, dz = c[0]-near[0], c[1]-near[1], c[2]-near[2]
            if (dx*dx+dy*dy+dz*dz) ** .5 > near[3]:
                continue
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
            uv = []
            for i in range(v["vcount"]):
                s, t = struct.unpack_from("<hh", vbdata, base + i * B["vsize"] + off)
                uv.append((s / 32768.0, t / 32768.0))
            # sample TRIANGLE CENTROIDS, not vertices: a vertex UV sits on the edge of its lightmap
            # island, where the padding is black, so vertex sampling reads 0 for healthy surfaces too
            tris = ib_tris(ibdata, ib, visuals[idx][1])
            pts = ([((uv[a][0]+uv[b][0]+uv[c2][0])/3, (uv[a][1]+uv[b][1]+uv[c2][1])/3)
                    for a, b, c2 in tris if max(a, b, c2) < len(uv)] or uv)
            vals = []
            for u_, t_ in pts:
                x = int(np.clip(u_, 0, 1) * (W - 1)); y = int(np.clip(t_, 0, 1) * (H - 1))
                vals.append(int(A[y, x]))
            kind = "lmap"
        elif nrm and nrm[1] == 4:
            off = nrm[0]
            vals = [vbdata[base + i * B["vsize"] + off + 3] for i in range(v["vcount"])]
            kind = "vert"
        else:
            continue
        rows.append((sum(vals)/len(vals), max(vals), kind, idx, v, c, sh))

    rows.sort(key=lambda r: (r[0], r[1]))
    print("surfaces measured: %d   (showing %d darkest)" % (len(rows), min(top, len(rows))))
    for avg, mx, kind, idx, v, c, sh in rows[:top]:
        print("  #%-6d %-4s hemi avg=%5.1f max=%3d verts=%-5d c=(%8.1f,%7.1f,%8.1f)  %s"
              % (idx, kind, avg, mx, v["vcount"], c[0], c[1], c[2], sh))


if __name__ == "__main__":
    main()
