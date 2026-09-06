# -*- coding: utf-8 -*-
"""Which surfaces cover a given world point, and what hemi does each of them actually sample?

The address of a level surface is its position: compiled levels keep no object names. Feed it the
point `look_at` printed and it lists every visual whose bbox contains it (plus a margin), so a black
patch under the crosshair can be turned into a visual index to patch.
"""
import sys
sys.stdout.reconfigure(encoding="utf-8")
from lvl_dark import measure

d = sys.argv[1]
pts = []
args = sys.argv[2:]
margin = 0.5
i = 0
while i < len(args):
    if args[i] == "--margin":
        margin = float(args[i+1]); i += 2
    else:
        pts.append(tuple(float(x) for x in args[i:i+3])); i += 3

rows = measure(d)
for p in pts:
    print("\n=== point (%.2f, %.2f, %.2f)" % p)
    hits = []
    for r in rows:
        bb = r["bb"]
        if all(bb[k] - margin <= p[k] <= bb[k+3] + margin for k in range(3)):
            hits.append(r)
    hits.sort(key=lambda r: r["avg"])
    for r in hits[:14]:
        bb = r["bb"]
        print("  #%-6d %-4s hemi avg=%6.1f max=%3d verts=%-5d bbox=(%.1f,%.1f,%.1f)..(%.1f,%.1f,%.1f)  %s"
              % (r["idx"], r["kind"], r["avg"], r["max"], r["verts"],
                 bb[0], bb[1], bb[2], bb[3], bb[4], bb[5], r["sh"]))
    if not hits:
        print("  nothing (try a bigger --margin)")
