#include "stdafx.h"
#include "weaponmagazinedwgrenade.h"
#include "HUDManager.h"
#include "entity.h"
#include "ParticlesObject.h"
#include "GrenadeLauncher.h"
#include "xrserver_objects_alife_items.h"
#include "ExplosiveRocket.h"
#include "Actor.h"
#include "xr_level_controller.h"
#include "object_broker.h"		// READ_IF_EXISTS
#include "level.h"
#include "object_broker.h"
#include "game_base_space.h"
#include "MathUtils.h"
#include "player_hud.h"
#include "Actor_Flags.h"

#ifdef DEBUG
#	include "phdebug.h"
#endif

CWeaponMagazinedWGrenade::CWeaponMagazinedWGrenade(ESoundTypes eSoundType) : CWeaponMagazined(eSoundType)
{
	m_ammoType2 = 0;
    m_bGrenadeMode = false;
}

CWeaponMagazinedWGrenade::~CWeaponMagazinedWGrenade()
{
}

// gwr: fold the loaded-grenade state into the base's change detection so the GL bone updates when the
// grenade is loaded/fired or its type changes. Packed: count(0/1) | ammoType<<1 | attached<<8.
int CWeaponMagazinedWGrenade::gwr_GLBonesState()
{
	int attached = IsGrenadeLauncherAttached() ? 1 : 0;
	return iAmmoElapsed2 | (int(m_ammoType2) << 1) | (attached << 8);
}

// Show the bone for the grenade currently in the launcher (like GS's ProcessAmmoGL): pick the param
// section by GL ammo type, hide all_bones, show configuration_<grenades loaded>.
void CWeaponMagazinedWGrenade::gwr_UpdateBonesGL()
{
	if (!GetHUDmode() || !HudItemData())		return;
	if (!IsGrenadeLauncherAttached())			return;

	// The loaded grenade's count/type live in the ACTIVE ammo slot while in grenade mode and in the
	// stored "2" slot while in bullet mode -- PerformSwitchGL swaps m_ammoType<->m_ammoType2 and the
	// magazines on every mode toggle. Reading the "2" fields unconditionally meant that in grenade mode
	// (where GL reloads actually happen) we read the *bullet* count/type, so the grenade bone never
	// showed during a GL reload. Pick the grenade slot by the current mode.
	const shared_str& sect = HudSection();
	u32 gren_count = m_bGrenadeMode ? (u32)iAmmoElapsed : (u32)iAmmoElapsed2;
	u32 gren_type  = m_bGrenadeMode ? m_ammoType        : m_ammoType2;

	// What to DISPLAY, with reload phasing. The grenade is only actually loaded at OnAnimationEnd, and on a
	// grenade-type change m_ammoType is still the OLD type until then -- so during the reload we must drive
	// the shown bone from the type being LOADED (m_set_next_ammoType_on_reload), not the stale current one,
	// or the launcher shows one grenade while you chamber another. The bone seats at gl_reload_insert_mark
	// (fraction of the reload anim, default 0.5), like the per-barrel ammo insert mark.
	int shown     = (int)gren_count;
	u32 show_type = gren_type;
	if (m_bGrenadeMode && GetState() == eReload)
	{
		u32 s = m_dwMotionStartTm, e = m_dwMotionEndTm, now = Device.dwTimeGlobal;
		float progress = (e > s) ? float(now - s) / float(e - s) : 1.0f;
		clamp(progress, 0.0f, 1.0f);
		float mark = READ_IF_EXISTS(pSettings, r_float, sect, "gl_reload_insert_mark", 0.5f);

		bool have_next  = (m_set_next_ammoType_on_reload != u32(-1));
		u32  next_type  = have_next ? m_set_next_ammoType_on_reload : gren_type;
		bool ammochange = have_next && (next_type != gren_type) && (gren_count > 0);
		bool seated     = (progress >= mark);

		if (ammochange)
		{
			// swap: the OLD grenade stays until the hand pulls it (mark), then the NEW one seats
			shown     = 1;
			show_type = seated ? next_type : gren_type;
		}
		else
		{
			// fresh load into an empty launcher: the grenade appears at the mark, of the loaded type
			shown     = seated ? 1 : 0;
			show_type = next_type;
		}
	}

	string128 key;  xr_sprintf(key, "gl_ammo_params_section_%d", show_type);
	LPCSTR bsect = NULL;
	if (pSettings->line_exist(sect, key))				bsect = pSettings->r_string(sect, key);
	else if (pSettings->line_exist(sect, "gl_ammo_params_section"))	bsect = pSettings->r_string(sect, "gl_ammo_params_section");

	if (!bsect)									return;

	if (pSettings->line_exist(bsect, "all_bones"))	gwr_SetBones(pSettings->r_string(bsect, "all_bones"), FALSE);
	xr_sprintf(key, "configuration_%d", shown);
	if (pSettings->line_exist(bsect, key))			gwr_SetBones(pSettings->r_string(bsect, key), TRUE);
}

