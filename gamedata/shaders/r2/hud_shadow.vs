#include "common.h"

// Full-screen quad for the HUD contact-shadow pass.
//
// The only reason this is not combine_1.vs is the depth it emits. The HUD is rasterised into the
// same G-buffer as the world but through a squashed viewport (rmNear: MinZ/MaxZ = 0 .. 0.02), so a
// quad drawn at exactly that edge, with ZFUNC = GREATER, covers the HUD band and nothing else --
// which is what keeps this pass off world geometry. combine_1.vs hardcodes hpos.z = 0 and would
// pass nowhere.
#define HUD_DEPTH_EDGE 0.02f

struct 	_in        	{
	float4 	p	: POSITION	;	// xy = pos, zw = tc0
	float2	tcJ	: TEXCOORD0;
};

struct 	_out        	{
	float4 	hpos	: POSITION	;
	float2	tc0		: TEXCOORD0	;
};

_out 	main	( _in   I )
{
	_out 		O;
	O.hpos 		= float4	(I.p.x, -I.p.y, HUD_DEPTH_EDGE, 1);
	O.tc0		= I.p.zw;
 	return		O;
}

FXVS;
