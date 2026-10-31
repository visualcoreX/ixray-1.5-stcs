#ifndef	DISTORT_COMBINE_H
#define	DISTORT_COMBINE_H
// Air distortion in the combine (combine_2_NAA / combine_2_AA) -- the DX9 half of
// shaders\r3\distort_combine.h, see the comments there. The mask: xy = screen offset biased to
// 127/255, z = blur, a = coverage (cleared to 0; every distorting draw ADDS its alpha to it --
// alpha_blend(one,one) in their scripts -- while lerping xy by the same alpha; water leaves it alone,
// alpha_blend(zero,one)). s_distort is linear-filtered (r_Sampler_clf).

#ifndef	EPSDEPTH
#define	EPSDEPTH	0.001
#endif

uniform sampler2D 	s_distort;
uniform float4		screen_res;	// xy = size, zw = 1/size

// 8-bit mask through a 3x3 tent (four bilinear taps on the pixel's corners): 1/16-step precision,
// so a fading haze ramps out instead of breaking into terraces with hard 0.4 px edges.
float4	distort_smooth	(float2 tc)
{
	float2	d	= 0.5f*screen_res.zw;
	return	0.25f*(	tex2D(s_distort, tc + float2(-d.x,-d.y))
				+	tex2D(s_distort, tc + float2( d.x,-d.y))
				+	tex2D(s_distort, tc + float2(-d.x, d.y))
				+	tex2D(s_distort, tc + float2( d.x, d.y)) );
}

// the centre of the pixel tc is in, so the linear filter of s_distort mixes in no neighbour
float2	distort_texel	(float2 tc)
{
	return	(floor(tc*screen_res.xy) + 0.5f)*screen_res.zw;
}

// On R2 FXAA/SMAA have run on the picture before this combine, so a pixel beside the hud weapon's edge
// is already part weapon colour while the position buffer and the mask there still say background:
// judged by that pixel alone it was free to be dragged into the haze, and a shifted copy of the
// weapon's outline hung beside it. So both are read over it and its four neighbours, and the worst
// case wins: the nearest surface (0 = sky, never in front), and "covered" only if all five are.
float	distort_depth	(float2 tc)
{
	float2	c	= distort_texel(tc);
	float2	px	= screen_res.zw;
	float	z[5];
	z[0]	= tex2Dlod(s_position, float4(c,                  0, 0)).z;
	z[1]	= tex2Dlod(s_position, float4(c + float2( px.x, 0), 0, 0)).z;
	z[2]	= tex2Dlod(s_position, float4(c + float2(-px.x, 0), 0, 0)).z;
	z[3]	= tex2Dlod(s_position, float4(c + float2(0,  px.y), 0, 0)).z;
	z[4]	= tex2Dlod(s_position, float4(c + float2(0, -px.y), 0, 0)).z;
	float	d	= 0;
	[unroll] for (int i=0; i<5; i++)
		if (z[i]>EPSDEPTH)	d	= (d>EPSDEPTH) ? min(d, z[i]) : z[i];
	return	d;
}

// Was a haze drawn over this point at all? Only a surface in FRONT of every haze (the hud weapon, a
// pole before an anomaly) hid it from the depth-tested mask pass.
bool	distort_covered	(float2 tc)
{
	float2	c	= distort_texel(tc);
	float2	px	= screen_res.zw;
	float	a	= tex2Dlod(s_distort, float4(c, 0, 0)).a;
	a	= min(a, tex2Dlod(s_distort, float4(c + float2( px.x, 0), 0, 0)).a);
	a	= min(a, tex2Dlod(s_distort, float4(c + float2(-px.x, 0), 0, 0)).a);
	a	= min(a, tex2Dlod(s_distort, float4(c + float2(0,  px.y), 0, 0)).a);
	a	= min(a, tex2Dlod(s_distort, float4(c + float2(0, -px.y), 0, 0)).a);
	return	a>=(0.5f/255.0f);
}

// A surface at tc nearer than `depth`; a sky pixel (depth 0) is behind everything.
bool	distort_nearer	(float2 tc, float depth)
{
	float	d	= distort_depth(tc);
	float	far	= (depth>EPSDEPTH) ? depth : 1e9f;
	return	d>EPSDEPTH && (d+EPSDEPTH)<far;
}

// In front of the haze: nearer than the pixel and no haze drawn over it.
bool	distort_blocked	(float2 tc, float depth)
{
	return	distort_nearer(tc, depth) && !distort_covered(tc);
}

#define	DISTORT_FADE	2.0f	// how many offsets ahead a surface in front of the haze starts fading it
#define	DISTORT_STEP	3.0f	// px between the probes along the offset
#define	DISTORT_PROBES	8		// at most, so a long offset is probed more sparsely

// Where the pixel at tc (scene depth `depth`, mask coverage `cover`) takes its picture from. A surface
// in front of the haze fades the offset out before it is reached (the pixel moves by D/DISTORT_FADE
// with the surface D away along it), so there is no band round the weapon with a seam; the surface is
// found by probing the path every few pixels, as its far end alone can lie past a thin part of the
// weapon, and a picture point the probes stepped over keeps the pixel's own. A nearer surface under
// the haze may be pulled in, held only to what the hazes can shift (0.71*a: beyond is water's ripple
// under them). See shaders\r3\distort_combine.h. Real loops: unrolled, the combine ran past ps_3_0's
// guaranteed 512 slots.
float2	distort_center	(float2 tc, float2 offset, float depth, float cover)
{
	float2	shift	= offset;
	float	path	= length(offset*screen_res.xy)*DISTORT_FADE;	// px
	[branch] if (path>0.5f)
	{
		float	n	= clamp(ceil(path/DISTORT_STEP), 1, DISTORT_PROBES);
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
			[loop] for (int j=0; j<3; j++)
			{
				float	mid	= 0.5f*(lo+hi);
				if (distort_blocked(tc + offset*mid, depth))	hi	= mid;
				else											lo	= mid;
			}
		}
		shift	= offset*(lo/DISTORT_FADE);
	}
	[branch] if (distort_nearer(tc + shift, depth))
	{
		[branch] if (!distort_covered(tc + shift))	shift	= 0;
		else
		{
			float	reach	= 0.71f*def_distort*cover;
			float	len		= length(shift);
			if (len>reach)	shift	*= reach/len;
		}
	}
	return	tc + shift;
}
#endif	//	DISTORT_COMBINE_H
