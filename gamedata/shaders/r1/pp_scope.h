// The 2D-scope eyepiece for R1: the same helpers as r2/postprocess.ps (zoom inside the glass, night-
// vision mask, scope shadow, aberration), shared by postprocess.ps and postprocess_d.ps. Include it
// after s_base0/s_base1 are declared. The game fills m_pp_mask/m_pp_zoom/m_pp_shadow every frame.
#ifndef R1_PP_SCOPE_H
#define R1_PP_SCOPE_H

// Scope night vision must stay INSIDE the optic: the tint comes from a full-screen post-process
// effector, but with a 2D scope on screen the world is only visible through the round eyepiece and
// the green used to spill over the black surround too. The game hands the eyepiece over in
// m_pp_mask -- xy = centre, zw = x/y radii in screen uv (two radii, so the circle stays round on any
// aspect). Radius 0 = nothing wants a mask and the whole screen is graded, exactly as before.
uniform float4 m_pp_mask;

// Digital magnification inside the same eyepiece circle. xy = its radii in
// screen uv, z = the factor (<= 1 = off). This is an upscale of the image that was already rendered
// -- it reveals no detail the camera did not draw -- so the game caps the factor; a truthful zoom
// needs the second scene render the 3D lens does.
uniform float4 m_pp_zoom;
uniform float4 m_pp_shadow;
#define PP_DIST_R0  0.45f		// the middle of the glass this distortion does not touch at all
#define PP_REST_RIM 0.875f		// where the rim shading sits with the eye centred (1.0 = none at all)

float2 pp_zoom_uv(float2 uv)
{
	if (m_pp_zoom.x <= 0.0f)							return uv;
	float2 d = (uv - 0.5f) / m_pp_zoom.xy;				// 0 at the centre, 1 at the rim of the glass
	float  r2 = dot(d, d);
	if (r2 > 1.0f)										return uv;	// outside the glass: as rendered
	// LENS DISTORTION -- IN THE OUTER RING ONLY, AND OUTWARDS. Real glass leaves the middle of the
	// field alone (that is the part you aim with: no zoom of its own, no offset, nothing to soften it)
	// and pulls the EDGES out -- the rim samples from further IN, so what sits out there is stretched.
	// The old 1 + w*r2 did the opposite: it sampled further OUT everywhere, squeezing the image into
	// the rim from the centre onwards. Flat until PP_DIST_R0 of the glass radius, then a squared ramp,
	// so the untouched middle and the bent ring meet with no seam.
	// m_pp_shadow.w carries the strength (0 = a flat lens).
	float  t = saturate((sqrt(r2) - PP_DIST_R0) / (1.0f - PP_DIST_R0));
	float  k = 1.0f - m_pp_shadow.w * t * t;
	float  z = max(m_pp_zoom.z, 1.0f);
	return 0.5f + (uv - 0.5f) * k / z;
}

// Chromatic aberration of the optic, the same shape the 3D lens uses (model_scope_lense.ps): red and
// blue are sampled a touch apart, by an offset that grows towards the rim of the glass. m_pp_zoom.w
// carries the scope's own scope_abberation, so a scope reads the same in 2D as it does in the lens.
float2 pp_abber_offset(float2 uv)
{
	if (m_pp_zoom.w <= 0.0001f || m_pp_zoom.x <= 0.0f)	return float2(0.0f, 0.0f);
	float r01 = length((uv - 0.5f) / m_pp_zoom.xy);		// 0 at the centre, 1 at the rim
	if (r01 > 1.0f)										return float2(0.0f, 0.0f);
	// NOT divided by the eyepiece zoom. The offset is in source uv, so the eyepiece magnifies this smear
	// along with the picture -- dividing it out did match the 3D lens exactly, but it left the flat scope
	// with almost no visible fringe at all, which is not what the effect is here for. The lens is the
	// reference for MAGNIFICATION, not for this.
	// Halved to match the toned-down separation the 3D lens uses (model_scope_lense.ps) -- the full
	// offset read as too strong a colour split once the lens side was tuned down.
	return m_pp_zoom.w * m_pp_zoom.y * r01 * 0.5f;
}

// Sampling for the digital magnification. Stretching an already-rendered image with the hardware's
// bilinear filter is what makes the staircases: between two texels it ramps LINEARLY, so the grid of
// the source pixels stays visible as facets, and every aliased edge in the scene is magnified along
// with it. Averaging the neighbourhood hides that but defocuses the whole eyepiece, and an edge
// filter (FXAA) misfires on foliage. Catmull-Rom is the purpose-built answer: a smooth cubic ramp
// through the same texels, so edges curve instead of stepping, and flat areas stay exactly as sharp.
// Five bilinear taps (the standard corner-skipping form), and only inside a magnified eyepiece.
// screen_res (x,y = resolution, z,w = 1/resolution) is declared by shared/common.h here

