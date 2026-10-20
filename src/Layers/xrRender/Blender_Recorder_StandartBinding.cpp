#include "stdafx.h"
#pragma hdrstop

#include "ResourceManager.h"
#include "blenders\Blender_Recorder.h"
#include "blenders\Blender.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

#include "dxRenderDeviceRender.h"
#include "xrRender_console.h"
#include "LightTrack.h"

// matrices
#define	BIND_DECLARE(xf)	\
class cl_xform_##xf	: public R_constant_setup {	virtual void setup (R_constant* C) { RCache.xforms.set_c_##xf (C); } }; \
	static cl_xform_##xf	binder_##xf
BIND_DECLARE(w);
BIND_DECLARE(invw);
BIND_DECLARE(v);
BIND_DECLARE(p);
BIND_DECLARE(wv);
BIND_DECLARE(vp);
BIND_DECLARE(wvp);

#define DECLARE_TREE_BIND(c)	\
	class cl_tree_##c: public R_constant_setup	{virtual void setup(R_constant* C) {RCache.tree.set_c_##c(C);} };	\
	static cl_tree_##c	tree_binder_##c

DECLARE_TREE_BIND(m_xform_v);
DECLARE_TREE_BIND(m_xform);
DECLARE_TREE_BIND(consts);
DECLARE_TREE_BIND(wave);
DECLARE_TREE_BIND(wind);
DECLARE_TREE_BIND(c_scale);
DECLARE_TREE_BIND(c_bias);
DECLARE_TREE_BIND(c_sun);

class cl_hemi_cube_pos_faces: public R_constant_setup
{
	virtual void setup(R_constant* C) {RCache.hemi.set_c_pos_faces(C);}
};

static cl_hemi_cube_pos_faces binder_hemi_cube_pos_faces;

class cl_hemi_cube_neg_faces: public R_constant_setup
{
	virtual void setup(R_constant* C) {RCache.hemi.set_c_neg_faces(C);}
};

static cl_hemi_cube_neg_faces binder_hemi_cube_neg_faces;

class cl_material: public R_constant_setup
{
	virtual void setup(R_constant* C) {RCache.hemi.set_c_material(C);}
};

static cl_material binder_material;

class cl_texgen : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fmatrix mTexgen;

#ifdef	USE_DX10
		Fmatrix			mTexelAdjust		= 
		{
			0.5f,				0.0f,				0.0f,			0.0f,
			0.0f,				-0.5f,				0.0f,			0.0f,
			0.0f,				0.0f,				1.0f,			0.0f,
			0.5f,				0.5f,				0.0f,			1.0f
		};
#else	//	USE_DX10
		float	_w						= float(Device.dwWidth);
		float	_h						= float(Device.dwHeight);
		float	o_w						= (.5f / _w);
		float	o_h						= (.5f / _h);
		Fmatrix			mTexelAdjust		= 
		{
			0.5f,				0.0f,				0.0f,			0.0f,
			0.0f,				-0.5f,				0.0f,			0.0f,
			0.0f,				0.0f,				1.0f,			0.0f,
			0.5f + o_w,			0.5f + o_h,			0.0f,			1.0f
		};
#endif	//	USE_DX10

		mTexgen.mul	(mTexelAdjust,RCache.xforms.m_wvp);

		RCache.set_c( C, mTexgen);
	}
};
static cl_texgen		binder_texgen;

class cl_VPtexgen : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fmatrix mTexgen;

#ifdef	USE_DX10
		Fmatrix			mTexelAdjust		= 
		{
			0.5f,				0.0f,				0.0f,			0.0f,
			0.0f,				-0.5f,				0.0f,			0.0f,
			0.0f,				0.0f,				1.0f,			0.0f,
			0.5f,				0.5f,				0.0f,			1.0f
		};
#else	//	USE_DX10
		float	_w						= float(Device.dwWidth);
		float	_h						= float(Device.dwHeight);
		float	o_w						= (.5f / _w);
		float	o_h						= (.5f / _h);
		Fmatrix			mTexelAdjust		= 
		{
			0.5f,				0.0f,				0.0f,			0.0f,
			0.0f,				-0.5f,				0.0f,			0.0f,
			0.0f,				0.0f,				1.0f,			0.0f,
			0.5f + o_w,			0.5f + o_h,			0.0f,			1.0f
		};
