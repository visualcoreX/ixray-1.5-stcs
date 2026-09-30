#include "common.h"

// Laser beam strip (r3_rendertarget_phase_laser.cpp): world-space vertices, world xform = identity.
// All the work is per pixel in laser_beam.ps; this only hands it the world position.

struct vi
{
	float4 P	: POSITION;
	float2 tc	: TEXCOORD0;	// x = along the beam 0..1, y = side -1..1 (unused)
};

struct v2p
{
	float3 wpos	: TEXCOORD0;
	float4 hpos	: SV_Position;
};

v2p main(vi v)
{
	v2p o;
	o.hpos	= mul(m_WVP, v.P);
	o.wpos	= v.P.xyz;
	return o;
}
