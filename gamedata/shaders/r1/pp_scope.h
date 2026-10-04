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

// FATIGUE (CActor, from the stamina), as on R2/R3 but without the sharpening (49 taps a pixel is no
// price for the renderer the weak machines pick): m_pp_fatigue.x = desaturation towards the edges (and
// the sepia riding it), y = vignette (pulsing), w = brightening in the middle; z (the sharpening) is
// not read here.
uniform float4 m_pp_fatigue;
// the colour drained towards grey through pp_warp_mask (the middle keeps it, the edges lose
// m_pp_fatigue.x of it) and a touch of sepia, riding the same amount (PP_FATIGUE_SEPIA x the
// desaturation: 0.4 x 0.175 = 0.07 of sepia at full). PP_FATIGUE_SEPIA_CENTRE picks where: 1 = the
// middle (the inverse mask: the eye's own area warms while the edges go grey), 0 = the edges with the
// desaturation (the faded colour turns warm instead of just grey). Then the light moved outwards-in:
// the middle lifted by m_pp_fatigue.w and the corners darkened by m_pp_fatigue.y, through ONE mask --
// the lift fades out exactly where the vignette comes in, so the two never fight over the same pixels.
// The vignette rides the same screen-shaped ellipse (1 = the middle of an edge, 1.41 = a corner):
// nothing inside PP_VIGNETTE_R0, m_pp_fatigue.y of the light gone at the corners -- never black, the
// game keeps that below 1.
#define PP_FATIGUE_SEPIA	0.4f	// sepia mixed in, as a share of the desaturation
#define PP_FATIGUE_SEPIA_CENTRE	1	// 1 = sepia in the middle, 0 = at the edges
#define PP_FATIGUE_SEPIA_TINT	float3(1.25f, 1.0f, 0.72f) / 1.043f	// warm, keeping the luma
#define PP_VIGNETTE_R0		0.45f
#define PP_VIGNETTE_R1		1.41f
float3 pp_fatigue_grade(float3 c, float2 uv)
{
	float l	= dot(c, float3(0.299f, 0.587f, 0.114f));
	float e	= pp_warp_mask(uv);
	c	= lerp(c, float3(l, l, l), m_pp_fatigue.x * e);
#if PP_FATIGUE_SEPIA_CENTRE
	e	= 1.0f - e;
#endif
	c	= lerp(c, l * (PP_FATIGUE_SEPIA_TINT), m_pp_fatigue.x * e * PP_FATIGUE_SEPIA);
	float r	= length((uv - 0.5f) / 0.5f);
	float v	= saturate((r - PP_VIGNETTE_R0) / (PP_VIGNETTE_R1 - PP_VIGNETTE_R0));
	v	= v * v * (3.0f - 2.0f * v);
	c	*= (1.0f + m_pp_fatigue.w * (1.0f - v)) * (1.0f - m_pp_fatigue.y * v);
	return c;
}

// ---------------------------------------------------------------------------------------------------------
// The HEALTH / BLEEDING / HIT effects, as on R2/R3 (shaders\r2\postprocess.ps), minus what R1 cannot afford or
// has no source for: no glare on the big drops (no bloom), the edge blur at 2 taps a side instead of 4, the
// droplets' glare lit from the picture instead of the bloom.
// ---------------------------------------------------------------------------------------------------------
// BLUR over the finished picture (m_pp_blur.x = amount, y = h/w so the kernel is round), as on R2/R3 --
// where it also carries the intoxication's pulse under a 3D lens; on R1 it is the hit flash's. The same
// poisson pattern as dof.h; PP_BLUR_R is the radius at amount 1, in screen uv.
uniform float4 m_pp_blur;
#define PP_BLUR_R 0.006f
static const float2 PP_BLUR_TAPS[12] = {
	float2(-0.326212f, -0.405810f), float2(-0.840144f, -0.073580f), float2(-0.695914f,  0.457137f),
	float2(-0.203345f,  0.620716f), float2( 0.962340f, -0.194983f), float2( 0.473434f, -0.480026f),
	float2( 0.519456f,  0.767022f), float2( 0.185461f, -0.893124f), float2( 0.507431f,  0.064425f),
	float2( 0.896420f,  0.412458f), float2(-0.321940f, -0.932615f), float2(-0.791559f, -0.597710f) };
