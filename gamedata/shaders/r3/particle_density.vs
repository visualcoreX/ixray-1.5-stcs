#include "common.h"

// Laser beams: blend-shaded particles (smoke, dust) redrawn into $user$laser_dens (CBlender_Particle
// element 3, r3_rendertarget_phase_laser.cpp). Same input as particle.vs, plus the view depth.

struct vv
{
	float4 P	: POSITION;
	float2 tc	: TEXCOORD0;
	float4 c	: COLOR0;
};

struct v2p
{
	float2 tc	: TEXCOORD0;
	float4 c	: COLOR0;
	float  z	: TEXCOORD1;	// view depth
	float4 hpos	: SV_Position;
};

v2p main(vv v)
{
	v2p o;
	o.hpos	= mul(m_WVP, v.P);
	o.tc	= v.tc;
	o.c		= unpack_D3DCOLOR(v.c);
	o.z		= mul(m_WV, v.P).z;
	return o;
}