#endif	//	USE_DX10

		mTexgen.mul	(mTexelAdjust,RCache.xforms.m_vp);

		RCache.set_c( C, mTexgen);
	}
};
static cl_VPtexgen		binder_VPtexgen;

// fog
#ifndef _EDITOR
class cl_fog_plane	: public R_constant_setup {
	u32			marker;
	Fvector4	result;
	virtual void setup(R_constant* C)
	{
		if (marker!=Device.dwFrame)
		{
			// Plane
			Fvector4		plane;
			Fmatrix&	M	= Device.mFullTransform;
			plane.x			= -(M._14 + M._13);
			plane.y			= -(M._24 + M._23);
			plane.z			= -(M._34 + M._33);
			plane.w			= -(M._44 + M._43);
			float denom		= -1.0f / _sqrt(_sqr(plane.x)+_sqr(plane.y)+_sqr(plane.z));
			plane.mul		(denom);

			// Near/Far
			float A			= g_pGamePersistent->Environment().CurrentEnv->fog_near;
			float B			= 1/(g_pGamePersistent->Environment().CurrentEnv->fog_far - A);
			result.set		(-plane.x*B, -plane.y*B, -plane.z*B, 1 - (plane.w-A)*B	);								// view-plane
		}
		RCache.set_c	(C,result);
	}
};
static cl_fog_plane		binder_fog_plane;

// fog-params
class cl_fog_params	: public R_constant_setup {
	u32			marker;
	Fvector4	result;
	virtual void setup(R_constant* C)
	{
		if (marker!=Device.dwFrame)
		{
			// Near/Far
			float	n		= g_pGamePersistent->Environment().CurrentEnv->fog_near	;
			float	f		= g_pGamePersistent->Environment().CurrentEnv->fog_far		;
			float	r		= 1/(f-n);
			result.set		(-n*r, r, r, r);
		}
		RCache.set_c	(C,result);
	}
};	static cl_fog_params	binder_fog_params;

// fog-color
class cl_fog_color	: public R_constant_setup {
	u32			marker;
	Fvector4	result;
	virtual void setup	(R_constant* C)	{
		if (marker!=Device.dwFrame)	{
			CEnvDescriptor&	desc	= *g_pGamePersistent->Environment().CurrentEnv;
		#if RENDER==R_R1
			result.set(desc.fog_color.x * ps_r1_fog_luminance, desc.fog_color.y * ps_r1_fog_luminance, desc.fog_color.z * ps_r1_fog_luminance, 0.0f);
		#else
			result.set(desc.fog_color.x, desc.fog_color.y, desc.fog_color.z, 0.0f);
		#endif // RENDER==R_R1
		}
		RCache.set_c	(C,result);
	}
};	static cl_fog_color		binder_fog_color;
#endif

// times
class cl_times		: public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		float 		t	= Device.fTimeGlobal;
		RCache.set_c	(C,t,t*10,t/10,_sin(t))	;
	}
};
static cl_times		binder_times;

// Gunslinger exo HUD-screen constants (model_exohealth / model_exoscreen).
// Filled by CActor each frame into g_pGamePersistent; see IGame_Persistent.
class cl_actor_params : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent)	RCache.set_c (C, g_pGamePersistent->hud_actor_params);
		else					RCache.set_c (C, 1.f, 1.f, 1.f, 1.f);
	}
};
static cl_actor_params	binder_actor_params;

class cl_affects : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent)	RCache.set_c (C, g_pGamePersistent->hud_affects);
		else					RCache.set_c (C, 0.f, 0.f, 0.f, 0.f);
	}
};
static cl_affects	binder_affects;

// Gunslinger 3D PiP scope lens constants (model_scope_lense.ps): m_hud_params (aim/abberation/lens-visibility)
// and m_zoom_deviation (lens image offset). Filled by CActor each frame into g_pGamePersistent.
class cl_hud_params : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent)	RCache.set_c (C, g_pGamePersistent->hud_scope_params);
		else					RCache.set_c (C, 1.f, 0.f, 0.f, 0.f);
	}
};
static cl_hud_params	binder_hud_params;

