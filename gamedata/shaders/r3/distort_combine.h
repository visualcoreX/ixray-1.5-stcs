#ifndef	DISTORT_COMBINE_H
#define	DISTORT_COMBINE_H
// Air distortion in the combine (combine_2_NAA / combine_2_AA): reading the mask and deciding where
// the offset may take a pixel. The mask: xy = screen offset biased to 127/255, z = blur, a = coverage
// (cleared to 0; every distorting draw ADDS its alpha to it -- alpha_blend(one,one) in their scripts --
// while lerping xy by the same alpha; water writes no alpha). So a > 0 says a haze was drawn over the
// pixel at all, however faint, and |xy - neutral| <= 0.5*a wherever only hazes put it there.

#ifndef	EPSDEPTH
#define	EPSDEPTH	0.001
#endif

#ifndef USE_MSAA
Texture2D 							s_distort;
#else
TEXTURE2DMS(float4, MSAA_SAMPLES)	s_distort;
#endif

float2	distort_dims	()
{
	float	w, h;
#ifndef USE_MSAA
	s_distort.GetDimensions(w, h);
#else
	float	n;	s_distort.GetDimensions(w, h, n);
#endif
	return	float2(w, h);
}

int2	distort_pixel	(float2 tc)
{
	float2	dim	= distort_dims();
	return	clamp(int2(tc*dim), int2(0,0), int2(dim)-1);
}

// The mask is 8-bit: one step of it is 0.05/255 of the screen, ~0.4 px at 1920. While a haze fades out
// only a few steps are left, and its smooth noise turns into flat terraces with hard 0.4 px edges --
// stripes just before it is gone. Reading it through a 3x3 tent (four bilinear taps on the pixel's
// corners) gives 1/16-step precision and ramps those edges over a couple of pixels.
float4	distort_smooth	(float2 tc)
{
#ifndef USE_MSAA
	float2	d	= 0.5/distort_dims();
	return	0.25*(	s_distort.Sample(smp_rtlinear, tc + float2(-d.x,-d.y))
				+	s_distort.Sample(smp_rtlinear, tc + float2( d.x,-d.y))
				+	s_distort.Sample(smp_rtlinear, tc + float2(-d.x, d.y))
				+	s_distort.Sample(smp_rtlinear, tc + float2( d.x, d.y)) );
#else // USE_MSAA
	// no filtering on a multisampled target: the same tent by hand, clamped to the screen
	int2	dim	= int2(distort_dims());
	int2	p	= distort_pixel(tc);
	float4	sum	= 0;
	[unroll] for (int y=-1; y<=1; y++)
	[unroll] for (int x=-1; x<=1; x++)
		sum	+= s_distort.Load(clamp(p+int2(x,y), int2(0,0), dim-1), 0) * ((2-abs(x))*(2-abs(y)));
	return	sum/16;
#endif // USE_MSAA
}

// Was a haze drawn over this point at all? Only a surface in FRONT of every haze (the hud weapon, a
// pole before an anomaly) hid it from the depth-tested mask pass. Under MSAA only if over every
// sample, for the same reason as distort_depth: a weapon edge pixel is not under the haze.
bool	distort_covered	(float2 tc)
{
#ifndef USE_MSAA
	float	a	= s_distort.SampleLevel(smp_nofilter, tc, 0).a;
#else
	int2	p	= distort_pixel(tc);
	float	a	= 1;
	[unroll] for (int s=0; s<MSAA_SAMPLES; s++)
		a	= min(a, s_distort.Load(p, s).a);
#endif
	return	a>=(0.5/255.0);
}

// The nearest surface at tc (0 = sky, which is never in front). Under MSAA over ALL the samples: the
// combine reads the RESOLVED picture, where a pixel on the hud weapon's edge is part weapon colour,
// while its sample 0 can be the background behind -- judged by that sample alone it was free to be
// dragged into the haze, and a shifted copy of the weapon's outline hung beside it.
float	distort_depth	(float2 tc)
{
#ifndef USE_MSAA
	return	s_position.SampleLevel(smp_nofilter, tc, 0).z;
#else
	int2	p	= distort_pixel(tc);
	float	d	= 0;
	[unroll] for (int s=0; s<MSAA_SAMPLES; s++)
	{
		float	z	= s_position.Load(p, s).z;
		if (z>EPSDEPTH)	d	= (d>EPSDEPTH) ? min(d, z) : z;
	}
	return	d;
#endif
}

