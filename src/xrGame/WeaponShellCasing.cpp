//////////////////////////////////////////////////////////////////////
// WeaponShellCasing.cpp:	real cartridge cases thrown out on a shot (see ShellCasing.h)
//
// WHICH case: the fired round's ammo section names it (shell_casing = <section>), so the calibre -- and
// for the shotguns the kind of shell -- follows the ammunition, not the gun.
// A belt-fed gun throws a second thing with every round, the belt link: shell_casing_extra = <section>
// in the weapon section, with shell_casing_extra_bone naming the hud bone that carries it.
//
// WHERE FROM, first person: the hud models carry an animated shell bone, and every shot animation
// throws it out of that gun's own ejection port. So instead of a table of ports per weapon, the shot
// is followed: the frame the animated shell actually FLIES -- a step at a believable speed, between two
// positions that are both at the gun -- a real case is spawned where it is, heading the way and at the
// speed it was going. The animated shell itself is not drawn during a shot at all: it is hidden from
// the shot on (and calculated on demand, to be followed), and shown again by whatever animation comes next.
// What that rule sorts out by itself:
//  - the idle pose keeps the bone parked metres away and the shot animation starts it in the chamber:
//    that jump is no flight (far end, absurd speed);
//  - a pump gun leaves the shell in the chamber until the slide is racked: the case appears then;
//  - a break-action gun does not move its shells on a shot at all: no case.
// WHERE FROM, third person and NPCs: the weapon's own shell_point, as the old shell particle did.
//
// Settings: section [shell_casings]. Per weapon, in the weapon section:
//   shell_casing        = false     no cases from this weapon
//   shell_casing_window = <sec>     how long to wait for the hud shell to fly (pump guns)
//   shell_casing_no_anim= true      the hud model does not animate its shell: throw at once
//   shell_casing_point  = x,y,z     ...from this point of the hud model, in the space of the bone
//   shell_casing_point_bone         named here (wpn_body by default)
//   shell_casing_hip_offset = x,y,z first person, out of the sights: move the point (right, up, forward; m)
//   shell_casing_side   = <m>       how far off the barrel line the hud shell has to get before the case
//                                   takes over (a pump gun: past the stretch the slide drags it back)
//   shell_casing_override = <sect>  this case, whatever round is loaded (the shooting-range guns)
//   shell_casing_bone   = <bone>    the hud shell bone, when it is none of the usual names
//   shell_casing_dir    = x,y,z     direction when there is no animation to take it from
//                                   (x to the right of the gun, y up, z along the barrel)
//   shell_casing_up_bias = <n>      this much lift instead of the common up_bias (below 0: thrown downwards)
//   shell_casing_unjam_speed = min,max  m/s of a case racked out by hand (clearing a jam, a shotgun's
//                                   reload-open), instead of unjam_speed_scale of the usual range
//   shell_casing_world_offset = x,y,z   third person: move the point, world model space (right, up, forward; m);
//                                   shell_casing_extra_world_offset the same for the extra thing
//   shell_casing_speed  = min,max   m/s; the animation's own speed is kept inside it too
//   shell_casing_extra, shell_casing_extra_bone, shell_casing_extra_dir, shell_casing_extra_speed
//                                   the same for the second thing thrown (a belt link)
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Weapon.h"
#include "ShellCasing.h"
#include "player_hud.h"
#include "level.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"

extern ENGINE_API float psHUD_FOV;	// hud fov as a fraction of the world fov

static LPCSTR SC_SECT = "shell_casings";

// per track: 0 = the cartridge case, 1 = the extra (belt link)
static LPCSTR KEY_WORLD_OFFSET[2] = { "shell_casing_world_offset", "shell_casing_extra_world_offset" };
static LPCSTR KEY_DIR	[2] = { "shell_casing_dir",		"shell_casing_extra_dir"	};
static LPCSTR KEY_SPEED	[2] = { "shell_casing_speed",	"shell_casing_extra_speed"	};
static LPCSTR TRACK_NAME[2] = { "case",					"extra"						};

// A point of the hud model is drawn with the hud projection; a case is a world object and goes through
// the world one. Move the point so that the world projection puts it where the hud projection shows it
// (the depth along the view is kept, only the part across it is scaled) -- the same correction the muzzle
// smoke and the laser dot get, see SmokePointWorldToHud in ShootingObject.cpp.
static void casing_point_hud_to_world(Fvector& p)
{
	const float t_hud = tanf(deg2rad(0.5f * psHUD_FOV * Device.fFOV_HUD));
	const float t_wld = tanf(deg2rad(0.5f * Device.fFOV));
	if (t_hud <= EPS_L || t_wld <= EPS_L)	return;

	const Fvector& cpos = Device.vCameraPosition;
	const Fvector& cdir = Device.vCameraDirection;
	Fvector v;		v.sub(p, cpos);
	const float along = v.dotproduct(cdir);
	if (along <= EPS_L)	return;

	Fvector par;	par.mul(cdir, along);
	Fvector perp;	perp.sub(v, par);
	perp.mul		(t_wld / t_hud);
	p.add			(cpos, par);
	p.add			(perp);
}

static void casing_camera_basis(Fmatrix& B)
{
	B.identity		();
	B.i.set			(Device.vCameraRight);
	B.j.set			(Device.vCameraTop);
	B.k.set			(Device.vCameraDirection);
}

// a world vector / point seen from the camera: right, up, forward -- for the debug line
static Fvector casing_in_camera(const Fvector& v, bool point)
{
	Fvector d;		d.set(v);
	if (point)		d.sub(Device.vCameraPosition);
	Fvector r;
	r.set			(d.dotproduct(Device.vCameraRight), d.dotproduct(Device.vCameraTop), d.dotproduct(Device.vCameraDirection));
	return			r;
}

static bool casing_debug()
{
	return !!READ_IF_EXISTS(pSettings, r_bool, SC_SECT, "debug", FALSE);
}

// How far into its current animation the hud model is, in seconds of the motion itself (the motion's own
// speed and the alias speed factor do not change it): the newest blend that is not fading out. -1 = none.
static float casing_hud_motion_time(IKinematics* K)
{
	IKinematicsAnimated* ka	= K ? K->dcast_PKinematicsAnimated() : NULL;
	if (!ka || !ka->partitions().count())	return -1.f;
	float t					= -1.f;
	for (u32 i = 0, n = ka->LL_PartBlendsCount(0); i < n; ++i)
	{
		CBlend* B			= ka->LL_PartBlend(0, i);
		if (!B || (B->blend == CBlend::eFalloff) || (B->blend == CBlend::eFREE_SLOT))	continue;
		if ((t < 0.f) || (B->timeCurrent < t))	t = B->timeCurrent;
	}
	return t;
}

// How fast a bone turned from one transform to the next, dt apart: the angular velocity, in the space of the
// transforms. For a small turn by t about a, each axis e moves by t*(a x e), and the three e x e' add up to 2t*a.
static Fvector casing_spin(const Fmatrix& from, const Fmatrix& to, float dt)
{
	Fvector w;			w.set(0.f, 0.f, 0.f);
	if (dt <= EPS_S)	return w;
	const Fvector* A[3]	= { &from.i, &from.j, &from.k };
	const Fvector* B[3]	= { &to.i, &to.j, &to.k };
	for (u32 k = 0; k < 3; ++k)
	{
		Fvector x;		x.set(*A[k]);	x.normalize_safe();
		Fvector y;		y.set(*B[k]);	y.normalize_safe();
		Fvector c;		c.crossproduct(x, y);
		w.add			(c);
	}
	w.mul				(0.5f / dt);
	return w;
}

// The keys of a dropped thing: reload_drop_<name> for a magazine, gl_drop_<name> for the grenade launcher's
// spent case (one buffer: use the result right away).
static LPCSTR drop_key(bool gl, LPCSTR name)
{
	static string64 k;
	xr_sprintf		(k, "%s_drop_%s", gl ? "gl" : "reload", name);
	return			k;
}

