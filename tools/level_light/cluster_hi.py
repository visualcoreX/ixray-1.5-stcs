# -*- coding: utf-8 -*-
"""Where do the near-black surfaces bunch up? Grouped by 30 m cell, biggest first."""
import sys
sys.stdout.reconfigure(encoding="utf-8")
from lvl_dark import measure
M = sys.argv[1]
rows = [r for r in measure(M) if r["avg"] <= 3 and not r["sh"].startswith("effects")]
cl = {}
for r in rows:
    cl.setdefault((int(r["c"][0]//30), int(r["c"][2]//30)), []).append(r)
print("near-black surfaces:", len(rows))
for k, items in sorted(cl.items(), key=lambda kv: -len(kv[1]))[:14]:
    ys = [i["c"][1] for i in items]; xs = [i["c"][0] for i in items]; zs = [i["c"][2] for i in items]
    tex = sorted({i["sh"].split("/")[-1].split(",")[0] for i in items})
    print("cluster (%7.0f, y %5.1f..%5.1f, %7.0f)  %2d surfaces  %s"
          % (sum(xs)/len(xs), min(ys), max(ys), sum(zs)/len(zs), len(items), ", ".join(tex[:5])))
