#include "stdafx.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/fmesh.h"
#include "../xrRender/FBasicVisual.h"

// Laser-designator beams (the one drawn by effects\laser_beam -- the old mesh beam on the weapon's
// `line` bones is untouched and still draws as part of the HUD model).
//
// The game hands the beams over already resolved (IGame_Persistent::GetLaserBeams): axis, radius,
// colour with the weather folded in, fades, dust drift. All this file does is
//  1) phase_laser_density: redraw the blend-shaded particles (smoke, dust) of the sorted list into a
//     small side buffer -- how much of each pixel they cover and how far away they are -- so the beam
//     can brighten where a puff of smoke actually crosses it;
//  2) phase_laser_beams: lay a camera-facing strip over each beam's axis and let laser_beam.ps turn
//     it into a round glowing tube with dust in it (the strip only has to cover the tube on screen).

// Same shape as sorted_L1 (r__dsgraph_render.cpp), but only particle effects whose shader has the
// density element (CBlender_Particle E[3], BLEND mode only) get drawn, and with that element.
static void __fastcall laser_density_L1(R_dsgraph::mapSorted_Node* N)
{
	dxRender_Visual* V		= N->val.pVisual;
	if (!V || MT_PARTICLE_EFFECT != V->Type || !V->shader._get())	return;
	ShaderElement* se		= &*V->shader->E[3];
	if (!se)				return;
	RCache.set_Element		(se);
	RCache.set_xform_world	(N->val.Matrix);
	V->Render				(0.f);
}

static bool laser_beams_get(const SLaserBeamRender*& beams, u32& count)
{
	beams	= nullptr;
	count	= g_pGamePersistent ? g_pGamePersistent->GetLaserBeams(beams) : 0;
	return	count && beams;
}

// No depth buffer in the density passes: under MSAA the scene's is multisampled and cannot sit with a
// 1-sample target. It is not needed either -- the view depth goes into the buffer next to the coverage
// and the beam shader only counts what is at the beam's own depth, which also throws out anything
// behind a wall.
static void laser_dens_begin(CRenderTarget* T)
{
	T->u_setrt				(T->rt_laser_dens, nullptr, nullptr, nullptr);
	D3D_VIEWPORT VP			= { 0, 0, T->rt_laser_dens->dwWidth, T->rt_laser_dens->dwHeight, 0.f, 1.f };
	RCache.set_Scissor		(NULL);		// a lens-frame rectangle is in full-res pixels
	HW.pDevice->RSSetViewports(1, &VP);
	RCache.set_Stencil		(FALSE);
	RCache.set_ColorWriteEnable();
}

// back to the forward target (phase_combine set it right before render_forward)
static void laser_dens_end(CRenderTarget* T)
{
	if (!RImplementation.o.dx10_msaa)	T->u_setrt(T->rt_Generic_0, 0, 0, HW.pBaseZB);
	else								T->u_setrt(T->rt_Generic_0, 0, 0, T->rt_MSAADepth->pZRT);
	RImplementation.rmNormal();			// full-screen viewport + the lens scissor, if any
}

void CRenderTarget::phase_laser_density()
{
	m_laser_dens_valid	= false;

	const SLaserBeamRender* beams;	u32 count;
	if (!laser_beams_get(beams, count))	return;
	bool smoke = false, rain = false;
	for (u32 i = 0; i < count; ++i)
	{
		smoke	|= beams[i].smoke_gain > 0.f;
		rain	|= beams[i].rain_gain > 0.f;
	}
	if (!smoke && !rain)				return;

	PIX_EVENT(laser_density);

	FLOAT zero[4] = { 0.f, 0.f, 0.f, 0.f };
	HW.pDevice->ClearRenderTargetView(rt_laser_dens->pRT, zero);
	m_laser_dens_valid		= true;		// cleared: the rain may add to it later this frame
	if (!smoke)							return;

	laser_dens_begin		(this);
	RCache.set_CullMode		(CULL_NONE);
	RImplementation.mapSorted.traverseRL(laser_density_L1);		// back to front, as the real draw
	laser_dens_end			(this);
	RCache.set_CullMode		(CULL_CCW);
}