class cl_zoom_deviation : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent)	RCache.set_c (C, g_pGamePersistent->hud_zoom_deviation);
		else					RCache.set_c (C, 0.f, 0.f, 0.f, 0.f);
	}
};
static cl_zoom_deviation	binder_zoom_deviation;

// The eye-relief crescent, the same constant the post-process pass uses for a flat scope: xy = how far
// the exit pupil has drifted off the axis in units of the glass radius, z = how soft that edge is.
// model_scope_lense.ps shades the lens with it so a 3D optic darkens exactly like the 2D one.
class cl_scope_shadow : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent)	RCache.set_c (C, g_pGamePersistent->pp_scope_shadow);
		else					RCache.set_c (C, 0.f, 0.f, 0.f, 0.f);
	}
};
static cl_scope_shadow	binder_scope_shadow;

// The coating on a collimator's glass: rgb = tint, 0.5 = clear. The glass's tint pass blends it
// as destcolor/srccolor, i.e. view * 2*rgb (models_lenses.s, model_collim_tint.ps).
class cl_collim_tint : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent)	RCache.set_c (C, g_pGamePersistent->hud_collim_tint);
		else					RCache.set_c (C, 0.5f, 0.5f, 0.5f, 0.5f);
	}
};
static cl_collim_tint	binder_collim_tint;

// The sky that glass mirrors (model_collim_reflect.ps): rgb = coating colour * strength, w = F0; and the
// actor's sky visibility along +x+y+z / -x-y-z to fade it where the sky is blocked.
// The glass's sky and reticle are drawn by its pass 0, which the world draws too: outside the HUD's
// strict-sorted pass they are 0, so a gun in the world shows its plain glass (see m_collim_glass).
extern bool r_dsgraph_hud_sorted_now;
class cl_collim_reflect : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent && r_dsgraph_hud_sorted_now)	RCache.set_c (C, g_pGamePersistent->hud_collim_reflect);
		else												RCache.set_c (C, 0.f, 0.f, 0.f, 0.25f);
	}
};
static cl_collim_reflect	binder_collim_reflect;

class cl_collim_reflect_ind : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent && r_dsgraph_hud_sorted_now)	RCache.set_c (C, g_pGamePersistent->hud_collim_reflect_ind);
		else												RCache.set_c (C, 0.f, 0.f, 0.f, 0.f);
	}
};
static cl_collim_reflect_ind	binder_collim_reflect_ind;

class cl_collim_reflect_shape : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent)	RCache.set_c (C, g_pGamePersistent->hud_collim_reflect_shape);
		else					RCache.set_c (C, 0.35f, 1.f, 0.f, 0.f);
	}
};
static cl_collim_reflect_shape	binder_collim_reflect_shape;

// The glass's own texture, faded by the aim (model_collim_glass.ps, pass 0 of the collimator glass).
// That pass is the world's too, so outside the HUD's strict-sorted pass it stays 1: the gun on the
// ground keeps its glass while the player aims his own.
extern bool r_dsgraph_hud_sorted_now;
class cl_collim_glass : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent && r_dsgraph_hud_sorted_now)	RCache.set_c (C, g_pGamePersistent->hud_collim_glass);
		else												RCache.set_c (C, 1.f, 1.f, 1.f, 0.f);
	}
};
static cl_collim_glass	binder_collim_glass;

// The collimator's shader reticle (model_collim_reflect): rgb = colour * intensity, 0 = no reticle.
class cl_collim_mark : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent && r_dsgraph_hud_sorted_now)	RCache.set_c (C, g_pGamePersistent->hud_collim_mark);
		else												RCache.set_c (C, 0.f, 0.f, 0.f, 0.f);
	}
};
static cl_collim_mark	binder_collim_mark;

