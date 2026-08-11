#include "common.h"

// Full-screen quad for the HUD contact-shadow pass. See hud_shadow.ps.
//
// The only reason this is not combine_1.vs is the depth it emits. The HUD is rasterised into the
// same G-buffer as the world but through a squashed viewport (rmNear: MinZ/MaxZ = 0 .. 0.02), so a
// quad drawn at exactly that edge, with the Z test inverted to GREATER, covers the HUD band and
// nothing else -- which is what keeps this pass off world geometry. combine_1.vs hardcodes
// hpos.z = 0 and would pass nowhere.
#define HUD_DEPTH_EDGE 0.02f

struct 	_in
{
	float4	P	: POSITIONT;	// xy = pos, zw = tc0
	float2	tcJ	: TEXCOORD0;
};

struct 	v2p
{
	float2	tc0		: TEXCOORD0;
	float4	hpos	: SV_Position;
};

v2p main ( _in I )
{
	v2p 	O;
	O.hpos	= float4	(I.P.x, -I.P.y, HUD_DEPTH_EDGE, 1);
	O.tc0	= I.P.zw;
	return	O;
}

FXVS;
