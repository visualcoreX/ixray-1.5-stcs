# -*- coding: utf-8 -*-
"""Find (and optionally fix) static level surfaces whose baked hemi occlusion is zero.

Those surfaces get no ambient at all -- hmodel() multiplies the whole ambient term by this one value
-- so they render pitch black and only a flashlight shows the texture.

Where the number lives:
  * level        chunk 2  = shader/texture strings, indexed by ogf_header.shader_id
                 chunk 3  = visuals, one OGF per sub-chunk: header (shader_id + bbox) and a
                            V/G container pointing at a slice of a vertex buffer
  * level.geom   chunk 9  = the vertex buffers, each preceded by its D3D vertex declaration
The hemi byte is the ALPHA of the packed NORMAL (deffer_base_flat.vs: O.position = float4(Pe, I.Nh.w)).

Usage:
  lvl_hemi.py <level dir> [--dump N] [--near X Y Z R] [--fix <visual ids> --out <dir> --value V]
"""
import os, struct, sys
sys.stdout.reconfigure(encoding="utf-8")

D3DDECLTYPE_SIZE = {0:12, 1:8, 2:12, 3:16, 4:4, 5:4, 6:4, 7:8, 8:4, 9:4, 10:8, 11:4, 12:4,
                    13:4, 14:8, 15:4, 16:8, 17:0}
USAGE_NORMAL, USAGE_POSITION = 3, 0


def chunks(buf):
    out, off = [], 0
    while off + 8 <= len(buf):
        cid, size = struct.unpack_from("<II", buf, off)
        off += 8
        if size > len(buf) - off:
            break
        out.append((cid, buf[off:off+size]))
        off += size
    return out


def read_level(dirpath):
    lvl = dict((c & 0x7FFFFFFF, p) for c, p in chunks(open(os.path.join(dirpath, "level"), "rb").read()))
    shaders = [s.decode("latin1") for s in lvl[2].split(b"\0") if s]
    visuals = flatten(chunks(lvl[3]))
    return shaders, visuals


def flatten(visuals, depth=0):
    """Visuals, INCLUDING the children of hierarchy ones (MT_HIERRARHY = type 1).

    A hierarchy visual carries no geometry itself: its parts sit in OGF_CHILDREN (chunk 9) as
    complete nested OGFs, each with its own shader_id and vertex container. Ignoring them loses whole
    props -- 4407 of the Swamps' 23531 visuals are of that kind, and the concrete ring that this hunt
    could not find is inside one.
    """
    out = []
    for cid, payload in visuals:
        out.append((cid, payload))
        sub = dict((c & 0x7FFFFFFF, p) for c, p in chunks(payload))
        if 9 in sub and depth < 4:
            out.extend(flatten(chunks(sub[9]), depth + 1))
    return out


def read_vbs(dirpath):
    geom = dict((c & 0x7FFFFFFF, p) for c, p in chunks(open(os.path.join(dirpath, "level.geom"), "rb").read()))
    vb, off = geom[9], 0
    count = struct.unpack_from("<I", vb, off)[0]; off += 4
    out = []
    for _ in range(count):
        decl, vsize, nrm_off = [], 0, None
        while True:
            stream, offset, typ, method, usage, uidx = struct.unpack_from("<HHBBBB", vb, off)
            off += 8
            if stream == 0xFF:
                break
            decl.append((offset, typ, usage))
            if usage == USAGE_NORMAL and nrm_off is None:
                nrm_off = (offset, typ)
            vsize = max(vsize, offset + D3DDECLTYPE_SIZE.get(typ, 0))
        vcount = struct.unpack_from("<I", vb, off)[0]; off += 4
        data_off = off
        off += vcount * vsize
        out.append({"vcount": vcount, "vsize": vsize, "nrm": nrm_off, "off": data_off, "decl": decl})
    return vb, out


def visual_info(payload):
    sub = dict((c & 0x7FFFFFFF, p) for c, p in chunks(payload))
    if 1 not in sub:
        return None
    fmt, typ, shader_id = struct.unpack_from("<BBH", sub[1], 0)
    bb = struct.unpack_from("<6f", sub[1], 4)
    vb_id = vbase = vcount = None
    if 7 in sub:                       # OGF_VCONTAINER
        vb_id, vbase, vcount = struct.unpack_from("<III", sub[7], 0)
    elif 21 in sub:                    # OGF_GCONTAINER: vb_id, vbase, vcount, ib_id, ibase, icount
        vb_id, vbase, vcount = struct.unpack_from("<III", sub[21], 0)
    return {"type": typ, "shader_id": shader_id, "bb": bb,
            "vb": vb_id, "vbase": vbase, "vcount": vcount}


def hemi_stats(vbdata, vb, v):
    if v["vb"] is None or v["vb"] >= len(vb):
        return None
    B = vb[v["vb"]]
    if B["nrm"] is None:
        return None
    n_off, n_typ = B["nrm"]
    if n_typ != 4:                     # D3DDECLTYPE_D3DCOLOR
        return None
    base = B["off"] + v["vbase"] * B["vsize"]
    lo, hi, tot = 255, 0, 0
    for i in range(v["vcount"]):
        a = vbdata[base + i * B["vsize"] + n_off + 3]
        lo = min(lo, a); hi = max(hi, a); tot += a
    return lo, hi, tot / max(1, v["vcount"])


def main():
    d = sys.argv[1]
    shaders, visuals = read_level(d)
    vbdata, vb = read_vbs(d)
    print("shaders: %d   visuals: %d   vertex buffers: %d" % (len(shaders), len(visuals), len(vb)))

    near = None
    if "--near" in sys.argv:
        i = sys.argv.index("--near")
        near = tuple(float(x) for x in sys.argv[i+1:i+5])

    rows = []
    for idx, (cid, payload) in enumerate(visuals):
        v = visual_info(payload)
        if not v:
            continue
        st = hemi_stats(vbdata, vb, v)
        if st is None:
            continue
        bb = v["bb"]
        c = ((bb[0]+bb[3])/2, (bb[1]+bb[4])/2, (bb[2]+bb[5])/2)
        if near:
            dx, dy, dz = c[0]-near[0], c[1]-near[1], c[2]-near[2]
            if (dx*dx + dy*dy + dz*dz) ** .5 > near[3]:
                continue
        rows.append((idx, v, st, c))

    dark = [r for r in rows if r[2][1] == 0]
    print("visuals with a readable hemi: %d, of them ALL-ZERO: %d" % (len(rows), len(dark)))
    show = dark if not near else rows
    show.sort(key=lambda r: r[2][2])
    for idx, v, st, c in show[:int(sys.argv[sys.argv.index("--dump")+1]) if "--dump" in sys.argv else 40]:
        sh = shaders[v["shader_id"]-1] if 0 < v["shader_id"] <= len(shaders) else "?"
        print("  #%-5d verts=%-6d hemi min/max/avg=%3d/%3d/%6.1f  c=(%8.1f,%7.1f,%8.1f)  %s"
              % (idx, v["vcount"], st[0], st[1], st[2], c[0], c[1], c[2], sh))


if __name__ == "__main__":
    main()