// ...and where it sits: x = texcoord per tangent of the angle off the sight line, y/z = the zeroing
// offset as tangents -- both in the HUD's angles. The size and offset come in degrees of the WORLD's view,
// and the HUD draws through its own, narrower projection: a direction at tan t on screen is tan t *
// tan(hud/2) / tan(world/2) in the HUD. So the reticle covers the same piece of the world whatever the
// HUD fov does while aiming, and its centre stays on the screen's centre.
extern ENGINE_API float psHUD_FOV;
static float collim_hud_k()
{
	const float hud		= tanf(deg2rad(psHUD_FOV * Device.fFOV_HUD) * 0.5f);
	const float world	= tanf(deg2rad(Device.fFOV) * 0.5f);
	return (world > EPS) ? hud / world : 1.f;
}
class cl_collim_mark_proj : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		Fvector4 s;
		if (g_pGamePersistent)	s = g_pGamePersistent->hud_collim_mark_shape;
		else					s.set(2.f, 0.f, 0.f, 0.f);
		const float k		= collim_hud_k();
		const float hs		= tanf(deg2rad(s.x) * 0.5f) * k;
		RCache.set_c (C, (hs > EPS) ? 0.5f / hs : 0.f, tanf(deg2rad(s.y)) * k, tanf(deg2rad(s.z)) * k, 0.f);
	}
};
static cl_collim_mark_proj	binder_collim_mark_proj;

// A prism sight's field stop and its etched reticle (model_collim_tint, model_collim_mask): x = tangent of
// half the field of view, y = width of its soft edge as a tangent -- both turned into the hud's angles as
// the reticle's are -- z = how much the reticle darkens the view, w = 1 for a prism (0: no stop, no cut).
class cl_collim_prism : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		Fvector4 s;
		if (g_pGamePersistent)	s = g_pGamePersistent->hud_collim_prism;
		else					s.set(0.f, 1.f, 0.f, 0.f);
		// x > 170.5 is the lens-off prism mode of a magnifying optic, not a prism sight: it has its own constant
		// (m_scope_prism, below). Seen here it turned an ordinary collimator glass on the same gun -- the G36's
		// -- into a prism sight, cut out every other frame of the lens's fade and darkened by .z: it flickered.
		if (s.x > 170.5f)		s.set(0.f, 1.f, 0.f, 0.f);
		const float k		= collim_hud_k();
		const bool	prism	= s.x > 0.f;
		const float lo		= prism ? tanf(deg2rad(_max(s.x - s.y, 0.f)) * 0.5f) * k : 0.f;
		const float hi		= prism ? tanf(deg2rad(s.x + s.y) * 0.5f) * k : 0.f;
		RCache.set_c (C, (lo + hi) * 0.5f, hi - lo, s.z, prism ? 1.f : 0.f);
	}
};
static cl_collim_prism	binder_collim_prism;

// The lens-off prism mode of a magnifying optic (CWeapon::IsScopePrismCfg; CActor marks it with hud_collim_prism.x
// = 171): w = 1 on a frame with the eyepiece's cut, 2 on one without it while the lens fades (r__dsgraph_render.cpp
// g_scope_prism_cut), 0 off; x = 1 while the lens fades (the frames take turns). Read by models_zoom*.s only
// (model_scope_lense*.ps, model_scope_mask.ps, model_scope_late.ps).
class cl_scope_prism : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		extern bool g_scope_prism_cut;
		extern bool g_scope_prism_fade;
		const bool	on	= g_pGamePersistent && g_pGamePersistent->hud_collim_prism.x > 170.5f;
		RCache.set_c (C, (on && g_scope_prism_fade) ? 1.f : 0.f, 0.f, 0.f, !on ? 0.f : (g_scope_prism_cut ? 1.f : 2.f));
	}
};
static cl_scope_prism	binder_scope_prism;

// The sun shafts' haze over a prism sight's black field stop (model_collim_late), drawn over the finished
// picture: x = its share of their full strength, y = 1 while the sun's shafts are on at all.
class cl_collim_veil : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		const float v = g_pGamePersistent ? g_pGamePersistent->hud_collim_prism.w : 0.f;
		RCache.set_c (C, v, (ps_r_sun_shafts > 0) ? 1.f : 0.f, 0.f, 0.f);
	}
};
static cl_collim_veil	binder_collim_veil;