float3 pp_blur0(float2 uv, float3 c)
{
	if (m_pp_blur.x <= 0.001f)	return c;
	float2 rad	= float2(m_pp_blur.y, 1.0f) * (PP_BLUR_R * m_pp_blur.x);
	float3 sum	= c;
	for (int i = 0; i < 12; i++)
		sum += tex2D(s_base0, uv + PP_BLUR_TAPS[i] * rad).rgb;
	return sum / 13.0f;
}

float3 pp_blur1(float2 uv, float3 c)
{
	if (m_pp_blur.x <= 0.001f)	return c;
	float2 rad	= float2(m_pp_blur.y, 1.0f) * (PP_BLUR_R * m_pp_blur.x);
	float3 sum	= c;
	for (int i = 0; i < 12; i++)
		sum += tex2D(s_base1, uv + PP_BLUR_TAPS[i] * rad).rgb;
	return sum / 13.0f;
}

// INJURY (CActor, from the health and the bleeding): m_pp_injury.x = how strongly the edges are dyed
// red (0..1, pulsing), y = the blood drops on the lens, 0..1 (pulsing while bleeding), z = the radial blur
// of the edges, w = how far in the blood vessels have crept, 0..1 (beating). PP_INJURY_TINT_ON 0 switches the red off.
// The red goes through its own radial mask (below) in two parts: PP_INJURY_MIX_BLEND of it by PP_INJURY_OVERLAY (1 = overlay:
// the picture's own light and shade stay, the hue turns red; 0 = multiply), then PP_INJURY_MIX_PLAIN of the
// colour itself laid plainly over that -- so the edges take the hue without going garishly saturated.
// Both are the share at x = 1 (full injury, a pulse's peak).
uniform float4 m_pp_injury;
uniform sampler2D	s_blood_drops;
#define PP_INJURY_TINT_ON	1	// 0 = the red off, the drops alone
#define PP_INJURY_TINT		float3(160.0f, 60.0f, 50.0f) / 255.0f	// a muted blood red
#define PP_INJURY_OVERLAY	1
#define PP_INJURY_MIX_BLEND	0.9f
#define PP_INJURY_MIX_PLAIN	0.45f	// 0.75 leaned too far off the overlay
// Its mask: the screen-shaped ellipse (1 = the middle of an edge, 1.41 = a corner), a plain smoothstep from
// PP_INJURY_TINT_R0 to PP_INJURY_TINT_R1 -- softer than pp_warp_mask (0.25..1.45, quintic), which held the
// red to the very rim; this lets it reach in towards the middle.
#define PP_INJURY_TINT_R0	0.1f
#define PP_INJURY_TINT_R1	1.3f
// The DROPS: fx_blood_lowhealth_00 is a grey (~0.5) plate with the drops darker/redder on it, laid on by
// overlay -- grey changes nothing, only what is darker or lighter than it shows. They gather from the corners
// in: nothing past PP_DROPS_R_OUT as they start, everything outside PP_DROPS_R_IN at y = 1, with a soft
// band PP_DROPS_SOFT wide (radii on the screen-shaped ellipse: 1 = the middle of an edge, 1.41 = a corner).
// No GLARE on R1: R2/R3 light fx_blood_lowhealth_02 from the bloom, and R1 has none (probing the picture
// itself drew the scene's silhouettes into it).
#define PP_DROPS_R_OUT		1.5f
#define PP_DROPS_R_IN		0.25f
#define PP_DROPS_SOFT		0.45f
float3 pp_overlay(float3 a, float3 b)
{
	a = saturate(a);
	return lerp(2.0f * a * b, 1.0f - 2.0f * (1.0f - a) * (1.0f - b), step(0.5f, a));
}

float3 pp_injury_tint(float3 c, float2 uv)
{
#if PP_INJURY_OVERLAY
	float3 t = pp_overlay(c, PP_INJURY_TINT);
#else
	float3 t = c * PP_INJURY_TINT;
#endif
	float  k = saturate((length((uv - 0.5f) / 0.5f) - PP_INJURY_TINT_R0) / (PP_INJURY_TINT_R1 - PP_INJURY_TINT_R0));
	float  m = m_pp_injury.x * k * k * (3.0f - 2.0f * k);
	c	= lerp(c, t, m * PP_INJURY_MIX_BLEND);
	return lerp(c, PP_INJURY_TINT, m * PP_INJURY_MIX_PLAIN);
}

