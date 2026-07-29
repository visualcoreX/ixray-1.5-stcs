#include "stdafx.h"

#include "Entity.h"
#include "WeaponCustomPistol.h"

CWeaponCustomPistol::CWeaponCustomPistol() : CWeaponMagazined(SOUND_TYPE_WEAPON_PISTOL)
{
}

CWeaponCustomPistol::~CWeaponCustomPistol()
{
}
void CWeaponCustomPistol::switch2_Fire	()
{
	m_bFireSingleShot			= true;
	bWorking					= false;
	m_iShotNum					= 0;
	m_bStopedAfterQueueFired	= false;
}



void CWeaponCustomPistol::FireEnd()
{
	// The fire key is up now regardless of the shot cooldown. The cooldown gate below only defers the
	// pending/teardown work (inherited::FireEnd), but it must NOT keep m_bTriggerHeld stuck true -- on
	// pistols and shotguns (CWeaponShotgun/BM16 derive from this) that stale flag made the aim-lock
	// autoshoot fire a shot by itself on the next aim in/out. Clear it here every release.
	m_bTriggerHeld			= false;
	if(fShotTimeCounter<=0)
	{
		SetPending			(FALSE);
		inherited::FireEnd	();
	}
}