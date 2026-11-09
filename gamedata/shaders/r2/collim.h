#ifndef COLLIM_H
#define COLLIM_H

// What the collimator glass's passes share (models_lenses): model_collim_glass, model_collim_tint and
// model_collim_mask. Their vertex shader is model_collim_reflect.vs: the world
// position and normal, the lens axis (-Z of the scope's bone) and the scope's right (+X), both carried
// through the skinning.
//
//   m_collim_tint           rgb = the coating's tint (0.5 = clear)
//   m_collim_reflect        rgb = coating colour * strength for the current aim, w = F0 -- in the light that
//                           falls on the glass (the sun, the way model_def_lq lights it)
//   m_collim_reflect_ind    rgb = the same mirror's colour in the sky's light alone (0 = none)
//   m_collim_cube           the dome the glass mirrors by (collim_cube_normal): x = curve
//   m_collim_reflect_shape  y = contrast,
//                           z = how much the glass texture's alpha masks the mirror (0 = not at all),
//                           w = the most indirect light the mirror takes (0 = no limit)
//   m_collim_hemi_*         the actor's sky visibility along +x+y+z / -x-y-z: no sky to mirror under a roof
//   m_sky_params            xyz = the weather's sky colour, w = blend from sky0 to sky1 (as the skybox)
//   m_sky_rotation          x = cos, y = sin of the skybox's turn about the vertical
//   m_collim_mark           rgb = the reticle's colour * intensity * glow (0 = no reticle)
//   m_collim_mark_proj      x = texcoord per tangent of the angle, y/z = zeroing offset as tangents
//   m_collim_prism          x = tangent of half the field of view, y = width of its edge (tangent),
//                           z = how much the reticle darkens the view, w = 1 for a prism sight
//   m_collim_glass          x = the glass texture's share for the current aim
//   m_collim_lights         the lights besides the sun that reach the hud's glass, COLLIM_LIGHTS triples:
//                           position (w = 1/range^2), colour (w = 1 for a spot), spot axis (w = cos of half
//                           its cone) -- collim_gather_lights; black for the world's glass
//   m_collim_spec           x = the highlight's strength (0 = none), y = glossiness (its exponent), z = how
//                           the glass texture's alpha picks the glossy parts (+1 clear, -1 opaque, 0 all), w = the
//                           lamps' share of it against the sun's
//   m_collim_spec_color     rgb = the highlight's tint (1 = the light's own colour)
// All the angles are the hud's: the binders turn the configs' degrees of the world's view into them.
uniform	float4	m_collim_tint;
uniform	float4	m_collim_reflect;
uniform	float4	m_collim_reflect_ind;
uniform	float4	m_collim_cube;
uniform	float4	m_collim_reflect_shape;
uniform	float4	m_collim_hemi_pos;
uniform	float4	m_collim_hemi_neg;
uniform	float4	m_sky_params;
uniform	float4	m_sky_rotation;
uniform	float4	m_collim_mark;
uniform	float4	m_collim_mark_proj;
uniform	float4	m_collim_prism;
uniform	float4	m_collim_glass;		// x = how much of the glass's texture shows (the aim fades it)
#define	COLLIM_LIGHTS	8
#define	COLLIM_LOOP		[loop]		// collim_lamps: a real loop -- unrolled, ps_3_0 runs out of temporaries
uniform	float4	m_collim_lights[COLLIM_LIGHTS*3];
uniform	float4	m_collim_spec;
uniform	float4	m_collim_spec_color;

uniform samplerCUBE	s_sky0;
uniform samplerCUBE	s_sky1;
uniform sampler2D	s_mark;		// $user$collim_mark: the reticle picked
#define	COLLIM_MARK(tc)		tex2D( s_mark, tc )
uniform sampler2D	s_spec;		// wpn\lenses_spec: rgb = where the glass shines and in what colour (laid out as the wpn\lenses atlas is)
#define	COLLIM_SPEC_MAP(tc)	tex2D( s_spec, tc )
#define	COLLIM_SKY_N	512.0	// the sky cubes' pixels a face: the stock ones. They have all their mips (CTexture's loader makes them)
#define	COLLIM_SKY_MIPS	10
#define	COLLIM_SKY_BLUR	6.0		// the blur: a texel of the level read as this many of the glass's pixels (the tent spans 1.5 texels
								// each way). Counted on the glass, so that it stays the same share of it whatever the mirror
								// shows: a flat glass sees a few degrees of sky, and a blur fixed in degrees left it one flat colour.
								// 0 = as sharp as the glass's pixels allow
