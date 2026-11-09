#ifndef LENS_WET_H
#define LENS_WET_H

// The rain on an optic's outer glass and on a collimator's (model_sniperlens.ps, model_collim_glass.ps; after
// collim.h) -- r3's lens_wet.h, see there. R2 has no cube of the world and no GGX in collim.h: the water mirrors the weather's sky where the glass looks at open sky,
// and its highlights are GGX of its own here.
//
//   m_lens_wet   x = how wet the glass is, 0 dry .. 1 soaked: the drops whose id is under it are there;
//                y = the rain's clock, seconds (runs only while the rain falls on the glass, as fast as it is heavy;
//                wraps at IGame_Persistent::HUD_LENS_WET_WRAP, 3600); z = the rain falling on it now, 0..1;
//                w = the held weapon's aim, 0..1 (a collimator's drops fade by LENS_WET_COLLIM_AIM in it).
//                CActor; the hud's glass only.
uniform	float4		m_lens_wet;
uniform sampler2D	s_wet;		// wpn\lenses_rain: rg = the drop's normal xy (*0.5+0.5), b = the water's depth, a = the drop's id (1 = none)
								// -- one id over the whole drop and a texel round it (r3's lens_wet.h)

#define	LENS_WET_TILE		30.0				// the map's repeats a metre of the model: a tile 3.3 cm across
#define	LENS_WET_BUMP		1.0					// how steep the drops' sides are (1 = as the map has them)
#define	LENS_WET_CYCLE		5.0					// rain-clock seconds from a drop's landing to the next at its place (3600
												// times a whole number, or they jump as the clock wraps)...
#define	LENS_WET_LIFE		float2( 2.5, 3.75 )	// ...of which it lives these (each drop its own): it lands, sits, then dries
												// or runs off, and the glass stays dry there the rest -- 1.25..2.5 s
#define	LENS_WET_COLLIM_AIM	0.5					// how much of a collimator's drops fade as the eye comes to it: in the aim
												// they sit right before it, and over the whole sight's glass
#define	LENS_WET_COLLIM		( 1 - LENS_WET_COLLIM_AIM * m_lens_wet.w )	// lens_wet's k on a collimator
#define	LENS_WET_RUN		0.2					// the share of the drops that run, in a heavy rain
#define	LENS_WET_RUN_SHAPE	float2( 1.4, 0.3 )	// the runlets: the map squeezed across, drawn out down the glass
#define	LENS_WET_RUN_SPEED	0.06				// their crawl: drawn-out tiles a rain-clock second (3600 times it a whole number,
												// or they jump as the clock wraps) -- 6.7 mm of the glass
#define	LENS_WET_RUN_CYCLE	4.0					// rain-clock seconds from a runlet's start to the next of it (3600 times...)
#define	LENS_WET_RUN_LIFE	float2( 1.5, 2.5 )	// ...of which it runs these (each its own), fading out over the last two
												// thirds: gone 1..1.7 cm down the glass, before its edge
#define	LENS_WET_ROUGH		0.05				// the water's GGX roughness: sharper than any coating
#define	LENS_WET_F0			0.02				// water's Fresnel looking straight in
#define	LENS_WET_SPEC		1.0					// the highlights of the sun and the lamps on the water
#define	LENS_WET_ENV		2.0					// the sky the water mirrors
#define	LENS_WET_SKY_LOD	3.0					// ...as soft as the sky cube's level 3
#define	LENS_WET_FILM		0.25				// the film over the whole soaked glass: its share of a drop's look
#define	LENS_WET_SHADE		1.5					// a clear glass (a collimator's): how dark a drop's steep rim shows against the view
												// behind -- it bends the view away there (0 = not at all)

struct	lens_wet_t
{
	float3	n;			// the glass's normal through the water, facing the eye
	float	m;			// how much water lies there, 0..1
};