void CWeaponMagazinedWGrenade::Load	(LPCSTR section)
{
	inherited::Load			(section);
	CRocketLauncher::Load	(section);
	
	
	//// Sounds
	m_sounds.LoadSound(section,"snd_shoot_grenade"	, "sndShotG"		, false, m_eSoundShot);
	m_sounds.LoadSound(section,"snd_reload_grenade"	, "sndReloadG"	, true, m_eSoundReload);
	// dedicated grenade-type-change sound (GS snd_change_grenade); falls back to sndReloadG if absent
	if (pSettings->line_exist(section, "snd_change_grenade"))
		m_sounds.LoadSound(section,"snd_change_grenade", "sndChangeGrenade", true, m_eSoundReload);
	m_sounds.LoadSound(section,"snd_switch"			, "sndSwitch"		, true, m_eSoundReload);
	

	m_sFlameParticles2 = pSettings->r_string(section, "grenade_flame_particles");

	
	if(m_eGrenadeLauncherStatus == ALife::eAddonPermanent)
	{
		CRocketLauncher::m_fLaunchSpeed = pSettings->r_float(section, "grenade_vel");
	}

	// load ammo classes SECOND (grenade_class)
	m_ammoTypes2.clear	(); 
	LPCSTR				S = pSettings->r_string(section,"grenade_class");
	if (S && S[0]) 
	{
		string128		_ammoItem;
		int				count		= _GetItemCount	(S);
		for (int it=0; it<count; ++it)	
		{
			_GetItem				(S,it,_ammoItem);
			m_ammoTypes2.push_back	(_ammoItem);
		}
		m_ammoName2 = pSettings->r_string(*m_ammoTypes2[0],"inv_name_short");
	}
	else
		m_ammoName2 = 0;

	iMagazineSize2 = iMagazineSize;
}

void CWeaponMagazinedWGrenade::net_Destroy()
{
	inherited::net_Destroy();
}


BOOL CWeaponMagazinedWGrenade::net_Spawn(CSE_Abstract* DC) 
{
	CSE_ALifeItemWeapon* const weapon		= smart_cast<CSE_ALifeItemWeapon*>(DC);
	R_ASSERT								(weapon);
	if ( IsGameTypeSingle() )
	{
		inherited::net_Spawn_install_upgrades	(weapon->m_upgrades);
	}

	BOOL l_res = inherited::net_Spawn(DC);
	 
	UpdateGrenadeVisibility(!!iAmmoElapsed);
	SetPending			(FALSE);

	iAmmoElapsed2	= weapon->a_elapsed_grenades.grenades_count;
	m_ammoType2		= weapon->a_elapsed_grenades.grenades_type;

	m_DefaultCartridge2.Load(*m_ammoTypes2[m_ammoType2], u8(m_ammoType2));
	
	if (!IsGameTypeSingle())
	{
		if (!m_bGrenadeMode && IsGrenadeLauncherAttached() && !getRocketCount() && iAmmoElapsed2)
		{
			m_magazine2.push_back(m_DefaultCartridge2);

			shared_str grenade_name = m_DefaultCartridge2.m_ammoSect;
			shared_str fake_grenade_name = pSettings->r_string(grenade_name, "fake_grenade_name");

			CRocketLauncher::SpawnRocket(*fake_grenade_name, this);
		}
	}else
	{
		xr_vector<CCartridge>* pM = NULL;
		bool b_if_grenade_mode	= (m_bGrenadeMode && iAmmoElapsed && !getRocketCount());
		if(b_if_grenade_mode)
			pM = &m_magazine;
			
		bool b_if_simple_mode	= (!m_bGrenadeMode && m_magazine2.size() && !getRocketCount());
		if(b_if_simple_mode)
			pM = &m_magazine2;

		if(b_if_grenade_mode || b_if_simple_mode) 
		{
			shared_str fake_grenade_name = pSettings->r_string(pM->back().m_ammoSect, "fake_grenade_name");
			
			CRocketLauncher::SpawnRocket(*fake_grenade_name, this);
		}
	}
	return l_res;
}

void CWeaponMagazinedWGrenade::switch2_Reload()
{
	VERIFY(GetState()==eReload);
	if(m_bGrenadeMode)
	{
		// Grenade-type change: a grenade is loaded and a DIFFERENT type is selected to load -> play the
		// dedicated grenade-change animation (ejects the old grenade, seats the new one) instead of the
		// plain reload, mirroring GS's anm_reload_ammochange_g (= ak74_gl_grenadechange). In grenade mode
		// m_magazine / m_ammoType are the grenade's (PerformSwitchGL swapped them in).
		bool ammochange = !m_magazine.empty()
			&& m_set_next_ammoType_on_reload != u32(-1)
			&& m_set_next_ammoType_on_reload != m_ammoType;

		LPCSTR anim = "anm_reload_g";
		if (ammochange && isHUDAnimationExist("anm_reload_ammochange_g"))
			anim = "anm_reload_ammochange_g";

		// the grenade-change gesture has its own, longer sound (GS snd_change_grenade); the plain
		// grenade load keeps sndReloadG. Fall back to sndReloadG if the change sound isn't configured.
		if (ammochange && m_sounds.FindSoundItem("sndChangeGrenade", false))
			PlaySound("sndChangeGrenade", get_LastFP2());
		else
			PlaySound("sndReloadG", get_LastFP2());

		PlayHUDMotion(anim, TRUE, this, GetState());	// blend in, like every other reload
		SetPending			(TRUE);
	}
	else
	     inherited::switch2_Reload();
}

void CWeaponMagazinedWGrenade::OnShot		()
{
	if(m_bGrenadeMode)
	{
		PlayAnimShoot		();
		PlaySound			("sndShotG", get_LastFP2());
		AddShotEffector		();
		StartFlameParticles2();
	} 
	else 
		inherited::OnShot	();
}

