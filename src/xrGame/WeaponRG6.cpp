#include "stdafx.h"
#include "WeaponRG6.h"
#include "entity.h"
#include "explosiveRocket.h"
#include "level.h"
#include "MathUtils.h"
#include "actor.h"

#ifdef DEBUG
#	include "phdebug.h"
#endif


CWeaponRG6::~CWeaponRG6()
{
}

BOOL	CWeaponRG6::net_Spawn				(CSE_Abstract* DC)
{
	BOOL l_res = inheritedSG::net_Spawn(DC);
	if (!l_res) return l_res;

	if (iAmmoElapsed && !getCurrentRocket())
	{
		shared_str grenade_name = m_ammoTypes[0];
		shared_str fake_grenade_name = pSettings->r_string(grenade_name, "fake_grenade_name");

		if (fake_grenade_name.size())
		{
			int k=iAmmoElapsed;
			while (k)
			{
				k--;
				inheritedRL::SpawnRocket(*fake_grenade_name, this);
			}
		}
//			inheritedRL::SpawnRocket(*fake_grenade_name, this);
	}
	

	
	return l_res;
};

void CWeaponRG6::Load(LPCSTR section)
{
	inheritedRL::Load(section);
	inheritedSG::Load(section);
}
#include "inventory.h"
#include "inventoryOwner.h"
// GS RL_SpawnRocket (WeaponEvents.pas:2164) + its CWeaponRG6::AddCartridge replacement: the launcher
// no longer spawns its "fake grenade" when a round is LOADED (that only ever worked through the
// shell-by-shell tri-state reload -- a plain magazined reload fills m_magazine via ReloadMagazine and
// never touches AddCartridge, so the gun had ammo but nothing to launch). Instead the rocket is created
// lazily, right before the shot, from the round that is about to be fired. Once per frame, like GS.
// Returns true when it had to spawn one -- GS skips that trigger pull and fires on the next one.
bool CWeaponRG6::SpawnRocketIfNeeded()
{
	if (getRocketCount() > 0 || m_magazine.empty())			return false;
	if (m_dwRocketSpawnFrame == Device.dwFrame)				return false;
	m_dwRocketSpawnFrame = Device.dwFrame;

	const CCartridge& c = m_magazine.back();				// the round that fires next
	LPCSTR sect = (c.m_LocalAmmoType < m_ammoTypes.size())
				? *m_ammoTypes[c.m_LocalAmmoType] : *m_ammoTypes[m_ammoType];
	if (!pSettings->line_exist(sect, "fake_grenade_name"))	return false;
	inheritedRL::SpawnRocket(pSettings->r_string(sect, "fake_grenade_name"), this);
	return true;
}

