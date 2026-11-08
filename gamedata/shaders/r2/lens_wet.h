#ifndef LENS_WET_H
#define LENS_WET_H

// The rain on an optic's outer glass (model_sniperlens.ps; after collim.h) -- r3's lens_wet.h, see there. R2 has no
// cube of the world and no GGX in collim.h: the water mirrors the weather's sky where the glass looks at open sky,
// and its highlights are GGX of its own here.
//
//   m_lens_wet   x = how wet the glass is, 0 dry .. 1 soaked: the drops whose id is under it are there;
//                y = the rain's clock, seconds (runs only while the rain falls on the glass, as fast as it is heavy;
//                wraps at IGame_Persistent::HUD_LENS_WET_WRAP, 3600); z = the rain falling on it now, 0..1.
//                CActor; the hud's glass only.
uniform	float4		m_lens_wet;
uniform sampler2D	s_wet;		// wpn\lenses_rain: rg = the drop's normal xy (*0.5+0.5), b = the water's depth, a = the drop's id (1 = none)

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
#define	LENS_WET_ENV		2.0					// the sky the water mirrors
#define	LENS_WET_SKY_LOD	3.0					// ...as soft as the sky cube's level 3
#define	LENS_WET_FILM		0.25				// the film over the whole soaked glass: its share of a drop's look

struct	lens_wet_t
{
	float3	n;			// the glass's normal through the water, facing the eye
	float	m;			// how much water lies there, 0..1
};

// The drops at p (the world position) on the glass of normal ng (facing the eye), obj: the model's x, y there.
lens_wet_t	lens_wet( float2 obj, float3 p, float3 ng )
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

	// the drops sitting still, each living LENS_WET_LIFE at a time of its own
	float4	d		= tex2D( s_wet, uv );
	float	on		= saturate( (m_lens_wet.x - d.a) * 16 );
	float	life	= frac( m_lens_wet.y / LENS_WET_LIFE + d.a * 37.1 );
	on				*= saturate( life * 40 ) * saturate( (1 - life) * 5 );

	// the runlets, crawling down the glass (the model's -y) while the rain falls
	float2	q		= uv * LENS_WET_RUN_SHAPE + float2( 0.37, m_lens_wet.y * LENS_WET_RUN_SPEED );
	float4	r		= tex2D( s_wet, q );
	float	ron		= saturate( (m_lens_wet.z * LENS_WET_RUN - r.a) * 16 );

	float2	nxy		= ( (d.rg * 2 - 1) * on + (r.rg * 2 - 1) * LENS_WET_RUN_SHAPE * ron ) * LENS_WET_BUMP;
	float	nz		= sqrt( saturate(1 - dot(nxy, nxy)) );

	lens_wet_t	w;
	w.n				= normalize( ng * nz + gu * nxy.x + gv * nxy.y );
	w.m				= max( saturate(d.b * on + r.b * ron), LENS_WET_FILM * m_lens_wet.x );
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


#endif
