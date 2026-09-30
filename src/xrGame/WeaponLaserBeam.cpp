#include "stdafx.h"
#include "WeaponLaserBeam.h"
#include "../xrEngine/IGame_Persistent.h"
#include "../xrEngine/Environment.h"
#include "../xrEngine/bone.h"
#include "../Include/xrRender/Kinematics.h"
#include "../xrEngine/EnnumerateVertices.h"

extern ENGINE_API float psHUD_FOV;	// hud fov as a fraction of the world fov

void SLaserBeamParams::Load(LPCSTR s)
{
	enabled = beam = dot = dot_light = false;
	dot_particle	= true;
	LightOff		();
	if (!s || !s[0] || !pSettings->section_exist(s))	return;
	// Only R3 draws the volumetric beam and the procedural dot (r3_rendertarget_phase_laser.cpp). On R1/R2
	// the whole section is ignored, so those keep what they always had: the model's mesh beam and the
	// particle dot. The RUNNING renderer (dx level: R3 is DX10), not the one picked in the options --
	// that one only starts after a restart.
	if (!::Render || ::Render->get_dx_level() < 0x000A0000)	return;
	beam			=!!READ_IF_EXISTS(pSettings, r_bool, s, "laser_beam_enabled", TRUE);

	// the procedural dot and its surface light; the particle dot is the fallback, on unless the new one is
	dot				= !!READ_IF_EXISTS(pSettings, r_bool, s, "laser_beam_dot", FALSE);
	dot_particle	= !!READ_IF_EXISTS(pSettings, r_bool, s, "laser_beam_dot_particle", !dot);
	dot_intensity	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dot_intensity", 4.f);
	dot_core		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dot_core", 0.004f);
	dot_halo		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dot_halo", 0.03f);
	dot_halo_gain	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dot_halo_gain", 0.15f);
	dot_white		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dot_white", 1.f);
	dot_min_px		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dot_min_px", 3.f);
	dot_halo_min_gain = READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dot_halo_min_gain", 0.f);
	clamp			(dot_halo_min_gain, 0.f, 1.f);
	dot_light		= !!READ_IF_EXISTS(pSettings, r_bool, s, "laser_beam_dot_light", FALSE);
	dot_light_range	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dot_light_range", 0.4f);
	dot_light_bright= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dot_light_bright", 0.6f);
	dot_light_offset= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dot_light_offset", 0.05f);
	enabled			= beam || dot;
	if (!enabled)										return;
	hide_mesh		= !!READ_IF_EXISTS(pSettings, r_bool, s, "laser_beam_hide_mesh", FALSE);

	color			= READ_IF_EXISTS(pSettings, r_fvector3, s, "laser_beam_color", Fvector().set(1.f, 0.08f, 0.05f));
	intensity		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_intensity", 1.f);
	radius			= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_radius", 0.0025f);
	max_length		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_max_length", 60.f);
	fade_cam_start	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_fade_cam_start", 3.f);
	fade_cam_end	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_fade_cam_end", 45.f);
	fade_start		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_fade_start", 0.05f);
	fade_end_hit	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_fade_end_hit", 0.05f);
	fade_end_far	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_fade_end_far", 15.f);
	falloff			= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_falloff", 0.f);
	view_boost		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_view_boost", 4.f);

	core			= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_core", 0.25f);
	dust			= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dust", 1.5f);
	dust_scale		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dust_scale", 4.f);
	dust_scale2		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dust_scale2", 0.6f);
	dust_threshold	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dust_threshold", 0.4f);
	dust_contrast	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dust_contrast", 5.f);
	dust_depth		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dust_depth", 2.f);
	dust_drift		= READ_IF_EXISTS(pSettings, r_fvector3, s, "laser_beam_dust_drift", Fvector().set(0.02f, 0.01f, 0.015f));
	dust_drift2		= READ_IF_EXISTS(pSettings, r_fvector3, s, "laser_beam_dust_drift2", Fvector().set(-0.015f, -0.005f, 0.02f));
	dust_wind		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_dust_wind", 0.01f);

	fog_base		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_fog_base", 0.5f);
	fog_gain		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_fog_gain", 1.f);
	fog_dist_gain	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_fog_dist_gain", 1.f);
	fog_dist_max	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_fog_dist_max", 1000.f);
	fog_dist_power	= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_fog_dist_power", 2.f);

	smoke_gain		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_smoke_gain", 4.f);
	smoke_range		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_smoke_range", 1.5f);
	rain_gain		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_rain_gain", 3.f);
	rain_range		= READ_IF_EXISTS(pSettings, r_float, s, "laser_beam_rain_range", 0.5f);

	texture			= READ_IF_EXISTS(pSettings, r_string, s, "laser_beam_texture", "fx\\fx_laser_dust");

	Fvector2 aa		= READ_IF_EXISTS(pSettings, r_fvector2, s, "laser_beam_axis_angle", Fvector2().set(10.f, 25.f));
	axis_angle_start= deg2rad(_min(aa.x, aa.y));
	axis_angle_end	= deg2rad(_max(aa.y, aa.x + 0.1f));
	// off by default: the dot keeps its own (tuned) logic; only the beam turns onto the weapon's axis
	dot_follow		= !!READ_IF_EXISTS(pSettings, r_bool, s, "laser_beam_dot_follow", FALSE);
	hud.model		= world.model	= nullptr;	// re-read the emitters
	hud.valid		= world.valid	= false;

	clamp			(dust_scale, 0.01f, 1000.f);
	clamp			(dust_scale2, 0.01f, 100.f);
	fog_dist_max	= _max(fog_dist_max, 1.f);
	fog_dist_power	= _max(fog_dist_power, 0.01f);
}

