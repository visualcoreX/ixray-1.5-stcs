# -*- coding: utf-8 -*-
"""Sectors and portals of a compiled `level`, and what they point at.

Run it on a level directory (or several) to see whether a map has a real sector graph or is the
single-sector kind:

    lvl_sectors.py <level dir> [<level dir> ...]
    lvl_sectors.py --all <levels root>

Layout, from xrEngine\\xrLevel.h and Layers\\xrRender\\{r__sector.cpp, r3_loader.cpp}:

  chunk 1  fsL_HEADER  : u16 XRLC_version, u16 XRLC_quality  (quality 1 = draft, 2 = high)
  chunk 3  fsL_VISUALS : one subchunk per visual -- this is the list fsP_Root indexes into
  chunk 4  fsL_PORTALS : array of b_portal, 80 bytes each:
                         u16 sector_front, u16 sector_back, Fvector array[6], u32 count
                         (svector<Fvector,6> stores its array FIRST and the count after it)
  chunk 8  fsL_SECTORS : one subchunk per sector, holding
                         subchunk 1 fsP_Portals = u16 portal ids
                         subchunk 2 fsP_Root    = u32 visual index

The thing worth knowing before trying to move sectors between levels: fsP_Root is an INDEX INTO THIS
LEVEL'S OWN VISUAL LIST, and every sector root is a separate OGF hierarchy holding that sector's
geometry. The compiler builds that partition; sectors are not an overlay that can be grafted onto a
level compiled without them.
"""
import os
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")

OGF_HEADER = 1


def chunks(buf):
    out, off = [], 0
    while off + 8 <= len(buf):
        cid, size = struct.unpack_from("<II", buf, off)
        off += 8
        if size > len(buf) - off:
            break
        out.append((cid & 0x7FFFFFFF, buf[off:off + size]))
        off += size
    return out


def read(path):
    f = os.path.join(path, "level")
    if not os.path.isfile(f):
        return None
    top = dict(chunks(open(f, "rb").read()))
    ver, qual = struct.unpack_from("<HH", top[1], 0) if 1 in top else (0, 0)
    vis = chunks(top[3]) if 3 in top else []
    port = top.get(4, b"")
    secs = chunks(top[8]) if 8 in top else []
    info = {"version": ver, "quality": qual, "visuals": len(vis),
            "portals": len(port) // 80, "sectors": len(secs), "roots": [], "sector_portals": []}
    for _cid, payload in secs:
        sub = dict(chunks(payload))
        info["roots"].append(struct.unpack_from("<I", sub[2], 0)[0] if 2 in sub and len(sub[2]) >= 4 else None)
        info["sector_portals"].append(len(sub.get(1, b"")) // 2)
    info["portal_pairs"] = [struct.unpack_from("<HH", port, i * 80) for i in range(info["portals"])]
    info["root_types"] = {}
    for r in info["roots"]:
        if r is None or r >= len(vis):
            continue
        sub = dict(chunks(vis[r][1]))
        if OGF_HEADER in sub:
            t = struct.unpack_from("<BB", sub[OGF_HEADER], 0)[1]
            info["root_types"][t] = info["root_types"].get(t, 0) + 1
    return info


def show(path, verbose):
    i = read(path)
    if not i:
        print("%-56s (no `level` file)" % path)
        return
    print("%-56s sectors=%-4d portals=%-5d visuals=%-6d quality=%d (xrlc %d)"
          % (os.path.basename(path.rstrip("\\/")) or path,
             i["sectors"], i["portals"], i["visuals"], i["quality"], i["version"]))
    if not verbose:
        return
    print("    sector roots : %s" % i["roots"])
    print("    distinct     : %d   root OGF types: %s" % (len(set(i["roots"])), i["root_types"]))
    print("    portals/sector: %s" % i["sector_portals"])
    for n, (f, b) in enumerate(i["portal_pairs"][:16]):
        print("      portal %-4d front sector %-4d back sector %d" % (n, f, b))
    if i["portals"] > 16:
        print("      ... %d more" % (i["portals"] - 16))


args = [a for a in sys.argv[1:] if a != "--all"]
if "--all" in sys.argv[1:]:
    root = args[0]
    for name in sorted(os.listdir(root)):
        p = os.path.join(root, name)
        if os.path.isdir(p):
            show(p, False)
else:
    for p in args:
        show(p, True)
