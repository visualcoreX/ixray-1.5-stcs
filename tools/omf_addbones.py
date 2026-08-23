"""Append bones to an .omf, holding them at the model's bind pose in every motion.

A track that is constant is written the way the format already does it elsewhere:
  flags = flRKeyAbsent, no flTKeyPresent  ->  one CKeyQR (8 b, no crc) + initT (12 b)
so the bone's local transform is exactly mk_xform(bind_quat, rest_offset) for the whole motion.
Bind values come from the SDK .object (rest_rotate/rest_offset), converted with the engine's own
setXYZi + quaternion::set -- verified against the 23 existing bones that already sit at bind pose.
"""
import os, struct, sys, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from omf import OMF, flRKeyAbsent, flTKeyPresent
from objbones import bones as obj_bones
from xrmath import set_xyzi, quat_of, quant_qr, dequant_qr, qangle

W = r"I:\SteamLibrary\steamapps\common\STALKER Clear Sky\gamedata\meshes\dynamics\weapons\wpn_pkm"
OMF_PATH = os.path.join(W, "wpn_pkm_hud_animation.omf")
OBJECTS = [os.path.join(W, "wpn_pkm_zulus_hud.object"),
           os.path.join(W, "wpn_pkm_hud.object")]
NEW = ['scope1', 'metka1', 'scope2', 'lense2', 'metka2', 'scope3']
BACKUP = OMF_PATH + ".bak_addscopebones"
APPLY = '--apply' in sys.argv

# ---- bind data, cross-checked between the two objects that share this omf --------------------
srcs = {}
for p in OBJECTS:
    srcs[os.path.basename(p)] = {b['name']: b for b in obj_bones(p)}
ref = srcs[os.path.basename(OBJECTS[0])]
for nm, tbl in list(srcs.items())[1:]:
    for b in NEW:
        da = qangle(quat_of(set_xyzi(*ref[b]['rotate'])), quat_of(set_xyzi(*tbl[b]['rotate'])))
        dt = max(abs(x-y) for x, y in zip(ref[b]['offset'], tbl[b]['offset'])) * 1000
        if da > 0.01 or dt > 0.01:
            print(f"!! {b}: objects disagree ({da:.4f} deg, {dt:.4f} mm) -- using "
                  f"{os.path.basename(OBJECTS[0])}")

o = OMF(OMF_PATH)
assert o.build() == o.data, "self-test failed: rebuild is not byte-identical"
print(f"self-test  : rebuild byte-identical  ({o.bone_count} bones, {o.motion_count} motions)")

existing = o.bone_names()
for b in NEW:
    assert b not in existing, f"{b} already in the omf"
    assert b in ref, f"{b} not found in the object"

# ---- build the constant tracks ---------------------------------------------------------------
tracks = []
for b in NEW:
    q = quat_of(set_xyzi(*ref[b]['rotate']))
    k = quant_qr(q)
    tracks.append({
        'flags': flRKeyAbsent,
        'R': struct.pack('<4h', *k), 'Rcrc': None,
        'Tcrc': None, 'T': b'', 'sizeT': None,
        'initT': struct.pack('<3f', *ref[b]['offset']),
    })
    print(f"  {b:<10} parent={ref[b]['parent']:<12} "
          f"q=({q[0]:+.6f},{q[1]:+.6f},{q[2]:+.6f},{q[3]:+.6f}) "
          f"requant err {qangle(q, dequant_qr(k)):.5f} deg  "
          f"T=({ref[b]['offset'][0]:+.5f},{ref[b]['offset'][1]:+.5f},{ref[b]['offset'][2]:+.5f})")

# ---- splice ------------------------------------------------------------------------------------
part_name, part_bones = o.parts[0]
next_idx = o.bone_count
for i, b in enumerate(NEW):
    part_bones.append((b.encode('latin1'), next_idx + i))
o.bone_count += len(NEW)
for m in o.motions:
    m['tracks'].extend(dict(t) for t in tracks)

out = o.build()
print(f"size       : {len(o.data)} -> {len(out)} (+{len(out)-len(o.data)})")

if not APPLY:
    print("dry run -- pass --apply to write")
    sys.exit(0)

if not os.path.exists(BACKUP):
    shutil.copy2(OMF_PATH, BACKUP)
    print(f"backup     : {os.path.basename(BACKUP)}")
open(OMF_PATH, 'wb').write(out)

# ---- verify by re-reading --------------------------------------------------------------------
o2 = OMF(OMF_PATH)
assert o2.build() == o2.data, "written file does not round-trip"
print(f"reparse    : {o2.bone_count} bones, {o2.motion_count} motions, every motion ends on its "
      f"subchunk boundary")
orig = OMF(BACKUP)
same = all(o2.motions[mi]['tracks'][bi] == orig.motions[mi]['tracks'][bi]
           for mi in range(len(orig.motions)) for bi in range(orig.bone_count))
print(f"old tracks : {'unchanged' if same else 'CHANGED -- BAD'}")
worst = 0.0
for mi in range(len(o2.motions)):
    for j, b in enumerate(NEW):
        t = o2.motions[mi]['tracks'][orig.bone_count + j]
        q = dequant_qr(struct.unpack('<4h', t['R']))
        worst = max(worst, qangle(q, quat_of(set_xyzi(*ref[b]['rotate']))))
        assert struct.unpack('<3f', t['initT']) == tuple(struct.unpack('<3f', struct.pack('<3f', *ref[b]['offset'])))
print(f"new tracks : bind pose reproduced in all {len(o2.motions)} motions, worst error {worst:.5f} deg")
print(f"bone order : {o2.bone_names()[orig.bone_count:]}")
