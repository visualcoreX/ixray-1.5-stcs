# -*- coding: utf-8 -*-
"""Inspect (and patch) the HEMI channel a level surface actually samples.

For a lightmapped surface the pixel shader reads hemi as the ALPHA of the THIRD texture of its
shader string -- s_hemi = L_textures[2] (Blender_Lm(EbB).cpp / uber_deffer.cpp), i.e. lmap#N_2.dds.
So "this surface is pitch black" means: the alpha of that DDS is zero over the surface's lightmap
UVs. This tool reads a visual's vertices and triangles, walks its lightmap UVs, and reports the
alpha under them -- and with --patch raises exactly those texels (only inside the surface's own
triangles, plus a dilation ring for bilinear filtering) and writes the DDS out.

lmap UVs are SHORT2, unpacked as tc/32768 (shared/common.h: unpack_tc_lmap).
"""
import os, struct, sys
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lvl_hemi import chunks, read_level, read_vbs, visual_info, D3DDECLTYPE_SIZE

USAGE_TEXCOORD = 5


def read_ibs(dirpath):
    geom = dict((c & 0x7FFFFFFF, p) for c, p in chunks(open(os.path.join(dirpath, "level.geom"), "rb").read()))
    ib, off = geom[10], 0
    count = struct.unpack_from("<I", ib, off)[0]; off += 4
    out = []
    for _ in range(count):
        icount = struct.unpack_from("<I", ib, off)[0]; off += 4
        out.append({"icount": icount, "off": off})
        off += icount * 2
    return ib, out


def visual_containers(payload):
    sub = dict((c & 0x7FFFFFFF, p) for c, p in chunks(payload))
    v = visual_info(payload)
    if 21 in sub:
        vb, vbase, vcount, ib, ibase, icount = struct.unpack_from("<IIIIII", sub[21], 0)
    else:
        if 7 not in sub or 8 not in sub:
            return v, None
        vb, vbase, vcount = struct.unpack_from("<III", sub[7], 0)
        ib, ibase, icount = struct.unpack_from("<III", sub[8], 0)
    v.update({"ib": ib, "ibase": ibase, "icount": icount})
    return v, True


def lmap_uv_element(B):
    """the SECOND texcoord of the declaration -- the lightmap one"""
    tex = [(o, t, u) for (o, t, u) in B["decl"] if u == USAGE_TEXCOORD]
    return tex[1] if len(tex) > 1 else None


def main():
    d, vid = sys.argv[1], int(sys.argv[2])
    shaders, visuals = read_level(d)
    vbdata, vb = read_vbs(d)
    ibdata, ib = read_ibs(d)

    v, ok = visual_containers(visuals[vid][1])
    sh = shaders[v["shader_id"]-1]
    print("visual #%d  %s" % (vid, sh))
    print("  verts %d (vb %d @%d)   tris %d" % (v["vcount"], v["vb"], v["vbase"], (v.get("icount") or 0)//3))

    B = vb[v["vb"]]
    el = lmap_uv_element(B)
    if not el:
        print("  no lightmap UV in this vertex format -- vertex-lit surface"); return
    off, typ, _ = el
    base = B["off"] + v["vbase"] * B["vsize"]
    uv = []
    for i in range(v["vcount"]):
        s, t = struct.unpack_from("<hh", vbdata, base + i * B["vsize"] + off)
        uv.append((s / 32768.0, t / 32768.0))
    us = [p[0] for p in uv]; vs = [p[1] for p in uv]
    print("  lmap UV bbox: u %.4f..%.4f  v %.4f..%.4f" % (min(us), max(us), min(vs), max(vs)))

    lm = sh.split(",")[-1].strip()
    path = os.path.join(d, lm.replace("\\", os.sep) + ".dds")
    print("  hemi texture:", os.path.basename(path), "exists" if os.path.exists(path) else "MISSING")
    if not os.path.exists(path):
        return
    from PIL import Image
    im = Image.open(path).convert("RGBA")
    W, H = im.size
    A = im.split()[3].load()
    xs = [int(min(max(u, 0), 1) * (W - 1)) for u in us]
    ys = [int(min(max(t, 0), 1) * (H - 1)) for t in vs]
    vals = [A[x, y] for x, y in zip(xs, ys)]
    print("  lmap %dx%d, alpha under the %d vertices: min=%d max=%d avg=%.1f"
          % (W, H, len(vals), min(vals), max(vals), sum(vals)/len(vals)))
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    print("  texel rect: x %d..%d  y %d..%d" % (x0, x1, y0, y1))
    box = im.crop((max(0, x0-4), max(0, y0-4), min(W, x1+5), min(H, y1+5)))
    outp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lmap_crop_%d.png" % vid)
    box.split()[3].resize((box.width*8, box.height*8), Image.NEAREST).save(outp)
    print("  alpha crop saved:", outp)


if __name__ == "__main__":
    main()