// ---- this frame's beams, for the renderer ------------------------------------------------------
static xr_vector<SLaserBeamRender>	s_beams;
static u32							s_beams_frame = u32(-1);

u32 LaserBeams_Get(const SLaserBeamRender*& beams)
{
	// A beam is published from CActor::UpdateCL, which does not run while the game is paused -- the
	// menu still renders the level behind it, so keep showing the last frame's beams then.
	if (s_beams.empty() || (s_beams_frame != Device.dwFrame && !Device.Paused()))
	{
		beams = nullptr;
		return 0;
	}
	beams = &s_beams.front();
	return (u32)s_beams.size();
}

static void wrap_to_tile(Fvector& v, float tile)
{
	v.x -= tile * floorf(v.x / tile);
	v.y -= tile * floorf(v.y / tile);
	v.z -= tile * floorf(v.z / tile);
}

// Offset that makes the shader sample noise at (Q - acc): the dust drifts WITH acc. Whole tiles
// next to the eye are subtracted as well -- the pattern repeats every tile, so nothing moves, but
// the shader's coordinates stay small (float precision) however far from the level origin we are.
static void dust_offset(Fvector& out, const Fvector& acc, float tile)
{
	const Fvector& e = Device.vCameraPosition;
	out.set(-acc.x - tile * floorf(e.x / tile),
			-acc.y - tile * floorf(e.y / tile),
			-acc.z - tile * floorf(e.z / tile));
}

void SLaserBeamParams::LightOff()
{
	if (light)	light->set_active(false);
}

