#include "common.h"
#include "skin.h"

// R1's lighter take on the collimator glass's sky reflection (r2/r3 work it out per pixel): everything
// but the cubemap lookup is done per vertex -- the mirror direction off the model's own normals, the
// Fresnel term, the sky visibility and the lighting -- and there is one sky (no blend between the
// weather's two), no flattening of the dome and no contrast. Third pass of models_lenses, added one/one.
//
//   m_collim_reflect   rgb = coating colour * strength for the current aim, w = F0
//   m_collim_hemi_*    the actor's sky visibility along +x+y+z / -x-y-z
//   m_sky_params       xyz = the weather's sky colour
//   m_sky_rotation     x = cos, y = sin of the skybox's turn about the vertical
uniform	float4	m_collim_reflect;
uniform	float4	m_collim_hemi_pos;
uniform	float4	m_collim_hemi_neg;
uniform	float4	m_sky_params;
uniform	float4	m_sky_rotation;

struct vf
{
	float4	hpos	: POSITION;
	float3	tc0		: TEXCOORD0;		// sky cubemap texcoord
	float3	c0		: TEXCOORD1;		// what the sky is multiplied by -- may pass 1, so not a COLOR
};

// World direction -> the sky cubemap's texcoord, as dxEnvironmentRender lays the skybox out (see
// r2/model_collim_reflect.ps): rotated by sky_rotation, the horizon pulled down to the cube's bottom.
float3	sky_tc( float3 d )
{
	float3	s	= float3( d.x*m_sky_rotation.x - d.z*m_sky_rotation.y, d.y, d.x*m_sky_rotation.y + d.z*m_sky_rotation.x );
	float3	p	= s / max( max( abs(s.x), abs(s.y) ), abs(s.z) );
	p.y			= max( p.y*2 - 1, -1 );
	return	p;
}

vf _main( v_model v )
{
	vf		o;

	float3	pos_w	= mul( m_W, v.pos );
	float3	v_dir	= normalize( pos_w - eye_position );
	float3	n		= normalize( mul( (float3x3)m_W, v.norm ) );
	n				= ( dot(n, v_dir) > 0 ) ? -n : n;			// the side facing the eye
	float3	r		= reflect( v_dir, n );

	float	c		= saturate( dot(-v_dir, n) );
	float	f		= lerp( m_collim_reflect.w, 1, pow(1 - c, 5) );
	float3	hc		= ( r < 0 ) ? m_collim_hemi_neg.xyz : m_collim_hemi_pos.xyz;
	float	vis		= saturate( dot(hc, abs(r)) );
	float3	light	= calc_model_lq_lighting( n ) * 2;

	o.hpos		= mul( m_WVP, v.pos );
	o.tc0		= sky_tc( r );
	o.c0		= m_sky_params.rgb * m_collim_reflect.rgb * f * vis * light;
	return o;
}

/////////////////////////////////////////////////////////////////////////
#define SKIN_LQ
#define SKIN_VF vf
#include "skin_main.h"
