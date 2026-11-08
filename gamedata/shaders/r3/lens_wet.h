#ifndef LENS_WET_H
#define LENS_WET_H

// The rain on an optic's outer glass and on a collimator's (model_sniperlens.ps, model_collim_glass.ps; after
// collim.h). The optic's glass keeps out of the rain's wet G-buffer (CRenderTarget::STENCIL_DRY): its own drops are
// drawn here instead, over its mirror. On a collimator's clear glass the drops' rims also hide some of the view behind
// (lens_wet_shade). A map of drops
// (wpn\lenses_rain) lies on the glass in the model's own x, y -- the same size on every sight, whatever part of the
// wpn\lenses atlas its glass takes -- and bends the normal the glass mirrors and shines by. The water itself mirrors
// the world and catches the highlights of the sun and the lamps, sharper than the coated glass under it, in its own
// colour, as much as Fresnel says; a thin film of it lies over the whole glass when soaked.
// Two layers of the same map: the drops sitting still, and the same drops drawn out down the glass and crawling --
// the runlets, only while the rain falls.
//
//   m_lens_wet   x = how wet the glass is, 0 dry .. 1 soaked: the drops whose id is under it are there;
//                y = the rain's clock, seconds (runs only while the rain falls on the glass, as fast as it is heavy;
//                wraps at IGame_Persistent::HUD_LENS_WET_WRAP, 3600); z = the rain falling on it now, 0..1.
//                CActor; the hud's glass only.
uniform	float4	m_lens_wet;
Texture2D	s_wet;		// wpn\lenses_rain: rg = the drop's normal xy (*0.5+0.5), b = the water's depth, a = the drop's id (1 = none)

#define	LENS_WET_TILE		25.0				// the map's repeats a metre of the model: a tile 4 cm across
#define	LENS_WET_BUMP		1.0					// how steep the drops' sides are (1 = as the map has them)
#define	LENS_WET_LIFE		7.0					// rain-clock seconds a drop lives: it lands, sits, then dries or runs off
#define	LENS_WET_RUN		0.2					// the share of the drops that run, in a heavy rain
#define	LENS_WET_RUN_SHAPE	float2( 1.4, 0.3 )	// the runlets: the map squeezed across, drawn out down the glass
#define	LENS_WET_RUN_SPEED	0.06				// their crawl: drawn-out tiles a rain-clock second (3600 times it a whole number,
												// or they jump as the clock wraps)
#define	LENS_WET_ROUGH		0.05				// the water's GGX roughness: sharper than any coating
#define	LENS_WET_F0			0.02				// water's Fresnel looking straight in
#define	LENS_WET_SPEC		1.0					// the highlights of the sun and the lamps on the water
#define	LENS_WET_ENV		2.0					// the world the water mirrors
#define	LENS_WET_FILM		0.25				// the film over the whole soaked glass: its share of a drop's look
#define	LENS_WET_SHADE		1.5					// a clear glass (a collimator's): how dark a drop's steep rim shows against the view
												// behind -- it bends the view away there (0 = not at all)

struct	lens_wet_t
{
	float3	n;			// the glass's normal through the water, facing the eye
	float	m;			// how much water lies there, 0..1
};

// The drops at p (the world position) on the glass of normal ng (facing the eye), obj: the model's x, y there.
lens_wet_t	lens_wet( float2 obj, float3 p, float3 ng )
{
	float2	uv		= obj * LENS_WET_TILE;

	// The map's frame on the glass: the gradients of its u and v across it, from the screen's derivatives -- the
	// glass's own tangents are the atlas's, not these
	float3	dp1		= ddx( p );
	float3	dp2		= ddy( p );
	float2	du1		= ddx( uv );
	float2	du2		= ddy( uv );
	float3	c1		= cross( dp2, ng );
	float3	c2		= cross( ng, dp1 );
	float3	gu		= c1 * du1.x + c2 * du2.x;
	float3	gv		= c1 * du1.y + c2 * du2.y;
	float	s		= ( dot(ng, cross(dp1, dp2)) < 0 ? -1 : 1 ) * rsqrt( max(max(dot(gu, gu), dot(gv, gv)), 1e-20) );
	gu				*= s;
	gv				*= s;

	// the drops sitting still: the ones the wetness lets in, each living LENS_WET_LIFE -- landing at once, drying
	// over the last fifth of it -- each at a time of its own
	float4	d		= s_wet.Sample( smp_base, uv );
	float	on		= saturate( (m_lens_wet.x - d.a) * 16 );
	float	life	= frac( m_lens_wet.y / LENS_WET_LIFE + d.a * 37.1 );
	on				*= saturate( life * 40 ) * saturate( (1 - life) * 5 );

	// the runlets: the map drawn out down the glass (the model's -y) and crawling down it, while the rain falls
	float2	q		= uv * LENS_WET_RUN_SHAPE + float2( 0.37, m_lens_wet.y * LENS_WET_RUN_SPEED );
	float4	r		= s_wet.Sample( smp_base, q );
	float	ron		= saturate( (m_lens_wet.z * LENS_WET_RUN - r.a) * 16 );

	float2	nxy		= ( (d.rg * 2 - 1) * on + (r.rg * 2 - 1) * LENS_WET_RUN_SHAPE * ron ) * LENS_WET_BUMP;
	float	nz		= sqrt( saturate(1 - dot(nxy, nxy)) );

	lens_wet_t	w;
	w.n				= normalize( ng * nz + gu * nxy.x + gv * nxy.y );
	w.m				= max( saturate(d.b * on + r.b * ron), LENS_WET_FILM * m_lens_wet.x );
	return	w;
}

// The water's own look (v: from the eye; w: lens_wet's): the world it mirrors (collim_env) and the GGX highlights of the
// sun and of the lamps near the eye -- as much of theirs as the glass's own take (m_collim_spec.w) -- in their own
// colours, by water's Fresnel.
float3	lens_water( float3 p, float3 v, lens_wet_t w )
{
	float3	r		= reflect( v, w.n );
	float	dr		= max( length(ddx(r)), length(ddy(r)) );
	float	f		= LENS_WET_F0 + ( 1 - LENS_WET_F0 ) * pow( 1 - saturate(dot(-v, w.n)), 5 );
	float3	hl		= L_sun_color * L_material.y * collim_ggx( w.n, v, -L_sun_dir_w, LENS_WET_ROUGH )
					+ collim_lamps_spec( p, w.n, v, LENS_WET_ROUGH ) * m_collim_spec.w;
	float3	env		= collim_env( r, LENS_WET_ROUGH, dr );
	return	( hl * LENS_WET_SPEC + env * LENS_WET_ENV ) * f * w.m;
}


// On a clear glass (a collimator's), how much of the view behind a drop hides -- as alpha: its steep rim bends the
// view away from the eye and shows dark, its flat middle lets it through
float	lens_wet_shade( lens_wet_t w, float3 ng )
{
	return	saturate( LENS_WET_SHADE * (1 - dot(w.n, ng)) );
}

#endif
