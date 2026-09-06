/*
------------------------------------------------------------------
SMAA blend weight calculation, vertex -- DX9 (SMAA_HLSL_3)
------------------------------------------------------------------
References:
https://github.com/iryoku/smaa
------------------------------------------------------------------
*/

#include "common.h"

#define SMAA_HLSL_3
#define SMAA_PRESET_ULTRA
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 0

uniform float4 screen_res;			// x=width, y=height, z=1/width, w=1/height
#define SMAA_RT_METRICS screen_res.zwxy

#include "smaa.h"

struct v
{
	float3	P			: POSITION;
	float2	tc0			: TEXCOORD0;
};

struct v2p_smaa
{
	float2	tc0			: TEXCOORD0;
	float2	pixcoord	: TEXCOORD1;
	float4	offset[3]	: TEXCOORD2;
	float4	HPos		: POSITION;
};

v2p_smaa main(v I)
{
	v2p_smaa O;
	O.HPos	= float4(I.P.x * screen_res.z * 2 - 1, -(I.P.y * screen_res.w * 2 - 1), 0, 1);
	O.tc0	= I.tc0;

	// scales its third offset by SMAA_MAX_SEARCH_STEPS -- hence the preset above
	SMAABlendingWeightCalculationVS(I.tc0, O.pixcoord, O.offset);

	return O;
}
