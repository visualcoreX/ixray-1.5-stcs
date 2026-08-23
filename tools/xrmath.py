"""X-Ray transform maths, transcribed from the engine (not re-derived).
  setHPB / setXYZi : xrCore/_matrix.h:560,577
  mk_xform         : xrCore/vector.h:258
  quaternion::set  : xrCore/vector.h:273
Matrices are row-major with rows i,j,k and translation c, so m[r][col] == _{r+1}{col+1}."""
import math


def set_hpb(h, p, b):
    sh, ch = math.sin(h), math.cos(h)
    sp, cp = math.sin(p), math.cos(p)
    sb, cb = math.sin(b), math.cos(b)
    cc, cs, sc, ss = ch*cb, ch*sb, sh*cb, sh*sb
    return [
        [cc - sp*ss,  -cp*sb,  sp*cs + sc],
        [sp*sc + cs,   cp*cb,  ss - sp*cc],
        [-cp*sh,       sp,     cp*ch     ],
    ]


def set_xyzi(x, y, z):
    """CBoneData: bind_transform.setXYZi(vXYZ) == setHPB(-y, -x, -z)"""
    return set_hpb(-y, -x, -z)


def quat_of(m):
    """_quaternion::set(matrix) -> (x, y, z, w)"""
    _11, _12, _13 = m[0]
    _21, _22, _23 = m[1]
    _31, _32, _33 = m[2]
    trace = _11 + _22 + _33
    if trace > 0.0:
        s = math.sqrt(trace + 1.0)
        w = s * 0.5
        s = 0.5 / s
        return ((_32 - _23) * s, (_13 - _31) * s, (_21 - _12) * s, w)
    # largest-diagonal branch
    if _11 > _22:
        biggest = 'I' if _33 > _11 else 'A'
    else:
        biggest = 'I' if _33 > _11 else 'E'
    if biggest == 'A':
        s = math.sqrt(_11 - (_22 + _33) + 1.0)
        x = s * 0.5; s = 0.5 / s
        return (x, (_12 + _21) * s, (_13 + _31) * s, (_32 - _23) * s)
    if biggest == 'E':
        s = math.sqrt(_22 - (_33 + _11) + 1.0)
        y = s * 0.5; s = 0.5 / s
        return ((_12 + _21) * s, y, (_23 + _32) * s, (_13 - _31) * s)
    s = math.sqrt(_33 - (_11 + _22) + 1.0)
    z = s * 0.5; s = 0.5 / s
    return ((_13 + _31) * s, (_23 + _32) * s, z, (_21 - _12) * s)


KEY_Quant = 32767.0


def quant_qr(q):
    """(x,y,z,w) float -> 4 int16, the CKeyQR encoding"""
    out = []
    for v in q:
        i = int(round(v * KEY_Quant))
        out.append(max(-32768, min(32767, i)))
    return out


def dequant_qr(k):
    return tuple(v / KEY_Quant for v in k)


def qangle(a, b):
    """angle between two quaternions in degrees, sign-insensitive and normalised
    (quantised keys drift ~2e-5 from unit; without normalising that reads as ~1 deg)"""
    na = math.sqrt(sum(c*c for c in a)) or 1.0
    nb = math.sqrt(sum(c*c for c in b)) or 1.0
    d = abs(sum(x*y for x, y in zip(a, b))) / (na*nb)
    return math.degrees(2.0 * math.acos(max(-1.0, min(1.0, d))))