// The drops at p (the world position) on the glass of normal ng (facing the eye), obj: the model's x, y there; k: how
// strong they are (1 on an optic's glass, LENS_WET_COLLIM on a collimator's).
lens_wet_t	lens_wet( float2 obj, float3 p, float3 ng, float k )
{
	float2	uv		= obj * LENS_WET_TILE;

	// the map's frame on the glass: the gradients of its u and v across it, from the screen's derivatives
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

	// the drops sitting still, each landing once a LENS_WET_CYCLE at a time of its own and living its LENS_WET_LIFE
	float4	d		= tex2D( s_wet, uv );
	float	on		= saturate( (m_lens_wet.x - d.a) * 16 );
	float	t		= frac( m_lens_wet.y / LENS_WET_CYCLE + d.a * 37.1 ) * LENS_WET_CYCLE;
	float	life	= t / lerp( LENS_WET_LIFE.x, LENS_WET_LIFE.y, frac(d.a * 91.7) );	// 0..1, then over 1 dry
	on				*= saturate( life * 40 ) * saturate( (1 - life) * 5 );

	// the runlets, crawling down the glass (the model's -y) while the rain falls, each starting once a
	// LENS_WET_RUN_CYCLE at a time of its own and fading out as it runs
	float2	q		= uv * LENS_WET_RUN_SHAPE + float2( 0.37, m_lens_wet.y * LENS_WET_RUN_SPEED );
	float4	r		= tex2D( s_wet, q );
	float	ron		= saturate( (m_lens_wet.z * LENS_WET_RUN - r.a) * 16 );
	float	rt		= frac( m_lens_wet.y / LENS_WET_RUN_CYCLE + r.a * 29.3 ) * LENS_WET_RUN_CYCLE;
	float	run		= rt / lerp( LENS_WET_RUN_LIFE.x, LENS_WET_RUN_LIFE.y, frac(r.a * 71.3) );	// 0..1, then over 1 gone
	ron				*= saturate( run * 40 ) * saturate( (1 - run) * 1.5 );

	float2	nxy		= ( (d.rg * 2 - 1) * on + (r.rg * 2 - 1) * LENS_WET_RUN_SHAPE * ron ) * LENS_WET_BUMP * k;
	float	nz		= sqrt( saturate(1 - dot(nxy, nxy)) );

	lens_wet_t	w;
	w.n				= normalize( ng * nz + gu * nxy.x + gv * nxy.y );
	w.m				= max( saturate(d.b * on + r.b * ron), LENS_WET_FILM * m_lens_wet.x ) * k;
	return	w;
}

// GGX, as r3's collim_ggx: D * visibility * N.L, times pi
float	lens_wet_ggx( float3 n, float3 v, float3 l )
{
	float	a		= LENS_WET_ROUGH;
	float3	h		= normalize( l - v );
	float	nh		= saturate( dot(n, h) );
	float	nl		= saturate( dot(n, l) );
	float	nv		= max( dot(n, -v), 0.001 );
	float	a2		= a * a;
	float	d		= nh * nh * (a2 - 1) + 1;
	float	k		= a * 0.5;
	return	a2 / ( d * d ) * 0.25 / ( (nl * (1 - k) + k) * (nv * (1 - k) + k) ) * nl;
}

// The water's own look (v: from the eye; w: lens_wet's): the sky it mirrors where the glass looks at open sky, and the
// highlights of the sun and of the lamps near the eye -- as much of theirs as the glass's own take (m_collim_spec.w) --
// by water's Fresnel.
float3	lens_water( float3 p, float3 v, lens_wet_t w )
{
	float3	r		= reflect( v, w.n );
	float	f		= LENS_WET_F0 + ( 1 - LENS_WET_F0 ) * pow( 1 - saturate(dot(-v, w.n)), 5 );
	float3	hl		= L_sun_color * L_material.y * lens_wet_ggx( w.n, v, -L_sun_dir_w );
	[loop]
	for ( int i = 0; i < COLLIM_LIGHTS; ++i )
	{
		float3	l;
		float3	c		= collim_lamp( i, p, l );
		hl				+= c * lens_wet_ggx( w.n, v, l ) * m_collim_spec.w;
	}
	float3	hc		= ( r < 0 ) ? m_collim_hemi_neg.xyz : m_collim_hemi_pos.xyz;
	float	vis		= saturate( dot(hc, abs(r)) );
	float3	sky		= m_sky_params.rgb * collim_sky( sky_tc(r), exp2(LENS_WET_SKY_LOD) / COLLIM_SKY_N, LENS_WET_SKY_LOD ) * vis;
	return	( hl * LENS_WET_SPEC + sky * LENS_WET_ENV ) * f * w.m;
}


// On a clear glass (a collimator's), how much of the view behind a drop hides -- as alpha: its steep rim bends the
// view away from the eye and shows dark, its flat middle lets it through
float	lens_wet_shade( lens_wet_t w, float3 ng )
{
	return	saturate( LENS_WET_SHADE * (1 - dot(w.n, ng)) );
}

#endif