// SCOPE SHADOW. An optic only shows its full circle while the eye sits on the axis; move off it and
// the exit pupil drifts, cutting a dark crescent out of the far side of the picture. m_pp_shadow.xy
// is that drift (in eyepiece radii, published by the weapon from how fast the aim is swinging) and .z
// how soft the crescent's edge is. Nothing to compute when the eye is centred.

float pp_scope_shadow(float2 uv)		// 1 = lit, 0 = in the shadow
{
	if (m_pp_shadow.z <= 0.0f || m_pp_zoom.x <= 0.0f)	return 1.0f;
	// ONLY inside the glass. Past its rim the optic's own body is drawn over the top, and darkening
	// there put a vignette across the whole screen instead of a crescent in the eyepiece.
	if (length((uv - 0.5f) / m_pp_zoom.xy) > 1.0f)		return 1.0f;
	float2 pupil = 0.5f + m_pp_shadow.xy * m_pp_zoom.xy;	// where the eye is looking from
	// Two things set the pupil's size. The mask circle is deliberately WIDER than the glass it covers
	// (its edge is meant to hide under the scope body), so the shadow works against the glass itself:
	// PP_GLASS_K of the mask radius. And it is then widened by its own fade, so with the eye centred
	// the darkening begins right at the rim of the glass and a still player sees a clean circle.
	// m_pp_zoom.xy IS the glass. PP_REST_RIM pulls the darkening a touch inside it, so a whisper of
	// shading sits on the kerb even with the eye dead centred -- optics do that anyway, and without
	// it the crescent had nothing to grow OUT of: it snapped into being the moment anything moved.
	float2 rad   = m_pp_zoom.xy * PP_REST_RIM / (1.0f - m_pp_shadow.z);
	float  d     = length((uv - pupil) / rad);
	return 1.0f - smoothstep(1.0f - m_pp_shadow.z, 1.0f, d);
}

float pp_zoom_soft(float2 uv)	// 1 = this pixel is inside a magnified eyepiece
{
	if (m_pp_zoom.z <= 1.001f || m_pp_zoom.x <= 0.0f)	return 0.0f;
	if (length((uv - 0.5f) / m_pp_zoom.xy) > 1.0f)		return 0.0f;
	return 1.0f;
}

float3 pp_cubic0(float2 uv, float on)
{
	if (on <= 0.0f || screen_res.z <= 0.0f)		return tex2D(s_base0, uv).rgb;

	float2 tsize = screen_res.xy;
	float2 rcp   = screen_res.zw;
	float2 pos   = uv * tsize;
	float2 tc1   = floor(pos - 0.5f) + 0.5f;
	float2 f     = pos - tc1;

	float2 w0  = f * (-0.5f + f * (1.0f - 0.5f * f));
	float2 w1  = 1.0f + f * f * (-2.5f + 1.5f * f);
	float2 w2  = f * (0.5f + f * (2.0f - 1.5f * f));
	float2 w3  = f * f * (-0.5f + 0.5f * f);
	float2 w12 = w1 + w2;

	float2 p0  = (tc1 - 1.0f) * rcp;
	float2 p3  = (tc1 + 2.0f) * rcp;
	float2 p12 = (tc1 + w2 / w12) * rcp;

	float3 c = 0.0f;
	c += tex2D(s_base0, float2(p12.x, p0.y)).rgb * (w12.x * w0.y);
	c += tex2D(s_base0, float2(p0.x,  p12.y)).rgb * (w0.x  * w12.y);
	c += tex2D(s_base0, float2(p12.x, p12.y)).rgb * (w12.x * w12.y);
	c += tex2D(s_base0, float2(p3.x,  p12.y)).rgb * (w3.x  * w12.y);
	c += tex2D(s_base0, float2(p12.x, p3.y)).rgb * (w12.x * w3.y);
	float wsum = w12.x*w0.y + w0.x*w12.y + w12.x*w12.y + w3.x*w12.y + w12.x*w3.y;
	return c / wsum;
}