bool CWeaponMagazinedWGrenade::SwitchMode() 
{
	if (!IsGrenadeLauncherAttached())
		return false;

	auto bUsefulStateToSwitch = (!IsPending() && !IsZoomed() && (GetState() == eIdle || GetState() == eMisfire));

	if (!bUsefulStateToSwitch)
		return false;

	SwitchState(eSwitch);

	m_dwAmmoCurrentCalcFrame = 0;

	return true;
}

void CWeaponMagazinedWGrenade::switch2_SwitchMode()
{
	SetPending(TRUE);
	PlaySound("sndSwitch", get_LastFP());
	PerformSwitchGL();
	PlayAnimModeSwitch();
}

void CWeaponMagazinedWGrenade::PerformSwitchGL()
{
	m_bGrenadeMode		= !m_bGrenadeMode;

	iMagazineSize		= m_bGrenadeMode?1:iMagazineSize2;

	m_ammoTypes.swap	(m_ammoTypes2);

	swap				(m_ammoType,m_ammoType2);
	swap				(m_ammoName,m_ammoName2);
	
	swap				(m_DefaultCartridge, m_DefaultCartridge2);

	xr_vector<CCartridge> l_magazine;
	while(m_magazine.size()) { l_magazine.push_back(m_magazine.back()); m_magazine.pop_back(); }
	while(m_magazine2.size()) { m_magazine.push_back(m_magazine2.back()); m_magazine2.pop_back(); }
	while(l_magazine.size()) { m_magazine2.push_back(l_magazine.back()); l_magazine.pop_back(); }
	iAmmoElapsed = (int)m_magazine.size();

}

bool CWeaponMagazinedWGrenade::Action(s32 cmd, u32 flags) 
{
	if(m_bGrenadeMode && cmd==kWPN_FIRE)
	{
		if(IsPending())		
			return				false;

		if(flags&CMD_START)
		{
			if(iAmmoElapsed)
				LaunchGrenade();
			else
			{
				if (psActorFlags.test(AF_AUTORELOAD))
					Reload();
				else
					OnEmptyClick();
			}
		}
		return					true;
	}
	if(inherited::Action(cmd, flags))
		return true;
	
	switch(cmd) 
	{
	case kWPN_FUNC: 
		{
            if (flags&CMD_START) 
				return SwitchMode();
			else
				return false;
		}
	}
	return false;
}

#include "inventory.h"
#include "inventoryOwner.h"
void CWeaponMagazinedWGrenade::state_Fire(float dt) 
{
	VERIFY(fOneShotTime>0.f);

	//����� �������� �������������
	if(m_bGrenadeMode)
	{
		/*
		fTime					-=dt;
		while (fTime<=0 && (iAmmoElapsed>0) && (IsWorking() || m_bFireSingleShot))
		{
			++m_iShotNum;
			OnShot			();
			
			// Ammo
			if(Local()) 
			{
				VERIFY				(m_magazine.size());
				m_magazine.pop_back	();
				--iAmmoElapsed;
			
				VERIFY((u32)iAmmoElapsed == m_magazine.size());
			}
		}
		UpdateSounds				();
		if(m_iShotNum == m_iQueueSize) 
			FireEnd();
		*/
	} 
	//����� �������� ���������
	else 
		inherited::state_Fire(dt);
}


void CWeaponMagazinedWGrenade::OnEvent(NET_Packet& P, u16 type) 
{
	inherited::OnEvent(P,type);
	u16 id;
	switch (type) 
	{
		case GE_OWNERSHIP_TAKE: 
			{
				P.r_u16(id);
				CRocketLauncher::AttachRocket(id, this);
			}
			break;
		case GE_OWNERSHIP_REJECT :
		case GE_LAUNCH_ROCKET : 
			{
				bool bLaunch	= (type==GE_LAUNCH_ROCKET);
				P.r_u16			(id);
				CRocketLauncher::DetachRocket(id, bLaunch);
				if(bLaunch)
				{
					PlayAnimShoot		();
					PlaySound			("sndShotG", get_LastFP2());
					AddShotEffector		();
					StartFlameParticles2();
				}
				break;
			}
	}
}

void CWeaponMagazinedWGrenade::LaunchGrenade_Correct(Fvector3* v)
{
	Fvector3 camdir = Device.vCameraDirection;

	camdir.y = 0;
	camdir.normalize();

	camdir.y = 1;
	camdir.normalize();

	*v = camdir;
}

