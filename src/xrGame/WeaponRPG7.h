#pragma once

#include "weaponpistol.h"
#include "rocketlauncher.h"
#include "script_export_space.h"

class CWeaponRPG7 :	public CWeaponCustomPistol,
					public CRocketLauncher
{
private:
	typedef CWeaponCustomPistol inherited;
public:
				CWeaponRPG7		();
	virtual		~CWeaponRPG7	();

	virtual BOOL net_Spawn		(CSE_Abstract* DC);
	virtual void OnStateSwitch	(u32 S);
	virtual void OnEvent		(NET_Packet& P, u16 type);
	virtual void ReloadMagazine	();
	// GS RPG7ReactiveHit: the backblast out of the tube (reactive_hit_* config family)
	void		 ReactiveHit	();
	// GS: a worn launcher can detonate its rocket in the tube (rocket_misfunc_* family)
	bool		 RocketMisfunction();
	virtual void Load			(LPCSTR section);
	virtual void switch2_Fire	();
	virtual	void FireTrace		(const Fvector& P, const Fvector& D);

	virtual void FireStart		();
	virtual void SwitchState	(u32 S);

			void UpdateMissileVisibility	();
	virtual void UnloadMagazine				(bool spawn_ammo = true);
	virtual void UpdateCL					();

	virtual void net_Import			( NET_Packet& P);				// import from server
protected:
	shared_str	m_sRocketSection;
	// What we last managed to apply to the HUD model's `grenade` bone: 1 shown, 0 hidden, -1 nothing
	// applied yet (our HUD model is not the attached one). See UpdateCL for why this is needed.
	int			m_hud_missile_vis;

	DECLARE_SCRIPT_REGISTER_FUNCTION
};
add_to_type_list(CWeaponRPG7)
#undef script_type_list
#define script_type_list save_type_list(CWeaponRPG7)
