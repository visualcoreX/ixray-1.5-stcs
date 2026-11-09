#ifndef LENS_CUBE_H_INCLUDED
#define LENS_CUBE_H_INCLUDED

// $user$lens_cube: the world round the camera, a cubemap the hud's glass mirrors -- as the New Vegas "Dynamic
// Reflections" mod makes its own. CRender::render_lens_cube draws one face a frame (the whole cube in six frames)
// with the level's own deferred shaders into a small G-buffer, and lens_cube.ps lights it the simple way into its
// place here: six faces of LENS_CUBE_N x LENS_CUBE_N, a cube texture of the usual order (+x -x +y -y +z -z).
// Alpha 1 = the face is drawn (0 before its first frame, and with r3_lens_cube_far 0: then the glass keeps the sky).
// It has LENS_CUBE_MIPS levels, made after every face, a level being the cube blurred by its texel. Each face's are
// made apart, but the hardware filters a cube across its faces' edges, so no level shows a seam. LENS_CUBE_N and
// LENS_CUBE_MIPS are the engine's too (r3_rendertarget.cpp).
#define	LENS_CUBE_N		128.0
#define	LENS_CUBE_MIPS	6

TextureCube	s_lens_cube;

// What lies the way d points (any length), as the cube holds it at mip level lod (0 = LENS_CUBE_N a face, trilinear between);
// alpha = whether that face is drawn. Bound with smp_linear: smp_rtlinear has no mips.
float4	lens_cube( float3 d, float lod )
{
	return	s_lens_cube.SampleLevel( smp_linear, d, lod );
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