// The collimator glass's highlight (collim.h collim_specular): x = strength, y = glossiness, z = the texture
// alpha's mask. The hud's glass only -- none on the world's.
class cl_collim_spec : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent && r_dsgraph_hud_sorted_now)	RCache.set_c (C, g_pGamePersistent->hud_collim_spec);
		else												RCache.set_c (C, 0.f, 64.f, 0.f, 0.f);
	}
};
static cl_collim_spec	binder_collim_spec;

// The lights besides the sun that reach a collimator's glass on the hud (collim.h collim_dynlight): an array of
// COLLIM_LIGHTS triples, see collim_gather_lights. The hud's glass only -- black for the world's.
class cl_collim_lights : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		Fvector4	set	[COLLIM_LIGHTS*3];
		if (g_pGamePersistent && r_dsgraph_hud_sorted_now)	collim_gather_lights(set);
		else	for (u32 i = 0; i < COLLIM_LIGHTS*3; ++i)	set[i].set(0.f, 0.f, 0.f, 0.f);
		for (u32 i = 0; i < COLLIM_LIGHTS*3; ++i)	RCache.set_ca(C, i, set[i]);
	}
};
static cl_collim_lights	binder_collim_lights;

class cl_collim_hemi_pos : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent)	RCache.set_c (C, g_pGamePersistent->hud_collim_hemi_pos);
		else					RCache.set_c (C, 1.f, 1.f, 1.f, 0.f);
	}
};
static cl_collim_hemi_pos	binder_collim_hemi_pos;

class cl_collim_hemi_neg : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		if (g_pGamePersistent)	RCache.set_c (C, g_pGamePersistent->hud_collim_hemi_neg);
		else					RCache.set_c (C, 1.f, 1.f, 1.f, 0.f);
	}
};
static cl_collim_hemi_neg	binder_collim_hemi_neg;

#ifndef _EDITOR
// The weather's sky, for a forward shader that samples $user$sky0/1 itself the way the skybox does
// (dxEnvironmentRender::RenderSky): xyz = sky colour, w = blend from sky0 to sky1.
class cl_sky_params : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		CEnvDescriptorMixer&	env	= *g_pGamePersistent->Environment().CurrentEnv;
		RCache.set_c	(C, env.sky_color.x, env.sky_color.y, env.sky_color.z, env.weight);
	}
};
static cl_sky_params	binder_sky_params;

// ...and its turn about the vertical: x = cos, y = sin of sky_rotation (the skybox is drawn rotateY'd by it).
class cl_sky_rotation : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		const float a	= g_pGamePersistent->Environment().CurrentEnv->sky_rotation;
		RCache.set_c	(C, _cos(a), _sin(a), 0.f, 0.f);
	}
};
static cl_sky_rotation	binder_sky_rotation;
#endif

// Gunslinger m_digiclock (r_constants.pas binder_digiclock_setup): the in-game clock drawn on electronic
// weapon displays -- the gauss MUI's digit screens (models_digiclock_{hh,hl,mh,ml}_screen). Each channel is
// one digit as an atlas offset 0.0..0.9; the digit shader samples s_base at digit + tc.x*0.1.
class cl_digiclock : public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		int h = 0, m = 0;
		if (g_pGamePersistent)
		{
			const float t	= g_pGamePersistent->Environment().GetGameTime();	// seconds of the game day
			h				= int(t / 3600.f) % 24;
			m				= int(t / 60.f) % 60;
		}
		RCache.set_c	(C, float(h/10)/10.f, float(h%10)/10.f, float(m/10)/10.f, float(m%10)/10.f);
	}
};
static cl_digiclock		binder_digiclock;

// eye-params
class cl_eye_P		: public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		Fvector&		V	= Device.vCameraPosition;
		RCache.set_c	(C,V.x,V.y,V.z,1);
	}
};
static cl_eye_P		binder_eye_P;

// eye-params
class cl_eye_D		: public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		Fvector&		V	= Device.vCameraDirection;
		RCache.set_c	(C,V.x,V.y,V.z,0);
	}
};
static cl_eye_D		binder_eye_D;