void  CWeaponMagazinedWGrenade::LaunchGrenade()
{
	if(!getRocketCount())	return;
	R_ASSERT				(m_bGrenadeMode);
	{
		Fvector						p1, d; 
		p1.set						(get_LastFP2());
		d.set						(get_LastFD());
		CEntity*					E = smart_cast<CEntity*>(H_Parent());

		if (E){
			CInventoryOwner* io		= smart_cast<CInventoryOwner*>(H_Parent());
			if(NULL == io->inventory().ActiveItem())
			{
				Log("current_state", GetState() );
				Log("next_state", GetNextState());
				Log("item_sect", cNameSect().c_str());
				Log("H_Parent", H_Parent()->cNameSect().c_str());
			}
			E->g_fireParams		(this, p1,d);
		}
		if (IsGameTypeSingle())
			p1.set						(get_LastFP2());
		
		Fmatrix							launch_matrix;
		launch_matrix.identity			();
		launch_matrix.k.set				(d);
		Fvector::generate_orthonormal_basis(launch_matrix.k,
											launch_matrix.j, 
											launch_matrix.i);

		launch_matrix.c.set				(p1);

		if(IsZoomed() && smart_cast<CActor*>(H_Parent()))
		{
			H_Parent()->setEnabled		(FALSE);
			setEnabled					(FALSE);

			collide::rq_result			RQ;
			BOOL HasPick				= Level().ObjectSpace.RayPick(p1, d, 300.0f, collide::rqtStatic, RQ, this);

			setEnabled					(TRUE);
			H_Parent()->setEnabled		(TRUE);

			if(HasPick)
			{
				Fvector					Transference;
				Transference.mul		(d, RQ.range);
				Fvector					res[2];
#ifdef		DEBUG
//.				DBG_OpenCashedDraw();
//.				DBG_DrawLine(p1, Fvector().add(p1, d), color_xrgb(255, 0, 0));
#endif
				u8 canfire0 = TransferenceAndThrowVelToThrowDir(Transference, 
																CRocketLauncher::m_fLaunchSpeed, 
																EffectiveGravity(), 
																res);
#ifdef DEBUG
//.				if (canfire0 > 0) DBG_DrawLine(p1, Fvector().add(p1, res[0]), color_xrgb(0, 255, 0));
//.				if (canfire0 > 1) DBG_DrawLine(p1, Fvector().add(p1, res[1]), color_xrgb(0, 0, 255));
//.				DBG_ClosedCashedDraw(30000);
#endif
				
				if (canfire0 != 0)
					d = res[0];
				else
					LaunchGrenade_Correct(&d);
			}
		};
		
		d.normalize						();
		d.mul							(CRocketLauncher::m_fLaunchSpeed);
		VERIFY2							(_valid(launch_matrix),"CWeaponMagazinedWGrenade::SwitchState. Invalid launch_matrix!");
		CRocketLauncher::LaunchRocket	(launch_matrix, d, zero_vel);

		CExplosiveRocket* pGrenade		= smart_cast<CExplosiveRocket*>(getCurrentRocket());
		VERIFY							(pGrenade);
		pGrenade->SetInitiator			(H_Parent()->ID());

		
		if (Local() && OnServer())
		{
			VERIFY				(m_magazine.size());
			m_magazine.pop_back	();
			--iAmmoElapsed;
			VERIFY((u32)iAmmoElapsed == m_magazine.size());

			NET_Packet					P;
			u_EventGen					(P,GE_LAUNCH_ROCKET,ID());
			P.w_u16						(getCurrentRocket()->ID());
			u_EventSend					(P);
		};
	}
}

void CWeaponMagazinedWGrenade::FireEnd() 
{
	if(m_bGrenadeMode)
	{
		CWeapon::FireEnd();
	}else
		inherited::FireEnd();
}

void CWeaponMagazinedWGrenade::ReloadMagazine() 
{
	auto last_bMisfire = bMisfire;
	inherited::ReloadMagazine();
	
	//����������� ������������� �����������
	if(m_bGrenadeMode)
	{
		bMisfire = last_bMisfire;
		if(iAmmoElapsed && !getRocketCount()) 
		{
			shared_str fake_grenade_name = pSettings->r_string(m_ammoTypes[m_ammoType].c_str(), "fake_grenade_name");
			CRocketLauncher::SpawnRocket(*fake_grenade_name, this);
		}
	}
}

void CWeaponMagazinedWGrenade::OnStateSwitch(u32 S) 
{
	switch (S)
	{
		case eSwitch:
			switch2_SwitchMode();
		break;
	}
	
	inherited::OnStateSwitch(S);
	UpdateGrenadeVisibility(!!iAmmoElapsed || S == eReload);
}

void CWeaponMagazinedWGrenade::OnAnimationEnd(u32 state)
{
	switch (state)
	{
		case eSwitch:
			SwitchState(eIdle);
			break;
		case eFire:
			// The GL shot is event-driven (state_Fire is empty in grenade mode), so the shoot anim is
			// played standalone at eFire and nothing else returns the weapon to idle -- the base
			// OnAnimationEnd has no eFire case. Without this the last shoot frame freezes on screen
			// (very visible while walking, since the moving-idle never resumes).
			if (m_bGrenadeMode)
			{
				SwitchState(eIdle);
				return;
			}
			break;
	}
	inherited::OnAnimationEnd(state);
}

void CWeaponMagazinedWGrenade::OnH_B_Independent(bool just_before_destroy)
{
	inherited::OnH_B_Independent(just_before_destroy);

	SetPending			(FALSE);
	if (m_bGrenadeMode) {
		SetState		( eIdle );
		SetPending		(FALSE);
	}
}

bool CWeaponMagazinedWGrenade::CanAttach(PIItem pIItem)
{
	CGrenadeLauncher* pGrenadeLauncher = smart_cast<CGrenadeLauncher*>(pIItem);
	
	if(pGrenadeLauncher &&
	   ALife::eAddonAttachable == m_eGrenadeLauncherStatus &&
	   0 == (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) &&
	   !xr_strcmp(*m_sGrenadeLauncherName, pIItem->object().cNameSect()))
       return true;
	else
		return inherited::CanAttach(pIItem);
}

