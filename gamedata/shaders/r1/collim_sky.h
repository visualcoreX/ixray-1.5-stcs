#ifndef COLLIM_SKY_H_INCLUDED
#define COLLIM_SKY_H_INCLUDED

// The sky the hud's glass mirrors on R1, for its two pixel shaders (model_collim_glass, model_sniperlens): as r2/collim.h
// reads it, of the one sky. r is the way the mirror looks, interpolated from the vertices (model_collim_reflect.vs).
//   m_collim_reflect_shape  y = the mirror's contrast, z = how much the glass texture's alpha masks it
//   m_sky_params            xyz = the weather's sky colour
//   m_sky_rotation          x = cos, y = sin of the skybox's turn about the vertical
uniform	float4		m_collim_reflect_shape;
uniform	float4		m_sky_params;
uniform	float4		m_sky_rotation;
uniform samplerCUBE	s_sky0;

#define	COLLIM_SKY_N		512.0	// the sky cubes' pixels a face: the stock ones. They have all their mips (CTexture's loader makes them)
#define	COLLIM_SKY_MIPS		10
#define	COLLIM_SKY_BLUR		6.0		// the blur: a texel of the level read as this many of the glass's pixels. 0 = as sharp as they allow
#define	COLLIM_SKY_TEXEL	0.03125	// the blur's largest texel, level 3: smaller levels' faces meet in a visible seam

// World direction -> the sky cubemap's texcoord, as dxEnvironmentRender lays the skybox out: rotated by sky_rotation,
// the horizon pulled down to the cube's bottom
float3	sky_tc( float3 d )
{
	float3	s	= float3( d.x*m_sky_rotation.x - d.z*m_sky_rotation.y, d.y, d.x*m_sky_rotation.y + d.z*m_sky_rotation.x );
	float3	p	= s / max( max( abs(s.x), abs(s.y) ), abs(s.z) );
	p.y			= max( p.y*2 - 1, -1 );
	return	p;
}

// The sky at mip level lod. Below the horizon stays on the side faces' bottom row, as sky_tc puts it.
float3	collim_sky( float3 tc, float s, float lod )
{
	tc.y		= max( tc.y, s - 1 );
	return	texCUBElod( s_sky0, float4(tc, lod) ).rgb;
}

// The sky round tc: four bilinear taps half a texel of the level off it, a tent three texels wide (r2/collim.h)
float3	collim_sky_blur( float3 tc )
{
	float	dr	= max( length(ddx(tc)), length(ddy(tc)) );					// how far tc moves from one pixel to the next
	float	t	= max( min(COLLIM_SKY_BLUR * dr, COLLIM_SKY_TEXEL), dr );		// a texel's size
	float	lod	= clamp( log2(t * COLLIM_SKY_N * 0.5), 0, COLLIM_SKY_MIPS - 1 );
	float	s	= exp2( lod ) / COLLIM_SKY_N;								// half a texel: a face's 1 is N/2 texels
	return	( collim_sky(tc + float3( s,  s,  s), s, lod)
			+ collim_sky(tc + float3( s, -s, -s), s, lod)
			+ collim_sky(tc + float3(-s,  s, -s), s, lod)
			+ collim_sky(tc + float3(-s, -s,  s), s, lod) ) / 4;
}

// What the mirror shows the way r looks: the sky in its colour, with the contrast -- dark sky adds less, the bright clouds stay
float3	collim_mirror( float3 r )
{
	float3	sky		= m_sky_params.rgb * collim_sky_blur( sky_tc(r) );
	float	lum		= max( dot(sky, float3(0.299, 0.587, 0.114)), 0.0001 );
	return	sky * pow( lum, m_collim_reflect_shape.y - 1 );
}

#endif