float3 pp_cubic1(float2 uv, float on)
{
	if (on <= 0.0f || screen_res.z <= 0.0f)		return tex2D(s_base1, uv).rgb;

	float2 tsize = screen_res.xy;
	float2 rcp   = screen_res.zw;
	float2 pos   = uv * tsize;
	float2 tc1   = floor(pos - 0.5f) + 0.5f;
	float2 f     = pos - tc1;

	float2 w0  = f * (-0.5f + f * (1.0f - 0.5f * f));
	float2 w1  = 1.0f + f * f * (-2.5f + 1.5f * f);
	float2 w2  = f * (0.5f + f * (2.0f - 1.5f * f));
	float2 w3  = f * f * (-0.5f + 0.5f * f);
	float2 w12 = w1 + w2;

	float2 p0  = (tc1 - 1.0f) * rcp;
	float2 p3  = (tc1 + 2.0f) * rcp;
	float2 p12 = (tc1 + w2 / w12) * rcp;

	float3 c = 0.0f;
	c += tex2D(s_base1, float2(p12.x, p0.y)).rgb * (w12.x * w0.y);
	c += tex2D(s_base1, float2(p0.x,  p12.y)).rgb * (w0.x  * w12.y);
	c += tex2D(s_base1, float2(p12.x, p12.y)).rgb * (w12.x * w12.y);
	c += tex2D(s_base1, float2(p3.x,  p12.y)).rgb * (w3.x  * w12.y);
	c += tex2D(s_base1, float2(p12.x, p3.y)).rgb * (w12.x * w3.y);
	float wsum = w12.x*w0.y + w0.x*w12.y + w12.x*w12.y + w3.x*w12.y + w12.x*w3.y;
	return c / wsum;
}

// The offset above is not where a second copy of the picture goes -- it is the FAR END of a smear.
// Glass spreads a point into a short spectrum, so red and blue arrive over a RANGE of positions with the
// green between them; taking one sample at each end is what made it read as a doubled image with hard
// coloured edges. Walk the offset instead and weight every tap by where its wavelength lands (red at
// +1, blue at -1, green centred at 0, a gaussian of PP_ABBER_SIGMA around each), and the same offset
// comes out as a soft colour blur that grows towards the rim -- all three channels softened alike, the
// same shape the 3D lens uses (model_scope_lense.ps) so the two scope modes read the same.
#define PP_ABBER_TAPS  5
#define PP_ABBER_SIGMA 0.6f
float3 pp_abber_rgb(float2 uv0, float2 uv1, float2 ab)
{
	float3 sum = 0.0f, wsum = 0.0f;
	[unroll] for (int i = 0; i < PP_ABBER_TAPS; ++i)
	{
		float  t = float(i) * (2.0f / float(PP_ABBER_TAPS - 1)) - 1.0f;		// -1 .. +1 along the offset
		float  dr = t - 1.0f, dg = t, db = t + 1.0f;
		float3 w = exp(-float3(dr*dr, dg*dg, db*db) * (0.5f / (PP_ABBER_SIGMA*PP_ABBER_SIGMA)));
		float3 c = (tex2D(s_base0, uv0 + ab*t).rgb + tex2D(s_base1, uv1 + ab*t).rgb) * 0.5f;
		sum  += c * w;
		wsum += w;
	}
	return sum / max(wsum, 0.0001f);
}

// Purple fringing (blue-leaning bloom), the same trick the 3D lens uses (model_scope_lense.ps): dark
// pixels sitting next to a bright neighbour get a blue/red-shifted, green-suppressed tint -- the way a
// real lens's chromatic smear reads once it blooms into the shadow beside a highlight, rather than the
// highlight itself just getting brighter. Only ever reached where pp_abber_offset already found the
// eyepiece and an aberration to draw, so it costs nothing on a scope with scope_abberation == 0.
float3 pp_sample_avg(float2 uv0, float2 uv1)
{
	float3 c0 = tex2D(s_base0, uv0).rgb;
	float3 c1 = tex2D(s_base1, uv1).rgb;
	return (c0 + c1) * 0.5f;
}

float3 pp_purple_fringe(float2 z_0, float2 z_1, float3 image)
{
	const float3 LUMA_W = float3(0.299f, 0.587f, 0.114f);
	const float2 glow_r = float2(0.002f, 0.0f);
	const float2 glow_u = float2(0.0f, 0.002f);

	float3 n0 = pp_sample_avg(z_0+glow_r, z_1+glow_r);
	float3 n1 = pp_sample_avg(z_0-glow_r, z_1-glow_r);
	float3 n2 = pp_sample_avg(z_0+glow_u, z_1+glow_u);
	float3 n3 = pp_sample_avg(z_0-glow_u, z_1-glow_u);
	float  neighbor_luma = max( max( dot(n0, LUMA_W), dot(n1, LUMA_W) ), max( dot(n2, LUMA_W), dot(n3, LUMA_W) ) );

	float luma          = dot(image, LUMA_W);
	float glow          = saturate((neighbor_luma-0.4f) * 3.5f);	// a bright source sits nearby
	float dark          = saturate(0.65f - luma);					// this pixel itself is dark
	float fringe_amount = 0.2f * glow * dark;

	image.r = saturate(image.r + fringe_amount*0.15f);
	image.g = image.g * (1.0f - fringe_amount*0.25f);
	image.b = saturate(image.b + fringe_amount*0.65f);
	return image;
}