bool CWeaponMagazinedWGrenade::CanDetach(LPCSTR item_section_name)
{
	if(ALife::eAddonAttachable == m_eGrenadeLauncherStatus &&
	   0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) &&
	   !xr_strcmp(*m_sGrenadeLauncherName, item_section_name))
	   return true;
	else
	   return inherited::CanDetach(item_section_name);
}

bool CWeaponMagazinedWGrenade::Attach(PIItem pIItem, bool b_send_event)
{
	CGrenadeLauncher* pGrenadeLauncher = smart_cast<CGrenadeLauncher*>(pIItem);
	
	if(pGrenadeLauncher &&
	   ALife::eAddonAttachable == m_eGrenadeLauncherStatus &&
	   0 == (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) &&
	   !xr_strcmp(*m_sGrenadeLauncherName, pIItem->object().cNameSect()))
	{
		m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher;

		CRocketLauncher::m_fLaunchSpeed = pGrenadeLauncher->GetGrenadeVel();

 		//���������� ������������ �� ���������
		if(b_send_event)
		{
			if (OnServer()) 
				pIItem->object().DestroyObject	();
		}
		InitAddons				();
		UpdateAddonsVisibility	();

		if(GetState()==eIdle)
			PlayAnimIdle		();

		return					true;
	}
	else
        return inherited::Attach(pIItem, b_send_event);
}

bool CWeaponMagazinedWGrenade::Detach(LPCSTR item_section_name, bool b_spawn_item)
{
	if (ALife::eAddonAttachable == m_eGrenadeLauncherStatus &&
	   0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) &&
	   !xr_strcmp(*m_sGrenadeLauncherName, item_section_name))
	{
		m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher;
		if(m_bGrenadeMode)
		{
			UnloadMagazine();
			PerformSwitchGL();
		}

		UpdateAddonsVisibility();

		if(GetState()==eIdle)
			PlayAnimIdle		();

		return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
	}
	else
		return inherited::Detach(item_section_name, b_spawn_item);
}

void CWeaponMagazinedWGrenade::InitAddons()
{	
	inherited::InitAddons();

	if(GrenadeLauncherAttachable())
	{
		if(IsGrenadeLauncherAttached())
		{
			CRocketLauncher::m_fLaunchSpeed = pSettings->r_float(*m_sGrenadeLauncherName,"grenade_vel");
		}
	}
}

bool	CWeaponMagazinedWGrenade::UseScopeTexture()
{
	if (IsGrenadeLauncherAttached() && m_bGrenadeMode) return false;
	if (IsLensedScope())	return false;	// 3D PiP lens scope -> skip the 2D scope texture, keep the weapon visible
	return true;
};

float	CWeaponMagazinedWGrenade::CurrentZoomFactor	()
{
	if (IsGrenadeLauncherAttached() && m_bGrenadeMode) return m_zoom_params.m_fIronSightZoomFactor;
	return inherited::CurrentZoomFactor();
}

//����������� ������� ��� ������������ �������� HUD
void CWeaponMagazinedWGrenade::PlayAnimShow()
{
	VERIFY(GetState()==eShowing);
	if(IsGrenadeLauncherAttached())
	{
		if(!m_bGrenadeMode)
			PlayHUDMotion("anm_show_w_gl", FALSE, this, GetState());
		else
			PlayHUDMotion("anm_show_g", FALSE, this, GetState());
	}	
	else
		PlayHUDMotion("anm_show", FALSE, this, GetState());
}

void CWeaponMagazinedWGrenade::PlayAnimHide()
{
	VERIFY(GetState()==eHiding);
	
	if(IsGrenadeLauncherAttached())
		if(!m_bGrenadeMode)
			PlayHUDMotion("anm_hide_w_gl", TRUE, this, GetState());
		else
			PlayHUDMotion("anm_hide_g", TRUE, this, GetState());

	else
		PlayHUDMotion("anm_hide", TRUE, this, GetState());
}

void CWeaponMagazinedWGrenade::PlayAnimReload()
{
	VERIFY(GetState()==eReload);

	if (IsGrenadeLauncherAttached())
	{
		if (isHUDAnimationExist("anm_reload_jammed_w_gl") && IsMisfire())
		{
			PlayHUDMotion("anm_reload_jammed_w_gl", TRUE, this, GetState());
			bMisfireReload = true;
		}
		else if (isHUDAnimationExist("anm_reload_empty_w_gl") && iAmmoElapsed == 0)
			PlayHUDMotion("anm_reload_empty_w_gl", TRUE, this, GetState());
		else
			PlayHUDMotion("anm_reload_w_gl", TRUE, this, GetState());
	}
	else
		inherited::PlayAnimReload();
}

void CWeaponMagazinedWGrenade::SelectActionAnim(LPCSTR base, string_path& result)
{
	if (IsGrenadeLauncherAttached())
	{
		string_path tmp;
		if (m_bGrenadeMode)		// grenade-launcher active
		{
			strconcat(sizeof(tmp), tmp, base, "_g");
			if (isHUDAnimationExist(tmp)) { xr_strcpy(result, tmp); return; }
		}
		if (IsMisfire())
		{
			strconcat(sizeof(tmp), tmp, base, "_jammed_w_gl");
			if (isHUDAnimationExist(tmp)) { xr_strcpy(result, tmp); return; }
		}
		if (iAmmoElapsed == 0)
		{
			strconcat(sizeof(tmp), tmp, base, "_empty_w_gl");
			if (isHUDAnimationExist(tmp)) { xr_strcpy(result, tmp); return; }
		}
		strconcat(sizeof(tmp), tmp, base, "_w_gl");
		if (isHUDAnimationExist(tmp)) { xr_strcpy(result, tmp); return; }
	}
	inherited::SelectActionAnim(base, result);
}

