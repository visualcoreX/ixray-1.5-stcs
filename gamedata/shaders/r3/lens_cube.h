#ifndef LENS_CUBE_H_INCLUDED
#define LENS_CUBE_H_INCLUDED

// $user$lens_cube: the world round the camera, a cubemap the hud's glass mirrors -- as the New Vegas "Dynamic
// Reflections" mod makes its own. CRender::render_lens_cube draws one face a frame (the whole cube in six frames)
// with the level's own deferred shaders into a small G-buffer, and lens_cube.ps lights it the simple way into its
// place here: six faces of LENS_CUBE_N x LENS_CUBE_N side by side, in a cube texture's order (+x -x +y -y +z -z).
// Alpha 1 = the face is drawn (0 before its first frame, and with r3_lens_cube_far 0: then the glass keeps the sky).
// It has LENS_CUBE_MIPS levels, made after every face: each halves the faces in place, so a level is the cube blurred
// by its texel, none of a face bleeding into the next. LENS_CUBE_N and LENS_CUBE_MIPS are the engine's too
// (r3_rendertarget.cpp).
#define	LENS_CUBE_N		128.0
#define	LENS_CUBE_MIPS	6

Texture2D	s_lens_cube;

// The face a direction falls in, and its forward, up and right -- right = up x forward, as build_camera_dir makes it
void	lens_cube_face( float3 d, out float face, out float3 F, out float3 U )
{
	float3	a	= abs( d );
	if ( a.x >= a.y && a.x >= a.z )	{ face = ( d.x > 0 ) ? 0 : 1;	F = float3( (d.x > 0) ? 1 : -1, 0, 0 );	U = float3( 0, 1, 0 ); }
	else if ( a.y >= a.z )			{ face = ( d.y > 0 ) ? 2 : 3;	F = float3( 0, (d.y > 0) ? 1 : -1, 0 );	U = float3( 0, 0, (d.y > 0) ? -1 : 1 ); }
	else							{ face = ( d.z > 0 ) ? 4 : 5;	F = float3( 0, 0, (d.z > 0) ? 1 : -1 );	U = float3( 0, 1, 0 ); }
}

// What lies the way d points (any length), as the cube holds it at mip level lod (0 = 128 a face, trilinear between);
// alpha = whether that face is drawn. Bound with smp_linear: smp_rtlinear has no mips.
float4	lens_cube( float3 d, float lod )
{
	float	face;
	float3	F, U;
	lens_cube_face( d, face, F, U );
	float3	R	= cross( U, F );
	float	m	= dot( d, F );
	float2	uv	= float2( 0.5 + 0.5 * dot(d, R) / m, 0.5 - 0.5 * dot(d, U) / m );
	float	h	= 0.5 * exp2( ceil(lod) ) / LENS_CUBE_N;	// half a texel of the smaller level read:
	uv			= clamp( uv, h, 1 - h );					// not into the next face
	return	s_lens_cube.SampleLevel( smp_linear, float2((face + uv.x) / 6, uv.y), lod );
}

// The same, smooth: four taps half a texel of that level off d, each a bilinear one -- together a tent three texels
// wide, with no gap between them to show the world doubled (as taps a few texels apart at the top level did), and
// none of the small level's blocks.
float4	lens_cube_blur( float3 d, float lod )
{
	d			= normalize( d );
	float3	a	= normalize( cross(d, (abs(d.y) < 0.9) ? float3(0, 1, 0) : float3(1, 0, 0)) );
	float3	b	= cross( d, a );
	float	s	= exp2( lod ) / LENS_CUBE_N;				// half a texel, as a tangent: a face's 1 is N/2 texels
	float4	c	= lens_cube( d + (a + b) * s, lod );
	c			+= lens_cube( d + (a - b) * s, lod );
	c			+= lens_cube( d - (a + b) * s, lod );
	c			+= lens_cube( d - (a - b) * s, lod );
	return	c / 4;
}

// The level for a blur of radius r (a tangent) where the glass shows the world as dr says (how far the mirrored
// direction turns from one pixel to the next): the blur's own, or a smaller one where the glass squeezes the world
// so small that the pixels would skip its texels -- the ripple they crawl with.
float	lens_cube_lod( float r, float dr )
{
	float	t	= max( r / 1.5, dr );						// a texel's size: the tent spans 1.5 of them each way
	return	clamp( log2(t * LENS_CUBE_N * 0.5), 0, LENS_CUBE_MIPS - 1 );
}

#endif