// A surface at tc nearer than `depth`. A sky pixel (depth 0) is behind everything: with the bare
// test nothing was ever nearer than it, and a sky pixel by the hud weapon took the weapon in.
bool	distort_nearer	(float2 tc, float depth)
{
	float	d	= distort_depth(tc);
	float	far	= (depth>EPSDEPTH) ? depth : 1e9;
	return	d>EPSDEPTH && (d+EPSDEPTH)<far;
}

// In front of the haze: nearer than the pixel and no haze drawn over it.
bool	distort_blocked	(float2 tc, float depth)
{
	return	distort_nearer(tc, depth) && !distort_covered(tc);
}

#define	DISTORT_FADE	2.0		// how many offsets ahead a surface in front of the haze starts fading it
#define	DISTORT_STEP	3.0		// px between the probes along the offset
#define	DISTORT_PROBES	8		// at most, so a long offset is probed more sparsely

// Where the pixel at tc (scene depth `depth`, mask coverage `cover`) takes its picture from.
//
// A surface in front of the haze must not be pulled into it. Dropping such an offset whole left a band
// round the hud weapon as wide as the offset, undistorted next to fully distorted surroundings; cutting
// it back to the silhouette left the same band stretched off one edge line. Either way a piece of the
// effect on its own, with a seam round it. So the offset fades out BEFORE it reaches the surface: with
// the surface D away along it (binary search up to DISTORT_FADE offsets), the pixel moves by
// D/DISTORT_FADE -- the background there is only squeezed against the silhouette, and still ripples,
// and nothing jumps where the fade begins.
//
// A nearer surface the haze is drawn over (a bush in it, a tree behind it) is fair game, as a farther
// one always is: refusing it -- as the old test did for a faint haze, whose a^2 coverage read as none --
// let a leaf take the background from a gap but never a gap the leaf, so the foliage thinned out where
// the offset was large and not where it crossed zero: light and dark rings on it. It is only held to
// what the hazes over this pixel can shift at all (0.5*a per axis, so 0.71*a along a diagonal):
// beyond that the offset is water's ripple under them, which does not move what stands in front of
// the water -- and it fades out with the haze.
//
// The surface is found by probing the offset's path every few pixels, not by its far end alone: the
// end could lie past a thin part of the weapon (the barrel, a sight, a finger), and the pixel then
// took the full offset into that part -- scattered dots of the weapon all round it. Whatever the
// probes miss, a pixel whose final picture point is still in front of the haze keeps its own.
float2	distort_center	(float2 tc, float2 offset, float depth, float cover)
{
	float2	shift	= offset;
	float	path	= length(offset*distort_dims())*DISTORT_FADE;	// px
	[branch] if (path>0.5)
	{
		int		n	= clamp(int(ceil(path/DISTORT_STEP)), 1, DISTORT_PROBES);
		float	dt	= DISTORT_FADE/n;
		float	lo	= 0, hi	= 0;	// free up to lo; hi>0: blocked at hi
		[loop] for (int i=1; i<=DISTORT_PROBES; i++)
		{
			if (i>n)	break;
			[branch] if (distort_blocked(tc + offset*(dt*i), depth))	{ hi	= dt*i; break; }
			lo	= dt*i;
		}
		[branch] if (hi>0)
		{
			[unroll] for (int j=0; j<3; j++)
			{
				float	mid	= 0.5*(lo+hi);
				if (distort_blocked(tc + offset*mid, depth))	hi	= mid;
				else											lo	= mid;
			}
		}
		shift	= offset*(lo/DISTORT_FADE);
	}
	[branch] if (distort_nearer(tc + shift, depth))
	{
		[branch] if (!distort_covered(tc + shift))	shift	= 0;	// a part of the weapon the probes stepped over
		else
		{
			float	reach	= 0.71*def_distort*cover;
			float	len		= length(shift);
			if (len>reach)	shift	*= reach/len;
		}
	}
	return	tc + shift;
}
#endif	//	DISTORT_COMBINE_H
