# -*- coding: utf-8 -*-
"""Raise the baked hemi of chosen level surfaces by painting their own lightmap islands.

For a lightmapped surface the pixel shader reads hemi as the ALPHA of lmap#N_2 under the surface's
lightmap UVs. Surfaces whose islands were baked to zero are pitch black in game. This paints alpha
inside exactly those surfaces' triangles -- rasterised from the level's own index buffer, so no
neighbour's island is touched -- and writes the DDS out for gamedata\\levels\\<level>\\, where a loose
file overrides the archive. No level recompile, and deleting the file reverts.

DXT5 is re-encoded only in the 4x4 BLOCKS the painted texels fall into; every other block is copied
from the original byte for byte, so the rest of the page keeps its exact compression.

Usage: lmap_patch.py <level dir> <out dir> <visual ids...> [--hemi V] [--dilate N] [--dry]
"""
import os, struct, sys
import numpy as np
from PIL import Image, ImageDraw
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lvl_hemi import read_level, read_vbs, visual_info
from lvl_scan import read_ibs, ib_tris, USAGE_TEXCOORD


def main():
    d, out = sys.argv[1], sys.argv[2]
    ids, hemi, dil, dry = [], 45, 1, "--dry" in sys.argv
    only_below, raw = 255, False
    a = sys.argv[3:]
    i = 0
    while i < len(a):
        if a[i] == "--hemi":   hemi = int(a[i+1]); i += 2
        elif a[i] == "--dilate": dil = int(a[i+1]); i += 2
        # paint ONLY the triangles that are actually broken, instead of the whole batch: a visual
        # spans a whole sector and most of it is fine
        elif a[i] == "--only-below": only_below = int(a[i+1]); i += 2
        # write the page UNCOMPRESSED. Re-encoding DXT5 damages the neighbours sharing a 4x4 block
        # (measured: 580 texels up to 36 darker), and these pages carry no mipmaps, so a 32-bit copy
        # is a faithful, artefact-free replacement at 4x the file size -- for one level, that is fine.
        elif a[i] == "--raw":  raw = True; i += 1
        elif a[i] == "--dry":  i += 1
        else:                  ids.append(int(a[i])); i += 1

    shaders, visuals = read_level(d)
    vbdata, vb = read_vbs(d)
    ibdata, ib = read_ibs(d)

    by_lmap = {}
    for vid in ids:
        v = visual_info(visuals[vid][1])
        sh = shaders[v["shader_id"]-1]
        by_lmap.setdefault(sh.split(",")[-1].strip(), []).append((vid, v))

    for lm, items in by_lmap.items():
        src = os.path.join(d, lm + ".dds")
        orig = bytearray(open(src, "rb").read())
        im = Image.open(src).convert("RGBA")
        W, H = im.size
        mask = Image.new("L", (W, H), 0)
        dr = ImageDraw.Draw(mask)
        tri_total = 0
        A0 = np.array(im)[:, :, 3]
        for vid, v in items:
            B = vb[v["vb"]]
            off = [o for (o, t, u) in B["decl"] if u == USAGE_TEXCOORD][1]
            base = B["off"] + v["vbase"] * B["vsize"]
            uv = []
            for k in range(v["vcount"]):
                s, t = struct.unpack_from("<hh", vbdata, base + k * B["vsize"] + off)
                uv.append(((s/32768.0) * (W-1), (t/32768.0) * (H-1)))
            for x, y, z in ib_tris(ibdata, ib, visuals[vid][1]):
                if max(x, y, z) >= len(uv):
                    continue
                if only_below < 255:
                    cx = int(np.clip((uv[x][0]+uv[y][0]+uv[z][0])/3, 0, W-1))
                    cy = int(np.clip((uv[x][1]+uv[y][1]+uv[z][1])/3, 0, H-1))
                    if A0[cy, cx] >= only_below:
                        continue
                dr.polygon([uv[x], uv[y], uv[z]], fill=255)
                tri_total += 1
        if dil:
            mask = mask.filter(__import__("PIL.ImageFilter", fromlist=["MaxFilter"]).MaxFilter(1 + 2*dil))
        M = np.array(mask) > 0
        arr = np.array(im)
        before = arr[:, :, 3][M]
        arr[:, :, 3] = np.where(M, np.maximum(arr[:, :, 3], hemi), arr[:, :, 3])
        print("%s: visuals %s, %d tris, %d texels painted (alpha was min/max/avg %d/%d/%.1f -> %d)"
              % (lm, [v[0] for v in items], tri_total, M.sum(),
                 before.min() if before.size else -1, before.max() if before.size else -1,
                 before.mean() if before.size else -1, hemi))
        if dry:
            continue

        os.makedirs(out, exist_ok=True)
        if raw:
            dst = os.path.join(out, lm + ".dds")
            Image.fromarray(arr).save(dst)          # uncompressed: nothing but the mask changes
            chk = np.array(Image.open(dst).convert("RGBA"))
            assert np.array_equal(chk[~M], np.array(im)[~M]), "unmasked texels changed!"
            print("   wrote %s  (uncompressed, %d bytes; unmasked texels byte-identical)"
                  % (dst, os.path.getsize(dst)))
            continue

        tmp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_patched.dds")
        Image.fromarray(arr).save(tmp, pixel_format="DXT5")
        new = open(tmp, "rb").read()
        assert len(new) == len(orig), (len(new), len(orig))
        ys, xs = np.nonzero(M)
        bw = W // 4
        blocks = set()
        for y, x in zip(ys, xs):
            blocks.add((y // 4) * bw + (x // 4))
        for b in blocks:
            o = 128 + b * 16
            orig[o:o+16] = new[o:o+16]
        os.makedirs(out, exist_ok=True)
        dst = os.path.join(out, lm + ".dds")
        open(dst, "wb").write(orig)
        os.unlink(tmp)
        print("   wrote %s  (%d of %d DXT blocks rewritten)" % (dst, len(blocks), (W//4)*(H//4)))


if __name__ == "__main__":
    main()
