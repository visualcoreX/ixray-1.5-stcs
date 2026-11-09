#include "common.h"
#include "skin.h"

// R1's lighter take on the collimator glass's sky reflection (r2/r3 work it out per pixel): everything
// but the cube's texcoord, its lookup and the contrast is done per vertex -- the mirror direction off the model's
// normals bent toward the flat axis by the curve, the Fresnel term, the sky visibility and the lighting
// -- and there is one sky (no blend between the weather's two). The vertex shader of both glass passes of
// models_lenses (model_collim_glass, model_collim_tint is fine with its extra outputs).
// The reticle (collimator_marks) cannot be cut that way -- it is a direction looked along, picked per
// pixel -- so the pixel shader gets the world position and the scope's axes too. Those ride the
// skinning in the tangent/binormal slots (their .w hold bone weights and are left alone), which is why
// the mains below use skin.h's full skinning and not the LQ one SKIN_LQ would pick: the LQ variants
// drop T and B.
//
//   m_collim_reflect   rgb = coating colour * strength for the current aim, w = F0
//   m_collim_hemi_*    the actor's sky visibility along +x+y+z / -x-y-z
// These must stay few: skin.h keeps c22 and up for the bones, and whatever does not fit under them lands past the bones,
// in c252 and up (the sky's colour goes on in the pixel shaders for this).
uniform	float4	m_collim_reflect;
uniform	float4	m_collim_reflect_ind;	// the mirror's colour in the sky's light alone (see collim.h)
uniform	float4	m_collim_cube;			// x = the curve of the dome the glass mirrors by (r2/collim.h collim_cube_normal)
uniform	float4	m_collim_hemi_pos;
uniform	float4	m_collim_hemi_neg;

struct vf
{
	float4	hpos	: POSITION;
	float3	tc0		: TEXCOORD0;		// the way the mirror looks
	float3	c0		: TEXCOORD1;		// what the sky is multiplied by -- may pass 1, so not a COLOR
	float3	pos_w	: TEXCOORD2;
	float3	axis_w	: TEXCOORD3;		// the lens axis, -Z of the scope's bone
	float3	right_w	: TEXCOORD4;		// the scope's right, +X
	float2	tcb		: TEXCOORD5;		// the glass's own texture
	float3	glight	: TEXCOORD6;		// ...lit as model_def_lq lights it
	float	fog		: TEXCOORD7;
};

// (0,0,-1) and (1,0,0) packed the way skin.h reads a direction: unpacked as 2*v-1, no swizzle on R1
#define AXIS_PACKED		float3( 0.5, 0.5, 0 )
#define RIGHT_PACKED	float3( 1, 0.5, 0.5 )

vf _main( v_model v )
{
	vf		o;

	float3	pos_w	= mul( m_W, v.pos );
	float3	v_dir	= normalize( pos_w - eye_position );
	float3	n		= normalize( mul( (float3x3)m_W, v.norm ) );
	n				= ( dot(n, v_dir) > 0 ) ? -n : n;			// the side facing the eye
	float3	na		= normalize( mul( (float3x3)m_W, v.T ) );	// the flat axis, the same way
	na				= ( dot(na, v_dir) > 0 ) ? -na : na;
	float	cn		= dot( n, na );
	float3	r		= reflect( v_dir, normalize( na + (n - na * cn) * m_collim_cube.x / max(cn, 0.001) ) );

	float	c		= saturate( dot(-v_dir, na) );
	float	f		= lerp( m_collim_reflect.w, 1, pow(1 - c, 5) );
	float3	hc		= ( r < 0 ) ? m_collim_hemi_neg.xyz : m_collim_hemi_pos.xyz;
	float	vis		= saturate( dot(hc, abs(r)) );
	float3	light	= calc_model_lq_lighting( n ) * 2;

	o.hpos		= mul( m_WVP, v.pos );
	o.tc0		= r;
	o.c0		= f * vis * ( light * m_collim_reflect.rgb + L_hemi_color.rgb * 2 * m_collim_reflect_ind.rgb );
	o.pos_w		= pos_w;
	o.axis_w	= mul( (float3x3)m_W, v.T );
	o.right_w	= mul( (float3x3)m_W, v.B );
	o.tcb		= v.tc.xy;
	o.glight	= calc_model_lq_lighting( normalize( mul( (float3x3)m_W, v.norm ) ) );
	o.fog		= calc_fogging( float4(pos_w, 1) );
	return o;
}

/////////////////////////////////////////////////////////////////////////
#ifdef 	SKIN_NONE
vf	main(v_model v) 		{ v.T = float3(0, 0, -1);	v.B = float3(1, 0, 0);	return _main(v); }
#endif

#ifdef 	SKIN_0
vf	main(v_model_skinned_0 v) 	{ v.T.xyz = AXIS_PACKED;	v.B.xyz = RIGHT_PACKED;	return _main(skinning_0(v)); }
#endif

#ifdef	SKIN_1
vf	main(v_model_skinned_1 v) 	{ v.T.xyz = AXIS_PACKED;	v.B.xyz = RIGHT_PACKED;	return _main(skinning_1(v)); }
#endif

#ifdef	SKIN_2
vf	main(v_model_skinned_2 v) 	{ v.T.xyz = AXIS_PACKED;	v.B.xyz = RIGHT_PACKED;	return _main(skinning_2(v)); }
#endif

#ifdef	SKIN_3
vf	main(v_model_skinned_3 v) 	{ v.T.xyz = AXIS_PACKED;	v.B.xyz = RIGHT_PACKED;	return _main(skinning_3(v)); }
#endif

#ifdef	SKIN_4
vf	main(v_model_skinned_4 v) 	{ v.T.xyz = AXIS_PACKED;	v.B.xyz = RIGHT_PACKED;	return _main(skinning_4(v)); }
#endif