// Bone visibility is a 64-bit mask in the engine: a bone id past it would alias another bone's bit.
static void casing_show_hud_bone(IKinematics* K, u16 bone, BOOL show)
{
	if (bone >= 64 || bone >= K->LL_BoneCount())	return;
	if (!!K->LL_GetBoneVisible(bone) != !!show)
		K->LL_SetBoneVisible	(bone, show, TRUE);
}

// The animated shell of a hud model: the one of the usual names that is shown. A gun that comes in two
// calibres carries a shell for each and shows the right one; the shotguns have one per kind of shell and
// the ammo-bones pass shows the one that matches what is loaded. own_hidden: the bone this weapon hid
// itself after the previous shot -- still the right one, just not visible at the moment.
static u16 casing_hud_bone(IKinematics* K, LPCSTR weapon_sect, u16 own_hidden)
{
	if (pSettings->line_exist(weapon_sect, "shell_casing_bone"))
		return K->LL_BoneID(pSettings->r_string(weapon_sect, "shell_casing_bone"));

	static LPCSTR names[] = { "shell", "shell5x45", "shell_red", "shell_blue", "shell_green", "gilza" };
	for (u32 i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
	{
		const u16 id = K->LL_BoneID(names[i]);
		if (id != BI_NONE && K->LL_GetBoneVisible(id))	return id;
	}
	return own_hidden;
}

// The case a round of this ammo section leaves, on this weapon. false: no real cases here.
static bool casing_section(LPCSTR ws, LPCSTR ammo, shared_str& sect)
{
	if (!IsGameTypeSingle())											return false;
	if (!pSettings->section_exist(SC_SECT))								return false;
	if (!READ_IF_EXISTS(pSettings, r_bool, SC_SECT, "enabled", TRUE))	return false;
	if (!READ_IF_EXISTS(pSettings, r_bool, ws, "shell_casing", TRUE))	return false;

	// the round's ammo section names it -- unless the weapon names its case itself, whatever it is
	// loaded with: the shooting-range guns fire a stand-in round, but they are the same guns and throw
	// the same cases
	if (pSettings->line_exist(ws, "shell_casing_override"))			sect = pSettings->r_string(ws, "shell_casing_override");
	else if (ammo && pSettings->line_exist(ammo, "shell_casing"))	sect = pSettings->r_string(ammo, "shell_casing");
	else																return false;
	return					(sect.size() && pSettings->section_exist(sect.c_str()));
}

bool CWeapon::gwr_ShellCasingOnShot(const Fvector& owner_vel)
{
	LPCSTR ws = cNameSect().c_str();

	// the round being fired is still on top of the magazine at this point
	LPCSTR ammo = NULL;
	if (!m_magazine.empty())					ammo = m_magazine.back().m_ammoSect.c_str();
	else if (m_ammoType < m_ammoTypes.size())	ammo = m_ammoTypes[m_ammoType].c_str();
	shared_str sect;
	if (!casing_section(ws, ammo, sect))		return false;

	// whatever of the previous shot is still waiting for its animation (fast fire at a low frame rate)
	// is settled now, with what is known
	gwr_ShellCasingUpdate	(true);

	// The first shot out of a freshly loaded drum (GS need_first_shoot_anims: the Protecta / SPAS-12) has
	// its own take, anm_shoot*_first: the drum turns and nothing is thrown -- the fired shell rides in the
	// chamber until the NEXT shot pushes it out. So no case from this one, and the animated shell stays on
	// the model. (Left to the tracking, the 3 cm the drum carries the shell sideways passed for a flight.)
	if (GetHUDmode())
	{
		LPCSTR cur		= CurrentMotion().c_str();
		if (cur && strstr(cur, "anm_shoot") && strstr(cur, "_first"))
		{
			if (casing_debug())
				Msg		("~ shell_casing [%s]: the first shot after a reload (%s) -- nothing thrown", ws, cur);
			return		true;
		}
	}

	// from here on the weapon is on real cases: no shell particle in third person (first person keeps it,
	// as the smoke out of the port), even when this one is too far to bother
	const float dist = READ_IF_EXISTS(pSettings, r_float, SC_SECT, "spawn_distance", 25.f);
	if (Device.vCameraPosition.distance_to_sqr(Position()) > dist * dist)	return true;

	gwr_ShellCasingArm		(0, sect, owner_vel);

	if (pSettings->line_exist(ws, "shell_casing_extra"))
	{
		const shared_str extra = pSettings->r_string(ws, "shell_casing_extra");
		if (extra.size() && pSettings->section_exist(extra.c_str()))
			gwr_ShellCasingArm	(1, extra, owner_vel);
	}
	return					true;
}

// The shot has jammed the gun, and the jammed variant of its animation is on: the case is caught in
// the port. No real case from this shot, and the animated one stays on the model.
void CWeapon::gwr_ShellCasingCancel()
{
	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : NULL;
	for (u32 idx = 0; idx < 2; ++idx)
	{
		SShellCasingTrack& T = m_shell_track[idx];
		if (T.active && casing_debug())
			Msg			("~ shell_casing [%s] %s: the shot jammed the gun -- nothing thrown", cNameSect().c_str(), TRACK_NAME[idx]);
		T.active		= false;
		if (T.reshow_time && hi && hi->m_model)
			casing_show_hud_bone	(hi->m_model, T.bone, TRUE);
		T.reshow_time	= 0;
	}
}

// Clearing a jam, first person: where the animation has the hands rack the stuck case out and it flies,
// a real one takes over -- by the same rule as on a shot. Only here the animated shell stays on the
// model until then (it is what the hands are working on), and an animation that never throws it
// gives no case. ammo: the round that was fired when the gun jammed.
// The open phase of a shotgun's reload goes the same way (CWeaponShotgun::PlayAnimOpenWeapon): on some
// guns it racks the spent shell out.
void CWeapon::gwr_ShellCasingOnUnjam(LPCSTR ammo)
{
	LPCSTR ws = cNameSect().c_str();
	shared_str sect;
	if (!casing_section(ws, ammo, sect))									return;
	if (!READ_IF_EXISTS(pSettings, r_bool, SC_SECT, "unjam", TRUE))			return;
	if (READ_IF_EXISTS(pSettings, r_bool, ws, "shell_casing_no_anim", FALSE))	return;

	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : NULL;
	if (!hi || !hi->m_model)	return;
	IKinematics* K		= hi->m_model;

	gwr_ShellCasingUpdate	(true);

	SShellCasingTrack& T = m_shell_track[0];
	const u16 bone		= casing_hud_bone(K, ws, T.reshow_time ? T.bone : BI_NONE);
	if (bone == BI_NONE || bone >= 64)	return;
	if (T.reshow_time)
	{
		casing_show_hud_bone	(K, T.bone, TRUE);
		T.reshow_time	= 0;
	}

	T.section			= sect;
	PHGetLinearVell		(T.owner_vel);
	T.speed_scale		= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "unjam_speed_scale", 0.5f);	// the hand, not the bolt
	T.by_hand			= true;
	T.ammo_bone			= false;
	T.drop				= false;
	T.hold_until	= 0;
	T.active			= true;
	T.stage				= 0;
	T.bone				= bone;
	T.frame				= Device.dwFrame;
	T.deadline			= Device.dwTimeGlobal + iFloor(1000.f * READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_unjam_window", 8.f));
	T.shot_time			= Device.dwTimeGlobal;
	T.motion			= MotionStartTm();	// the unjam animation: once it is replaced, there is nothing to wait for
}

// A reload animation has just been started: the EXTRA thing a weapon throws (the PKM's belt link) may be
// thrown by the hands in it -- the PKM's full reload tosses the spent link out at its very end. Followed
// by hand, like the jam clear: the link stays on the model until it flies, and a reload that only moves
// it about gives nothing (shell_casing_hand_side: how far it has to get off the barrel line first).
void CWeapon::gwr_ShellCasingOnReloadAnim()
{
	LPCSTR ws = cNameSect().c_str();
	if (pSettings->line_exist(ws, "reload_drop_bones"))
	{
		gwr_ShellCasingArmReloadDrop	();
		return;
	}
	if (pSettings->line_exist(ws, "shell_casing_reload_bones"))
	{
		gwr_ShellCasingArmReloadBones	();
		return;
	}
	if (!pSettings->line_exist(ws, "shell_casing_extra") || !pSettings->line_exist(ws, "shell_casing_extra_bone"))	return;
	if (!READ_IF_EXISTS(pSettings, r_bool, SC_SECT, "reload", TRUE))												return;

	// the same gates casing_section applies -- the extra thing is named by the weapon, not by the ammo
	if (!IsGameTypeSingle() || !pSettings->section_exist(SC_SECT))			return;
	if (!READ_IF_EXISTS(pSettings, r_bool, SC_SECT, "enabled", TRUE))		return;
	if (!READ_IF_EXISTS(pSettings, r_bool, ws, "shell_casing", TRUE))		return;
	const shared_str sect	= pSettings->r_string(ws, "shell_casing_extra");
	if (!sect.size() || !pSettings->section_exist(sect.c_str()))	return;

	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : NULL;
	if (!hi || !hi->m_model)	return;
	IKinematics* K		= hi->m_model;
	const u16 bone		= K->LL_BoneID(pSettings->r_string(ws, "shell_casing_extra_bone"));
	if (bone == BI_NONE || bone >= 64)	return;

	gwr_ShellCasingUpdate	(true);

	SShellCasingTrack& T = m_shell_track[1];
	if (T.reshow_time)
	{
		casing_show_hud_bone	(K, T.bone, TRUE);
		T.reshow_time	= 0;
	}

	T.section			= sect;
	PHGetLinearVell		(T.owner_vel);
	T.speed_scale		= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "unjam_speed_scale", 0.5f);
	T.by_hand			= true;
	T.ammo_bone			= false;
	T.drop				= false;
	T.hold_until	= 0;
	T.active			= true;
	T.stage				= 0;
	T.bone				= bone;
	T.frame				= Device.dwFrame;
	// a reload is long: the PKM's link leaves 11 s in. The wait ends with the animation anyway.
	T.deadline			= Device.dwTimeGlobal + iFloor(1000.f * READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_reload_window", 20.f));
	T.shot_time			= Device.dwTimeGlobal;
	T.motion			= MotionStartTm();
}

// Break-action guns (the BM-16): a FULL reload has the opened breech throw both spent shells straight back,
// out along the barrels -- not sideways, so the shot's "off the barrel line" test would never see it go.
// Followed by hand, any direction counting (ammo_bone). Which reloads: shell_casing_reload_anims, exact
// alias names -- the partial one has the hand pull the shell out and keeps it animated. Which bones:
// shell_casing_reload_bones / _bones2, one list per barrel; the one SHOWN is that barrel's spent shell (an
// unfired barrel shows a round instead and throws nothing), and the colour in its name, looked up in the
// hud's ammo_bone_type_<ammo>, says which round it was. Gated by shell_casing_reload only: such a gun has
// shell_casing = false, nothing leaves it on the shot.
void CWeapon::gwr_ShellCasingArmReloadBones()
{
	LPCSTR ws = cNameSect().c_str();
	if (!IsGameTypeSingle() || !pSettings->section_exist(SC_SECT))		return;
	if (!READ_IF_EXISTS(pSettings, r_bool, SC_SECT, "enabled", TRUE))	return;
	if (!READ_IF_EXISTS(pSettings, r_bool, SC_SECT, "reload", TRUE))		return;
	if (!READ_IF_EXISTS(pSettings, r_bool, ws, "shell_casing_reload", TRUE))	return;

	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : NULL;
	if (!hi || !hi->m_model)	return;
	IKinematics* K		= hi->m_model;

	LPCSTR cur			= CurrentMotion().c_str();
	if (!cur || !cur[0])	return;
	LPCSTR anims		= READ_IF_EXISTS(pSettings, r_string, ws, "shell_casing_reload_anims", "");
	bool listed			= false;
	for (int i = 0, n = _GetItemCount(anims); (i < n) && !listed; ++i)
	{
		string128	a;
		_GetItem	(anims, i, a);
		listed		= !xr_strcmp(a, cur);
	}
	if (!listed)
	{
		if (casing_debug())
			Msg		("~ shell_casing [%s]: reload %s is not one that throws its shells", ws, cur);
		return;
	}

	gwr_ShellCasingUpdate	(true);

	static LPCSTR keys[2]	= { "shell_casing_reload_bones", "shell_casing_reload_bones2" };
	for (u32 idx = 0; idx < 2; ++idx)
	{
		if (!pSettings->line_exist(ws, keys[idx]))	continue;
		LPCSTR list		= pSettings->r_string(ws, keys[idx]);
		u16 bone		= BI_NONE;
		string128	bn;
		for (int i = 0, n = _GetItemCount(list); (i < n) && (bone == BI_NONE); ++i)
		{
			_GetItem	(list, i, bn);
			const u16 b	= K->LL_BoneID(bn);
			if ((b != BI_NONE) && (b < 64) && K->LL_GetBoneVisible(b))
				bone	= b;
		}
		if (bone == BI_NONE)	continue;			// that barrel holds no spent shell

		// the round: the colour in the bone's name, against the hud's ammo_bone_type_<ammo> (or _<index>)
		shared_str sect;
		for (u32 t = 0; (t < m_ammoTypes.size()) && !sect.size(); ++t)
		{
			string128	k;
			strconcat	(sizeof(k), k, "ammo_bone_type_", m_ammoTypes[t].c_str());
			if (!pSettings->line_exist(HudSection(), k))
				xr_sprintf	(k, "ammo_bone_type_%u", t);
			if (!pSettings->line_exist(HudSection(), k))	continue;
			string64	col;
			strconcat	(sizeof(col), col, "_", pSettings->r_string(HudSection(), k));
			if (!strstr(bn, col))	continue;
			if (pSettings->line_exist(m_ammoTypes[t].c_str(), "shell_casing"))
				sect	= pSettings->r_string(m_ammoTypes[t].c_str(), "shell_casing");
		}
		if (!sect.size() || !pSettings->section_exist(sect.c_str()))	continue;

		SShellCasingTrack& T = m_shell_track[idx];
		if (T.reshow_time && !T.ammo_bone)
			casing_show_hud_bone	(K, T.bone, TRUE);
		T.reshow_time	= 0;
		T.section		= sect;
		PHGetLinearVell	(T.owner_vel);
		T.speed_scale	= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "unjam_speed_scale", 0.5f);
		T.by_hand		= true;
		T.ammo_bone		= true;
		T.drop			= false;
		T.hold_until	= 0;
		T.active		= true;
		T.stage			= 0;
		T.bone			= bone;
		T.frame			= Device.dwFrame;
		T.deadline		= Device.dwTimeGlobal + iFloor(1000.f * READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_reload_window", 20.f));
		T.shot_time		= Device.dwTimeGlobal;
		T.motion		= MotionStartTm();
	}
}

// A full (empty) reload lets the old magazine fall. reload_drop_bones = <bone>:<section>, ... -- the magazine
// bones of the hud model, each with the object it becomes; the first one SHOWN is the magazine in the gun (an
// upgrade swaps mag30 for mag45 or mag60). reload_drop_anims: the reloads that drop it, by alias PREFIX
// (default anm_reload_empty; the partial reload keeps the magazine in the hand). The animation re-uses the
// bone for the NEW magazine afterwards -- by a jump or by bringing it back up -- so it is shown again the
// moment it heads back towards the gun (see the watch in gwr_ShellCasingFollow).
// gl = the grenade launcher's reload (CWeaponMagazinedWGrenade::switch2_Reload): its spent case falls the same
// way, by gl_drop_bones / _frame / _speed / ... -- and any of its reloads counts but the grenade change.
void CWeapon::gwr_ShellCasingArmReloadDrop(bool gl)
{
	LPCSTR ws = cNameSect().c_str();
	if (!IsGameTypeSingle() || !pSettings->section_exist(SC_SECT))		return;
	if (!READ_IF_EXISTS(pSettings, r_bool, SC_SECT, "enabled", TRUE))	return;
	if (!READ_IF_EXISTS(pSettings, r_bool, SC_SECT, gl ? "gl_drop" : "reload_drop", TRUE))	return;
	if (!pSettings->line_exist(ws, drop_key(gl, "bones")))	return;

	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : NULL;
	if (!hi || !hi->m_model)	return;
	IKinematics* K		= hi->m_model;

	LPCSTR cur			= CurrentMotion().c_str();
	if (!cur || !cur[0])	return;
	// Which reloads: by default every EMPTY one -- an anm_reload alias carrying the _empty token, whatever
	// firemode mark sits in front of it (the AKs play anm_reload_auto_empty) and not the launcher's (_g).
	// reload_drop_anims = <prefix>, ... replaces that with a list of alias prefixes.
	bool listed			= gl;		// (the launcher's: the caller has picked the reload already)
	if (gl)
		;
	else if (pSettings->line_exist(ws, "reload_drop_anims"))
	{
		LPCSTR anims	= pSettings->r_string(ws, "reload_drop_anims");
		for (int i = 0, n = _GetItemCount(anims); (i < n) && !listed; ++i)
		{
			string128	a;
			_GetItem	(anims, i, a);
			listed		= a[0] && (0 == strncmp(cur, a, xr_strlen(a)));
		}
	}
	else
	{
		const u32 n		= xr_strlen(cur);
		listed			= (0 == strncmp(cur, "anm_reload", 10)) && strstr(cur, "_empty") && !strstr(cur, "_jammed")
						  && !((n >= 2) && (0 == xr_strcmp(cur + n - 2, "_g")));
	}
	if (!listed)
	{
		if (casing_debug())
			Msg		("~ shell_casing [%s]: reload %s drops no magazine", ws, cur);
		return;
	}

	LPCSTR list			= pSettings->r_string(ws, drop_key(gl, "bones"));
	u16 bone			= BI_NONE;
	string128		sect;	sect[0] = 0;
	u16 spare			= BI_NONE;				// the first that merely exists: a magazine the animation only shows
	string128		spare_sect;	spare_sect[0] = 0;	// as it falls (the Desert Eagle's mag2)
	for (int i = 0, n = _GetItemCount(list); (i < n) && (bone == BI_NONE); ++i)
	{
		string256	item;
		_GetItem	(list, i, item);
		LPSTR colon	= strchr(item, ':');
		if (!colon)	continue;
		*colon		= 0;
		const u16 b	= K->LL_BoneID(item);
		if ((b == BI_NONE) || (b >= 64) || !pSettings->section_exist(colon + 1))	continue;
		if (K->LL_GetBoneVisible(b))
		{
			bone		= b;
			xr_strcpy	(sect, colon + 1);
		}
		else if (spare == BI_NONE)
		{
			spare		= b;
			xr_strcpy	(spare_sect, colon + 1);
		}
	}
	if ((bone == BI_NONE) && (spare != BI_NONE))
	{
		bone			= spare;		// followed once it shows (a hidden bone is waited out)
		xr_strcpy		(sect, spare_sect);
	}
	if (bone == BI_NONE)
	{
		if (casing_debug())
			Msg		("~ shell_casing [%s]: reload %s -- none of the magazine bones is shown", ws, cur);
		return;
	}

	gwr_ShellCasingUpdate	(true);

	SShellCasingTrack& T = m_shell_track[1];
	if (T.reshow_time && !T.ammo_bone)
		casing_show_hud_bone	(K, T.bone, TRUE);
	T.reshow_time		= 0;
	T.section			= sect;
	PHGetLinearVell		(T.owner_vel);
	T.speed_scale		= 1.f;
	T.by_hand			= true;
	T.ammo_bone			= false;
	T.drop				= true;
	T.gl				= gl;
	T.far_max			= 0.f;
	T.vA.set			(0.f, 0.f, 0.f);
	T.sA				= 0.f;
	T.wA.set			(0.f, 0.f, 0.f);
	T.hold_until		= 0;
	T.active			= true;
	T.stage				= 0;
	T.bone				= bone;
	T.frame				= Device.dwFrame;
	T.deadline			= Device.dwTimeGlobal + iFloor(1000.f * READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_reload_window", 20.f));
	T.shot_time			= Device.dwTimeGlobal;
	T.motion			= MotionStartTm();
}

void CWeapon::gwr_ShellCasingArm(u32 idx, const shared_str& sect, const Fvector& owner_vel)
{
	SShellCasingTrack& T = m_shell_track[idx];
	LPCSTR ws		= cNameSect().c_str();
	T.section		= sect;
	T.owner_vel		= owner_vel;
	T.speed_scale	= 1.f;
	T.by_hand		= false;
	T.ammo_bone		= false;
	T.drop			= false;
	T.hold_until	= 0;

	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : NULL;
	if (hi && hi->m_model)
	{
		IKinematics* K	= hi->m_model;
		u16 bone		= BI_NONE;
		if (idx == 0)
			bone		= casing_hud_bone(K, ws, T.reshow_time ? T.bone : BI_NONE);
		else if (pSettings->line_exist(ws, "shell_casing_extra_bone"))
			bone		= K->LL_BoneID(pSettings->r_string(ws, "shell_casing_extra_bone"));

		const bool no_anim = (idx == 0) && !!READ_IF_EXISTS(pSettings, r_bool, ws, "shell_casing_no_anim", FALSE);
		if (bone != BI_NONE && !no_anim)
		{
			// follow the animated shell (see the head of the file) -- without drawing it: from the shot on
			// it is hidden, the real case is the only one to be seen
			if (T.reshow_time && T.bone != bone)
				T.reshow_time	= 0;			// another shell is loaded now: the old one is the ammo-bones pass's again
			casing_show_hud_bone	(K, bone, FALSE);
			T.reshow_time	= 1;				// hidden
			T.motion		= MotionStartTm();
			float window	= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_window", 0.35f);
			window			= READ_IF_EXISTS(pSettings, r_float, ws, "shell_casing_window", window);
			T.active		= true;
			T.stage			= 0;				// no sample yet: what the bone holds now is the pose before the shot
			T.bone			= bone;
			T.frame			= Device.dwFrame;
			T.deadline		= Device.dwTimeGlobal + iFloor(1000.f * window);
			T.shot_time		= Device.dwTimeGlobal;
			return;
		}
		if (idx != 0)		return;				// an extra with nothing on the hud model to show it: none in this view

		// no animated shell to follow: a configured point of the hud model, the (still) shell bone, or
		// the hud shell point
		Fvector p;
		LPCSTR from		= "hud shell point";
		if (pSettings->line_exist(ws, "shell_casing_point"))
		{
			// in the space of a bone of the hud model (the body by default): the model's bones are animated,
			// a point given in model space would stay behind when the idle pose moves the gun
			const u16 pb	= K->LL_BoneID(READ_IF_EXISTS(pSettings, r_string, ws, "shell_casing_point_bone", "wpn_body"));
			Fvector pm		= pSettings->r_fvector3(ws, "shell_casing_point");
			if (pb != BI_NONE)
				K->LL_GetTransform(pb).transform_tiny(pm);
			hi->m_item_transform.transform_tiny(p, pm);
			from		= "configured point";
		}
		else if (bone != BI_NONE)
		{
			hi->m_item_transform.transform_tiny(p, K->LL_GetTransform(bone).c);
			from		= "still bone";
		}
		else
			p.set		(get_LastSP());

		if (p.distance_to_sqr(Device.vCameraPosition) > 1.5f * 1.5f)
		{
			// parked somewhere out of the way: not a real point. Beside the sights, for want of better.
			p.set		(Device.vCameraPosition);
			p.mad		(Device.vCameraRight,		 0.10f);
			p.mad		(Device.vCameraTop,			-0.08f);
			p.mad		(Device.vCameraDirection,	 0.40f);
			from		= "fallback beside the sights";
		}
		else
			casing_point_hud_to_world	(p);

		if (casing_debug())
		{
			const Fvector c = casing_in_camera(p, true);
			Msg			("~ shell_casing [%s]: no animation, %s, at cam(right %.3f up %.3f fwd %.3f)", ws, from, c.x, c.y, c.z);
		}
		Fmatrix B;				casing_camera_basis(B);
		gwr_ShellCasingThrow	(idx, p, B, NULL, 0.f, true);
		return;
	}

	// Third person, an NPC, a weapon nobody holds. The WORLD model carries a shell bone of its own, sitting
	// in its chamber in the bind pose -- far more reliable than the configured shell_point, which on many
	// guns was copied over from another model (up to 40 cm off; on the PKM below and behind the gun).
	// The bind pose, not the animated one: the world animations may park or hide the bone. No such bone
	// (the PKM, the G36, the P90...) = the shell_point, as before.
	get_LastSP		();							// brings XFORM up to date
	// model space, until the very end. NOT vLoadedShellPoint: CShootingObject loads that only along with
	// shell_particles, which nearly every weapon has commented out -- so it was (0,0,0), the grip, on all of them.
	Fvector m		= pSettings->line_exist(ws, "shell_point") ? pSettings->r_fvector3(ws, "shell_point") : vLoadedShellPoint;
	IKinematics* WK	= Visual() ? smart_cast<IKinematics*>(Visual()) : NULL;
	u16 wb			= BI_NONE;
	if (WK)
	{
		if (idx == 0)
		{
			static LPCSTR names[] = { "shell", "shell5x45", "shell_red", "shell_blue", "shell_green", "gilza" };
			for (u32 i = 0; (i < sizeof(names) / sizeof(names[0])) && (wb == BI_NONE); ++i)
				wb	= WK->LL_BoneID(names[i]);
		}
		else if (pSettings->line_exist(ws, "shell_casing_extra_bone"))
			wb		= WK->LL_BoneID(pSettings->r_string(ws, "shell_casing_extra_bone"));

		if (wb != BI_NONE)
		{
			Fmatrix bind;	bind.set(WK->LL_GetData(wb).bind_transform);		// local ones: up the chain
			for (u16 b = WK->LL_GetData(wb).GetParentID(); b != BI_NONE; b = WK->LL_GetData(b).GetParentID())
				bind.mulA_43	(WK->LL_GetData(b).bind_transform);
			m.set		(bind.c);
		}
	}
	// per weapon and per track, a nudge in the world model's own space (x right, y up, z forward; m)
	if (pSettings->line_exist(ws, KEY_WORLD_OFFSET[idx]))
		m.add		(pSettings->r_fvector3(ws, KEY_WORLD_OFFSET[idx]));
	Fvector p;		XFORM().transform_tiny(p, m);
	if (casing_debug())
	{
		const Fvector fp = get_LastFP();
		Msg			("~ shell_casing [%s] %s third person: from %s, model (%.3f %.3f %.3f) -> world (%.2f %.2f %.2f); %.2f m from the gun's origin, %.2f m from its muzzle, %.2f m from the camera",
					ws, TRACK_NAME[idx], (wb != BI_NONE) ? WK->LL_BoneName_dbg(wb) : "shell_point", m.x, m.y, m.z, p.x, p.y, p.z,
					p.distance_to(XFORM().c), p.distance_to(fp), p.distance_to(Device.vCameraPosition));
	}
	gwr_ShellCasingThrow	(idx, p, XFORM(), NULL, 0.f, false);
}

void CWeapon::gwr_ShellCasingUpdate(bool force)
{
	gwr_ShellCasingFollow	(0, force);
	gwr_ShellCasingFollow	(1, force);
}

void CWeapon::gwr_ShellCasingFollow(u32 idx, bool force)
{
	SShellCasingTrack& T = m_shell_track[idx];
	if (!T.active && !T.reshow_time)	return;

	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : NULL;
	if (!hi || !hi->m_model || T.bone >= hi->m_model->LL_BoneCount())
	{
		T.active		= false;		// out of the hands in between: nothing left to follow
		return;
	}
	IKinematics* K		= hi->m_model;

	// The animated shell is hidden for the shot animation only. Any other animation gets it back the
	// moment it starts: the idle one parks it out of sight, the reload and the unjam show it (a round
	// racked out of the chamber). The next shot hides it again itself, before anything is drawn.
	// And an animation that was replaced before its shell flew has no flight left to wait for.
	if (MotionStartTm() != T.motion)
	{
		if (T.active && casing_debug())
			Msg			("~ shell_casing [%s] %s: the animation was replaced before the hud bone flew -- nothing thrown", cNameSect().c_str(), TRACK_NAME[idx]);
		T.active		= false;
		T.hold_until	= 0;
		if (T.reshow_time)
		{
			// (a reload's spent shell is not shown again here: by now the barrels hold rounds, and which
			// bones show is the ammo-bones pass's call)
			if (!T.ammo_bone)
				casing_show_hud_bone	(K, T.bone, TRUE);
			T.reshow_time	= 0;
		}
	}
	// A dropped magazine's bone is re-used by the animation for the NEW magazine: once it heads back towards
	// the gun -- a jump, or the hand bringing it up -- it is shown again. Followed hidden, calculated on demand.
	// (the bone of a magazine just let go stays on until the real one is there: hidden at once, nothing would be
	// seen for the frame or two the server takes to hand it over)
	if (T.hold_until && ((CShellCasing::Spawned() != T.hold_mark) || (Device.dwTimeGlobal >= T.hold_until)))
	{
		casing_show_hud_bone	(K, T.bone, FALSE);
		T.hold_until	= 0;
	}
	if (!T.active && T.drop && T.reshow_time && !T.hold_until && (Device.dwFrame != T.frame))
	{
		T.frame			= Device.dwFrame;
		casing_show_hud_bone	(K, T.bone, TRUE);
		K->CalculateBones_Invalidate	();
		K->CalculateBones	(TRUE);
		const Fmatrix WT	= K->LL_GetTransform(T.bone);
		casing_show_hud_bone	(K, T.bone, FALSE);
		const u16 wp	= K->LL_GetData(T.bone).GetParentID();
		Fvector q;
		if (wp != BI_NONE)	{ Fmatrix inv; inv.invert(K->LL_GetTransform(wp)); inv.transform_tiny(q, WT.c); }
		else				q.set(WT.c);
		const float dist	= q.distance_to(T.p0);
		if (dist > T.far_max)
			T.far_max	= dist;
		else if (dist < T.far_max - READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_drop_return", 0.05f))
		{
			casing_show_hud_bone	(K, T.bone, TRUE);
			T.reshow_time	= 0;
		}
	}
	if (!T.active)	return;

	// A case racked out by hand: not while its animation is still blending in. The shell is on its way
	// from where the last pose kept it to where this one wants it, and that passes for a flight (an
	// open started from the "just reloaded" idle threw a case that was never there).
	if (T.by_hand && !T.drop)	// (a magazine sits in the well in the idle and the reload alike: nothing to wait out)
	{
		const u32 skip	= iFloor(1000.f * READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_unjam_skip", 0.3f));
		if (Device.dwTimeGlobal < T.shot_time + skip)
		{
			if (force)	T.active = false;
			return;
		}
	}

	const bool timeout	= Device.dwTimeGlobal >= T.deadline;
	// the bones are calculated once a frame, when the hud is drawn: nothing new to look at within a frame
	if (Device.dwFrame == T.frame)
	{
		if (force)		T.active = false;
		return;
	}

	const float near_r	= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_track_near", 1.2f);
	const Fvector2 vr	= READ_IF_EXISTS(pSettings, r_fvector2, SC_SECT, "hud_track_speed", Fvector2().set(0.4f, 30.f));
	float side			= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_track_side", 0.015f);
	side				= READ_IF_EXISTS(pSettings, r_float, cNameSect().c_str(), "shell_casing_side", side);
	// by hand the shell (the link) is pushed about a good deal before it is thrown: a weapon can ask for
	// more of a distance off the barrel line there
	if (T.by_hand)
		side			= READ_IF_EXISTS(pSettings, r_float, cNameSect().c_str(), "shell_casing_hand_side", side);
	if (T.drop)				// a magazine: once clear of the well
		side			= READ_IF_EXISTS(pSettings, r_float, cNameSect().c_str(), drop_key(T.gl, "side"),
						  READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_drop_side", 0.1f));

	// The bone is followed in the space of its PARENT bone (the gun's body): in model space it moves with
	// the body, and the body is what a shot kicks back.
	// On a shot it is hidden, and the engine does not calculate hidden bones: show it for one calculation
	// of the model, take its place, hide it again. Nothing is drawn in between. (Clearing a jam, it is
	// on the model and calculated with it.)
	const bool hidden	= (0 != T.reshow_time);
	if (hidden)
	{
		casing_show_hud_bone	(K, T.bone, TRUE);
		K->CalculateBones_Invalidate	();
		K->CalculateBones	(TRUE);
	}
	const Fmatrix BT	= K->LL_GetTransform(T.bone);
	if (hidden)
		casing_show_hud_bone	(K, T.bone, FALSE);
	const u16 parent	= K->LL_GetData(T.bone).GetParentID();
	Fvector p;
	if (parent != BI_NONE)
	{
		Fmatrix inv;	inv.invert(K->LL_GetTransform(parent));
		inv.transform_tiny	(p, BT.c);
	}
	else
		p.set			(BT.c);
	Fvector pos;		hi->m_item_transform.transform_tiny(pos, BT.c);
	const bool is_near	= pos.distance_to_sqr(Device.vCameraPosition) < near_r * near_r;

	// A case racked out by hand has to be seen AT REST at the gun first (stage 1: a sample, not at rest
	// yet). However long its animation takes to blend in, until then the shell is travelling from the
	// last pose to this one -- and the hands only start on a shell that lies still.
	// ...and seen at all: a hidden bone is not calculated, it sits wherever it was hidden (or at the
	// model's origin), and the frame it is shown again would pass for a long, fast step.
	if (T.by_hand && !K->LL_GetBoneVisible(T.bone))
	{
		T.stage			= 0;
		T.frame			= Device.dwFrame;
		if (force || timeout)
			T.active	= false;
		return;
	}
	if (T.by_hand && T.stage != 2)
	{
		const bool still	= (T.stage == 1) && is_near && (Device.fTimeDelta > EPS_S)
							&& (p.distance_to(T.pA) / Device.fTimeDelta < vr.x);
		T.pA.set		(p);
		T.frame			= Device.dwFrame;
		if (still)		{ T.p0.set(p); T.stage = 2; }
		else			T.stage = 1;
		if (force || timeout)
			T.active	= false;
		return;
	}

	// the first look at the bone at the gun after the shot: that is where the shell rests (the chamber)
	if (is_near && T.stage != 2)
		T.p0.set		(p);

	// A flight is a step at a believable speed, between two positions at the gun, that has taken the shell
	// OUT of the gun: sideways off the line of the barrel by more than the receiver is wide. A pump gun's
	// slide first drags the shell straight back along the barrel (a Winchester: 11 cm in two frames) and only
	// then flips it out -- that first stretch is inside the gun, and the case must not appear there.
	bool flies			= false;
	Fvector d;			d.set(0.f, 0.f, 0.f);
	float len			= 0.f, speed = 0.f, off_axis = 0.f;
	Fvector dir_world;	dir_world.set(0.f, 0.f, 0.f);
	if (T.stage == 2 && is_near && Device.fTimeDelta > EPS_S)
	{
		d.sub			(p, T.pA);
		len				= d.magnitude();
		speed			= len / Device.fTimeDelta;

		// How far from its rest it is by now, across the barrel. The barrel is the model's own Z axis, taken
		// into the parent bone's space by its bind pose -- not the line of sight: clearing a jam the hands
		// turn the gun over, and the stroke that drags the shell straight back then counted as leaving it
		// (a SPAS-12 threw the case backwards, from inside the receiver).
		Fvector t;		t.sub(p, T.p0);
		Fvector axis;	axis.set(0.f, 0.f, 1.f);
		if (parent != BI_NONE)
		{
			Fmatrix bind;	bind.set(K->LL_GetData(parent).bind_transform);		// local ones: up the chain
			for (u16 b = K->LL_GetData(parent).GetParentID(); b != BI_NONE; b = K->LL_GetData(b).GetParentID())
				bind.mulA_43	(K->LL_GetData(b).bind_transform);
			Fmatrix inv;	inv.invert(bind);
			inv.transform_dir	(axis);
			const float am	= axis.magnitude();
			if (am > EPS)	axis.div(am);
			else			axis.set(0.f, 0.f, 1.f);
		}
		if (!T.ammo_bone && !T.drop)	// a reload's shell leaves straight back, a magazine straight down
			t.mad		(axis, -t.dotproduct(axis));
		Fvector tw;
		if (parent != BI_NONE)	{ Fvector tm; K->LL_GetTransform(parent).transform_dir(tm, t); hi->m_item_transform.transform_dir(tw, tm); }
		else					hi->m_item_transform.transform_dir(tw, t);
		off_axis		= tw.magnitude();

		flies			= (speed >= vr.x) && (speed <= vr.y) && (len > EPS_L) && (off_axis >= side);
		// Hands throw slowly (0.5..3 m/s in every animation measured); a step faster than this is the
		// animation putting the bone somewhere else in one frame. That is no throw -- and the place it
		// lands is the new rest to measure from, so the shell has to be seen still there first.
		if (T.by_hand && !T.drop && (speed > READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_hand_speed_max", 5.f)))
		{
			flies		= false;
			T.stage		= 1;
		}
		// A dropped magazine is let go where its path BREAKS, once it is clear of the well: a jump (the animation
		// re-places the bone for the new magazine), a sharp turn (the fall ends, the hand takes the bone
		// elsewhere), or the point it stops getting further from the well (the AKs' hand swings the old
		// magazine away in an arc and brings the new one back). It leaves from where it was a frame ago,
		// the way it was going then -- the first threshold took it over while the hand still held it.
		// reload_drop_frame = <frame of the reload animation, 30 a second>: the moment is given by hand instead --
		// let go right there, from where the bone is, the way it went over this last frame.
		const float drop_frame	= T.drop ? READ_IF_EXISTS(pSettings, r_float, cNameSect().c_str(), drop_key(T.gl, "frame"), -1.f) : -1.f;
		if (drop_frame >= 0.f)
		{
			flies				= casing_hud_motion_time(K) >= drop_frame / 30.f;
			if (flies)
			{
				if (len <= EPS_L)		// standing still on that frame: the last step it made, or just let it fall
				{
					const float vlen	= T.vA.magnitude();
					if (vlen > EPS_L)	{ d.set(T.vA); len = vlen; speed = T.sA; }
					else				{ d.set(0.f, -1.f, 0.f); len = 1.f; speed = 0.f; }
				}
				else
					T.wA		= casing_spin(T.mA, BT, Device.fTimeDelta);	// the turn over this same step
				T.pA.set		(p);
				T.mA.set		(BT);
				T.launch_time	= Device.dwTimeGlobal - iFloor(1000.f * Device.fTimeDelta);	// (the bones were calculated a frame ago)
			}
		}
		else if (T.drop)
		{
			const float vlen	= T.vA.magnitude();
			const bool went		= (T.sA >= vr.x) && (vlen > EPS_L) && (T.pA.distance_to(T.p0) >= side);
			const bool jump		= speed > READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_drop_speed_max", 10.f);
			const bool turn		= (len > EPS_L) && (d.dotproduct(T.vA) < 0.5f * len * vlen);
			const bool back		= p.distance_to(T.p0) < T.pA.distance_to(T.p0) - 0.001f;
			flies				= went && (jump || turn || back);
			if (!flies && jump)	T.stage = 1;		// jumped before it ever left: a new rest to wait for
			if (flies)
			{
				d.set			(T.vA);
				len				= vlen;
				speed			= T.sA;
				T.launch_time	= Device.dwTimeGlobal - iFloor(2000.f * Device.fTimeDelta);
				// ...from where it was a frame ago
				if (parent != BI_NONE)	{ Fvector tm; K->LL_GetTransform(parent).transform_tiny(tm, T.pA); hi->m_item_transform.transform_tiny(pos, tm); }
				else					hi->m_item_transform.transform_tiny(pos, T.pA);
			}
		}
		if (flies)
		{
			Fvector dn;	dn.mul(d, 1.f / len);
			if (parent != BI_NONE)	{ Fvector dm; K->LL_GetTransform(parent).transform_dir(dm, dn); hi->m_item_transform.transform_dir(dir_world, dm); }
			else					hi->m_item_transform.transform_dir(dir_world, dn);
		}
	}

	if (!flies)
	{
		// remember this position for the next frame's step; give up when the time is out
		T.pA.set		(p);
		T.vA.set		(d);
		T.sA			= speed;
		if (T.drop && (T.stage == 2))
			T.wA		= casing_spin(T.mA, BT, Device.fTimeDelta);
		T.mA.set		(BT);
		if (!(T.by_hand && (1 == T.stage)))		// (by hand, stage 1 = wait for a new rest)
			T.stage		= is_near ? 2 : 1;
		T.frame			= Device.dwFrame;
		if (force || timeout)
		{
			if (casing_debug())
				Msg		("~ shell_casing [%s] %s: the hud bone did not fly (%s) -- nothing thrown", cNameSect().c_str(), TRACK_NAME[idx], force ? "next shot came" : "window is over");
			T.active	= false;
		}
		return;
	}

	// a case does not start off into the player's face, whatever the animation does at that moment:
	// keep what it has across the line of sight
	if (dir_world.dotproduct(Device.vCameraDirection) < -0.5f)
	{
		Fvector along;	along.mul(Device.vCameraDirection, dir_world.dotproduct(Device.vCameraDirection) + 0.5f);
		dir_world.sub	(along);
		const float m	= dir_world.magnitude();
		if (m > EPS)	dir_world.div(m);
	}

	// Per weapon, the point can be nudged for the pose out of the sights: metres to the right, up and
	// forward of the view, in the hud's own space. It fades out as the weapon comes up to the eye.
	if (pSettings->line_exist(cNameSect().c_str(), "shell_casing_hip_offset"))
	{
		const Fvector o	= pSettings->r_fvector3(cNameSect().c_str(), "shell_casing_hip_offset");
		float k			= 1.f - GetZoomRotationFactor();
		clamp			(k, 0.f, 1.f);
		pos.mad			(Device.vCameraRight,		o.x * k);
		pos.mad			(Device.vCameraTop,			o.y * k);
		pos.mad			(Device.vCameraDirection,	o.z * k);
	}

	// a magazine is let go, not thrown: whatever the hand was doing, it does not leave upwards
	if (T.drop)
	{
		const float up	= dir_world.dotproduct(Device.vCameraTop);
		if (up > 0.f)
		{
			dir_world.mad	(Device.vCameraTop, -up);
			const float m	= dir_world.magnitude();
			if (m > EPS)	dir_world.div(m);
			else			dir_world.set(Device.vCameraTop).invert();
			speed			*= (1.f - up);
		}
		// reload_drop_forward = <share>: how much of what it has along the line of sight it keeps (1 = all)
		const float keep	= READ_IF_EXISTS(pSettings, r_float, cNameSect().c_str(), drop_key(T.gl, "forward"), 1.f);
		const float fwd		= dir_world.dotproduct(Device.vCameraDirection);
		if ((keep < 1.f) && (fwd > 0.f))
		{
			Fvector v;		v.mul(dir_world, speed);
			v.mad			(Device.vCameraDirection, -speed * fwd * (1.f - keep));
			const float m	= v.magnitude();
			if (m > EPS)	{ dir_world.div(v, m); speed = m; }
		}
	}

	const Fvector hud_pos = pos;
	casing_point_hud_to_world	(pos);

	if (casing_debug())
	{
		const Fvector a = casing_in_camera(hud_pos, true), b = casing_in_camera(pos, true), c = casing_in_camera(dir_world, false);
		Msg				("~ shell_casing [%s] %s: bone %d out by %.1f cm, step %.1f cm at %.2f m/s, %d ms after the shot; hud cam(right %.3f up %.3f fwd %.3f) -> world cam(right %.3f up %.3f fwd %.3f); dir cam(right %.2f up %.2f fwd %.2f)",
						cNameSect().c_str(), TRACK_NAME[idx], T.bone, off_axis * 100.f, len * 100.f, speed, int(Device.dwTimeGlobal - T.shot_time),
						a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
	}

	Fmatrix B;			casing_camera_basis(B);
	if (T.drop)
	{
		// the magazine leaves the way it lies in the hand -- not squared up to the camera like a case
		B.mul_43		(hi->m_item_transform, T.mA);
		B.i.normalize_safe();	B.j.normalize_safe();	B.k.normalize_safe();
		B.c.set			(0.f, 0.f, 0.f);
		hi->m_item_transform.transform_dir	(T.wA);		// ...turning the way it did (world from here: the throw reads it)
		T.far_max		= T.pA.distance_to(T.p0);	// (the release point: what the bone jumped or turned back from)
		T.frame			= Device.dwFrame;
	}
	T.active			= false;
	gwr_ShellCasingThrow(idx, pos, B, &dir_world, speed, true);

	// the real case takes over from the animated one, until this animation is replaced by the next one
	if (T.drop)
	{
		T.hold_mark		= CShellCasing::Spawned();
		T.hold_until	= Device.dwTimeGlobal + 200;
	}
	else
		casing_show_hud_bone(K, T.bone, FALSE);
	T.reshow_time		= 1;					// hidden
	T.motion			= MotionStartTm();
}

// True while the animated shell of this hud bone is hidden because a real case has taken over from it.
// The ammo-bones pass of the magazined weapons (gwr_SetBones) asks before showing a bone: it re-applies
// its lists after every shot and would bring the shell straight back next to the real case.
bool CWeapon::gwr_ShellCasingHidesBone(u16 bone) const
{
	for (u32 idx = 0; idx < 2; ++idx)
	{
		const SShellCasingTrack& T = m_shell_track[idx];
		if (T.reshow_time && T.bone == bone)	return true;
	}
	return false;
}

// The ammo-bones pass of the magazined weapons re-applies its own show/hide lists whenever the ammunition
// changes -- which is right after every shot -- and would bring the animated shell straight back next to
// the real case. Called after that pass, every frame.
void CWeapon::gwr_ShellCasingKeepHidden()
{
	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : NULL;
	if (!hi || !hi->m_model)	return;
	for (u32 idx = 0; idx < 2; ++idx)
	{
		SShellCasingTrack& T = m_shell_track[idx];
		if (T.reshow_time)
			casing_show_hud_bone	(hi->m_model, T.bone, FALSE);
	}
}

// pos: where the case appears. basis: how it lies (along the barrel) and the frame the configured
// direction is given in. dir / speed: taken from the animation, when there was one to take them from.
// hud: thrown from the first-person view -- drawn the way the gun in the hands is, see CShellCasing::RenderHud.
void CWeapon::gwr_ShellCasingThrow(u32 idx, const Fvector& pos, const Fmatrix& basis, const Fvector* dir, float speed, bool hud)
{
	LPCSTR ws			= cNameSect().c_str();
	Fvector d_local		= READ_IF_EXISTS(pSettings, r_fvector3, SC_SECT, "dir", Fvector().set(1.f, 0.6f, -0.1f));
	d_local				= READ_IF_EXISTS(pSettings, r_fvector3, ws, KEY_DIR[idx], d_local);
	Fvector2 range		= READ_IF_EXISTS(pSettings, r_fvector2, SC_SECT, "speed", Fvector2().set(1.8f, 3.2f));
	range				= READ_IF_EXISTS(pSettings, r_fvector2, ws, KEY_SPEED[idx], range);
	// a case racked out by hand: a share of that -- or, per weapon, a range of its own (what its animation does)
	const Fvector2 fired	= range;
	float scale			= m_shell_track[idx].speed_scale;
	if (m_shell_track[idx].drop)
	{
		range			= READ_IF_EXISTS(pSettings, r_fvector2, SC_SECT, "drop_speed", Fvector2().set(0.3f, 8.f));
		range			= READ_IF_EXISTS(pSettings, r_fvector2, ws, drop_key(m_shell_track[idx].gl, "speed"), range);
		scale			= 1.f;
	}
	else if (m_shell_track[idx].by_hand && pSettings->line_exist(ws, "shell_casing_unjam_speed"))
	{
		range			= pSettings->r_fvector2(ws, "shell_casing_unjam_speed");
		scale			= 1.f;
	}
	const Fvector2 spin	= READ_IF_EXISTS(pSettings, r_fvector2, SC_SECT, "spin", Fvector2().set(8.f, 30.f));
	// a magazine leaves exactly the way the animation had it going: no scatter, no jitter, no tumble of its own
	const bool drop		= m_shell_track[idx].drop;
	const float cone	= drop ? 0.f : deg2rad(READ_IF_EXISTS(pSettings, r_float, SC_SECT, "dispersion", 14.f));
	const float jitter	= drop ? 0.f : READ_IF_EXISTS(pSettings, r_float, SC_SECT, "speed_jitter", 0.2f);

	Fvector d;
	if (dir)	d.set(*dir);
	else		basis.transform_dir(d, d_local);
	float m				= d.magnitude();
	if (m < EPS)		{ d.set(basis.i); m = 1.f; }
	d.div				(m);

	// The shot animations throw their shells well forward or well back along the gun -- it reads better
	// on the hud model. For the real case that part is scaled down; and it can be given some extra lift.
	// A case thrown upwards keeps the least of it: it stays in the air the longest, so whatever it has
	// along the barrel carries it metres ahead or behind, when it should go up and come down by the feet.
	float up			= 0.f;	// 0: thrown sideways or down, 1: thrown up
	if (!m_shell_track[idx].drop)		// (a magazine just falls: none of this)
	{
		const float fwd_scale	= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "forward_scale", 1.f);
		const float fwd_up		= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "up_forward_scale", fwd_scale);
		const Fvector2 up_range	= READ_IF_EXISTS(pSettings, r_fvector2, SC_SECT, "up_range", Fvector2().set(0.4f, 0.75f));
		float up_bias			= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "up_bias", 0.f);
		up_bias					= READ_IF_EXISTS(pSettings, r_float, ws, "shell_casing_up_bias", up_bias);
		if (up_range.y > up_range.x + EPS)
		{
			up				= (d.dotproduct(basis.j) - up_range.x) / (up_range.y - up_range.x);
			clamp			(up, 0.f, 1.f);
		}
		const float fwd		= fwd_scale + (fwd_up - fwd_scale) * up;
		d.mad				(basis.k, d.dotproduct(basis.k) * (fwd - 1.f));
		d.mad				(basis.j, up_bias);
		m					= d.magnitude();
		if (m < EPS)		{ d.set(basis.i); m = 1.f; }
		d.div				(m);
	}

	// scatter inside a cone around it (a case thrown upwards: hardly any of it along the barrel)
	Fvector r;			r.set(::Random.randFs(1.f), ::Random.randFs(1.f), ::Random.randFs(1.f));
	{
		const float sc_up	= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "up_forward_scatter", 1.f);
		r.mad				(basis.k, r.dotproduct(basis.k) * (sc_up - 1.f) * up);
	}
	d.mad				(r, tanf(cone) * 0.6f);
	m					= d.magnitude();
	if (m > EPS)		d.div(m);

	// the animation's own speed, a little off either way -- kept inside the configured range (the
	// animations move their shell at anything from 1 to 5 m/s, and the slow ones just drop out of the port)
	// (clamping it to the range instead made every slow animation throw at exactly the lower bound)
	float v				= speed * (1.f + ::Random.randFs(jitter));
	if (drop)
		clamp			(v, range.x, range.y);		// (a magazine: never faster or slower than it went)
	else if (speed <= 0.f || v < range.x || v > range.y)
		v				= ::Random.randF(range.x, range.y);
	v					*= scale;

	if (casing_debug())
	{
		const Fvector c	= casing_in_camera(d, false);
		Msg				("~ shell_casing [%s] %s: thrown cam(right %.2f up %.2f fwd %.2f) at %.2f m/s, upward %.2f", ws, TRACK_NAME[idx], c.x, c.y, c.z, v, up);
	}

	Fvector linear;		linear.mul(d, v);
	linear.add			(m_shell_track[idx].owner_vel);

	// tumbling: mostly about the axes across the case, a little about its own
	Fvector a_local;	a_local.set(::Random.randFs(1.f), ::Random.randFs(1.f), ::Random.randFs(0.35f));
	m					= a_local.magnitude();
	if (m < EPS)		{ a_local.set(1.f, 0.f, 0.f); m = 1.f; }
	a_local.div			(m);
	Fvector angular;	basis.transform_dir(angular, a_local);
	float w				= ::Random.randF(spin.x, spin.y);
	if (m_shell_track[idx].by_hand)
	{
		// tipped out by the hand, it tumbles as much slower as it leaves slower than a fired one
		float k			= v / _max(0.5f * (fired.x + fired.y), EPS_L);
		clamp			(k, 0.15f, 1.f);
		w				*= k;
	}
	angular.mul			(w);
	if (drop)
	{
		// the turn it had in the hand (gwr_ShellCasingFollow left it in world space), within the same limit --
		// slowed down as much as the flight was (reload_drop_speed caps it: the AKs' 3.7 m/s swing leaves at 1.5,
		// and at the hand's full 8 rad/s it whirled off), times reload_drop_spin per weapon
		angular.set		(m_shell_track[idx].wA);
		float k			= READ_IF_EXISTS(pSettings, r_float, ws, drop_key(m_shell_track[idx].gl, "spin"), 1.f);
		if ((speed > EPS) && (v < speed))
			k			*= v / speed;
		angular.mul		(k);
		const float wm	= angular.magnitude();
		if (wm > spin.y)	angular.mul(spin.y / wm);
	}

	Fmatrix xform;		xform.set(basis);
	xform.c.set			(pos);

	// first person: how much larger the hud projection shows things than the world one does, at the
	// same place (0 = not a hud throw)
	float hud_scale		= 0.f;
	if (hud)
	{
		hud_scale		= 1.f;
		const float t_hud = tanf(deg2rad(0.5f * psHUD_FOV * Device.fFOV_HUD));
		const float t_wld = tanf(deg2rad(0.5f * Device.fFOV));
		if (t_hud > EPS_L && t_wld > t_hud)
			hud_scale	= t_wld / t_hud;
		hud_scale		*= READ_IF_EXISTS(pSettings, r_float, SC_SECT, "hud_scale", 1.f);
		if (hud_scale < 1.f)	hud_scale = 1.f;
	}
	CShellCasing::Spawn	(m_shell_track[idx].section.c_str(), xform, linear, angular, hud_scale, drop ? m_shell_track[idx].launch_time : 0);
}