void SLaserBeamParams::Publish(const Fvector& from, const Fvector& axis, const Fvector& to, bool hit, bool dot_ok, const Fvector& dot_pos)
{
	if (!enabled || !g_pGamePersistent)	{ LightOff(); return; }

	// Only what lies IN FRONT of the emitter along its axis. Pressed against a wall, the HUD gun passes
	// through it and the emitter ends up beyond the surface the trace stops at: the dot lands behind the
	// emitter and the beam would turn round towards the camera. Then there is no beam (it is stopped at
	// the aperture), and the dot fades out over its last few centimetres in front of the emitter.
	const float	front_fade	= 0.05f;
	float		dot_fade	= 1.f;
	dot_ok			= dot_ok && dot && _valid(dot_pos);
	if (dot_ok)
	{
		dot_fade	= Fvector().sub(dot_pos, from).dotproduct(axis) / front_fade;
		clamp		(dot_fade, 0.f, 1.f);
		dot_ok		= dot_fade > 0.f;
	}
	Fvector d;		d.sub(to, from);
	float len =		d.magnitude();
	const bool draw_beam = beam && len > EPS_L && _valid(d) && d.dotproduct(axis) > 0.01f;
	if (!draw_beam && !dot_ok)			{ LightOff(); return; }
	if (draw_beam)	d.mul(1.f / len);
	else			{ d.set(0.f, 0.f, 1.f); len = 0.f; }
	float fend =	hit ? fade_end_hit : fade_end_far;
	if (len > max_length)	{ len = max_length; fend = fade_end_far; }

	CEnvDescriptor& E	= *g_pGamePersistent->Environment().CurrentEnv;

	// dust drift: its own two directions plus a share of the weather's wind, integrated here so a
	// change of wind speeds the dust up instead of making it jump
	Fvector wind;	wind.setHP(E.wind_direction, 0.f);
	wind.mul		(E.wind_velocity * dust_wind);
	const float dt	= Device.Paused() ? 0.f : Device.fTimeDelta;
	const float tile1 = 1.f / dust_scale;
	const float tile2 = tile1 / dust_scale2;
	acc1.mad(dust_drift, dt);	acc1.mad(wind, dt);	wrap_to_tile(acc1, tile1);
	acc2.mad(dust_drift2, dt);	acc2.mad(wind, dt);	wrap_to_tile(acc2, tile2);

	SLaserBeamRender B;
	B.start			= from;
	B.length		= len;			// 0 = no beam, only the dot
	B.dir			= d;
	B.radius		= radius;
	// more fog = more stuff in the air for the beam to light up: a denser fog, and a fog wall standing closer
	float fog_near	= 1.f - E.fog_distance / fog_dist_max;
	clamp			(fog_near, 0.f, 1.f);
	float fog_k		= fog_base + fog_gain * E.fog_density + fog_dist_gain * powf(fog_near, fog_dist_power);
	B.color.mul		(color, intensity * _max(fog_k, 0.f));
	B.view_boost	= view_boost;
	B.fade_cam_start= fade_cam_start;
	B.fade_cam_end	= fade_cam_end;
	B.fade_start	= fade_start;
	B.fade_end		= fend;
	B.dust_scale	= dust_scale;
	B.dust_threshold= dust_threshold;
	B.dust_contrast	= dust_contrast;
	B.dust_amount	= dust;
	dust_offset		(B.dust_offset1, acc1, tile1);
	B.core_amount	= core;
	dust_offset		(B.dust_offset2, acc2, tile2);
	B.dust_scale2	= dust_scale2;
	B.smoke_gain	= smoke_gain;
	B.smoke_range	= smoke_range;
	B.falloff		= falloff;
	B.rain_gain		= rain_gain;
	B.rain_range	= rain_range;
	B.dust_depth	= dust_depth;
	B._reserved[0]	= B._reserved[1] = 0.f;
	B.texture		= texture;

	B.dot_show		= dot_ok ? dot_fade : 0.f;
	B.dot_pos		= dot_ok ? dot_pos : from;
	B.dot_color.mul	(color, dot_intensity);
	B.dot_core		= dot_core;
	B.dot_halo		= dot_halo;
	B.dot_halo_gain	= dot_halo_gain;
	B.dot_white		= dot_white;
	B.dot_min_px	= dot_min_px;
	B.dot_halo_min_gain	= dot_halo_min_gain;
	B._reserved_dot[0] = B._reserved_dot[1] = B._reserved_dot[2] = 0.f;

	// the dot's light: a small shadowless point light hung just off the surface, on the side the laser
	// comes from (the trace gives no normal; the emitter is on the lit side by definition)
	if (dot_ok && dot_light)
	{
		if (!light)
		{
			light = ::Render->light_create();
			light->set_type		(IRender_Light::POINT);
			light->set_shadow	(false);
			light->set_volumetric(false);
			light->set_hud_mode	(false);
		}
		Fvector back;	back.sub(from, dot_pos);	back.normalize_safe();
		Fvector lp;		lp.mad(dot_pos, back, dot_light_offset);
		light->set_position	(lp);
		light->set_range	(dot_light_range);
		const float lb		= dot_light_bright * dot_fade;
		light->set_color	(color.x * lb, color.y * lb, color.z * lb);
		light->set_active	(true);
	}
	else
		LightOff();

	if (s_beams_frame != Device.dwFrame)
	{
		s_beams.clear	();
		s_beams_frame	= Device.dwFrame;
	}
	s_beams.push_back	(B);
}

// tan(world fov / 2) / tan(hud fov / 2): how much further off the view axis a HUD point has to be put
// for the world projection to draw it over the same pixel
static float hud_to_world_k()
{
	const float th	= tanf(deg2rad(0.5f * psHUD_FOV * Device.fFOV_HUD));
	const float tw	= tanf(deg2rad(0.5f * Device.fFOV));
	return (th > EPS_S) ? tw / th : 1.f;
}

void LaserBeam_HudDirToWorld(Fvector& d)
{
	const Fvector& cdir	= Device.vCameraDirection;
	Fvector par;	par.mul(cdir, d.dotproduct(cdir));
	Fvector perp;	perp.sub(d, par);
	perp.mul		(hud_to_world_k());
	d.add			(par, perp);
	d.normalize_safe();
}

