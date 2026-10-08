"""Where in its loop each foot of a leg animation comes down -- for the actor's footstep table.

Reads the skeleton (bone names + parents) from an .ogf and the motions from an .omf, poses the leg chain
of every motion matching a pattern frame by frame exactly as the engine does (CKeyQR -> quaternion,
mk_xform rows, child = local * parent), and reports, per foot, the share of the loop at which the foot
bone lands: the first frame, after the foot has been up, at which its height (model space) gets within
`--tol` metres of the lowest it reaches in that loop.

    python tools/foot_contacts.py <actor.ogf> <animations.omf> <regex> [--ltx <section>] [--tol 0.015]

--ltx prints the result as [actor_step_manager] lines (cycles, left time, power, right time, power), with
the powers taken from the stalker table where the motion has one (--powers <m_stalker.ltx>).
"""
import argparse, math, re, struct


def chunks(d, p=0, end=None):
    end = len(d) if end is None else end
    out = {}
    while p + 8 <= end:
        cid, sz = struct.unpack_from('<II', d, p)
        out[cid & 0x7FFFFFFF] = (p + 8, sz)
        p += 8 + sz
    return out


def zstr(d, p):
    e = d.index(b'\0', p)
    return d[p:e].decode('latin1').lower(), e + 1


def ogf_parents(path):
    d = open(path, 'rb').read()
    c = chunks(d)
    p, sz = c[13]                                   # OGF_S_BONE_NAMES
    n, = struct.unpack_from('<I', d, p); p += 4
    parents = {}
    for _ in range(n):
        name, p = zstr(d, p)
        par, p = zstr(d, p)
        p += 60                                     # Fobb
        parents[name] = par
    return parents


def omf_motions(path, want):
    d = open(path, 'rb').read()
    c = chunks(d)
    # track index -> bone name (partitions)
    p, sz = c[15]
    vers, = struct.unpack_from('<H', d, p); p += 2
    parts, = struct.unpack_from('<H', d, p); p += 2
    track_bone = {}
    for _ in range(parts):
        _, p = zstr(d, p)
        nb, = struct.unpack_from('<H', d, p); p += 2
        for _ in range(nb):
            bn, p = zstr(d, p)
            idx, = struct.unpack_from('<I', d, p); p += 4
            track_bone[idx] = bn
    nb_total = len(track_bone)
    # motions
    p0, sz = c[14]
    q = p0
    subs = []
    while q + 8 <= p0 + sz:
        cid, s2 = struct.unpack_from('<II', d, q)
        subs.append((q + 8, s2))
        q += 8 + s2
    out = {}
    for (sp, s2) in subs[1:]:
        name, r = zstr(d, sp)
        if not want(name):
            continue
        n, = struct.unpack_from('<I', d, r); r += 4
        tracks = {}
        for t in range(nb_total):
            fl = d[r]; r += 1
            if fl & 2:                              # flRKeyAbsent: one key
                R = [struct.unpack_from('<4h', d, r)] * n; r += 8
            else:
                r += 4
                R = [struct.unpack_from('<4h', d, r + 8 * i) for i in range(n)]; r += 8 * n
            T = None
            if fl & 1:                              # flTKeyPresent
                r += 4
                w, fmt = (6, '<3h') if fl & 4 else (3, '<3b')
                keys = [struct.unpack_from(fmt, d, r + w * i) for i in range(n)]; r += w * n
                size = struct.unpack_from('<3f', d, r); r += 12
                init = struct.unpack_from('<3f', d, r); r += 12
                T = [tuple(k * s + i0 for k, s, i0 in zip(kk, size, init)) for kk in keys]
            else:
                init = struct.unpack_from('<3f', d, r); r += 12
                T = [init] * n
            tracks[track_bone[t]] = ([tuple(v / 32767.0 for v in rr) for rr in R], T)
        out[name] = (n, tracks)
    return out


def mk_xform(q, t):
    x, y, z, w = q
    xx, yy, zz, xy, xz, yz, wx, wy, wz = x*x, y*y, z*z, x*y, x*z, y*z, w*x, w*y, w*z
    return [[1 - 2*(yy + zz), 2*(xy - wz), 2*(xz + wy)],
            [2*(xy + wz), 1 - 2*(xx + zz), 2*(yz - wx)],
            [2*(xz - wy), 2*(yz + wx), 1 - 2*(xx + yy)],
            list(t)]


def mul(a, b):
    """row-vector composition a then b (engine mul_43(B, A) = A * B): rotation a*b, origin a.c*b + b.c"""
    rot = [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    c = [sum(a[3][k] * b[k][j] for k in range(3)) + b[3][j] for j in range(3)]
    return rot + [c]


def foot_heights(n, tracks, parents, foot):
    chain = []
    b = foot
    while b:
        chain.append(b)
        b = parents.get(b, '')
    chain.reverse()                                  # root first
    hs = []
    for f in range(n):
        M = None
        for bn in chain:
            if bn not in tracks:
                continue
            R, T = tracks[bn]
            L = mk_xform(R[f], T[f])
            M = L if M is None else mul(L, M)
        hs.append(M[3][1])
    return hs


def contact(hs, tol):
    n = len(hs)
    lo = min(hs)
    up = [h > lo + max(tol * 3, 0.04) for h in hs]
    # the landing: a frame within tol of the lowest whose predecessor (cyclically) was up, else still coming down
    for start in range(n):
        if up[start]:
            break
    else:
        return None
    for k in range(1, n + 1):
        i = (start + k) % n
        if hs[i] <= lo + tol:
            return i / float(n)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ogf'); ap.add_argument('omf'); ap.add_argument('pattern')
    ap.add_argument('--tol', type=float, default=0.015)
    ap.add_argument('--ltx', default=None)
    ap.add_argument('--powers', default=None)
    a = ap.parse_args()
    parents = ogf_parents(a.ogf)
    rx = re.compile(a.pattern)
    motions = omf_motions(a.omf, lambda s: bool(rx.search(s)))
    powers = {}
    if a.powers:
        for line in open(a.powers, 'rb').read().decode('latin1').splitlines():
            m = re.match(r'\s*([a-z0-9_]+)\s*=\s*(\d+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)', line)
            if m:
                powers[m.group(1)] = (float(m.group(3 + 1)), float(m.group(6)))
    lines = []
    for name in sorted(motions):
        n, tracks = motions[name]
        hl = foot_heights(n, tracks, parents, 'bip01_l_foot')
        hr = foot_heights(n, tracks, parents, 'bip01_r_foot')
        cl, cr = contact(hl, a.tol), contact(hr, a.tol)
        print('%-26s %4d frames  left %s  right %s   (left %.3f..%.3f m, right %.3f..%.3f m)' % (
            name, n, '%.3f' % cl if cl is not None else '  -  ', '%.3f' % cr if cr is not None else '  -  ',
            min(hl), max(hl), min(hr), max(hr)))
        if cl is not None and cr is not None:
            pl, pr = powers.get(name, (0.25, 0.25))
            lines.append('%-24s= 1,    %.3f,  %.2f,   %.3f,  %.2f' % (name, cl, pl, cr, pr))
    if a.ltx:
        print('\n[%s]' % a.ltx)
        print('\n'.join(lines))


if __name__ == '__main__':
    main()