#define	COLLIM_SKY_TEXEL	0.03125	// the blur's largest texel, level 3: smaller levels' faces meet in a visible seam (D3D9 filters
								// a cube inside each face)

struct v2p_collim
{
	float2	tc0		: TEXCOORD0;
	float3	pos_w	: TEXCOORD1;
	float3	norm_w	: TEXCOORD2;
	float3	axis_w	: TEXCOORD3;
	float3	right_w	: TEXCOORD4;
	float2	obj		: TEXCOORD5;	// the model's x, y (lens_wet.h)
};

// World direction -> the sky cubemaps' texcoord, as dxEnvironmentRender lays the skybox out: rotated
// about Y by sky_rotation, and with the horizon pulled down to the cube's bottom edge -- the box's side
// faces map box-y 0..1 to tc-y -1..1, and everything under the horizon to the bottom (hbox_verts).
float3	sky_tc( float3 d )
{
	float3	s	= float3( d.x*m_sky_rotation.x - d.z*m_sky_rotation.y, d.y, d.x*m_sky_rotation.y + d.z*m_sky_rotation.x );
	float3	p	= s / max( max( abs(s.x), abs(s.y) ), abs(s.z) );
	p.y			= max( p.y*2 - 1, -1 );
	return	p;
}

// The weather's two skies at mip level lod. Below the horizon stays on the side faces' bottom row, as sky_tc puts it.
float3	collim_sky( float3 tc, float s, float lod )
{
	tc.y		= max( tc.y, s - 1 );
	return	lerp( texCUBElod(s_sky0, float4(tc, lod)).rgb, texCUBElod(s_sky1, float4(tc, lod)).rgb, m_sky_params.w );
}

// The sky round tc, as r3's lens_cube_blur reads its cube: four bilinear taps half a texel of the level off it -- a
// tent three texels wide. tc lies on the cube's surface, so whichever face it is on, two of these three axes are that
// face's plane and make the tent's four corners, and the third only moves the point along its own line. The level: the
// blur's own, or a smaller one where the glass squeezes the sky so small that its pixels would skip texels.
float3	collim_sky_blur( float3 tc )
{
	float	dr	= max( length(ddx(tc)), length(ddy(tc)) );					// how far tc moves from one pixel to the next
	float	t	= max( min(COLLIM_SKY_BLUR * dr, COLLIM_SKY_TEXEL), dr );		// a texel's size
	float	lod	= clamp( log2(t * COLLIM_SKY_N * 0.5), 0, COLLIM_SKY_MIPS - 1 );
	float	s	= exp2( lod ) / COLLIM_SKY_N;											// half a texel: a face's 1 is N/2 texels
	return	( collim_sky(tc + float3( s,  s,  s), s, lod)
			+ collim_sky(tc + float3( s, -s, -s), s, lod)
			+ collim_sky(tc + float3(-s,  s, -s), s, lod)
			+ collim_sky(tc + float3(-s, -s,  s), s, lod) ) / 4;
}

float3	face_eye( float3 n, float3 v )	{ return ( dot(n, v) > 0 ) ? -n : n; }

// Where the eye looks, against the scope's line of sight: the tangents of the angle off it, right and up,
// and in `a` how far along it (<= 0: looking back at the sight from in front). v is from the eye; the
// sight line runs opposite the lens axis.
float2	collim_sight( float3 v, float3 axis_w, float3 right_w, out float a )
{
	float3	fwd		= -normalize( axis_w );
	float3	rt		= normalize( right_w - fwd*dot(right_w, fwd) );
	float3	up		= cross( fwd, rt );
	a				= dot( v, fwd );
	return	float2( dot(v, rt), dot(v, up) ) / max( a, 0.001 );
}

// The reticle hangs at infinity on the sight line: the angle picks the texel. Aimed, the eye sits on the
// line and the mark on the screen's centre; off it, the mark slides across the glass and its rim cuts it.
float2	collim_mark_tc( float2 t, float a, out float inside )
{
	float2	tc		= 0.5 + float2( t.x - m_collim_mark_proj.y, m_collim_mark_proj.z - t.y ) * m_collim_mark_proj.x;
	inside			= ( a > 0 && all(tc == saturate(tc)) ) ? 1 : 0;
	return	tc;
}

