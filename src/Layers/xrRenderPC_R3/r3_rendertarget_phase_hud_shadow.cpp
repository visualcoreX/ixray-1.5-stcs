#include "stdafx.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

// Screen-space contact shadows for the HUD models. DX10 twin of R2's
// r2_rendertarget_phase_hud_shadow.cpp -- see hud_shadow.ps for why it is done this way and not by
// putting the HUD into the sun cascade.
//
// L == nullptr is the SUN: runs straight after render_sun_cascades(), when the accumulator holds
// the sun and nothing else, so multiplying it down is exactly "the sun does not reach here".
// L != nullptr is a point/spot light, run at the tail of accum_point/accum_spot -- the trade-off
// that makes is written out in the R2 twin.
void CRenderTarget::phase_hud_shadow	(light* L)
{
	if (!ps_r__common_flags.test(RFLAG_HUD_SHADOW))	return;	// the "SSS" options checkbox
	if (ps_r2_hud_shadow <= EPS)					return;

	Fvector		L_dir;
	Fvector4	L_pos;
	float		strength	= ps_r2_hud_shadow;
	float		thickness	= ps_r2_hud_shadow_thickness;

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

		// Cheap rejects, and they are not only about cost -- the multiply hits the whole
		// accumulator, so a lamp that does not actually light the HUD must not run. The second one
		// throws out lights that sit INSIDE the HUD models (muzzle flash, weapon flashlight,
		// headlamp): a screen-space march towards those always hits the geometry it started from,
		// which is what blacked the whole weapon out on every shot. See R2 twin.
		float	L_camdist	= Device.vCameraPosition.distance_to(L->position);
		if (L_camdist >= L->range)											return;
		// ...and it must not be one of the lights that live INSIDE these models. This used to be a
		// distance test, which threw out campfires the moment the player walked up to one; the
		// lights that actually break the march say so themselves now (set_inside_hud, put on the
		// muzzle flash, the weapon lamp, the handheld torch and the headlamp while they are the
		// player's own -- an NPC carrying the same gear stays a world light).
		if (L->flags.bHudMode || L->flags.bInsideHud)						return;
		if (IRender_Light::SPOT == L->flags.type)
		{
			Fvector	d;	d.sub		(Device.vCameraPosition, L->position);
			if (d.magnitude() > EPS)	{
				d.normalize				();
				if (d.dotproduct(L->direction) < _cos(L->cone*.5f))			return;
			}
		}

		float	L_R		= L->range*.95f;
		Fvector	p;		Device.mView.transform_tiny	(p, L->position);
		L_dir.set		(0.f, 0.f, 1.f);
		L_pos.set		(p.x, p.y, p.z, 1.f/(L_R*L_R));
		if (ps_r2_hud_shadow_debug)
			Msg("~ [hudshadow] type=%d pos=(%3.2f,%3.2f,%3.2f) range=%3.2f camdist=%3.2f hud=%d inside=%d shadow=%d",
				L->flags.type, VPUSH(L->position), L->range, L_camdist,
				L->flags.bHudMode, L->flags.bInsideHud, L->flags.bShadow);
		strength		*= ps_r2_hud_shadow_lights;
		thickness		*= 4.f;		// flat grazing angles from a lamp, see the R2 twin
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

	u32		Offset = 0;

	// R3 feeds clip space straight through (see r3\combine_1.vs): POSITION is float4 = xy clip +
	// zw tc, which is why FVF::TL::set(x,y,z,w,c,u,v) is used with the tc in z/w.
	FVF::TL* pv					= (FVF::TL*) RCache.Vertex.Lock	(4,g_combine->vb_stride,Offset);
	pv->set						( -1,  1, 0, 1, 0, 0, 0);	pv++;
	pv->set						( -1, -1, 0, 0, 0, 0, 0);	pv++;
	pv->set						(  1,  1, 1, 1, 0, 0, 0);	pv++;
	pv->set						(  1, -1, 1, 0, 0, 0, 0);	pv++;
	RCache.Vertex.Unlock		(4,g_combine->vb_stride);

	// Stencil off: the depth band is the mask here, and the light-marker values in the stencil have
	// nothing to say about which pixels are HUD.
	RCache.set_Stencil			(FALSE);
	RCache.set_CullMode			(CULL_NONE);
	RCache.set_ColorWriteEnable	();

	RCache.set_Element			(s_hud_shadow->E[0]);
	RCache.set_Geometry			(g_combine);
	RCache.set_c				("hud_sh_dir",		L_dir.x, L_dir.y, L_dir.z, ps_r2_hud_shadow_hardness);
	RCache.set_c				("hud_sh_params",	ps_r2_hud_shadow_len, thickness,
													strength, ps_r2_hud_shadow_maxz);
	// zw unused on R3 -- the jitter there rides on SV_Position, which is already in pixels
	RCache.set_c				("hud_sh_proj",		m_hud._11, m_hud._22, 0.f, 0.f);
	RCache.set_c				("hud_sh_lpos",		L_pos.x, L_pos.y, L_pos.z, L_pos.w);
	RCache.Render				(D3DPT_TRIANGLELIST,Offset,0,4,0,2);

	// Put the accumulator's stencil rule back the way the light passes expect to find it.
	RCache.set_Stencil			(TRUE,D3DCMP_LESSEQUAL,0x01,0xff,0x00);
}
