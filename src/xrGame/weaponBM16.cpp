#include "stdafx.h"
#include "weaponBM16.h"

CWeaponBM16::~CWeaponBM16()
{
}

void CWeaponBM16::Load	(LPCSTR section)
{
	inherited::Load		(section);
	m_sounds.LoadSound	(section, "snd_reload_1", "sndReload1", true, m_eSoundShot);
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
		strconcat(sizeof(tmp), tmp, "anm_idle_aim_walk", sh);	// fwd aim-walk fallback
		if (isHUDAnimationExist(tmp))	{ xr_strcpy(result, tmp); return; }
	}
	strconcat(sizeof(tmp), tmp, "anm_idle_aim", sh);			// static aim
	xr_strcpy(result, isHUDAnimationExist(tmp) ? tmp : "anm_idle_aim");
}

void CWeaponBM16::PlayReloadSound()
{
	if(m_magazine.size()==1)	
		PlaySound	("sndReload1",get_LastFP());
	else						
		PlaySound	("sndReload",get_LastFP());
}

void CWeaponBM16::PlayAnimShoot()
{
	// ADS shoot variant when zoomed (Gunslinger-style); falls back to the hip shot if no alias.
	bool aimed = IsZoomed();
	switch( m_magazine.size() )
	{
	case 1:
		if (aimed && isHUDAnimationExist("anm_shot_1_aim"))
			PlayHUDMotion("anm_shot_1_aim",FALSE,this,GetState());
		else
			PlayHUDMotion("anm_shot_1",FALSE,this,GetState());
		break;
	case 2:
		if (aimed && isHUDAnimationExist("anm_shot_2_aim"))
			PlayHUDMotion("anm_shot_2_aim",FALSE,this,GetState());
		else
			PlayHUDMotion("anm_shot_2",FALSE,this,GetState());
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
	bool b_both = HaveCartridgeInInventory(2);

	VERIFY(GetState()==eReload);
	if(m_magazine.size()==1 || !b_both)
		PlayHUDMotion("anm_reload_1",TRUE,this,GetState());
	else
		PlayHUDMotion("anm_reload_2",TRUE,this,GetState());
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

void  CWeaponBM16::PlayAnimIdleSprint()
{
	switch( m_magazine.size() )
	{
	case 0:
		PlayHUDMotion("anm_idle_sprint_0",TRUE,this,GetState());
		break;
	case 1:
		PlayHUDMotion("anm_idle_sprint_1",TRUE,this,GetState());
		break;
	case 2:
		PlayHUDMotion("anm_idle_sprint_2",TRUE,this,GetState());
		break;
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