// A prism sight's field stop: the world shows only within half its field of view off the sight line,
// beyond it the inside of the tube -- black. Aimed, a clear picture with a dark rim; the eye off the axis,
// a dark crescent closing in; from the side, a dark eyepiece. 1 everywhere for anything but a prism.
float	collim_field_stop( float2 t, float a )
{
	if ( m_collim_prism.w <= 0 )	return 1;
	float	f		= saturate( (m_collim_prism.x + 0.5*m_collim_prism.y - length(t)) / max(m_collim_prism.y, 0.0001) );
	return	( a > 0 ) ? f : 0;
}

// The lamps, torches, fires and muzzle flashes near the eye, on the glass at p (the glass is drawn forward:
// the deferred lights the gun around it gets never reach it). The fall-off is the deferred lights' own
// (1 - d^2/range^2); a spot's edge is softened over a quarter of its cone, where the engine's projected spot
// texture fades too.
// One of them at p: its colour as it arrives (fall-off, spot edge), l = the way to it.
float3	collim_lamp( int i, float3 p, out float3 l )
{
	float4	lp		= m_collim_lights[i*3+0];
	float4	lc		= m_collim_lights[i*3+1];
	float4	ld		= m_collim_lights[i*3+2];
	float3	d		= lp.xyz - p;
	float	d2		= max( dot(d, d), 0.0001 );
	l				= d * rsqrt( d2 );
	float	att		= saturate( 1 - d2*lp.w );
	float	spot	= ( lc.w > 0.5 ) ? smoothstep( ld.w, lerp(ld.w, 1, 0.25), dot(-l, ld.xyz) ) : 1;
	return	lc.rgb * att * spot;
}

// All of them at once, in one pass over the lamps (n: the glass's normal facing the eye, v: from the eye):
// diff -- as a surface facing them is lit (the glass's texture), spec -- their Blinn highlight at m_collim_spec.y's glossiness. One loop, not one
// per use: on R2 (ps_3_0, 32 temporaries) three unrolled ones ran out of registers.
struct collim_lamps_t
{
	float3	diff;
	float3	spec;
};

collim_lamps_t	collim_lamps( float3 p, float3 n, float3 v )
{
	collim_lamps_t	r;
	r.diff			= 0;
	r.spec			= 0;
	float	g		= m_collim_spec.y;
	COLLIM_LOOP
	for ( int i = 0; i < COLLIM_LIGHTS; ++i )
	{
		float3	l;
		float3	c		= collim_lamp( i, p, l );
		float	ndl		= saturate( dot(n, l) );
		r.diff			+= c * ndl;
		r.spec			+= c * pow( saturate(dot(n, normalize(l - v))), g ) * ndl;
	}
	return	r;
}

// The glass's highlight (Blinn), from the sun -- as much of it as reaches the actor, the same share the
// glass's diffuse light takes (L_material.y) -- and from the lights near the eye (collim_lamps' spec), as much
// of theirs as m_collim_spec.w lets through. The model's lighting has no specular at all, and in a direct light
// the glass's texture alone read flat and matte. v: from the eye; ng: the glass's normal facing the eye; a: the
// glass texture's alpha, picking the glossy parts as m_collim_spec.z says; sm: the specular map's rgb
// (COLLIM_SPEC_MAP), where the glass shines and in what colour. Glossier means a smaller, sharper highlight,
// kept as bright: (gloss+8)/8. It fades for the aim with the glass's texture (m_collim_glass.x): through the
// sight it would cover the view. (R3 draws it GGX.)
float3	collim_specular( float3 v, float3 ng, float a, float3 sm, float3 lamps )
{
	if ( m_collim_spec.x <= 0 )	return 0;
	float	g		= m_collim_spec.y;

	float3	ls		= -L_sun_dir_w;
	float3	res		= L_sun_color * L_material.y * pow( saturate(dot(ng, normalize(ls - v))), g ) * saturate( dot(ng, ls) )
					+ lamps * m_collim_spec.w;

	float	m		= m_collim_spec.z;
	float	mask	= ( m >= 0 ) ? lerp( 1, 1 - a, m ) : lerp( 1, a, -m );
	return	res * m_collim_spec_color.rgb * m_collim_spec.x * m_collim_glass.x * mask * sm * (g + 8) / 8;
}

// A light's brightness held under c (0 = not held), hue kept: as it is up to 3/4 of c, then easing toward c.
float3	collim_limit( float3 x, float c )
{
	if ( c <= 0 )	return x;
	float	l		= dot( x, float3(0.299, 0.587, 0.114) );
	float	k		= 0.75 * c;
	float	o		= ( l <= k ) ? l : k + (l - k) / ( 1 + (l - k) / (c - k) );
	return	x * ( o / max(l, 0.0001) );
}

