#ifndef LENS_CUBE_H_INCLUDED
#define LENS_CUBE_H_INCLUDED

// $user$lens_cube: the world round the camera, a cubemap the hud's glass mirrors -- as the New Vegas "Dynamic
// Reflections" mod makes its own. CRender::render_lens_cube draws one face a frame (the whole cube in six frames)
// with the level's own deferred shaders into a small G-buffer, and lens_cube.ps lights it the simple way into its
// place here: six faces of LENS_CUBE_N x LENS_CUBE_N side by side, in a cube texture's order (+x -x +y -y +z -z).
// Alpha 1 = the face is drawn (0 before its first frame, and with r3_lens_cube_far 0: then the glass keeps the sky).
// LENS_CUBE_N is the engine's too (r3_rendertarget.cpp).
#define	LENS_CUBE_N		128.0

Texture2D	s_lens_cube;

// The face a direction falls in, and its forward, up and right -- right = up x forward, as build_camera_dir makes it
void	lens_cube_face( float3 d, out float face, out float3 F, out float3 U )
{
	float3	a	= abs( d );
	if ( a.x >= a.y && a.x >= a.z )	{ face = ( d.x > 0 ) ? 0 : 1;	F = float3( (d.x > 0) ? 1 : -1, 0, 0 );	U = float3( 0, 1, 0 ); }
	else if ( a.y >= a.z )			{ face = ( d.y > 0 ) ? 2 : 3;	F = float3( 0, (d.y > 0) ? 1 : -1, 0 );	U = float3( 0, 0, (d.y > 0) ? -1 : 1 ); }
	else							{ face = ( d.z > 0 ) ? 4 : 5;	F = float3( 0, 0, (d.z > 0) ? 1 : -1 );	U = float3( 0, 1, 0 ); }
}

// What lies the way d points (any length), as the cube holds it; alpha = whether that face is drawn
float4	lens_cube( float3 d )
{
	float	face;
	float3	F, U;
	lens_cube_face( d, face, F, U );
	float3	R	= cross( U, F );
	float	m	= dot( d, F );
	float2	uv	= float2( 0.5 + 0.5 * dot(d, R) / m, 0.5 - 0.5 * dot(d, U) / m );
	uv			= clamp( uv, 0.5 / LENS_CUBE_N, 1 - 0.5 / LENS_CUBE_N );	// not into the next face
	return	s_lens_cube.SampleLevel( smp_rtlinear, float2((face + uv.x) / 6, uv.y), 0 );
}

// The same, softened: the direction and four around it at radius s (a tangent), the middle weighed double
float4	lens_cube_soft( float3 d, float s )
{
	d			= normalize( d );
	float3	a	= normalize( cross(d, (abs(d.y) < 0.9) ? float3(0, 1, 0) : float3(1, 0, 0)) );
	float3	b	= cross( d, a );
	float4	c	= lens_cube( d ) * 2;
	c			+= lens_cube( d + a * s );
	c			+= lens_cube( d - a * s );
	c			+= lens_cube( d + b * s );
	c			+= lens_cube( d - b * s );
	return	c / 6;
}

#endif