float SLaserBeamParams::AxisWeight(float swing) const
{
	float t = (swing - axis_angle_start) / (axis_angle_end - axis_angle_start);
	clamp(t, 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

// ---- the mesh beam's emitter -----------------------------------------------------------------------
struct SLaserVertCollect : public SEnumVerticesCallback
{
	Fmatrix					xf;		// bind-pose model space -> ref bone space
	xr_vector<Fvector>*		out;
	virtual void operator () (const Fvector& p)	{ Fvector q; xf.transform_tiny(q, p); out->push_back(q); }
};

static void split_bone_csv(LPCSTR csv, xr_vector<shared_str>& out)
{
	if (!csv)	return;
	string256 item;
	for (int i = 0, n = _GetItemCount(csv); i < n; ++i)
	{
		_GetItem(csv, i, item);
		if (item[0])	out.push_back(item);
	}
}

bool SLaserEmitter::Update(IKinematics* K, LPCSTR ray_bones, LPCSTR attach_bone)
{
	if (K == model)		return valid;
	model	= K;
	valid	= false;
	if (!K || !attach_bone || !attach_bone[0])	return false;

	const u16 attach = K->LL_BoneID(attach_bone);
	if (attach == BI_NONE)						return false;

	xr_vector<u16> rays;
	{
		xr_vector<shared_str> names;
		split_bone_csv((ray_bones && ray_bones[0]) ? ray_bones : attach_bone, names);
		for (const shared_str& n : names)
		{
			u16 id = K->LL_BoneID(n);
			if (id != BI_NONE)	rays.push_back(id);
		}
	}
	if (rays.empty())							return false;

	// the device: walk up from the attach bone past every beam bone
	u16 ref = attach;
	while (std::find(rays.begin(), rays.end(), ref) != rays.end())
	{
		u16 parent = K->LL_GetData(ref).GetParentID();
		if (parent == BI_NONE)	break;
		ref = parent;
	}

	xr_vector<Fmatrix> binds;
	K->LL_GetBindTransform(binds);
	if (ref >= binds.size() || attach >= binds.size())	return false;
	Fmatrix inv_ref;	inv_ref.invert(binds[ref]);
	attach_rel.mul_43	(inv_ref, binds[attach]);

	// the beam geometry (EnumBoneVertices hands out bind-pose, model-space positions), in ref space
	xr_vector<Fvector> P;
	SLaserVertCollect C;	C.xf.set(inv_ref);	C.out = &P;
	for (u16 id : rays)		K->EnumBoneVertices(C, id);
	if (P.size() < 2)
	{
		Msg("! laser beam: no mesh-beam geometry on bones [%s] -- the beam starts at [%s] itself", ray_bones, attach_bone);
		return false;
	}

	// its long axis: the two vertices farthest apart (two sweeps give the diameter closely enough for a
	// thin stick of a mesh)
	u32 a = 0, b = 0;	float best = -1.f;
	for (u32 i = 0; i < P.size(); ++i)	{ float d = P[i].distance_to_sqr(P[0]);	if (d > best) { best = d; a = i; } }
	best = -1.f;
	for (u32 i = 0; i < P.size(); ++i)	{ float d = P[i].distance_to_sqr(P[a]);	if (d > best) { best = d; b = i; } }
	const float len = _sqrt(best);
	if (len < 0.05f)							return false;

	// the emitter end is the one nearer the weapon (the model's origin): the mesh runs far out of the gun
	Fvector ma, mb;
	binds[ref].transform_tiny(ma, P[a]);
	binds[ref].transform_tiny(mb, P[b]);
	if (mb.square_magnitude() < ma.square_magnitude())	std::swap(a, b);
	// Rough axis from that pair -- but the pair are opposite CORNERS of the stick (a 25 mm cross-section
	// over 0.7 m tilts it by ~3 deg), so the real axis runs between the middles of the two end caps.
	Fvector rough;	rough.sub(P[b], P[a]);	rough.mul(1.f / len);
	const float cap = _max(0.005f, len * 0.02f);
	Fvector s0, s1;	s0.set(0.f, 0.f, 0.f);	s1.set(0.f, 0.f, 0.f);
	u32 n0 = 0, n1 = 0;
	for (const Fvector& p : P)
	{
		const float t = Fvector().sub(p, P[a]).dotproduct(rough);
		if (t < cap)			{ s0.add(p); ++n0; }
		else if (t > len - cap)	{ s1.add(p); ++n1; }
	}
	if (!n0 || !n1)								return false;
	pos.div		(s0, float(n0));				// the middle of the emitter end
	s1.div		(float(n1));
	dir.sub		(s1, pos);
	if (dir.magnitude() < 0.05f)				return false;
	dir.normalize();

	ref_bone	= ref;
	valid		= _valid(pos) && _valid(dir);
	return valid;
}

void LaserBeam_HudPointToWorld(Fvector& p)
{
	// A view-space point (x, y, z) lands on screen at x*cot(fov/2)/z, so drawn with the world fov it
	// has to sit at x * tan(world/2) / tan(hud/2) to cover the pixel the hud projection gives it.
	const Fvector& cpos	= Device.vCameraPosition;
	const Fvector& cdir	= Device.vCameraDirection;
	Fvector v;		v.sub(p, cpos);
	float along =	v.dotproduct(cdir);
	if (along <= EPS_L)		return;
	Fvector par;	par.mul(cdir, along);
	Fvector perp;	perp.sub(v, par);
	perp.mul		(hud_to_world_k());
	p.add			(cpos, par);
	p.add			(perp);
}