// The light on the glass's own texture: the sun and the sky as model_def_lq gives them (its light*2), and
// the lamps near the eye (collim_lamps' diff) as the deferred lights give them to the gun around it -- once,
// not twice: in model_def_lq's doubling a fire beside the actor made the glass brighter than anything around
// it. They are held under m_collim_reflect_shape.w too, as the mirror's indirect light is.
float3	collim_glass_light( float3 n, float3 lamps )
{
	return	calc_model_lq_lighting( n ) * 2 + collim_limit( lamps, m_collim_reflect_shape.w );
}

// The normal the glass mirrors by (ng: the glass's own, na: its flat axis, both facing the eye): the tangent of the
// glass's tilt off the axis times the curve (m_collim_cube.x). The tangent is what the lens's normals keep even across
// its triangles (CSkeletonX_ext::_LensGlassNormals), and however large it gets, the tilt stays under 90 degrees.
float3	collim_cube_normal( float3 ng, float3 na )
{
	float	c		= dot( ng, na );
	return	normalize( na + (ng - na * c) * m_collim_cube.x / max(c, 0.001) );
}

// The sky the coating mirrors, in its colour (v: from the eye; ng: the glass's own normal, facing the eye;
// na: its flat axis). A domed lens is a convex mirror: the full dome shrinks the sky into a wide, still
// picture; bent toward the flat axis it becomes a close-up that pans as the gun sways -- the mirror turns
// twice as fast as the glass does. 0 outside the hud (the binder). dn: what the rain's drops on it turn the
// mirror's normal by (lens_wet.h; collim_reflection: none).
float3	collim_reflection_dn( float3 v, float3 ng, float3 na, float3 dn )
{
	float3	r		= reflect( v, normalize(collim_cube_normal(ng, na) + dn) );

	// Schlick: F0 looking straight in, all of it at a grazing angle -- by the flat axis: the dome is the curve's, not the glass's
	float	c		= saturate( dot(-v, na) );
	float	f		= lerp( m_collim_reflect.w, 1, pow(1 - c, 5) );

	// how much open sky lies the way the glass looks
	float3	hc		= ( r < 0 ) ? m_collim_hemi_neg.xyz : m_collim_hemi_pos.xyz;
	float	vis		= saturate( dot(hc, abs(r)) );

	// Two lights, each with its colour -- a coloured specular without a map. The direct one is the glass's
	// own diffuse lighting (model_def_lq: light*base*2): bright where the sun falls on it. The indirect one is
	// the sky's light, whatever way the glass faces -- model_def_lq's hemi term lights only what faces up, so
	// a glass looking at the sun's far side got next to nothing and went flat black. Both still need open sky
	// the way the glass looks (vis): under a roof they go out together. The lamps near the eye light
	// neither: this is a picture of the sky, and a shot or a torch beside the gun flared all of it at once (they
	// have their own highlight, collim_lamps' spec). The indirect light is held under m_collim_reflect_shape.w.
	float3	direct	= calc_model_lq_lighting( ng ) * 2 * m_collim_reflect.rgb;
	float3	ind		= collim_limit( L_hemi_color.rgb * vis * 2, m_collim_reflect_shape.w ) * m_collim_reflect_ind.rgb;

	float3	sky		= m_sky_params.rgb * collim_sky_blur( sky_tc(r) );

	// contrast: the brightness raised to a power, hue kept -- dark sky adds less, the bright clouds and
	// the sun's side stay, so the mirror reads as highlights over the view rather than a wash
	float	lum		= max( dot(sky, float3(0.299, 0.587, 0.114)), 0.0001 );
	sky				*= pow( lum, m_collim_reflect_shape.y - 1 );

	return	sky * f * ( vis * direct + ind );
}

float3	collim_reflection( float3 v, float3 ng, float3 na )	{ return collim_reflection_dn( v, ng, na, 0 ); }

// The reticle's glow (its etched lines are the tint pass's darkening). 0 outside the hud (the binder).
float3	collim_glow( float2 t, float a )
{
	float	inside;
	float2	tc		= collim_mark_tc( t, a, inside );
	float4	mark	= COLLIM_MARK( tc );
	return	mark.rgb * mark.a * m_collim_mark.rgb * inside;
}

#endif
