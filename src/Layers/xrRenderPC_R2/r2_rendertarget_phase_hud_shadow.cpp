#include "stdafx.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

float	hclip(float v, float dim);

// Screen-space contact shadows for the HUD models. See hud_shadow.ps for why it is done this way
// and not by putting the HUD into the sun cascade.
//
// L == nullptr is the SUN: called straight after render_sun_cascades(), when the accumulator holds
// the sun and nothing else, so multiplying it down is exactly "the sun does not reach here".
//
// L != nullptr is a POINT or SPOT light, called at the tail of accum_point/accum_spot, right after
// that light has added itself. The multiply is no longer surgical there -- it also dims whatever
// the accumulator already held from the sun and from earlier lamps. Living with that is deliberate:
// isolating one light's share would mean either re-deriving its full contribution (lightmap and
// shadow map included) to subtract it, or a per-light occlusion atlas the accum shaders sample --
// which is what SSFX does with its sss_id/s_ssfx_sss channels, and it needs the light ids plumbed
// all the way into every accum blender. The error only shows when two lights light the HUD at once,
// and the lamp's own falloff (att, in the shader) keeps it small; r2_hud_shadow_lights scales it,
// 0 goes back to sun-only.
void CRenderTarget::phase_hud_shadow	(light* L)
{
	if (!ps_r__common_flags.test(RFLAG_HUD_SHADOW))	return;	// the "SSS" options checkbox
	if (ps_r2_hud_shadow <= EPS)					return;

	// xyz: direction TOWARDS the light in view space, for the sun. w<=0 in hud_sh_lpos tells the
	// shader to use it instead of a per-pixel direction.
	Fvector	L_dir;
	Fvector4 L_pos;
	float	strength	= ps_r2_hud_shadow;
	float	thickness	= ps_r2_hud_shadow_thickness;

	if (nullptr == L)
	{
		if (!RImplementation.Lights.sun_adapted._get())	return;

		light*	fuckingsun	= (light*)RImplementation.Lights.sun_adapted._get();

		// The same transform accum_direct does for Ldynamic_dir, negated because that one is the
		// direction the light travels.
		Device.mView.transform_dir	(L_dir, fuckingsun->direction);
		L_dir.normalize				();
		L_dir.invert				();
		L_pos.set					(0.f, 0.f, 0.f, -1.f);
	}
	else
	{
		if (ps_r2_hud_shadow_lights <= EPS)				return;

		// Cheap rejects, and they are not only about cost. This multiply hits the whole accumulator,
		// so a lamp that does not actually light the HUD must not run at all: out of range its
		// falloff is zero anyway (and its stencil mask never claimed the HUD pixels), and a spot
		// aimed elsewhere lights nothing here no matter how close it is.
		float	L_camdist	= Device.vCameraPosition.distance_to(L->position);
		if (L_camdist >= L->range)											return;

		// AND THE LIGHT MUST BE OUTSIDE THE HUD ITSELF. A muzzle flash (CShootingObject::Light_Render
		// puts an omni straight into the fire point), a weapon flashlight, the headlamp -- all of
		// them live INSIDE the models this pass shadows. Marching the depth buffer towards a light
		// embedded in the very geometry being marched always finds that geometry in the way, so the
		// occlusion came out ~1 over the whole HUD and every shot blacked the weapon out. Screen
		// space cannot answer this case at all: the buffer holds one surface per pixel and knows
		// nothing about the light sitting behind it.
		//
		// This used to be a distance test (borrowing maxz, 2 m), which threw out campfires the
		// moment the player walked up to one. The lights that actually break the march say so
		// themselves now: set_inside_hud is put on the muzzle flash, the weapon lamp, the handheld
		// torch and the headlamp while they are the PLAYER's -- an NPC carrying the same gear is
		// an ordinary world light and still lights the hud.
		if (L->flags.bHudMode || L->flags.bInsideHud)						return;

		// AND IT MUST BE A LIGHT THAT CASTS SHADOWS AT ALL. A lamp the level author marked as
		// non-shadowing (CSE_ALifeObjectHangingLamp::flCastShadow off -> CHangingLamp does
		// set_shadow(false)) never renders a shadow map, so nothing around it -- its own cage or
		// grate included -- darkens anything in the world. This march does not know that: it reads
		// the depth buffer, where that geometry sits like any other, so the weapon alone picked up a
		// grid of shadows the rest of the level does not have (user 2026-08-25: lamps behind grates
		// cast no world shadows, yet the grate shows up on the HUD models). Take the light's word.
		if (!L->flags.bShadow)												return;

		if (IRender_Light::SPOT == L->flags.type)
		{
			Fvector	d;	d.sub		(Device.vCameraPosition, L->position);
			if (d.magnitude() > EPS)	{
				d.normalize				();
				if (d.dotproduct(L->direction) < _cos(L->cone*.5f))			return;
			}
		}

		// Same range accum_point/accum_spot light with, so the shader's falloff matches the one the
		// accumulator was filled with.
		float	L_R		= L->range*.95f;
		Fvector	p;		Device.mView.transform_tiny	(p, L->position);
		L_dir.set		(0.f, 0.f, 1.f);
		L_pos.set		(p.x, p.y, p.z, 1.f/(L_R*L_R));
		strength		*= ps_r2_hud_shadow_lights;

		// A lamp stands off to the side, so its rays cross the weapon at a much flatter angle than
		// the sun's and the depth slab a thin thickness carves out keeps missing. SSFX runs into the
		// same thing and uses a far thicker test for its omni pass than for the sun (1.0 vs 0.3).
		thickness		*= 4.f;
	}

	// The HUD projection, rebuilt exactly as r_dsgraph_render_hud does it -- the march has to
	// project with this one, since that is what rasterised these pixels.
	Fmatrix	m_hud;
	extern ENGINE_API float		psHUD_FOV;
	m_hud.build_projection		(
		deg2rad(psHUD_FOV*Device.fFOV),
		Device.fASPECT, VIEWPORT_NEAR,
		g_pGamePersistent->Environment().CurrentEnv->far_plane);

	phase_accumulator			();

	u32			Offset = 0;
	float		_w	= float(Device.dwWidth);
	float		_h	= float(Device.dwHeight);
	Fvector2	p0,p1;
	p0.set		(.5f/_w, .5f/_h);
	p1.set		((_w+.5f)/_w, (_h+.5f)/_h );

	// g_combine_VP: POSITION is float4 = clip xy + tc xy (see combine_1.vs / hud_shadow.vs), so the
	// FVF::TL::set(x,y,z,w,c,u,v) below writes the tc into z/w. Same abuse as phase_ssao.
	FVF::TL* pv					= (FVF::TL*) RCache.Vertex.Lock	(4,g_combine_VP->vb_stride,Offset);
	pv->set						(hclip(EPS,		_w),	hclip(_h+EPS,	_h),	p0.x, p1.y, 0, 0, 0);	pv++;
	pv->set						(hclip(EPS,		_w),	hclip(EPS,		_h),	p0.x, p0.y, 0, 0, 0);	pv++;
	pv->set						(hclip(_w+EPS,	_w),	hclip(_h+EPS,	_h),	p1.x, p1.y, 0, 0, 0);	pv++;
	pv->set						(hclip(_w+EPS,	_w),	hclip(EPS,		_h),	p1.x, p0.y, 0, 0, 0);	pv++;
	RCache.Vertex.Unlock		(4,g_combine_VP->vb_stride);

	// Stencil off: the depth band is the mask here, and the light-marker values in the stencil have
	// nothing to say about which pixels are HUD.
	RCache.set_Stencil			(FALSE);
	RCache.set_CullMode			(CULL_NONE);
	RCache.set_ColorWriteEnable	();

	RCache.set_Element			(s_hud_shadow->E[0]);
	RCache.set_Geometry			(g_combine_VP);
	RCache.set_c				("hud_sh_dir",		L_dir.x, L_dir.y, L_dir.z, ps_r2_hud_shadow_hardness);
	RCache.set_c				("hud_sh_params",	ps_r2_hud_shadow_len, thickness,
													strength, ps_r2_hud_shadow_maxz);
	// zw = screen size: the shader needs pixel coords for the per-pixel jitter that keeps the march
	// from banding into stripes
	RCache.set_c				("hud_sh_proj",		m_hud._11, m_hud._22, _w, _h);
	RCache.set_c				("hud_sh_lpos",		L_pos.x, L_pos.y, L_pos.z, L_pos.w);
	RCache.Render				(D3DPT_TRIANGLELIST,Offset,0,4,0,2);

	// Put the accumulator's stencil rule back the way the light passes expect to find it.
	RCache.set_Stencil			(TRUE,D3DCMP_LESSEQUAL,0x01,0xff,0x00);
}