float3 pp_injury_drops(float3 c, float2 uv)
{
	float  r	= length((uv - 0.5f) / 0.5f);
	float  e	= lerp(PP_DROPS_R_OUT, PP_DROPS_R_IN, m_pp_injury.y);
	float  m	= smoothstep(e, e + PP_DROPS_SOFT, r);
	return lerp(c, pp_overlay(c, tex2D(s_blood_drops, uv).rgb), m);
}

// The VESSELS: fx_blood_vessels_00, a grey (0.5) plate with the vessels darker/redder towards the rim, laid
// on by overlay like the drops -- grey changes nothing -- and gathering in from the corners the same way:
// nothing past PP_VESSELS_R_OUT as they start, everything outside PP_VESSELS_R_IN at w = 1.
#define PP_VESSELS_R_OUT	1.5f
#define PP_VESSELS_R_IN		0.25f
#define PP_VESSELS_SOFT		0.45f
uniform sampler2D	s_blood_vessels;
float3 pp_injury_vessels(float3 c, float2 uv)
{
	float  r	= length((uv - 0.5f) / 0.5f);
	float  e	= lerp(PP_VESSELS_R_OUT, PP_VESSELS_R_IN, m_pp_injury.w);
	return lerp(c, pp_overlay(c, tex2D(s_blood_vessels, uv).rgb), smoothstep(e, e + PP_VESSELS_SOFT, r));
}

// The RADIAL BLUR of the edges, as an optic's rim smears: the picture averaged along the line through the
// middle, PP_RBLUR_TAPS each way, the streak m_pp_injury.z of the distance from the middle long (so it grows
// towards the rim on its own) and faded in through pp_warp_mask -- the middle stays sharp.
#define PP_RBLUR_TAPS		2		// 4 a side on R2/R3; 2 here, R1's budget
float3 pp_injury_rblur0(float2 uv, float3 c, float2 at)
{
	float  a	= m_pp_injury.z * pp_warp_mask(at);
	if (a <= 0.0001f)	return c;			// (lod 0 below: the branch differs per pixel)
	float2 d	= (uv - 0.5f) * (a / PP_RBLUR_TAPS);
	float3 sum	= c;
	for (int i = 1; i <= PP_RBLUR_TAPS; i++)
		sum += tex2Dlod(s_base0, float4(uv - d * i, 0.0f, 0.0f)).rgb + tex2Dlod(s_base0, float4(uv + d * i, 0.0f, 0.0f)).rgb;
	return sum / (2.0f * PP_RBLUR_TAPS + 1.0f);
}

float3 pp_injury_rblur1(float2 uv, float3 c, float2 at)
{
	float  a	= m_pp_injury.z * pp_warp_mask(at);
	if (a <= 0.0001f)	return c;
	float2 d	= (uv - 0.5f) * (a / PP_RBLUR_TAPS);
	float3 sum	= c;
	for (int i = 1; i <= PP_RBLUR_TAPS; i++)
		sum += tex2Dlod(s_base1, float4(uv - d * i, 0.0f, 0.0f)).rgb + tex2Dlod(s_base1, float4(uv + d * i, 0.0f, 0.0f)).rgb;
	return sum / (2.0f * PP_RBLUR_TAPS + 1.0f);
}