// eye-params
class cl_eye_N		: public R_constant_setup {
	virtual void setup(R_constant* C)
	{
		Fvector&		V	= Device.vCameraTop;
		RCache.set_c	(C,V.x,V.y,V.z,0);
	}
};
static cl_eye_N		binder_eye_N;

#ifndef _EDITOR
// D-Light0
class cl_sun0_color	: public R_constant_setup {
	u32			marker;
	Fvector4	result;
	virtual void setup	(R_constant* C)	{
		if (marker!=Device.dwFrame)	{
			CEnvDescriptor&	desc	= *g_pGamePersistent->Environment().CurrentEnv;
			result.set				(desc.sun_color.x,	desc.sun_color.y, desc.sun_color.z,	0);
		}
		RCache.set_c	(C,result);
	}
};	static cl_sun0_color		binder_sun0_color;
class cl_sun0_dir_w	: public R_constant_setup {
	u32			marker;
	Fvector4	result;
	virtual void setup	(R_constant* C)	{
		if (marker!=Device.dwFrame)	{
			CEnvDescriptor&	desc	= *g_pGamePersistent->Environment().CurrentEnv;
			result.set				(desc.sun_dir.x,	desc.sun_dir.y, desc.sun_dir.z,	0);
		}
		RCache.set_c	(C,result);
	}
};	static cl_sun0_dir_w		binder_sun0_dir_w;
class cl_sun0_dir_e	: public R_constant_setup {
	u32			marker;
	Fvector4	result;
	virtual void setup	(R_constant* C)	{
		if (marker!=Device.dwFrame)	{
			Fvector D;
			CEnvDescriptor&	desc		= *g_pGamePersistent->Environment().CurrentEnv;
			Device.mView.transform_dir	(D,desc.sun_dir);
			D.normalize					();
			result.set					(D.x,D.y,D.z,0);
		}
		RCache.set_c	(C,result);
	}
};	static cl_sun0_dir_e		binder_sun0_dir_e;

//
class cl_amb_color	: public R_constant_setup {
	u32			marker;
	Fvector4	result;
	virtual void setup	(R_constant* C)	{
		if (marker!=Device.dwFrame)	{
			CEnvDescriptorMixer&	desc	= *g_pGamePersistent->Environment().CurrentEnv;
			result.set				(desc.ambient.x, desc.ambient.y, desc.ambient.z, desc.weight);
		}
		RCache.set_c	(C,result);
	}
};	static cl_amb_color		binder_amb_color;
class cl_hemi_color	: public R_constant_setup {
	u32			marker;
	Fvector4	result;
	virtual void setup	(R_constant* C)	{
		if (marker!=Device.dwFrame)	{
			CEnvDescriptor&	desc	= *g_pGamePersistent->Environment().CurrentEnv;
			result.set				(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z, desc.hemi_color.w);
		}
		RCache.set_c	(C,result);
	}
};	static cl_hemi_color		binder_hemi_color;

#endif

static class cl_screen_res : public R_constant_setup		
{	
	virtual void setup	(R_constant* C)
	{
		RCache.set_c	(C, (float)Device.dwWidth, (float)Device.dwHeight, 1.0f/(float)Device.dwWidth, 1.0f/(float)Device.dwHeight);
	}
}	binder_screen_res;


