#include "stdafx.h"
#include "weaponBM16.h"

CWeaponBM16::~CWeaponBM16()
{
}

void CWeaponBM16::Load	(LPCSTR section)
{
	inherited::Load		(section);
	m_sounds.LoadSound	(section, "snd_reload_1", "sndReload1", true, m_eSoundShot);
	// dedicated ammo-change reload sounds (optional; fall back to the normal reload sound if absent)
	if (pSettings->line_exist(section, "snd_reload_ammochange"))
		m_sounds.LoadSound(section, "snd_reload_ammochange", "sndReloadAmmochange", true, m_eSoundShot);
	if (pSettings->line_exist(section, "snd_reload_ammochange_1"))
		m_sounds.LoadSound(section, "snd_reload_ammochange_1", "sndReloadAmmochange1", true, m_eSoundShot);
}

LPCSTR CWeaponBM16::ShellSuffix()
{
	switch (m_magazine.size())
	{
	case 0:		return "_0";
	case 1:		return "_1";
	default:	return "_2";
	}
}

// aim in/out: pick anm_idle_aim_start/_end _0/_1/_2 by loaded shells, fall back to the
// unsuffixed alias (matches CWeaponMagazined::SelectAimTransitionAnim's contract).
void CWeaponBM16::SelectAimTransitionAnim(bool bAimIn, string_path& result)
{
	LPCSTR base = bAimIn ? "anm_idle_aim_start" : "anm_idle_aim_end";
	string_path tmp;
	strconcat(sizeof(tmp), tmp, base, ShellSuffix());
	if (isHUDAnimationExist(tmp))	{ xr_strcpy(result, tmp); return; }
	xr_strcpy(result, isHUDAnimationExist(base) ? base : "");
}

// torch/NV gesture: pick anm_headlamp_on/off / anm_nv_on/off _0/_1/_2 by loaded shells,
// else fall back to the base _jammed/_empty/base selection.
void CWeaponBM16::SelectActionAnim(LPCSTR base, string_path& result)
{
	string_path tmp;
	strconcat(sizeof(tmp), tmp, base, ShellSuffix());
	if (isHUDAnimationExist(tmp))	{ xr_strcpy(result, tmp); return; }
	inherited::SelectActionAnim(base, result);
}

// aim idle / directional aim-walk with the loaded-shell suffix (_0/_1/_2):
// anm_idle_aim[_walk[_dir]]_<n>, with graceful fallback to fwd aim-walk then static aim.
void CWeaponBM16::SelectAimIdleAnim(string_path& result)
{
	LPCSTR dir = AimWalkDirSuffix();	// "" / "_walk" / "_walk_back|left|right"
	LPCSTR sh  = ShellSuffix();		// "_0" / "_1" / "_2"
	string_path tmp;
	if (dir[0])
	{
		xr_sprintf(tmp, "anm_idle_aim%s%s", dir, sh);
		if (isHUDAnimationExist(tmp))	{ xr_strcpy(result, tmp); return; }
		strconcat(sizeof(tmp), tmp, "anm_idle_aim_moving_forward", sh);	// fwd aim-move fallback
		if (isHUDAnimationExist(tmp))	{ xr_strcpy(result, tmp); return; }
	}
	strconcat(sizeof(tmp), tmp, "anm_idle_aim", sh);			// static aim
	xr_strcpy(result, isHUDAnimationExist(tmp) ? tmp : "anm_idle_aim");
}