// The bleeding's LENS DROPLETS (CActor, pp_droplets): up to PP_DROPLETS single drops, m_pp_droplets[i] =
// centre (screen uv), radius (share of the screen height; negative = mirrored), opacity (0 = none).
// The texture packs a drop into its channels: R = its diffuse mask, tinted here; G, B = the refraction
// offset in units of the drop's radius (0.5 + offset / 4); A = its glare, a bokeh of its own shape.
// pp_droplets_refract bends the scene's sampling through them (before everything else -- the drop refracts
// the plain picture); pp_droplets_colour tints them and adds the glare over everything, on the lens. R2/R3
// light the glare from the bloom; R1 has none, so it reads the picture itself once, at the droplet's centre,
// from PP_DROPLET_GLARE_LO to PP_DROPLET_GLARE_HI of luma -- the whole droplet lights up or dims at once.
#define PP_DROPLETS			8
#define PP_DROPLET_REFRACT	1.0f	// how strongly a drop bends what is behind it
#define PP_DROPLET_TINT		float3(70.0f, 7.0f, 5.0f) / 255.0f	// was (140, 15, 10), halved
#define PP_DROPLET_MIX		0.7f	// the tint, by overlay
#define PP_DROPLET_GLARE	1.0f	// the glare, added (x the light behind it)
#define PP_DROPLET_GLARE_LO	0.6f	// the picture's luma where the glare starts to light...
#define PP_DROPLET_GLARE_HI	1.0f	// ...and where it is full
uniform float4 m_pp_droplets[PP_DROPLETS];
uniform sampler2D	s_blood_droplet;
// the drop's own -1..1 square at this pixel; false outside it
bool pp_droplet_local(float4 dr, float2 uv, out float2 p)
{
	p	= (uv - dr.xy) / (abs(dr.z) * float2(screen_res.y * screen_res.z, 1.0f));
	if (dr.z < 0.0f)	p.x = -p.x;
	return dr.w > 0.0f && all(abs(p) < 1.0f);
}

float2 pp_droplets_refract(float2 uv)
{
	float2 off = float2(0.0f, 0.0f);
	for (int i = 0; i < PP_DROPLETS; i++)
	{
		float4 dr = m_pp_droplets[i];
		float2 p;
		if (!pp_droplet_local(dr, uv, p))	continue;		// (lod 0 below: the branch differs per pixel)
		float4 t	= tex2Dlod(s_blood_droplet, float4(p * 0.5f + 0.5f, 0.0f, 0.0f));
		float2 o	= (t.gb - 0.5f) * 4.0f;
		if (dr.z < 0.0f)	o.x = -o.x;
		off += o * abs(dr.z) * float2(screen_res.y * screen_res.z, 1.0f) * (t.r * dr.w * PP_DROPLET_REFRACT);
	}
	return off;
}

float3 pp_droplets_colour(float3 c, float2 uv)
{
	for (int i = 0; i < PP_DROPLETS; i++)
	{
		float4 dr = m_pp_droplets[i];
		float2 p;
		if (!pp_droplet_local(dr, uv, p))	continue;
		float4 t	= tex2Dlod(s_blood_droplet, float4(p * 0.5f + 0.5f, 0.0f, 0.0f));
		c	= lerp(c, pp_overlay(c, PP_DROPLET_TINT), t.r * dr.w * PP_DROPLET_MIX);
		float3 bl	= tex2Dlod(s_base0, float4(dr.xy, 0.0f, 0.0f)).rgb;
		float  lit	= sqrt(saturate((dot(bl, float3(0.299f, 0.587f, 0.114f)) - PP_DROPLET_GLARE_LO) / (PP_DROPLET_GLARE_HI - PP_DROPLET_GLARE_LO)));
		c	+= t.a * dr.w * lit * PP_DROPLET_GLARE;
	}
	return c;
}

// The HIT FLASH (CActor, pp_hit): m_pp_hit.x = the edges flushed red -- the injury's mask and its two-part
// blend, in a colour half as bright (PP_HIT_TINT) -- and m_pp_hit.y = a vignette, the fatigue one's shape
// (PP_VIGNETTE_R0..R1), that much of the light gone in the corners. Both flash in and out with the blur.
uniform float4 m_pp_hit;
#define PP_HIT_TINT			float3(80.0f, 30.0f, 25.0f) / 255.0f	// the injury red (160, 60, 50), halved
float3 pp_hit_flash(float3 c, float2 uv)
{
	float  r	= length((uv - 0.5f) / 0.5f);
	float  k	= saturate((r - PP_INJURY_TINT_R0) / (PP_INJURY_TINT_R1 - PP_INJURY_TINT_R0));
	float  m	= m_pp_hit.x * k * k * (3.0f - 2.0f * k);
	c	= lerp(c, pp_overlay(c, PP_HIT_TINT), m * PP_INJURY_MIX_BLEND);
	c	= lerp(c, PP_HIT_TINT, m * PP_INJURY_MIX_PLAIN);
	float  v	= saturate((r - PP_VIGNETTE_R0) / (PP_VIGNETTE_R1 - PP_VIGNETTE_R0));
	return c * (1.0f - m_pp_hit.y * v * v * (3.0f - 2.0f * v));
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