// Standart constant-binding
void	CBlender_Compile::SetMapping	()
{
	// matrices
	r_Constant				("m_W",				&binder_w);
	r_Constant				("m_invW",			&binder_invw);
	r_Constant				("m_V",				&binder_v);
	r_Constant				("m_P",				&binder_p);
	r_Constant				("m_WV",			&binder_wv);
	r_Constant				("m_VP",			&binder_vp);
	r_Constant				("m_WVP",			&binder_wvp);

	r_Constant				("m_xform_v",		&tree_binder_m_xform_v);
	r_Constant				("m_xform",			&tree_binder_m_xform);
	r_Constant				("consts",			&tree_binder_consts);
	r_Constant				("wave",			&tree_binder_wave);
	r_Constant				("wind",			&tree_binder_wind);
	r_Constant				("c_scale",			&tree_binder_c_scale);
	r_Constant				("c_bias",			&tree_binder_c_bias);
	r_Constant				("c_sun",			&tree_binder_c_sun);

	//hemi cube
	r_Constant				("L_material",			&binder_material);
	r_Constant				("hemi_cube_pos_faces",			&binder_hemi_cube_pos_faces);
	r_Constant				("hemi_cube_neg_faces",			&binder_hemi_cube_neg_faces);

	//	Igor	temp solution for the texgen functionality in the shader
	r_Constant				("m_texgen",			&binder_texgen);
	r_Constant				("mVPTexgen",			&binder_VPtexgen);

#ifndef _EDITOR
	// fog-params
	r_Constant				("fog_plane",		&binder_fog_plane);
	r_Constant				("fog_params",		&binder_fog_params);
	r_Constant				("fog_color",		&binder_fog_color);
#endif
	// time
	r_Constant				("timers",			&binder_times);

	// Gunslinger exo HUD-screen shader constants
	r_Constant				("m_actor_params",	&binder_actor_params);
	r_Constant				("m_affects",		&binder_affects);

	// Gunslinger 3D PiP scope lens shader constants
	r_Constant				("m_hud_params",	&binder_hud_params);
	r_Constant				("m_zoom_deviation",&binder_zoom_deviation);
	r_Constant				("m_scope_shadow",	&binder_scope_shadow);
	r_Constant				("m_collim_tint",	&binder_collim_tint);
	r_Constant				("m_collim_reflect",	&binder_collim_reflect);
	r_Constant				("m_collim_reflect_ind",	&binder_collim_reflect_ind);
	r_Constant				("m_collim_reflect_shape",	&binder_collim_reflect_shape);
	r_Constant				("m_collim_glass",	&binder_collim_glass);
	r_Constant				("m_collim_mark",	&binder_collim_mark);
	r_Constant				("m_collim_mark_proj",	&binder_collim_mark_proj);
	r_Constant				("m_collim_prism",	&binder_collim_prism);
	r_Constant				("m_scope_prism",	&binder_scope_prism);
	r_Constant				("m_collim_veil",	&binder_collim_veil);
	r_Constant				("m_collim_lights",	&binder_collim_lights);
	r_Constant				("m_collim_spec",	&binder_collim_spec);
	r_Constant				("m_collim_hemi_pos",	&binder_collim_hemi_pos);
	r_Constant				("m_collim_hemi_neg",	&binder_collim_hemi_neg);
#ifndef _EDITOR
	r_Constant				("m_sky_params",	&binder_sky_params);
	r_Constant				("m_sky_rotation",	&binder_sky_rotation);
#endif
	r_Constant				("m_digiclock",		&binder_digiclock);

	// eye-params
	r_Constant				("eye_position",	&binder_eye_P);
	r_Constant				("eye_direction",	&binder_eye_D);
	r_Constant				("eye_normal",		&binder_eye_N);

#ifndef _EDITOR
	// global-lighting (env params)
	r_Constant				("L_sun_color",		&binder_sun0_color);
	r_Constant				("L_sun_dir_w",		&binder_sun0_dir_w);
	r_Constant				("L_sun_dir_e",		&binder_sun0_dir_e);
//	r_Constant				("L_lmap_color",	&binder_lm_color);
	r_Constant				("L_hemi_color",	&binder_hemi_color);
	r_Constant				("L_ambient",		&binder_amb_color);
#endif
	r_Constant				("screen_res",		&binder_screen_res);

	// detail
	//if (bDetail	&& detail_scaler)
	//	Igor: bDetail can be overridden by no_detail_texture option.
	//	But shader can be deatiled implicitly, so try to set this parameter
	//	anyway.
	if (detail_scaler)
		r_Constant			("dt_params",		detail_scaler);

	// other common
	for (u32 it=0; it<DEV->v_constant_setup.size(); it++)
	{
		std::pair<shared_str,R_constant_setup*>	cs	= DEV->v_constant_setup[it];
		r_Constant			(*cs.first,cs.second);
	}
}