void CWeaponBM16::PlayReloadSound()
{
	// Mirror PlayAnimReload's single-vs-full split so the sound matches the animation. A "single-barrel"
	// load = one shell already loaded (top-up), OR an empty gun with fewer than 2 rounds on hand (the unique
	// anm_reload_only_0 / toz66_reload_last). Both use the short sndReload1, not the full sndReload.
	const int  cur    = (int)m_magazine.size();
	const bool change = (m_set_next_ammoType_on_reload != u32(-1));	// see PlayAnimReload: pending-set, not != m_ammoType
	const u32  reload_type = change ? m_set_next_ammoType_on_reload : (u32)m_ammoType;
	const bool only   = (GetAmmoCountByType(reload_type) < 2);
	const bool single = (cur == 1) || (cur <= 0 && only);
	const bool ammochange = change && cur > 0;

	if (ammochange)
	{
		// Match PlayAnimReload: the single-barrel change sound plays only when the change anim is the single
		// one (cur==1 with 2+ on hand -> _ammochange_1). Every other change (cur==2, or <2 on hand -> _only)
		// plays the full both-barrel motion. (Per-weapon: bm16's _ammochange_1 is a distinct single anim;
		// toz34 maps it to the full motion, so its snd_reload_ammochange_1 is the full sound.)
		LPCSTR s = (cur == 1 && !only) ? "sndReloadAmmochange1" : "sndReloadAmmochange";
		if (m_sounds.FindSoundItem(s, false)) { PlaySound(s, get_LastFP()); return; }
		if (m_sounds.FindSoundItem("sndReloadAmmochange", false)) { PlaySound("sndReloadAmmochange", get_LastFP()); return; }
	}
	PlaySound(single ? "sndReload1" : "sndReload", get_LastFP());
}

void CWeaponBM16::PlayAnimShoot()
{
	// ADS shoot variant when zoomed (Gunslinger-style); falls back to the hip shot if no alias.
	bool aimed = IsZoomed();
	switch( m_magazine.size() )
	{
	// GS double-barrel shoot names: anm_shoot_<shells left>, ADS = anm_shoot_aim_<n> (aim before count)
	case 1:
		if (aimed && isHUDAnimationExist("anm_shoot_aim_1"))
			PlayHUDMotion("anm_shoot_aim_1",FALSE,this,GetState());
		else
			PlayHUDMotion("anm_shoot_1",FALSE,this,GetState());
		break;
	case 2:
		if (aimed && isHUDAnimationExist("anm_shoot_aim_2"))
			PlayHUDMotion("anm_shoot_aim_2",FALSE,this,GetState());
		else
			PlayHUDMotion("anm_shoot_2",FALSE,this,GetState());
		break;
	}
}

void CWeaponBM16::PlayAnimShow()
{
	switch( m_magazine.size() )
	{
	case 0:
		PlayHUDMotion("anm_show_0",TRUE,this,GetState());
		break;
	case 1:
		PlayHUDMotion("anm_show_1",TRUE,this,GetState());
		break;
	case 2:
		PlayHUDMotion("anm_show_2",TRUE,this,GetState());
		break;
	}
}

void CWeaponBM16::PlayAnimHide()
{
	switch( m_magazine.size() )
	{
	case 0:
		PlayHUDMotion("anm_hide_0",TRUE,this,GetState());
		break;
	case 1:
		PlayHUDMotion("anm_hide_1",TRUE,this,GetState());
		break;
	case 2:
		PlayHUDMotion("anm_hide_2",TRUE,this,GetState());
		break;
	}
}

void CWeaponBM16::PlayAnimBore()
{
	switch( m_magazine.size() )
	{
	case 0:
		PlayHUDMotion("anm_bore_0",TRUE,this,GetState());
		break;
	case 1:
		PlayHUDMotion("anm_bore_1",TRUE,this,GetState());
		break;
	case 2:
		PlayHUDMotion("anm_bore_2",TRUE,this,GetState());
		break;
	}
}

