"""Read bone definitions out of an SDK .object.
EOBJ_CHUNK_BONES2 = 0x0921 -> one sub-chunk per bone; inside each:
  0x0002 BONE_CHUNK_DEF       stringZ name, stringZ parent, stringZ wmap
  0x0003 BONE_CHUNK_BIND_POSE fvector3 rest_offset, fvector3 rest_rotate, float rest_length
(EditObjectIO.cpp:302 + bone.cpp CBone::Load_1)"""
import struct, sys

EOBJ_CHUNK_BONES2 = 0x0921
BONE_CHUNK_DEF = 0x0002
BONE_CHUNK_BIND_POSE = 0x0003


def walk(d):
    p = 0
    while p + 8 <= len(d):
        cid, sz = struct.unpack_from('<II', d, p)
        if sz > len(d) - p - 8:
            break
        yield cid & 0x7FFFFFFF, d[p+8:p+8+sz]
        p += 8 + sz


def strz(d, p):
    e = d.index(b'\0', p)
    return d[p:e].decode('latin1'), e + 1


def bones(path):
    d = open(path, 'rb').read()
    # the whole editor object sits inside one 0x7777 container
    for cid, pl in walk(d):
        if cid == 0x7777:
            d = pl
            break
    payload = None
    for cid, pl in walk(d):
        if cid == EOBJ_CHUNK_BONES2:
            payload = pl
            break
    if payload is None:
        return []
    out = []
    for _, bpl in walk(payload):
        rec = {}
        for scid, spl in walk(bpl):
            # SaveData writes BONE_CHUNK_DEF twice: the full one (name/parent/wmap) and a short
            # one carrying only the name. find_chunk takes the first, so do the same.
            if scid == BONE_CHUNK_DEF and 'name' not in rec:
                nm, p = strz(spl, 0)
                par, _ = strz(spl, p)
                rec['name'], rec['parent'] = nm.lower(), par.lower()
            elif scid == BONE_CHUNK_BIND_POSE:
                off = struct.unpack_from('<3f', spl, 0)
                rot = struct.unpack_from('<3f', spl, 12)
                rec['offset'], rec['rotate'] = off, rot
        if 'name' in rec:
            out.append(rec)
    return out


if __name__ == '__main__':
    bs = bones(sys.argv[1])
    print(f"{len(bs)} bones in {sys.argv[1].split(chr(92))[-1]}")
    want = sys.argv[2:] if len(sys.argv) > 2 else None
    for i, b in enumerate(bs):
        if want and b['name'] not in want:
            continue
        o = b.get('offset'); r = b.get('rotate')
        print(f"  {i:3} {b['name']:<16} parent={b['parent']:<14} "
              f"off=({o[0]:+.6f},{o[1]:+.6f},{o[2]:+.6f}) "
              f"rot=({r[0]:+.6f},{r[1]:+.6f},{r[2]:+.6f})")
