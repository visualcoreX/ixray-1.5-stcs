"""OMF reader/writer. Layout taken from motions_value::load (src/xrEngine/SkeletonMotions.cpp:75),
not guessed. Round-trips byte-for-byte -- that self-test must pass before anything is edited."""
import struct, zlib

OGF_S_MOTIONS  = 14
OGF_S_SMPARAMS = 15

flTKeyPresent = 0x01
flRKeyAbsent  = 0x02
flTKey16IsBit = 0x04


class R:
    def __init__(self, d): self.d, self.p = d, 0
    def u8(self):  v = self.d[self.p]; self.p += 1; return v
    def u16(self): v = struct.unpack_from('<H', self.d, self.p)[0]; self.p += 2; return v
    def u32(self): v = struct.unpack_from('<I', self.d, self.p)[0]; self.p += 4; return v
    def f3(self):  v = struct.unpack_from('<3f', self.d, self.p); self.p += 12; return v
    def raw(self, n): v = self.d[self.p:self.p+n]; self.p += n; return v
    def strz(self):
        e = self.d.index(b'\0', self.p)
        v = self.d[self.p:e]; self.p = e + 1; return v
    def eof(self): return self.p >= len(self.d)


def chunks(d):
    """-> {id: bytes} in file order (top level)"""
    out, p = {}, 0
    while p + 8 <= len(d):
        cid, sz = struct.unpack_from('<II', d, p)
        out[cid & 0x7FFFFFFF] = d[p+8:p+8+sz]
        p += 8 + sz
    return out


def pack_chunk(cid, payload):
    return struct.pack('<II', cid, len(payload)) + payload


class OMF:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        self.top = []                       # [(id, payload)] in order
        p = 0
        while p + 8 <= len(self.data):
            cid, sz = struct.unpack_from('<II', self.data, p)
            self.top.append((cid, self.data[p+8:p+8+sz]))
            p += 8 + sz
        self._parse_params()
        self._parse_motions()

    # ---- SMPARAMS: partitions + motion defs -------------------------------------------------
    def _parse_params(self):
        payload = dict((c & 0x7FFFFFFF, pl) for c, pl in self.top)[OGF_S_SMPARAMS]
        r = R(payload)
        self.vers = r.u16()
        part_count = r.u16()
        self.parts = []                     # [(name, [(bone_name, m_idx)])]
        for _ in range(part_count):
            nm = r.strz()
            n = r.u16()
            bones = [(r.strz(), r.u32()) for _ in range(n)]
            self.parts.append((nm, bones))
        self.bone_count = sum(len(b) for _, b in self.parts)
        self.mdefs_raw_off = r.p            # motion defs start here; kept verbatim
        self.mot_count_in_params = struct.unpack_from('<H', payload, r.p)[0]
        self.params_tail = payload[r.p:]

    def build_params(self):
        out = struct.pack('<HH', self.vers, len(self.parts))
        for nm, bones in self.parts:
            out += nm + b'\0' + struct.pack('<H', len(bones))
            for bn, idx in bones:
                out += bn + b'\0' + struct.pack('<I', idx)
        return out + self.params_tail

    # ---- MOTIONS ----------------------------------------------------------------------------
    def _parse_motions(self):
        payload = dict((c & 0x7FFFFFFF, pl) for c, pl in self.top)[OGF_S_MOTIONS]
        sub, p = [], 0
        while p + 8 <= len(payload):
            cid, sz = struct.unpack_from('<II', payload, p)
            sub.append((cid, payload[p+8:p+8+sz]))
            p += 8 + sz
        self.motion_count = struct.unpack('<I', sub[0][1])[0]
        self.motions = []                   # [{'name','dwLen','tracks':[dict]}]
        for cid, pl in sub[1:]:
            r = R(pl)
            name = r.strz()
            dwLen = r.u32()
            tracks = []
            for _ in range(self.bone_count):
                fl = r.u8()
                t = {'flags': fl}
                if fl & flRKeyAbsent:
                    t['R'] = r.raw(8); t['Rcrc'] = None
                else:
                    t['Rcrc'] = r.u32(); t['R'] = r.raw(8 * dwLen)
                if fl & flTKeyPresent:
                    t['Tcrc'] = r.u32()
                    w = 6 if (fl & flTKey16IsBit) else 3
                    t['T'] = r.raw(w * dwLen)
                    t['sizeT'] = r.raw(12)
                else:
                    t['Tcrc'] = None; t['T'] = b''; t['sizeT'] = None
                t['initT'] = r.raw(12)
                tracks.append(t)
            assert r.eof(), f"{name}: {len(pl)-r.p} bytes left over"
            self.motions.append({'name': name, 'dwLen': dwLen, 'tracks': tracks, 'cid': cid})

    def build_motions(self):
        out = pack_chunk(0, struct.pack('<I', self.motion_count))
        for m in self.motions:
            b = m['name'] + b'\0' + struct.pack('<I', m['dwLen'])
            for t in m['tracks']:
                b += struct.pack('<B', t['flags'])
                if t['flags'] & flRKeyAbsent:
                    b += t['R']
                else:
                    b += struct.pack('<I', t['Rcrc']) + t['R']
                if t['flags'] & flTKeyPresent:
                    b += struct.pack('<I', t['Tcrc']) + t['T'] + t['sizeT']
                b += t['initT']
            out += pack_chunk(m['cid'], b)
        return out

    def build(self):
        out = b''
        for cid, pl in self.top:
            base = cid & 0x7FFFFFFF
            if base == OGF_S_SMPARAMS:  pl = self.build_params()
            elif base == OGF_S_MOTIONS: pl = self.build_motions()
            out += struct.pack('<II', cid, len(pl)) + pl
        return out

    def bone_names(self):
        """index -> name, ordered by the m_idx the engine remaps through"""
        m = {}
        for _, bones in self.parts:
            for bn, idx in bones:
                m[idx] = bn.decode('latin1')
        return [m[i] for i in range(self.bone_count)]


if __name__ == '__main__':
    import sys
    o = OMF(sys.argv[1])
    print(f"chunks     : {[hex(c) for c, _ in o.top]}")
    print(f"smparams v : {o.vers}   partitions: {len(o.parts)}")
    print(f"bones      : {o.bone_count}")
    print(f"motions    : {o.motion_count} (subchunks parsed: {len(o.motions)})")
    ok = o.build() == o.data
    print(f"round-trip : {'byte-identical' if ok else 'MISMATCH'}")
    for nm, bones in o.parts:
        print(f"  partition {nm.decode('latin1')!r}: {len(bones)} bones")
    print("bone order (by m_idx):")
    for i, n in enumerate(o.bone_names()):
        print(f"   {i:3} {n}")