void CWeaponBM16::PlayAnimReload()
{
	VERIFY(GetState()==eReload);
	// GS anm_reload_selector (WeaponAnims.pas): build "anm_reload[_only][_ammochange][_only]<count>". The
	// count suffix (_0/_1/_2 = shells currently loaded) is ModifierBM16; the prefixes mark the special
	// double-barrel reloads. Key case: an EMPTY gun with fewer than 2 rounds of the type to load plays the
	// unique single-round "_only_0" (toz66_reload_last -- seats one shell in the LOWER barrel).
	const int  cur    = (int)m_magazine.size();		// 0/1/2 shells loaded now
	// GS GetAmmoTypeChangingStatus: an ammo change is "a new type is pending" (m_set_next set), NOT "differs
	// from m_ammoType". When the old type is exhausted, TryReload's fallback sets m_ammoType to the new type,
	// so a != m_ammoType test wrongly reads false and plays the plain reload. SwitchAmmoType only ever sets
	// m_set_next to a different type, so "!= -1" alone is correct.
	const bool change = (m_set_next_ammoType_on_reload != u32(-1));
	const u32  reload_type = change ? m_set_next_ammoType_on_reload : (u32)m_ammoType;
	const bool only   = (GetAmmoCountByType(reload_type) < 2);	// <2 in inventory -> only one round loads

	string64 anim;	xr_strcpy(anim, "anm_reload");
	if (cur <= 0)
	{
		// empty gun: only a single-round (or single-round + type change) load gets a dedicated anim;
		// with 2+ rounds on hand it's a normal full reload (anm_reload_0), even across a type change.
		if (only)	xr_strcat(anim, change ? "_only_ammochange" : "_only");
		xr_strcat(anim, ShellSuffix());		// _0
	}
	else if (change)
	{
		// GS ModifierBM16: the change anim carries the CURRENT loaded count. bm16 has a dedicated single-barrel
		// change (anm_reload_ammochange_1 = toz66_ammochange) distinct from the both-barrel _2; toz34 maps both
		// suffixes to the full motion. <2 of the new type on hand keeps the _only variant.
		xr_strcat(anim, "_ammochange");
		if (only)	xr_strcat(anim, "_only");
		xr_strcat(anim, ShellSuffix());
	}
	else
		xr_strcat(anim, ShellSuffix());		// plain reload: _0/_1/_2 by current count

	// graceful fallback if the specific variant isn't configured for this weapon
	if (!isHUDAnimationExist(anim))
	{
		xr_strcpy(anim, "anm_reload");	xr_strcat(anim, ShellSuffix());
		if (!isHUDAnimationExist(anim))	xr_strcpy(anim, "anm_reload_2");
	}
	PlayHUDMotion(anim, TRUE, this, GetState());
}

void  CWeaponBM16::PlayAnimIdleMoving()
{
	switch( m_magazine.size() )
	{
	case 0:
		PlayHUDMotion(SelectMovingAnim("anm_idle_moving_0"),TRUE,this,GetState());
		break;
	case 1:
		PlayHUDMotion(SelectMovingAnim("anm_idle_moving_1"),TRUE,this,GetState());
		break;
	case 2:
		PlayHUDMotion(SelectMovingAnim("anm_idle_moving_2"),TRUE,this,GetState());
		break;
	}
}

// shell-count suffix for the sprint idle base; the shared CHudItem::PlayAnimIdleSprint derives the
// enter/exit transitions (anm_idle_sprint_start/_end + _0/_1/_2) from this.
LPCSTR CWeaponBM16::SprintLoopBase()
{
	switch( m_magazine.size() )
	{
	case 0:		return "anm_idle_sprint_0";
	case 1:		return "anm_idle_sprint_1";
	default:	return "anm_idle_sprint_2";
	}
}

void CWeaponBM16::PlayAnimIdle()
{
	if(TryPlayAnimIdle())	return;

	if(IsZoomed())
	{
		PlayAnimAim();	// -> SelectAimIdleAnim: directional aim-walk + loaded-shell suffix
	}else{
		switch (m_magazine.size())
		{
		case 0:{
			PlayHUDMotion("anm_idle_0", TRUE, NULL, GetState());
		}break;
		case 1:{
			PlayHUDMotion("anm_idle_1", TRUE, NULL, GetState());
		}break;
		case 2:{
			PlayHUDMotion("anm_idle_2", TRUE, NULL, GetState());
		}break;
		};
	}
}
