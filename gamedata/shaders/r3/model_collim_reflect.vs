#include "common.h"
#include "skin.h"

// The collimator glass's reflection pass (models_lenses / models_transparent): hands the pixel shader
// the world position and normal, so the mirror direction is worked out per pixel -- per vertex, as
// model_env_lq does it, a lens of a few dozen vertices mirrors a smear. Also the glass's flat axis:
// the lens is a dome, and the pixel shader bends its normals toward the axis to flatten the mirror.
// HUD glass faces the eye along the model's -Z, so the axis is -Z run through the skinning in the
// tangent's slot (its .w holds bone weights and is left alone) -- it then turns with the scope's
// bone through the animations. The glass's own tangents would not do: on a dome T x B is the
// domed normal again.

// (0,0,-1) packed the way skin.h reads a direction: swizzled .zyx, then unpacked as 2*v-1
#define AXIS_PACKED	float3( 0, 0.5, 0.5 )

struct vf
{
	float2	tc0		: TEXCOORD0;
	float3	pos_w	: TEXCOORD1;
	float3	norm_w	: TEXCOORD2;
	float3	axis_w	: TEXCOORD3;
	float4	hpos	: SV_Position;
};

vf _main( v_model v )
{
	vf		o;

	o.hpos		= mul( m_WVP, v.P );
	o.tc0		= v.tc.xy;
	o.pos_w		= mul( m_W, v.P );
	o.norm_w	= mul( (float3x3)m_W, v.N );
	o.axis_w	= mul( (float3x3)m_W, v.T );					// the lens axis, see AXIS_PACKED

	return o;
}

/////////////////////////////////////////////////////////////////////////
#ifdef 	SKIN_NONE
vf	main(v_model v) 		{ v.T = float3(0, 0, -1);	return _main(v); 		}
#endif

#ifdef 	SKIN_0
vf	main(v_model_skinned_0 v) 	{ v.T.xyz = AXIS_PACKED;	return _main(skinning_0(v)); }
#endif

#ifdef	SKIN_1
vf	main(v_model_skinned_1 v) 	{ v.T.xyz = AXIS_PACKED;	return _main(skinning_1(v)); }
#endif

#ifdef	SKIN_2
vf	main(v_model_skinned_2 v) 	{ v.T.xyz = AXIS_PACKED;	return _main(skinning_2(v)); }
#endif

#ifdef	SKIN_3
vf	main(v_model_skinned_3 v) 	{ v.T.xyz = AXIS_PACKED;	return _main(skinning_3(v)); }
#endif

#ifdef	SKIN_4
vf	main(v_model_skinned_4 v) 	{ v.T.xyz = AXIS_PACKED;	return _main(skinning_4(v)); }
#endif