void CRenderTarget::phase_laser_density_rain(ref_geom& geom, u32 vOffset, u32 vCount)
{
	if (!m_laser_dens_valid || !vCount)	return;

	const SLaserBeamRender* beams;	u32 count;
	if (!laser_beams_get(beams, count))	return;
	bool rain = false;
	for (u32 i = 0; i < count; ++i)
		rain |= beams[i].rain_gain > 0.f;
	if (!rain)							return;

	PIX_EVENT(laser_density_rain);

	if (!s_laser_dens_rain)	s_laser_dens_rain.create("effects\\laser_dens_rain", "fx\\fx_rain");	// dxRainRender's texture

	laser_dens_begin		(this);
	RCache.set_xform_world	(Fidentity);
	RCache.set_Shader		(s_laser_dens_rain);
	RCache.set_CullMode		(CULL_NONE);	// after set_Shader, which brings its own rasterizer state
	RCache.set_Geometry		(geom);
	RCache.Render			(D3DPT_TRIANGLELIST, vOffset, 0, vCount, 0, vCount / 2);
	laser_dens_end			(this);
}

void CRenderTarget::phase_laser_beams()
{
	const SLaserBeamRender* beams;	u32 count;
	if (!laser_beams_get(beams, count))	return;

	PIX_EVENT(laser_beams);

	const Fvector&	eye		= Device.vCameraPosition;
	const Fvector&	cdir	= Device.vCameraDirection;
	// world size of one pixel at unit view depth (mProject._22 = cot(vfov/2))
	const float		pix		= 2.f / (_max(Device.mProject._22, EPS_S) * float(Device.dwHeight));

	// The eyepiece glass of a prism sight, or of an optic in the lens-off (fake 3D) mode, cuts the weapon out
	// and has the world drawn in its place -- the weapon the beam leaves among it. Seen through that glass,
	// the beam would start in the air. (The same for the flat 2D scope's glass: see glass_circle_mark.) So the glass is marked in the stencil (bit 0x80, cleared first: the
	// lights have been writing marker ids into it) and every beam drawn twice: outside the glass as it is,
	// inside it with its start run back along it to just in front of the eye, out of the picture -- as on a
	// 3D lens frame (LaserBeam_LensFrameStart), but only where the weapon is not there to be left.
	RCache.set_xform_world	(Fidentity);
	auto glass_bit_clear = [this]()
	{
		u32		Offset;
		const float	_w	= float(Device.dwWidth);
		const float	_h	= float(Device.dwHeight);
		const u32	C	= color_rgba(255, 255, 255, 255);
		FVF::TL* pv		= (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(-0.5f,		_h - 0.5f,	0.f, 1.f, C, 0, 0);	pv++;
		pv->set(-0.5f,		-0.5f,		0.f, 1.f, C, 0, 0);	pv++;
		pv->set(_w - 0.5f,	_h - 0.5f,	0.f, 1.f, C, 0, 0);	pv++;
		pv->set(_w - 0.5f,	-0.5f,		0.f, 1.f, C, 0, 0);	pv++;
		RCache.Vertex.Unlock	(4, g_combine->vb_stride);
		RCache.set_Element		(s_occq->E[2]);		// a full-screen pass that writes no colour
		RCache.set_Stencil		(TRUE, D3DCMP_ALWAYS, 0x00, 0x00, 0x80, D3DSTENCILOP_ZERO, D3DSTENCILOP_ZERO, D3DSTENCILOP_ZERO);
		RCache.set_Geometry		(g_combine);
		RCache.Render			(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	};
	// The flat 2D scope's glass: the eyepiece circle the pp pass magnifies, shades and bends (pp_zoom_circle --
	// centred, xy = radii in screen uv, 0 = no flat picture). The hud model is hidden altogether while it is up,
	// but the art around the glass is open -- it only paints the scope's body -- so the world shows there
	// unmagnified, and a beam run back towards the eye over the whole screen would be seen crossing it. The
	// pp pass reads the glass only from inside this circle (the zoom and the curvature both pull towards the
	// centre), so marking the circle as it is covers everything the glass shows.
	auto glass_circle_mark = [this](const Fvector4& zc)
	{
		const u32	SEG	= 64;
		u32		Offset;
		const float	_w	= float(Device.dwWidth);
		const float	_h	= float(Device.dwHeight);
		const float	cx	= 0.5f * _w - 0.5f, cy = 0.5f * _h - 0.5f;
		const float	rx	= zc.x * _w, ry = zc.y * _h;
		const u32	C	= color_rgba(255, 255, 255, 255);
		FVF::TL* pv		= (FVF::TL*)RCache.Vertex.Lock(SEG * 3, g_combine->vb_stride, Offset);
		for (u32 i = 0; i < SEG; ++i)
		{
			const float a0 = PI_MUL_2 * float(i) / float(SEG), a1 = PI_MUL_2 * float(i + 1) / float(SEG);
			pv->set(cx,						cy,						0.f, 1.f, C, 0, 0);	pv++;
			pv->set(cx + rx * _cos(a0),		cy + ry * _sin(a0),		0.f, 1.f, C, 0, 0);	pv++;
			pv->set(cx + rx * _cos(a1),		cy + ry * _sin(a1),		0.f, 1.f, C, 0, 0);	pv++;
		}
		RCache.Vertex.Unlock	(SEG * 3, g_combine->vb_stride);
		RCache.set_Element		(s_occq->E[2]);
		RCache.set_CullMode		(CULL_NONE);
		RCache.set_Stencil		(TRUE, D3DCMP_ALWAYS, 0x80, 0x00, 0x80, D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE);
		RCache.set_Geometry		(g_combine);
		RCache.Render			(D3DPT_TRIANGLELIST, Offset, SEG);
	};
	extern bool g_hud_glass_cut;	// r__dsgraph_render.cpp: an eyepiece cut the hud out this frame
	const bool	hud_glass	= g_hud_glass_cut && !RImplementation.lstHUDLensLate.empty();
	const Fvector4 zc		= g_pGamePersistent ? g_pGamePersistent->pp_zoom_circle : Fvector4().set(0.f, 0.f, 1.f, 0.f);
	const bool	circle		= zc.x > 0.f && zc.y > 0.f;
	bool glass = false;
	if (hud_glass || circle)
		for (u32 b = 0; b < count && !glass; ++b)
			glass = beams[b].length >= EPS_L;
	if (glass)
	{
		glass_bit_clear		();
		if (hud_glass)		glass = RImplementation.r_dsgraph_mark_hud_glass();
		else				glass_circle_mark(zc);
		RCache.set_xform_world(Fidentity);
	}
	RCache.set_Stencil		(FALSE);

	const u32 N = 32;	// strip segments; bunched up near the emitter, where the width changes fastest

	for (u32 b = 0; b < count; ++b)
	{
		const SLaserBeamRender& B = beams[b];
		if (B.length < EPS_L || B.radius <= 0.f)	continue;

		ref_shader& sh = s_laser_beam[B.texture];
		if (!sh)	sh.create("effects\\laser_beam", B.texture.size() ? B.texture.c_str() : "fx\\fx_laser_dust");
		if (!sh || !sh->E[0])	continue;

		// how far back the start goes inside the glass: to just past the near plane (0 = not at all, for a
		// beam that does not run away from the eye)
		float ext = 0.f;
		if (glass)
		{
			const float s		= B.dir.dotproduct(cdir);
			const float depth	= Fvector().sub(B.start, eye).dotproduct(cdir);
			const float target	= 2.f * VIEWPORT_NEAR;
			if (s > 0.1f && depth > target)	ext = (depth - target) / s;
		}
		// 0: the whole screen; 1: outside the glass; 2: inside it, with the start run back
		for (u32 pass = ext > 0.f ? 1 : 0; pass <= (ext > 0.f ? 2u : 0u); ++pass)
		{
			const float e		= (pass == 2) ? ext : 0.f;
			Fvector start;		start.mad(B.start, B.dir, -e);
			const float length	= B.length + e;
			// the falloff runs from the start, so the longer beam would come out dimmer by what it added
			const float gain	= (e > 0.f && B.falloff > 0.f) ? expf(e / B.falloff) : 1.f;

			// Strip points: the axis, widened sideways (across both the beam and the line of sight) far
			// enough to hold the shader's gaussian -- and never narrower than ~2 px, which is where the
			// shader stops thinning a distant beam and dims it instead.
			Fvector P[N + 1], S[N + 1];
			float	W[N + 1];
			Fvector prev_side;	prev_side.set(Device.vCameraRight);
			for (u32 i = 0; i <= N; ++i)
			{
				float f = float(i) / float(N);
				P[i].mad(start, B.dir, length * f * f);
				Fvector to;		to.sub(P[i], eye);
				Fvector side;	side.crossproduct(B.dir, to);
				float	m = side.magnitude();
				if (m < EPS_L * to.magnitude() || m < EPS_S)	side.set(prev_side);	// looking straight down the beam
				else											side.mul(1.f / m);
				if (side.dotproduct(prev_side) < 0.f)			side.invert();		// keep the strip untwisted
				prev_side.set(side);
				S[i].set(side);
				float z = _max(to.dotproduct(cdir), VIEWPORT_NEAR);
				W[i]	= _max(B.radius, z * pix * 0.75f) * 2.6f;
			}

			u32 Offset;
			FVF::V* pv = (FVF::V*)RCache.Vertex.Lock(N * 4, g_laser_beam->vb_stride, Offset);
			for (u32 i = 0; i < N; ++i)
			{
				float u0 = float(i) / float(N), u1 = float(i + 1) / float(N);
				Fvector a;
				a.mad(P[i],		S[i],		-W[i]);		pv->set(a, u0, -1.f);	pv++;
				a.mad(P[i],		S[i],		 W[i]);		pv->set(a, u0,  1.f);	pv++;
				a.mad(P[i + 1],	S[i + 1],	-W[i + 1]);	pv->set(a, u1, -1.f);	pv++;
				a.mad(P[i + 1],	S[i + 1],	 W[i + 1]);	pv->set(a, u1,  1.f);	pv++;
			}
			RCache.Vertex.Unlock(N * 4, g_laser_beam->vb_stride);

			RCache.set_Element	(sh->E[0]);
			// after set_Element: the pass brings its own rasterizer state (cull CCW) and would override it.
			// The strip's winding on screen flips with the side it is seen from, so nothing may be culled.
			RCache.set_CullMode	(CULL_NONE);
			if (pass == 1)		RCache.set_Stencil(TRUE, D3DCMP_NOTEQUAL, 0x80, 0x80, 0x00);
			else if (pass == 2)	RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x80, 0x80, 0x00);
			else				RCache.set_Stencil(FALSE);
			RCache.set_Geometry	(g_laser_beam);
			RCache.set_c		("lb_start",		start.x, start.y, start.z, length);
			RCache.set_c		("lb_dir",			B.dir.x, B.dir.y, B.dir.z, B.radius);
			RCache.set_c		("lb_color",		B.color.x * gain, B.color.y * gain, B.color.z * gain, B.view_boost);
			RCache.set_c		("lb_fade",			B.fade_cam_start, _max(B.fade_cam_end, B.fade_cam_start + EPS_L), B.fade_start, B.fade_end);
			RCache.set_c		("lb_dust",			B.dust_scale, B.dust_threshold, B.dust_contrast, B.dust_amount);
			RCache.set_c		("lb_dust_off1",	B.dust_offset1.x, B.dust_offset1.y, B.dust_offset1.z, B.core_amount);
			RCache.set_c		("lb_dust_off2",	B.dust_offset2.x, B.dust_offset2.y, B.dust_offset2.z, B.dust_scale2);
			RCache.set_c		("lb_smoke",		m_laser_dens_valid ? B.smoke_gain : 0.f, _max(B.smoke_range, 0.01f), B.falloff, pix);
			RCache.set_c		("lb_screen",		1.f / float(Device.dwWidth), 1.f / float(Device.dwHeight), 0.f, 0.f);
			RCache.set_c		("lb_misc",			_max(B.dust_depth, 0.25f), 0.f, 0.f, 0.f);
			RCache.set_c		("lb_rain",			m_laser_dens_valid ? B.rain_gain : 0.f, _max(B.rain_range, 0.01f), 0.f, 0.f);
			RCache.Render		(D3DPT_TRIANGLELIST, Offset, 0, N * 4, 0, N * 2);
		}
	}
	RCache.set_Stencil		(FALSE);
	if (glass)
	{
		glass_bit_clear		();		// leave the bit as nothing after this expects to find it: clear
		RCache.set_Stencil	(FALSE);
		RCache.set_xform_world(Fidentity);
	}

	// The dots: a camera-facing square over each one, big enough for its halo (and never under
	// dot_min_px, where laser_dot.ps starts dimming it instead of shrinking it); laser_dot.ps does the rest.
	for (u32 b = 0; b < count; ++b)
	{
		const SLaserBeamRender& B = beams[b];
		if (B.dot_show <= 0.f || B.dot_core <= 0.f)	continue;

		if (!s_laser_dot)	s_laser_dot.create("effects\\laser_dot");
		if (!s_laser_dot || !s_laser_dot->E[0])		break;

		Fvector to;		to.sub(B.dot_pos, eye);
		const float z	= _max(to.dotproduct(cdir), VIEWPORT_NEAR);
		const float h	= _max(_max(B.dot_halo, B.dot_core), z * pix * B.dot_min_px * 0.5f) * 2.6f;
		Fvector R;		R.mul(Device.vCameraRight,	h);
		Fvector U;		U.mul(Device.vCameraTop,	h);

		u32 Offset;
		FVF::V* pv = (FVF::V*)RCache.Vertex.Lock(4, g_laser_beam->vb_stride, Offset);
		Fvector a;
		a.sub(B.dot_pos, R);	a.sub(U);	pv->set(a, -1.f, -1.f);	pv++;
		a.sub(B.dot_pos, R);	a.add(U);	pv->set(a, -1.f,  1.f);	pv++;
		a.add(B.dot_pos, R);	a.sub(U);	pv->set(a,  1.f, -1.f);	pv++;
		a.add(B.dot_pos, R);	a.add(U);	pv->set(a,  1.f,  1.f);	pv++;
		RCache.Vertex.Unlock(4, g_laser_beam->vb_stride);

		RCache.set_Element	(s_laser_dot->E[0]);
		RCache.set_CullMode	(CULL_NONE);
		RCache.set_Geometry	(g_laser_beam);
		RCache.set_c		("ld_pos",		B.dot_pos.x, B.dot_pos.y, B.dot_pos.z, B.dot_core);
		RCache.set_c		("ld_color",	B.dot_color.x, B.dot_color.y, B.dot_color.z, B.dot_halo);
		RCache.set_c		("ld_params",	B.dot_halo_gain, B.dot_white, B.dot_min_px, pix);
		RCache.set_c		("ld_misc",		B.dot_halo_min_gain, _min(B.dot_show, 1.f), 0.f, 0.f);
		RCache.set_c		("lb_screen",	1.f / float(Device.dwWidth), 1.f / float(Device.dwHeight), 0.f, 0.f);
		RCache.Render		(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}

	RCache.set_CullMode		(CULL_CCW);
}

// Dust in a handheld torch's light cone. The cone can fill the whole screen and the eye is often inside
// it, so there is no proxy geometry: one quad over the view, and torch_dust.ps meets every view ray with
// the cone analytically and walks the mote grid only over the part inside it (early-out elsewhere).
void CRenderTarget::phase_torch_dust()
{
	const STorchDustRender* cones	= nullptr;
	const u32 count	= g_pGamePersistent ? g_pGamePersistent->GetTorchDust(cones) : 0;
	if (!count || !cones)	return;

	PIX_EVENT(torch_dust);

	const Fvector&	eye		= Device.vCameraPosition;
	const float		pix		= 2.f / (_max(Device.mProject._22, EPS_S) * float(Device.dwHeight));

	// the quad: 1 m ahead, a bit larger than the view there (no depth test, so the distance is free)
	const float		d		= 1.f;
	Fvector C;		C.mad(eye, Device.vCameraDirection, d);
	Fvector R;		R.mul(Device.vCameraRight,	d / _max(Device.mProject._11, EPS_S) * 1.2f);
	Fvector U;		U.mul(Device.vCameraTop,	d / _max(Device.mProject._22, EPS_S) * 1.2f);

	RCache.set_xform_world	(Fidentity);
	RCache.set_Stencil		(FALSE);

	for (u32 c = 0; c < count; ++c)
	{
		const STorchDustRender& D = cones[c];
		if (D.length < EPS_L || D.cell <= 0.f || D.density <= 0.f)	continue;

		ref_shader& sh = s_torch_dust[D.texture];
		if (!sh)	sh.create("effects\\torch_dust", D.texture.size() ? D.texture.c_str() : "fx\\fx_laser_dust");
		if (!sh || !sh->E[0])	continue;

		u32 Offset;
		FVF::V* pv = (FVF::V*)RCache.Vertex.Lock(4, g_laser_beam->vb_stride, Offset);
		Fvector a;
		a.sub(C, R);	a.sub(U);	pv->set(a, 0.f, 0.f);	pv++;
		a.sub(C, R);	a.add(U);	pv->set(a, 0.f, 1.f);	pv++;
		a.add(C, R);	a.sub(U);	pv->set(a, 1.f, 0.f);	pv++;
		a.add(C, R);	a.add(U);	pv->set(a, 1.f, 1.f);	pv++;
		RCache.Vertex.Unlock(4, g_laser_beam->vb_stride);

		RCache.set_Element	(sh->E[0]);
		RCache.set_CullMode	(CULL_NONE);
		RCache.set_Geometry	(g_laser_beam);
		RCache.set_c		("td_apex",		D.apex.x, D.apex.y, D.apex.z, D.length);
		RCache.set_c		("td_dir",		D.dir.x, D.dir.y, D.dir.z, D.cos_half);
		RCache.set_c		("td_color",	D.color.x, D.color.y, D.color.z, D.range);
		RCache.set_c		("td_fade",		D.fade_cam_start, _max(D.fade_cam_end, D.fade_cam_start + EPS_L), _max(D.fade_start, EPS_L), D.edge);
		RCache.set_c		("td_mote",		D.cell, D.density, D.radius, D.radius_var);
		RCache.set_c		("td_twinkle",	D.twinkle, D.twinkle_speed, Device.fTimeGlobal, pix);
		RCache.set_c		("td_mote_off",	D.mote_offset.x, D.mote_offset.y, D.mote_offset.z, 0.f);
		RCache.set_c		("td_noise",	D.noise_scale, D.noise_threshold, D.noise_contrast, D.noise_amount);
		RCache.set_c		("td_noise_off",D.noise_offset.x, D.noise_offset.y, D.noise_offset.z, _max(D.noise_depth, 0.25f));
		RCache.set_c		("td_screen",	1.f / float(Device.dwWidth), 1.f / float(Device.dwHeight), 0.f, 0.f);
		RCache.Render		(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}

	RCache.set_CullMode		(CULL_CCW);
}