// INTOXICATION FISHEYE (gunsl_peredoz.script -> level.set_intox_screen_fx -> CActor). m_pp_warp.x = how
// far the outer part of the screen is pulled (0 = off; it swells with the pulse), y = the strength of
// the colour swim, z = a slow wave rippling the same part, w = time in seconds. All three go through
// one mask (pp_warp_mask): the middle of the picture is left alone, the edges take it all.
uniform float4 m_pp_warp;
#define PP_WARP_R0 0.25f	// the mask: untouched inside this elliptical radius...
#define PP_WARP_R1 1.45f	// ...full strength past this one (1 = the middle of a screen edge)
// The MASK is an ellipse the shape of the screen (per-axis radius: 1 at the middle of each edge,
// 1.41 in a corner), after the reference the user drew. The fade is deliberately LONG and quintic
// (zero slope AND zero curvature at both ends): a short smoothstep(0.7, 1.3) left a visible line where
// the effect began. Now it creeps in from a quarter of the way out and the eye finds no border.
float pp_warp_mask(float2 uv)
{
	float  r	= length((uv - 0.5f) / 0.5f);
	float  x	= saturate((r - PP_WARP_R0) / (PP_WARP_R1 - PP_WARP_R0));
	return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

// The colour swim, as gunsl_peredoz.script drew it into the PPE: three sines at 0.07 Hz and its 1/1.5
// and 1.5 multiples, normalised so it wanders through the hues instead of pulsing brighter. Returns
// the colour_add equivalent; the caller doubles it like the pp mad does.
float3 pp_intox_colour()
{
	float  arg	= 6.2831853f * 0.07f * m_pp_warp.w;
	float3 c	= abs(float3(sin(arg), sin(arg / 1.5f), sin(arg * 1.5f)));
	float  m	= length(c) * 3.0f;
	return (m > 0.0f) ? c * (m_pp_warp.y / m) : float3(0.0f, 0.0f, 0.0f);
}

float2 pp_warp_uv(float2 uv)
{
	if (m_pp_warp.x <= 0.0001f)	return uv;
	float2 d	= uv - 0.5f;
	float  t	= pp_warp_mask(uv);
	float2 w	= 0.5f + d * (1.0f - m_pp_warp.x * t);
	// the wave: two slow sines across each other, fading in with the same ring, so the centre stays
	// steady and only the edges shimmer
	if (m_pp_warp.z > 0.0f)
		w += float2(sin(uv.y * 9.0f + m_pp_warp.w * 1.3f), sin(uv.x * 7.0f - m_pp_warp.w * 1.1f))
			 * (m_pp_warp.z * t);
	return saturate(w);
}

float pp_mask_factor(float2 uv)
{
	if (m_pp_mask.z <= 0.0f)	return 1.0f;
	float2 d = (uv - m_pp_mask.xy) / m_pp_mask.zw;
	return 1.0f - smoothstep(0.95f, 1.0f, length(d));	// the fade lands under the scope body
}

// 2D NIGHT SCOPE BRIGHTNESS. The tint comes from the scope's PPE, whose green gain is a u32 colour and
// clamps at 1.0 from the lowest brightness step up -- so the steps used to change only its noise. The
// game fills m_pp_nv only while a night scope's flat picture owns m_pp_mask:
//   x = a gain for the picture inside the eyepiece: 1 at the bottom step, pp_nv_2d_max_gain at the top
//       (never below 1 -- the dimmest setting is the picture the PPE alone gives);
//   y = the gauss's lens tint (model_scope_gauss.ps, m_zoom_deviation.z there): its night picture has no
//       gain, it lifts the image to a grey-blue by this much -- 0 at the bottom step leaves it untouched.
uniform float4 m_pp_nv;
float3 pp_nv_gain(float3 image)
{
	if (m_pp_mask.z <= 0.0f)	return image;
	if (m_pp_nv.z > 0.5f)
	{
		// chained with the suit's NV: the 3D night lens's formula (pnv.h) with z = the scope's brightness
		// (R1 has no ungraded copy here, so it lands on the graded picture); the gauss: its lens lift
		float z = m_pp_nv.x;
		if (m_pp_nv.y > 0.5f)
		{
			float g	= (image.r + image.g + image.b) * z * 7.0f;
			image	= image * (1.0f - z) + g;
			image.b	+= 0.4f * z;
		}
		else
			image	= z * float3(1.0f, 2.0f, 1.0f) * dot(image, float3(0.3f, 0.38f, 0.22f) * 5.0f);
		return image;
	}
	if (m_pp_nv.y > 0.0f)
	{
		float gray	= (image.r + image.g + image.b) * m_pp_nv.y * 7.0f;
		image		= image * (1.0f - m_pp_nv.y) + gray;
		image.b	   += 0.4f * m_pp_nv.y;
	}
	if (m_pp_nv.x > 1.0f)		image *= m_pp_nv.x;
	return image;
}


#endif
