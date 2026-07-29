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

// empty magazine -> the empty sprint idle base; the shared CHudItem::PlayAnimIdleSprint derives the
// enter/exit transitions (anm_idle_sprint_start_empty / _end_empty) from it, so they work when empty.
LPCSTR CWeaponPistol::SprintLoopBase()
{
	if(iAmmoElapsed==0)
		return "anm_idle_sprint_empty";
	return inherited::SprintLoopBase();
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
			if(xr_strcmp(dir, "_moving_forward") != 0 && isHUDAnimationExist("anm_idle_aim_moving_forward_empty"))
				{ xr_strcpy(result, "anm_idle_aim_moving_forward_empty"); return; }
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
// NOTE: no CWeaponPistol::SelectAimTransitionAnim override. The empty-magazine slide-locked aim
// transition is handled generically now: the base returns anm_idle_aim_start/_end and PlayHUDMotion's
// NeedEmptyAnim() rewrite appends "_empty" (MakeStateName) -> anm_idle_aim_start_empty / _end_empty,
// which is Gunslinger's exact key name/order (so GS pistol configs copy over directly).


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

// (SelectShootAnim was here: the ADS shot + the last-round slide-lock. It now lives in
// CWeaponMagazined so every magazined weapon gets it -- e.g. the SVD/SVU, which derive from
// CWeaponCustomPistol and so never saw this override. Same behaviour for pistols.)

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