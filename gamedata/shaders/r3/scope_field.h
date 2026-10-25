#ifndef SCOPE_FIELD_H
#define SCOPE_FIELD_H

// THE FIELD STOP of an optic's lens, the one the prism sight has (collim.h collim_field_stop): the glass shows
// its picture only to a look along the scope's axis; off it the inside of the tube closes in -- a dark crescent
// -- and from the side the eyepiece is black. It is part of the lens's own picture, so it fades in with the
// lens as the aim comes up (nothing at the hip).
//   The axis is the weapon's own forward (the hud item's +Z, axis_v from model_scope_eye.vs): at full aim it is the
// view axis on every optic (tools/hud_aim_center.py: 0 deg; the Protecta's 1.7 and the PKM's 0.3 are the glass's
// own tilt there too). Not the glass's normal: the ELCAN's glass is a 3 mm dome, each triangle's normal leaned its
// own way and the stop came out a small polygon. Not the lens bone's axis either: the PKM's is turned 90 degrees.
//   The field is the glass's own angular size from where the eye is, so at full aim the stop lies on the aperture
// and changes nothing; it shows once the look and the axis part. The glass's size and centre come from the
// derivatives, flattened onto the plane across the axis -- the uv is affine there, so they are the same on every
// triangle whatever the dome does.
// pos_v: the pixel in VIEW space (the eye at the origin). Derivatives of hud geometry in world space are noise
// -- centimetres against hundreds of metres -- so view space only. Call it first thing in main: it takes
// derivatives, which mean nothing after a branch. Needs LENS_GLASS_R (the aperture's radius in the glass's uv).
#define LENS_FIELD_SOFT	0.2f		// the stop's edge, as a share of the field's own size
#define LENS_FIELD_SCALE	1.8f		// the clear field, times the glass's own size (user: the black closed in too soon)

float lens_field_stop(float3 pos_v, float3 axis_v, float2 uv)
{
	float3	dpx	= ddx(pos_v);
	float3	dpy	= ddy(pos_v);
	float2	dux	= ddx(uv);
	float2	duy	= ddy(uv);
	float	det	= dux.x*duy.y - dux.y*duy.x;
	if (abs(det) < 1e-12f)				return 1.0f;
	float3	fwd	= normalize(axis_v);
	// the glass's edges per unit of uv, flattened across the axis (a dome's bulge dropped)
	float3	P_u	= (dpx*duy.y - dpy*dux.y) / det;
	float3	P_v	= (dpy*dux.x - dpx*duy.x) / det;
	P_u		-= fwd * dot(P_u, fwd);
	P_v		-= fwd * dot(P_v, fwd);
	float3	c	= pos_v - (uv.x - 0.5f)*P_u - (uv.y - 0.5f)*P_v;	// the glass's centre
	fwd			= (dot(fwd, c) > 0.0f) ? fwd : -fwd;				// into the scope, away from the eye
	float	R	= LENS_GLASS_R * 0.5f * (length(P_u) + length(P_v));
	float	tf	= LENS_FIELD_SCALE * R / max(dot(c, fwd), 0.0001f);	// the field: tangent of its half-angle
	// where this pixel's ray runs against the axis
	float3	v	= normalize(pos_v);
	float	a	= dot(v, fwd);
	float	t	= length(v - fwd*a) / max(a, 0.0001f);
	float	w	= tf * LENS_FIELD_SOFT;
	float	f	= saturate((tf + 0.5f*w - t) / max(w, 0.00001f));
	return	(a > 0.0f) ? f : 0.0f;
}
// The lens-off prism mode: the world under the glass is the frame's own (not a picture warped on its own, as the 3D
// lens's), and the distortion mask over the glass is put back to "none" so the reticle does not ripple
// (r_dsgraph_render_lens_undistort). The world still has to: its offset is read from the frame before's mask,
// copied before the glass took itself out of it ($user$scopedistort), at the world's own point -- uv here is where
// the lens samples the frame -- so the ripple is magnified with the world. The reticle is laid on afterwards.
Texture2D	s_scope_distort;
float2 lens_prism_distort(float2 uv)
{
	return	(s_scope_distort.SampleLevel(smp_base, uv, 0).xy - (127.0f/255.0f)) * def_distort;
}
#endif