void CWeaponMagazinedWGrenade::PlayAnimIdle()
{
	if (TryPlayAnimIdle())
		return;

	if (IsGrenadeLauncherAttached())
	{
		if (IsZoomed())
			PlayAnimAim();
		else
		{
			if (m_bGrenadeMode)
				PlayHUDMotion("anm_idle_g", TRUE, NULL, eIdle);
			else
				PlayHUDMotion("anm_idle_w_gl", TRUE, NULL, eIdle);
		}
	}
	else
		inherited::PlayAnimIdle();
}

void CWeaponMagazinedWGrenade::PlayAnimIdleMoving()
{
	if (IsGrenadeLauncherAttached())
	{
		if (m_bGrenadeMode)
			PlayHUDMotion(SelectMovingAnim("anm_idle_moving_g"), TRUE, NULL, eIdle);
		else
			PlayHUDMotion(SelectMovingAnim("anm_idle_moving_w_gl"), TRUE, NULL, eIdle);
	}
	else
		inherited::PlayAnimIdleMoving();
}

// GL suffix for the sprint idle base; the shared CHudItem::PlayAnimIdleSprint derives the
// enter/exit transitions (anm_idle_sprint_start/_end + _g/_w_gl) from this.
LPCSTR CWeaponMagazinedWGrenade::SprintLoopBase()
{
	if (IsGrenadeLauncherAttached())
		return m_bGrenadeMode ? "anm_idle_sprint_g" : "anm_idle_sprint_w_gl";
	return inherited::SprintLoopBase();
}

void CWeaponMagazinedWGrenade::SelectDryFireAnim(string_path& result)
{
	if (IsGrenadeLauncherAttached())
	{
		LPCSTR gl = m_bGrenadeMode ? "_g" : "_w_gl";
		bool empty = (iAmmoElapsed == 0);
		string_path tmp;
		if (IsZoomed())
		{
			strconcat(sizeof(tmp), tmp, empty ? "anm_fakeshoot_aim_empty" : "anm_fakeshoot_aim", gl);
			if (isHUDAnimationExist(tmp))	{ xr_strcpy(result, tmp); return; }
			strconcat(sizeof(tmp), tmp, "anm_fakeshoot_aim", gl);
			if (isHUDAnimationExist(tmp))	{ xr_strcpy(result, tmp); return; }
		}
		strconcat(sizeof(tmp), tmp, empty ? "anm_fakeshoot_empty" : "anm_fakeshoot", gl);
		if (isHUDAnimationExist(tmp))		{ xr_strcpy(result, tmp); return; }
	}
	inherited::SelectDryFireAnim(result);
}

void CWeaponMagazinedWGrenade::SelectAimIdleAnim(string_path& result)
{
	if (IsGrenadeLauncherAttached())
	{
		LPCSTR glsuf = m_bGrenadeMode ? "_g" : "_w_gl";
		LPCSTR dir   = AimWalkDirSuffix();
		const bool scoped = UseScopeAnims();

		// firemode-aware existence: PlayHUDMotion re-inserts the mask_firemode mark BEFORE the GL suffix, so the
		// scope aim-walk (authored only as ..._auto_w_gl) must be tested with that mark, not the bare name.
		auto exists = [&](LPCSTR nm)->bool {
			if (isHUDAnimationExist(nm))	return true;
			string_path marked;	MakeFireModeName(nm, marked);
			return (0 != xr_strcmp(marked, nm)) && isHUDAnimationExist(marked);
		};

		string_path cand;
		// 1) scope + GL aim-walk (GS order: anm_idle_aim_scope[_moving_<dir>]<glsuf>)
		if (scoped && dir[0])
		{
			xr_sprintf(cand, "anm_idle_aim_scope%s%s", dir, glsuf);
			if (exists(cand))	{ xr_strcpy(result, cand); return; }
			xr_sprintf(cand, "anm_idle_aim_scope_moving%s", glsuf);	// forward implicit
			if (exists(cand))	{ xr_strcpy(result, cand); return; }
		}
		// 2) non-scope GL aim-walk (animated fallback -- e.g. single fire mode, no scope walk authored)
		if (dir[0])
		{
			xr_sprintf(cand, "anm_idle_aim%s%s", dir, glsuf);
			if (isHUDAnimationExist(cand))	{ xr_strcpy(result, cand); return; }
			xr_sprintf(cand, "anm_idle_aim_moving_forward%s", glsuf);
			if (isHUDAnimationExist(cand))	{ xr_strcpy(result, cand); return; }
		}
		// 3) static scope + GL aim, else static GL aim
		if (scoped)
		{
			xr_sprintf(cand, "anm_idle_aim_scope%s", glsuf);
			if (exists(cand))	{ xr_strcpy(result, cand); return; }
		}
		xr_strcpy(result, m_bGrenadeMode ? "anm_idle_aim_g" : "anm_idle_aim_w_gl");
		return;
	}
	inherited::SelectAimIdleAnim(result);
}