void CWeaponRG6::FireStart ()
{
	if (GetState() == eIdle && SpawnRocketIfNeeded())	return;	// grenade created this frame -> pull again

	if(GetState() == eIdle	&& getRocketCount() )
	{
		inheritedSG::FireStart ();
	
		Fvector p1, d; 
		p1.set(get_LastFP()); 
		d.set(get_LastFD());

		CEntity* E = smart_cast<CEntity*>(H_Parent());
		if (E){
			CInventoryOwner* io		= smart_cast<CInventoryOwner*>(H_Parent());
			if(NULL == io->inventory().ActiveItem())
			{
			Log("current_state", GetState() );
			Log("next_state", GetNextState());
			Log("item_sect", cNameSect().c_str());
			Log("H_Parent", H_Parent()->cNameSect().c_str());
			}
			E->g_fireParams (this, p1,d);
		}

		Fmatrix launch_matrix;
		launch_matrix.identity();
		launch_matrix.k.set(d);
		Fvector::generate_orthonormal_basis(launch_matrix.k,
											launch_matrix.j, launch_matrix.i);
		launch_matrix.c.set(p1);

		if (IsZoomed() && smart_cast<CActor*>(H_Parent()))
		{
			H_Parent()->setEnabled(FALSE);
			setEnabled(FALSE);
		
			collide::rq_result RQ;
			BOOL HasPick = Level().ObjectSpace.RayPick(p1, d, 300.0f, collide::rqtStatic, RQ, this);

			setEnabled(TRUE);
			H_Parent()->setEnabled(TRUE);

			if (HasPick)
			{
				//			collide::rq_result& RQ = HUD().GetCurrentRayQuery();
				Fvector Transference;
				//Transference.add(p1, Fvector().mul(d, RQ.range));				
				Transference.mul(d, RQ.range);
				Fvector res[2];
/*#ifdef		DEBUG
				DBG_OpenCashedDraw();
				DBG_DrawLine(p1, Fvector().add(p1, d), color_xrgb(255, 0, 0));
#endif*/
				u8 canfire0 = TransferenceAndThrowVelToThrowDir(Transference, CRocketLauncher::m_fLaunchSpeed, EffectiveGravity(), res);
/*#ifdef DEBUG
				if (canfire0 > 0) DBG_DrawLine(p1, Fvector().add(p1, res[0]), color_xrgb(0, 255, 0));
				if (canfire0 > 1) DBG_DrawLine(p1, Fvector().add(p1, res[1]), color_xrgb(0, 0, 255));
				DBG_ClosedCashedDraw(30000);
#endif*/
				if (canfire0 != 0)
				{
//					Msg ("d[%f,%f,%f] - res [%f,%f,%f]", d.x, d.y, d.z, res[0].x, res[0].y, res[0].z);
					d = res[0];
				};
			}
		};

		d.normalize();
		d.mul(m_fLaunchSpeed);
		VERIFY2(_valid(launch_matrix),"CWeaponRG6::FireStart. Invalid launch_matrix");
		CRocketLauncher::LaunchRocket(launch_matrix, d, zero_vel);

		CExplosiveRocket* pGrenade = smart_cast<CExplosiveRocket*>(getCurrentRocket());
		VERIFY(pGrenade);
		pGrenade->SetInitiator(H_Parent()->ID());

		if (OnServer())
		{
			NET_Packet P;
			u_EventGen(P,GE_LAUNCH_ROCKET,ID());
			P.w_u16(u16(getCurrentRocket()->ID()));
			u_EventSend(P);
		}
		dropCurrentRocket();
	}
}

// GS CWeaponRG6__AddCartridge_Replace_Patch (WeaponEvents.pas:2156 / installed at :3157): GS replaces
// this override with the plain CWeaponShotgun::AddCartridge -- loading a round must NOT spawn a rocket,
// because the shell-by-shell path is only one of the two ways rounds get in (see SpawnRocketIfNeeded).
u8 CWeaponRG6::AddCartridge		(u8 cnt)
{
	u8 res = inheritedSG::AddCartridge(cnt);
	SpawnRocketIfNeeded();		// at most ONE, unlike the vanilla per-round spawn this replaced
	return res;
}

// The magazined reload path fills m_magazine directly (no AddCartridge), so hook the grenade spawn here
// too. GS creates it at the first trigger pull and swallows that pull; doing it as soon as the round is
// loaded means the shot after a reload fires immediately -- the spawn is a net event, so it has to happen
// at least one frame before the shot either way.
void CWeaponRG6::ReloadMagazine()
{
	inheritedSG::ReloadMagazine();
	SpawnRocketIfNeeded();
}

void CWeaponRG6::OnEvent(NET_Packet& P, u16 type) 
{
	inheritedSG::OnEvent(P,type);

	u16 id;
	switch (type) {
		case GE_OWNERSHIP_TAKE : {
			P.r_u16(id);
			inheritedRL::AttachRocket(id, this);
		} break;
		case GE_OWNERSHIP_REJECT : 
		case GE_LAUNCH_ROCKET : 
			{
			bool bLaunch = (type==GE_LAUNCH_ROCKET);
			P.r_u16						(id);
			inheritedRL::DetachRocket	(id, bLaunch);
		} break;
	}
}
