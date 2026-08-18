#ifndef VECTOR3D_EXT_INCLUDED
#define VECTOR3D_EXT_INCLUDED

#include "_vector3d.h"

ICF Fvector cr_fvector3 (float f)
{
	Fvector res = { f, f, f };
	return  res;
}

ICF Fvector cr_fvector3 (float x, float y, float z)
{
	Fvector res = { x, y, z };
	return  res;
}

ICF Fvector cr_fvector3_hp (float h, float p)
{
	Fvector res;
	res.setHP(h, p);
	return  res;
}

ICF Fvector operator + (const Fvector& v1, const Fvector& v2)
{
	return  cr_fvector3(v1.x+v2.x, v1.y+v2.y, v1.z+v2.z);
}

ICF Fvector operator - (const Fvector& v1, const Fvector& v2)
{
	return  cr_fvector3(v1.x-v2.x, v1.y-v2.y, v1.z-v2.z);
}

ICF Fvector operator - (const Fvector& v)
{
	return  cr_fvector3(-v.x, -v.y, -v.z);
}

ICF Fvector operator * (const Fvector& v, float f)
{
	return  cr_fvector3(v.x*f, v.y*f, v.z*f);
}

ICF Fvector operator * (float f, const Fvector& v)
{
	return  cr_fvector3(v.x*f, v.y*f, v.z*f);
}

ICF Fvector   min (const Fvector& v1, const Fvector& v2)
{
	Fvector r;
	r.min(v1, v2);
	return r;
}

ICF Fvector   max (const Fvector& v1, const Fvector& v2)
{
	Fvector r;
	r.max(v1, v2);
	return r;
}

ICF Fvector   abs (const Fvector& v)
{
	Fvector r;
	r.abs(v);
	return r;
}

ICF Fvector   normalize (const Fvector& v)
{
	Fvector r(v);
	r.normalize();
	return r;
}

ICF float   magnitude (const Fvector& v)
{
	return v.magnitude();
}

ICF float   sqaure_magnitude (const Fvector& v)
{
	return v.square_magnitude();
}

ICF	float	dotproduct (const Fvector& v1, const Fvector& v2)
{	
	return v1.x*v2.x + v1.y*v2.y + v1.z*v2.z;
}

// CrossProduct
ICF	Fvector   crossproduct (const Fvector& v1, const Fvector& v2)
{
	Fvector r;
	r.crossproduct(v1, v2);
	return r;
}

// angle between two vectors, [0..PI]
ICF	float	angle_between_vectors (const Fvector& v1, const Fvector& v2)
{
	float	magnitudes	= v1.magnitude() * v2.magnitude();
	if (magnitudes < EPS_S) return 0.f;

	float	cos_alpha	= dotproduct(v1, v2) / magnitudes;
	clamp	(cos_alpha, -1.f, +1.f);
	return	acosf(cos_alpha);
}

// rotate a point around the Y axis by <angle>
ICF	Fvector   rotate_point (const Fvector& point, float angle)
{
	float	cos_alpha	= _cos(angle);
	float	sin_alpha	= _sin(angle);

	Fvector	r;
	r.set	(point.x*cos_alpha - point.z*sin_alpha, point.y, point.x*sin_alpha + point.z*cos_alpha);
	return	r;
}

ICF	Fvector   cr_vectorHP (float h, float p)
{
	float ch = _cos(h), cp=_cos(p), sh=_sin(h), sp=_sin(p);
	Fvector r;
	r.x = -cp*sh;
	r.y = sp;
	r.z = cp*ch;
	return r;
}

#endif // VECTOR3D_EXT_INCLUDED