void CWeaponMagazinedWGrenade::SelectAimTransitionAnim(bool bAimIn, string_path& result)
{
	LPCSTR base = bAimIn ? "anm_idle_aim_start" : "anm_idle_aim_end";
	if (IsGrenadeLauncherAttached())
	{
		string_path tmp;
		strconcat(sizeof(tmp), tmp, base, m_bGrenadeMode ? "_g" : "_w_gl");
		if (isHUDAnimationExist(tmp)) { xr_strcpy(result, tmp); return; }
	}
	xr_strcpy(result, isHUDAnimationExist(base) ? base : "");
}

void CWeaponMagazinedWGrenade::SelectShootAnim(string_path& result)
{
	if (m_bGrenadeMode)
	{
		if (IsZoomed() && isHUDAnimationExist("anm_shoot_aim_g"))
			{ xr_strcpy(result, "anm_shoot_aim_g"); return; }
		xr_strcpy(result, "anm_shoot_g");
		return;
	}
	if (IsGrenadeLauncherAttached())
	{
		// last chambered round -> bolt-back variant, same gating as the base class (lr300 has
		// lr300_gloff_shoot_last / _aim_shoot_last; weapons without them are unaffected)
		bool last = (iAmmoElapsed <= 1);
		if (IsZoomed() && isHUDAnimationExist("anm_shoot_aim_w_gl"))
		{
			// Firing through a use_scope_anims optic while the GL is mounted. GS keeps "_scope" right
			// after "_aim" -- before "_last" and before the trailing "_w_gl" -- so the names are
			// anm_shoot_aim_scope_last_w_gl / anm_shoot_aim_scope_w_gl (see GS aks74/an94 aliases).
			// Without this the GL branch fell straight through to the non-scope ADS shot, so a scoped
			// weapon lost its scope shooting animation as soon as a grenade launcher was attached.
			if (UseScopeAnims())
			{
				if (last && isHUDAnimationExist("anm_shoot_aim_scope_last_w_gl"))
					{ xr_strcpy(result, "anm_shoot_aim_scope_last_w_gl"); return; }
				if (isHUDAnimationExist("anm_shoot_aim_scope_w_gl"))
					{ xr_strcpy(result, "anm_shoot_aim_scope_w_gl"); return; }
			}
			if (last && isHUDAnimationExist("anm_shoot_aim_last_w_gl"))
				{ xr_strcpy(result, "anm_shoot_aim_last_w_gl"); return; }
			xr_strcpy(result, "anm_shoot_aim_w_gl");
			return;
		}
		if (last && isHUDAnimationExist("anm_shoot_last_w_gl"))
			{ xr_strcpy(result, "anm_shoot_last_w_gl"); return; }
		xr_strcpy(result, "anm_shoot_w_gl");
		return;
	}
	inherited::SelectShootAnim(result);
}

void CWeaponMagazinedWGrenade::PlayAnimShoot()
{
	string_path anim;
	SelectShootAnim(anim);
	PlayHUDMotion(anim, FALSE, this, eFire);
}

void CWeaponMagazinedWGrenade::PlayAnimFireModeSwitch()
{
	LPCSTR base = m_sFireModeAnim.size()
		? m_sFireModeAnim.c_str()
		: (IsAutoFireMode() ? "anm_changefiremode_from_1_to_a" : "anm_changefiremode_from_a_to_1");	// GS alias names
	if (IsGrenadeLauncherAttached())
	{
		string_path a;
		strconcat(sizeof(a), a, base, m_bGrenadeMode ? "_g" : "_w_gl");
		if (isHUDAnimationExist(a)) { PlayHUDMotion(a, TRUE, this, eFireModeSwitch); return; }
	}
	inherited::PlayAnimFireModeSwitch();
}

void CWeaponMagazinedWGrenade::PlayAnimModeSwitch()
{
	// capture the raise/lower window so the laser dot can fade out/in with GS timing (see UpdateLaserDot)
	u32 t = m_bGrenadeMode
		? PlayHUDMotion("anm_switch_g", TRUE, this, eSwitch)
		: PlayHUDMotion("anm_switch",   TRUE, this, eSwitch);
	m_dwGLSwitchStartTm = Device.dwTimeGlobal;
	m_dwGLSwitchEndTm   = Device.dwTimeGlobal + t;
}

void CWeaponMagazinedWGrenade::PlayAnimBore()
{
	if(IsGrenadeLauncherAttached())
	{
		if(m_bGrenadeMode)
			PlayHUDMotion	("anm_bore_g", TRUE, this, GetState());
		else
			PlayHUDMotion	("anm_bore_w_gl", TRUE, this, GetState());
	}else
		inherited::PlayAnimBore();
}

void CWeaponMagazinedWGrenade::UpdateSounds	()
{
	inherited::UpdateSounds			();
	Fvector P						= get_LastFP();
	m_sounds.SetPosition("sndShotG", P);
	m_sounds.SetPosition("sndReloadG", P);
	if (m_sounds.FindSoundItem("sndChangeGrenade", false))
		m_sounds.SetPosition("sndChangeGrenade", P);
	m_sounds.SetPosition("sndSwitch", P);
}

void CWeaponMagazinedWGrenade::UpdateGrenadeVisibility(bool visibility)
{
	if(!GetHUDmode())							return;
	HudItemData()->set_bone_visible				("grenade", visibility, TRUE);
}

void CWeaponMagazinedWGrenade::save(NET_Packet &output_packet)
{
	inherited::save								(output_packet);
	save_data									(m_bGrenadeMode, output_packet);
	save_data									(m_magazine2.size(), output_packet);

}

