// The optic lens's normal pass (models_zoom*.s): model_def_lplanes, plus the pixel's view-space position for
// the field stop (scope_field.h).
#include "common.h"
#include "skin.h"

struct vf
{
	float2 tc0	: TEXCOORD0;		// base
	float4 c0	: COLOR0;		// color
	float3 pos_v	: TEXCOORD1;		// view space: the lens's field stop (scope_field.h)
	float3 axis_v	: TEXCOORD2;		// view space: the weapon's forward, the scope's axis (scope_field.h)
	float4 hpos	: SV_Position;
};

vf 	_main (v_model v)
{
	vf 		o;

	o.hpos 		= mul			(m_WVP, v.P);		// xform, input in world coords
	o.tc0		= v.tc.xy;					// copy tc

	// calculate fade
	float3  dir_v 	= normalize		(mul(m_WV,v.P));
	float3 	norm_v 	= normalize 		(mul(m_WV,v.N));
	float 	fade 	= abs			(dot(dir_v,norm_v));
	o.c0		= fade;
	o.pos_v		= mul(m_WV, v.P);
	o.axis_v		= mul((float3x3)m_WV, float3(0, 0, 1));

	return o;
}

/////////////////////////////////////////////////////////////////////////
#ifdef 	SKIN_NONE
vf	main(v_model v) 		{ return _main(v); 		}
#endif

#ifdef 	SKIN_0
vf	main(v_model_skinned_0 v) 	{ return _main(skinning_0(v)); }
#endif

#ifdef	SKIN_1
vf	main(v_model_skinned_1 v) 	{ return _main(skinning_1(v)); }
#endif

#ifdef	SKIN_2
vf	main(v_model_skinned_2 v) 	{ return _main(skinning_2(v)); }
#endif

#ifdef	SKIN_3
vf	main(v_model_skinned_3 v) 	{ return _main(skinning_3(v)); }
#endif

#ifdef	SKIN_4
vf	main(v_model_skinned_4 v) 	{ return _main(skinning_4(v)); }
#endif
