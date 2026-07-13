#include "stdafx.h"
#include "weaponpistol.h"
#include "ParticlesObject.h"
#include "actor.h"

CWeaponPistol::CWeaponPistol()
{
	m_eSoundClose		= ESoundTypes(SOUND_TYPE_WEAPON_RECHARGING);
	SetPending			(FALSE);
}

CWeaponPistol::~CWeaponPistol(void)
{
}

void CWeaponPistol::net_Destroy()
{
	inherited::net_Destroy();
}


void CWeaponPistol::Load	(LPCSTR section)
{
	inherited::Load		(section);

	m_sounds.LoadSound(section, "snd_close", "sndClose", false, m_eSoundClose);
}

void CWeaponPistol::OnH_B_Chield		()
{
	inherited::OnH_B_Chield		();
}

void CWeaponPistol::PlayAnimShow	()
{
	VERIFY(GetState()==eShowing);

	if(iAmmoElapsed==0)
		PlayHUDMotion("anm_show_empty", FALSE, this, GetState());
	else
		inherited::PlayAnimShow();
}

void CWeaponPistol::PlayAnimBore()
{
	if(iAmmoElapsed==0)
		PlayHUDMotion	("anm_bore_empty", TRUE, this, GetState());
	else
		inherited::PlayAnimBore();
}

void CWeaponPistol::PlayAnimIdleSprint()
{
	if(iAmmoElapsed==0)
	{
		PlayHUDMotion("anm_idle_sprint_empty", TRUE, NULL, GetState());
	}else{
		inherited::PlayAnimIdleSprint();
	}
}

void CWeaponPistol::PlayAnimIdleMoving()
{
	if(iAmmoElapsed==0)
	{
		PlayHUDMotion(SelectMovingAnim("anm_idle_moving_empty"), TRUE, NULL, GetState());
	}else{
		inherited::PlayAnimIdleMoving();
	}
}


void CWeaponPistol::PlayAnimIdle()
{
	if (TryPlayAnimIdle()) return;

	VERIFY(GetState()==eIdle);
	if(IsZoomed())
	{
		PlayAnimAim();	// ADS idle -> anm_idle_aim_empty (empty) / directional aim-walk
		return;
	}
	if(iAmmoElapsed==0)
		PlayHUDMotion("anm_idle_empty", TRUE, NULL, GetState());
	else
		inherited::PlayAnimIdle		();
}

// empty magazine: directional empty aim-walk (anm_idle_aim_walk[_dir]_empty) when
// moving, else the static empty aim (anm_idle_aim_empty). Loaded -> base directional.
void CWeaponPistol::SelectAimIdleAnim(string_path& result)
{
	if(iAmmoElapsed==0)
	{
		LPCSTR dir = AimWalkDirSuffix();
		if(dir[0])
		{
			xr_sprintf(result, "anm_idle_aim%s_empty", dir);
			if(isHUDAnimationExist(result))		return;
			if(xr_strcmp(dir, "_walk") != 0 && isHUDAnimationExist("anm_idle_aim_walk_empty"))
				{ xr_strcpy(result, "anm_idle_aim_walk_empty"); return; }
		}
		xr_strcpy(result, "anm_idle_aim_empty");
		return;
	}
	inherited::SelectAimIdleAnim(result);
}

void CWeaponPistol::PlayAnimReload()
{
	inherited::PlayAnimReload();
}

// empty aim in/out: slide-locked transition (anm_idle_aim_empty_start/_end) when the
// magazine is empty, else the normal aim transition.
void CWeaponPistol::SelectAimTransitionAnim(bool bAimIn, string_path& result)
{
	if (iAmmoElapsed == 0)
	{
		LPCSTR e = bAimIn ? "anm_idle_aim_empty_start" : "anm_idle_aim_empty_end";
		if (isHUDAnimationExist(e)) { xr_strcpy(result, e); return; }
	}
	inherited::SelectAimTransitionAnim(bAimIn, result);
}


void CWeaponPistol::PlayAnimHide()
{
	VERIFY(GetState()==eHiding);
	if(iAmmoElapsed==0) 
	{
		PlaySound			("sndClose", get_LastFP());
		PlayHUDMotion		("anm_hide_empty" , TRUE, this, GetState());
	} 
	else 
		inherited::PlayAnimHide();
}

// Pick the pistol shoot motion. When zoomed use the ADS shot (anm_shots_aim, with an
// _empty/last variant if present); otherwise the hip-fire shot, with anm_shot_l for the
// last chambered round (slide locks back). Base PlayAnimShoot() plays the result.
void CWeaponPistol::SelectShootAnim(string_path& result)
{
	if (IsZoomed() && isHUDAnimationExist("anm_shots_aim"))
	{
		if (iAmmoElapsed <= 1 && isHUDAnimationExist("anm_shots_aim_last"))
			xr_strcpy(result, "anm_shots_aim_last");
		else
			xr_strcpy(result, "anm_shots_aim");
		return;
	}
	if (iAmmoElapsed > 1)
		xr_strcpy(result, "anm_shots");
	else
		xr_strcpy(result, "anm_shot_l");
}


void CWeaponPistol::switch2_Reload()
{
	inherited::switch2_Reload();
}

void CWeaponPistol::OnAnimationEnd(u32 state)
{
	inherited::OnAnimationEnd(state);
}

void CWeaponPistol::OnShot		()
{
	PlaySound		(m_sSndShotCurrent.c_str(),get_LastFP());

	AddShotEffector	();
	
	PlayAnimShoot	();

	// Shell Drop
	Fvector vel; 
	PHGetLinearVell(vel);
	OnShellDrop					(get_LastSP(),  vel);

	// ����� �� ������
	
	StartFlameParticles	();
	R_ASSERT2(!m_pFlameParticles || !m_pFlameParticles->IsLooped(),
			  "can't set looped particles system for shoting with pistol");
	
	//��� �� ������
	StartSmokeParticles	(get_LastFP(), vel);
}

void CWeaponPistol::UpdateSounds()
{
	inherited::UpdateSounds();
	m_sounds.SetPosition("sndClose", get_LastFP());
}