void CWeaponMagazinedWGrenade::load(IReader &input_packet)
{
	inherited::load				(input_packet);
	bool b;
	load_data					(b, input_packet);
	if(b!=m_bGrenadeMode)		
		PerformSwitchGL();

	u32 sz;
	load_data					(sz, input_packet);

	CCartridge					l_cartridge; 
	l_cartridge.Load			(*m_ammoTypes2[m_ammoType2], u8(m_ammoType2));

	while (sz > m_magazine2.size())
		m_magazine2.push_back(l_cartridge);
}

void CWeaponMagazinedWGrenade::net_Export	(NET_Packet& P)
{
	P.w_u8						(m_bGrenadeMode ? 1 : 0);

	inherited::net_Export		(P);
}

void CWeaponMagazinedWGrenade::net_Import	(NET_Packet& P)
{
	bool NewMode				= FALSE;
	NewMode						= !!P.r_u8();	
	if (NewMode != m_bGrenadeMode)
		PerformSwitchGL();

	inherited::net_Import		(P);
}

bool CWeaponMagazinedWGrenade::IsNecessaryItem	    (const shared_str& item_sect)
{
	return (	std::find(m_ammoTypes.begin(), m_ammoTypes.end(), item_sect) != m_ammoTypes.end() ||
				std::find(m_ammoTypes2.begin(), m_ammoTypes2.end(), item_sect) != m_ammoTypes2.end() 
			);
}

u8 CWeaponMagazinedWGrenade::GetCurrentHudOffsetIdx()
{
	bool b_aiming		= 	((IsZoomed() && m_zoom_params.m_fZoomRotationFactor<=1.f) ||
							(!IsZoomed() && m_zoom_params.m_fZoomRotationFactor>0.f));
	
	if(!b_aiming)
		return		0;
	else
	if(m_bGrenadeMode)
		return		2;
	else
		return		1;
}

bool CWeaponMagazinedWGrenade::install_upgrade_ammo_class	( LPCSTR section, bool test )
{
	LPCSTR str;

	bool result = process_if_exists( section, "ammo_mag_size", &CInifile::r_s32, iMagazineSize2, test );
	iMagazineSize		= m_bGrenadeMode?1:iMagazineSize2;

	//	ammo_class = ammo_5.45x39_fmj, ammo_5.45x39_ap  // name of the ltx-section of used ammo
	bool result2 = process_if_exists_set( section, "ammo_class", &CInifile::r_string, str, test );
	if ( result2 && !test ) 
	{
		xr_vector<shared_str>& ammo_types	= m_bGrenadeMode ? m_ammoTypes2 : m_ammoTypes;
		ammo_types.clear					(); 
		for ( int i = 0, count = _GetItemCount( str ); i < count; ++i )	
		{
			string128						ammo_item;
			_GetItem						( str, i, ammo_item );
			ammo_types.push_back			( ammo_item );
		}

		shared_str& ammo_name				= m_bGrenadeMode ? m_ammoName2 : m_ammoName;
		ammo_name							= pSettings->r_string( *ammo_types[0], "inv_name_short" );		
		m_ammoType  = 0;
		m_ammoType2 = 0;
	}
	result |= result2;

	return result2;
}

bool CWeaponMagazinedWGrenade::install_upgrade_impl( LPCSTR section, bool test )
{
	LPCSTR str;
	bool result = inherited::install_upgrade_impl( section, test );
	
	//	grenade_class = ammo_vog-25, ammo_vog-25p          // name of the ltx-section of used grenades
	bool result2 = process_if_exists_set( section, "grenade_class", &CInifile::r_string, str, test );
	if ( result2 && !test )
	{
		xr_vector<shared_str>& ammo_types	= !m_bGrenadeMode ? m_ammoTypes2 : m_ammoTypes;
		ammo_types.clear					(); 
		for ( int i = 0, count = _GetItemCount( str ); i < count; ++i )	
		{
			string128						ammo_item;
			_GetItem						( str, i, ammo_item );
			ammo_types.push_back			( ammo_item );
		}

		shared_str& ammo_name				= !m_bGrenadeMode ? m_ammoName2 : m_ammoName;
		ammo_name							= pSettings->r_string( *ammo_types[0], "inv_name_short" );
		m_ammoType  = 0;
		m_ammoType2 = 0;
	}
	result |= result2;

	result |= process_if_exists( section, "launch_speed", &CInifile::r_float, m_fLaunchSpeed, test );

	result2 = process_if_exists_set( section, "snd_shoot_grenade", &CInifile::r_string, str, test );
	if ( result2 && !test ) { m_sounds.LoadSound( section, "snd_shoot_grenade", "sndShotG", false, m_eSoundShot );	}
	result |= result2;

	result2 = process_if_exists_set( section, "snd_reload_grenade", &CInifile::r_string, str, test );
	if ( result2 && !test ) { m_sounds.LoadSound( section, "snd_reload_grenade", "sndReloadG", true, m_eSoundReload );	}
	result |= result2;

	result2 = process_if_exists_set( section, "snd_switch", &CInifile::r_string, str, test );
	if ( result2 && !test ) { m_sounds.LoadSound( section, "snd_switch", "sndSwitch", true, m_eSoundReload );	}
	result |= result2;

	return result;
}

void CWeaponMagazinedWGrenade::net_Spawn_install_upgrades	( Upgrades_type saved_upgrades )
{
	// do not delete this
	// this is intended behaviour
}
