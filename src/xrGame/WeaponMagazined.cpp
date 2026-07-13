#include "pch_script.h"
#include "hudmanager.h"
#include "WeaponMagazined.h"
#include "entity.h"
#include "actor.h"
#include "ParticlesObject.h"
#include "scope.h"
#include "silencer.h"
#include "GrenadeLauncher.h"
#include "inventory.h"
#include "xrserver_objects_alife_items.h"
#include "ActorEffector.h"
#include "EffectorZoomInertion.h"
#include "xr_level_controller.h"
#include "level.h"
#include "object_broker.h"
#include "string_table.h"
#include "MPPlayersBag.h"
#include "ui/UIXmlInit.h"
#include "ui/UIStatic.h"
#include "player_hud.h"
#include "CustomDetector.h"
#include "Actor_Flags.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "../Include/xrRender/animation_blend.h"
ENGINE_API	bool	g_dedicated_server;

CUIXml*				pWpnScopeXml = NULL;

void createWpnScopeXML()
{
	if(!pWpnScopeXml)
	{
		pWpnScopeXml			= xr_new<CUIXml>();
		pWpnScopeXml->Load		(CONFIG_PATH, UI_PATH, "scopes.xml");
	}
}

CWeaponMagazined::CWeaponMagazined(ESoundTypes eSoundType) : CWeapon()
{
	m_eSoundShow				= ESoundTypes(SOUND_TYPE_ITEM_TAKING | eSoundType);
	m_eSoundHide				= ESoundTypes(SOUND_TYPE_ITEM_HIDING | eSoundType);
	m_eSoundShot				= ESoundTypes(SOUND_TYPE_WEAPON_SHOOTING | eSoundType);
	m_eSoundEmptyClick			= ESoundTypes(SOUND_TYPE_WEAPON_EMPTY_CLICKING | eSoundType);
	m_eSoundReload				= ESoundTypes(SOUND_TYPE_WEAPON_RECHARGING | eSoundType);
	
	m_sSndShotCurrent			= NULL;
	m_sSilencerFlameParticles	= m_sSilencerSmokeParticles = NULL;
	m_dwAimTransitionEndTm		= 0;
	m_bDryFirePending			= false;
	m_bDryFirePlaying			= false;
	m_bAimInPending				= false;
	m_bAimOutPending			= false;
	m_bTriggerHeld				= false;

	m_bFireSingleShot			= false;
	m_iShotNum					= 0;
	m_iQueueSize				= WEAPON_ININITE_QUEUE;
	m_bLockType					= false;
	bMisfireReload				= false;

	m_fire_mode_bone_id			= BI_NONE;
	m_fire_selector_hold		= false;
	m_fire_selector_capturing	= false;
	m_fire_selector_valid		= false;
	m_fire_selector_cb			= false;
	m_fire_selector_xform.identity();
}

CWeaponMagazined::~CWeaponMagazined()
{
	// sounds
}


void CWeaponMagazined::net_Destroy()
{
	DetachFireSelectorBone	();
	inherited::net_Destroy();
}

bool CWeaponMagazined::WeaponSoundExist(LPCSTR section, LPCSTR sound_name)
{
	LPCSTR str;
	bool sec_exist = process_if_exists_set(section, sound_name, &CInifile::r_string, str, true);
	if (sec_exist)
		return true;
	else
	{
#ifdef DEBUG
		Msg("~ [WARNING] ------ Sound [%s] does not exist in [%s]", sound_name, section);
#endif
		return false;
	}
}

void CWeaponMagazined::Load	(LPCSTR section)
{
	inherited::Load		(section);

	// fire-selector bone to hold across anims (HUD section, optional; "" disables the feature)
	m_fire_mode_bone	= READ_IF_EXISTS(pSettings, r_string, HudSection(), "fire_mode_bone", "");

	// Sounds
	m_sounds.LoadSound(section,"snd_draw", "sndShow"		, false, m_eSoundShow		);
	m_sounds.LoadSound(section,"snd_holster", "sndHide"		, false, m_eSoundHide		);
	m_sounds.LoadSound(section,"snd_shoot", "sndShot"		, false, m_eSoundShot		);
	m_sounds.LoadSound(section,"snd_empty", "sndEmptyClick"	, false, m_eSoundEmptyClick	);
	m_sounds.LoadSound(section,"snd_reload", "sndReload"	, true, m_eSoundReload		);

	if (WeaponSoundExist(section, "snd_reload_empty") && isHUDAnimationExist("anm_reload_empty"))
		m_sounds.LoadSound(section,"snd_reload_empty", "sndReloadEmpty"	, true, m_eSoundReload);
	
	if (WeaponSoundExist(section, "snd_reload_misfire") && isHUDAnimationExist("anm_reload_misfire"))
		m_sounds.LoadSound(section, "snd_reload_misfire", "sndReloadMis", true, m_eSoundReload);

	if (WeaponSoundExist(section, "snd_changefiremode"))	// fire-selector switch sound (optional)
		m_sounds.LoadSound(section, "snd_changefiremode", "sndFireModes", false, m_eSoundEmptyClick);

	m_sSndShotCurrent = "sndShot";
		
	//звуки и партиклы глушителя, еслит такой есть
	if ( m_eSilencerStatus == ALife::eAddonAttachable || m_eSilencerStatus == ALife::eAddonPermanent )
	{
		if(pSettings->line_exist(section, "silencer_flame_particles"))
			m_sSilencerFlameParticles = pSettings->r_string(section, "silencer_flame_particles");
		if(pSettings->line_exist(section, "silencer_smoke_particles"))
			m_sSilencerSmokeParticles = pSettings->r_string(section, "silencer_smoke_particles");
		
		m_sounds.LoadSound(section,"snd_silncer_shot", "sndSilencerShot", false, m_eSoundShot);
	}

	if (pSettings->line_exist(section, "dispersion_start"))
		m_iShootEffectorStart = pSettings->r_u8(section, "dispersion_start");
	else
		m_iShootEffectorStart = 0;

	if (pSettings->line_exist(section, "fire_modes"))
	{
		m_bHasDifferentFireModes = true;
		shared_str FireModesList = pSettings->r_string(section, "fire_modes");
		int ModesCount = _GetItemCount(FireModesList.c_str());
		m_aFireModes.clear();
		
		for (int i=0; i<ModesCount; i++)
		{
			string16 sItem;
			_GetItem(FireModesList.c_str(), i, sItem);
			m_aFireModes.push_back	((s8)atoi(sItem));
		}
		
		m_iCurFireMode = ModesCount - 1;
		m_iPrefferedFireMode = READ_IF_EXISTS(pSettings, r_s16,section,"preffered_fire_mode",-1);
	}
	else
	{
		m_bHasDifferentFireModes = false;
	}
	LoadSilencerKoeffs();
}

void CWeaponMagazined::FireStart		()
{
	m_bTriggerHeld = true;	// trigger pressed (held until FireEnd); used to resume fire after a transition

	// let the jam (misfire) dry-fire gesture finish before another trigger pull; the empty
	// dry-fire stays spammable (each click re-triggers it)
	if ((m_bDryFirePending || m_bDryFirePlaying) && IsMisfire())	return;

	// no firing while an aim in/out transition is playing; a held trigger resumes firing when it
	// ends (UpdateCL handoff re-checks the trigger state).
	if (m_dwAimTransitionEndTm && Device.dwTimeGlobal < m_dwAimTransitionEndTm)
		return;

	if(!IsMisfire())
	{
		if(IsValid()) 
		{
			if(!IsWorking() || AllowFireWhileWorking())
			{
				if(GetState()==eReload) return;
				if(GetState()==eShowing) return;
				if(GetState()==eHiding) return;
				if(GetState()==eMisfire) return;

				inherited::FireStart();
				
				if (iAmmoElapsed == 0)
					switch2_Empty();
				else
				{
					R_ASSERT(H_Parent());
					SwitchState(eFire);
				}
			}
		}
		else 
		{
			if (GetState() == eIdle)
				switch2_Empty();
		}
	}else
	{//misfire
		if(smart_cast<CActor*>(this->H_Parent()) && (Level().CurrentViewEntity()==H_Parent()) )
			HUD().GetUI()->AddInfoMessage("gun_jammed");

		if (GetState()==eIdle)
		{
			m_bDryFirePending = true;
			SwitchState(eIdle);		// route the dry-fire through the deferred switch2_Idle
		}
		OnEmptyClick();
	}
}

void CWeaponMagazined::FireEnd()
{
	m_bTriggerHeld = false;	// trigger released
	inherited::FireEnd();

	if (psActorFlags.test(AF_AUTORELOAD))
	{
		CActor	*actor = smart_cast<CActor*>(H_Parent());
		if(!iAmmoElapsed && actor && GetState()!=eReload) 
			Reload();
	}
}

void CWeaponMagazined::Reload()
{
	// the jam (misfire) inspect gesture must play out fully before the jam can be cleared:
	// block reload while it's on screen. The empty-mag dry-fire (not a misfire) stays reloadable.
	if (m_bDryFirePlaying && IsMisfire())
		return;

	auto i1 = g_player_hud->attached_item(1);
	if (i1 && HudItemData())
	{
		auto det = smart_cast<CCustomDetector*>(i1->m_parent_hud_item);
		if (det && det->GetState() != CCustomDetector::eIdle)
			return;
	}

	inherited::Reload();
	TryReload();
}

#include "game_object_space.h"
#include "script_callback_ex.h"
#include "script_game_object.h"

bool CWeaponMagazined::TryReload() 
{
	if(m_pInventory) 
	{
		if(IsGameTypeSingle() && ParentIsActor())
		{
			int	AC					= GetSuitableAmmoTotal();
			Actor()->callback(GameObject::eWeaponNoAmmoAvailable)(lua_game_object(), AC);
		}
		if (m_ammoType < m_ammoTypes.size())
			m_pAmmo = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(*m_ammoTypes[m_ammoType] ));
		else
			m_pAmmo = NULL;

		
		if(IsMisfire() && iAmmoElapsed)
		{
			SetPending			(TRUE);
			SwitchState			(eReload); 
			return				true;
		}

		if(m_pAmmo || unlimited_ammo())  
		{
			SetPending			(TRUE);
			SwitchState			(eReload); 
			return				true;
		} 
		else for(u32 i = 0; i < m_ammoTypes.size(); ++i) 
		{
			m_pAmmo = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny( *m_ammoTypes[i] ));
			if(m_pAmmo) 
			{ 
				m_ammoType			= i; 
				SetPending			(TRUE);
				SwitchState			(eReload);
				return				true;
			}
		}

	}
	
	if(GetState()!=eIdle)
		SwitchState(eIdle);

	return false;
}

bool CWeaponMagazined::IsAmmoAvailable()
{
	if (smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(*m_ammoTypes[m_ammoType])))
		return	(true);
	else
		for(u32 i = 0; i < m_ammoTypes.size(); ++i)
			if (smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(*m_ammoTypes[i])))
				return	(true);
	return		(false);
}

void CWeaponMagazined::UnloadMagazine(bool spawn_ammo)
{
	xr_map<LPCSTR, u16> l_ammo;
	
	while(!m_magazine.empty()) 
	{
		CCartridge &l_cartridge = m_magazine.back();
		xr_map<LPCSTR, u16>::iterator l_it;
		for(l_it = l_ammo.begin(); l_ammo.end() != l_it; ++l_it) 
		{
            if(!xr_strcmp(*l_cartridge.m_ammoSect, l_it->first)) 
            { 
				 ++(l_it->second); 
				 break; 
			}
		}

		if(l_it == l_ammo.end()) l_ammo[*l_cartridge.m_ammoSect] = 1;
		m_magazine.pop_back(); 
		--iAmmoElapsed;
	}

	VERIFY((u32)iAmmoElapsed == m_magazine.size());
	
	if (!spawn_ammo)
		return;

	xr_map<LPCSTR, u16>::iterator l_it;
	for(l_it = l_ammo.begin(); l_ammo.end() != l_it; ++l_it) 
	{
		CWeaponAmmo *l_pA = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(l_it->first));
		if(l_pA) 
		{
			u16 l_free = l_pA->m_boxSize - l_pA->m_boxCurr;
			l_pA->m_boxCurr = l_pA->m_boxCurr + (l_free < l_it->second ? l_free : l_it->second);
			l_it->second = l_it->second - (l_free < l_it->second ? l_free : l_it->second);
		}
		if(l_it->second && !unlimited_ammo()) SpawnAmmo(l_it->second, l_it->first);
	}
}

void CWeaponMagazined::ReloadMagazine() 
{
	m_dwAmmoCurrentCalcFrame = 0;	

	//устранить осечку при перезарядке
	if(IsMisfire())	bMisfire = false;
	
	if (!m_bLockType) {
		m_ammoName	= NULL;
		m_pAmmo		= NULL;
	}
	
	if (!m_pInventory) return;

	if(m_set_next_ammoType_on_reload != u32(-1)){		
		m_ammoType						= m_set_next_ammoType_on_reload;
		m_set_next_ammoType_on_reload	= u32(-1);
	}
	
	if(!unlimited_ammo()) 
	{
		//попытаться найти в инвентаре патроны текущего типа 
		m_pAmmo = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(*m_ammoTypes[m_ammoType]));
		
		if(!m_pAmmo && !m_bLockType) 
		{
			for(u32 i = 0; i < m_ammoTypes.size(); ++i) 
			{
				//проверить патроны всех подходящих типов
				m_pAmmo = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(*m_ammoTypes[i]));
				if(m_pAmmo) 
				{ 
					m_ammoType = i; 
					break; 
				}
			}
		}
	}
	else
		m_ammoType = m_ammoType;


	//нет патронов для перезарядки
	if(!m_pAmmo && !unlimited_ammo() ) return;

	//разрядить магазин, если загружаем патронами другого типа
	if(!m_bLockType && !m_magazine.empty() && 
		(!m_pAmmo || xr_strcmp(m_pAmmo->cNameSect(), 
					 *m_magazine.back().m_ammoSect)))
		UnloadMagazine();

	VERIFY((u32)iAmmoElapsed == m_magazine.size());

	if (m_DefaultCartridge.m_LocalAmmoType != m_ammoType)
		m_DefaultCartridge.Load(*m_ammoTypes[m_ammoType], u8(m_ammoType));
	CCartridge l_cartridge = m_DefaultCartridge;
	while(iAmmoElapsed < iMagazineSize)
	{
		if (!unlimited_ammo())
		{
			if (!m_pAmmo->Get(l_cartridge)) break;
		}
		++iAmmoElapsed;
		l_cartridge.m_LocalAmmoType = u8(m_ammoType);
		m_magazine.push_back(l_cartridge);
	}
	m_ammoName = (m_pAmmo) ? m_pAmmo->m_nameShort : NULL;

	VERIFY((u32)iAmmoElapsed == m_magazine.size());

	//выкинуть коробку патронов, если она пустая
	if(m_pAmmo && !m_pAmmo->m_boxCurr && OnServer()) 
		m_pAmmo->SetDropManual(TRUE);

	if(iMagazineSize > iAmmoElapsed) 
	{ 
		m_bLockType = true; 
		ReloadMagazine(); 
		m_bLockType = false; 
	}

	VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeaponMagazined::OnStateSwitch	(u32 S)
{
	inherited::OnStateSwitch(S);
	switch (S)
	{
	case eIdle:
		switch2_Idle	();
		break;
	case eFire:
		switch2_Fire	();
		break;
	case eMisfire:
		if(smart_cast<CActor*>(this->H_Parent()) && (Level().CurrentViewEntity()==H_Parent()) )
			HUD().GetUI()->AddInfoMessage("gun_jammed");
		break;
	case eReload:
		switch2_Reload	();
		break;
	case eActionAnim:
		switch2_ActionAnim	();
		break;
	case eFireModeSwitch:
		switch2_FireModeSwitch	();
		break;
	case eShowing:
		switch2_Showing	();
		break;
	case eHiding:
		switch2_Hiding	();
		break;
	case eHidden:
		switch2_Hidden	();
		break;
	}
}


void CWeaponMagazined::UpdateCL			()
{
	inherited::UpdateCL	();
	float dt = Device.fTimeDelta;

	// aim in/out transition handoff fallback: when its OnAnimationEnd didn't fire on time
	// (unreliable while moving), force the switch to the aim idle / moving idle at the
	// transition's wall-clock deadline so the one-shot transition can't stick/loop.
	if(m_dwAimTransitionEndTm && Device.dwTimeGlobal >= m_dwAimTransitionEndTm)
	{
		m_bIdleTransitionLock	= false;
		m_dwAimTransitionEndTm	= 0;
		if(GetState()==eIdle)
		{
			PlayAnimIdle();
			// trigger still held through the transition -> resume ONLY genuine continuous auto
			// fire. semi-auto / burst / pistols / shotguns / SVD need a fresh trigger press.
			// CanAutoResumeFire() (NOT IsAutoFireMode(), which is wrongly TRUE for the semi-auto
			// CWeaponCustomPistol family) gates this, so a held pistol never self-fires.
			if(m_bTriggerHeld && CanAutoResumeFire())
				FireStart();
		}
	}

	// selector callback + first-pickup auto pose are set up in on_a_hud_attach (HudItemData() is null here)

	//когда происходит апдейт состояния оружия
	//ничего другого не делать
	if(GetNextState() == GetState())
	{
		switch (GetState())
		{
		case eShowing:
		case eHiding:
		case eReload:
		case eActionAnim:
		case eFireModeSwitch:
		case eIdle:
			{
				fShotTimeCounter	-=	dt;
				clamp				(fShotTimeCounter, 0.0f, flt_max);
			}break;
		case eFire:			
			{
				state_Fire		(dt);
			}break;
		case eMisfire:		state_Misfire	(dt);	break;
		case eHidden:		break;
		}
	}

	UpdateSounds		();
}

void CWeaponMagazined::UpdateSounds	()
{
	if (Device.dwFrame == dwUpdateSounds_Frame)  
		return;
	
	dwUpdateSounds_Frame = Device.dwFrame;

	Fvector P = get_LastFP();
	m_sounds.SetPosition("sndShow", P);
	m_sounds.SetPosition("sndHide", P);
	m_sounds.SetPosition("sndReload", P);
	if (m_sounds.FindSoundItem("sndReloadEmpty", false))
		m_sounds.SetPosition("sndReloadEmpty", P);
	if (m_sounds.FindSoundItem("sndReloadMis", false))
		m_sounds.SetPosition("sndReloadMis", P);
}

void CWeaponMagazined::state_Fire(float dt)
{
	if(iAmmoElapsed > 0)
	{
		VERIFY(fOneShotTime>0.f);

		Fvector					p1, d; 
		p1.set(get_LastFP());
		d.set(get_LastFD());

		if (!H_Parent()) return;
		if (smart_cast<CMPPlayersBag*>(H_Parent()) != NULL)
		{
			Msg("! WARNING: state_Fire of object [%d][%s] while parent is CMPPlayerBag...", ID(), cNameSect().c_str());
			return;
		}

		CInventoryOwner* io		= smart_cast<CInventoryOwner*>(H_Parent());
		if(NULL == io->inventory().ActiveItem())
		{
				Log("current_state", GetState() );
				Log("next_state", GetNextState());
				Log("item_sect", cNameSect().c_str());
				Log("H_Parent", H_Parent()->cNameSect().c_str());
		}

		CEntity* E = smart_cast<CEntity*>(H_Parent());
		E->g_fireParams	(this, p1,d);

		if( !E->g_stateFire() )
			StopShooting();

		if (m_iShotNum == 0)
		{
			m_vStartPos = p1;
			m_vStartDir = d;
		};
		
		VERIFY(!m_magazine.empty());

		while (	!m_magazine.empty() &&
				fShotTimeCounter<0 &&
				(IsWorking() || m_bFireSingleShot) &&
				(m_iQueueSize<0 || m_iShotNum<m_iQueueSize)
			   )
		{
			m_bFireSingleShot		= false;

			fShotTimeCounter		+=	fOneShotTime;

			++m_iShotNum;

			OnShot					();

			if (m_iShotNum>m_iShootEffectorStart)
				FireTrace		(p1,d);
			else
				FireTrace		(m_vStartPos, m_vStartDir);

			// jam (misfire) can only occur AFTER a shot has actually been fired -
			// the fired round leaves the barrel, then the action jams (stovepipe /
			// failure-to-eject). Rolled here (post-shot) instead of before the shot so
			// a fresh trigger pull never jams in place of firing. Skip the roll when the
			// shot emptied the magazine (nothing left to chamber -> just empty, not jammed).
			if( !m_magazine.empty() && CheckForMisfire() )
			{
				StopShooting();
				return;
			}
		}
	
		if(m_iShotNum == m_iQueueSize)
			m_bStopedAfterQueueFired = true;

		UpdateSounds			();
	}

	if(fShotTimeCounter<0)
	{
/*
		if(bDebug && H_Parent() && (H_Parent()->ID() != Actor()->ID()))
		{
			Msg("stop shooting w=[%s] magsize=[%d] sshot=[%s] qsize=[%d] shotnum=[%d]",
					IsWorking()?"true":"false", 
					m_magazine.size(),
					m_bFireSingleShot?"true":"false",
					m_iQueueSize,
					m_iShotNum);
		}
*/
		if(iAmmoElapsed == 0)
			OnMagazineEmpty();

		StopShooting();
	}
	else
	{
		fShotTimeCounter			-=	dt;
	}
}

void CWeaponMagazined::state_Misfire	(float dt)
{
	OnEmptyClick			();
	SwitchState				(eIdle);
	
	bMisfire				= true;

	UpdateSounds			();
}

void CWeaponMagazined::SetDefaults	()
{
	CWeapon::SetDefaults		();
}


void CWeaponMagazined::OnShot()
{
	// Sound
	PlaySound					(m_sSndShotCurrent.c_str(), get_LastFP());

	// Camera
	AddShotEffector				();

	// Animation
	PlayAnimShoot				();
	
	// Shell Drop
	Fvector vel; 
	PHGetLinearVell				(vel);
	OnShellDrop					(get_LastSP(), vel);
	
	// Огонь из ствола
	StartFlameParticles			();

	//дым из ствола
	ForceUpdateFireParticles	();
	StartSmokeParticles			(get_LastFP(), vel);
}


void CWeaponMagazined::OnEmptyClick	()
{
	PlaySound	("sndEmptyClick",get_LastFP());
}

// dry-fire gesture on an empty/jammed trigger pull. aim: anm_dry_aim[_empty]; hip:
// anm_dry_empty (empty) / anm_dry (jammed). GL subclass overrides for _w_gl/_g.
void CWeaponMagazined::SelectDryFireAnim(string_path& result)
{
	bool empty = (iAmmoElapsed == 0);
	if (IsZoomed())
	{
		if (empty && isHUDAnimationExist("anm_dry_aim_empty"))	{ xr_strcpy(result, "anm_dry_aim_empty"); return; }
		if (isHUDAnimationExist("anm_dry_aim"))					{ xr_strcpy(result, "anm_dry_aim"); return; }
	}
	if (empty && isHUDAnimationExist("anm_dry_empty"))			{ xr_strcpy(result, "anm_dry_empty"); return; }
	xr_strcpy(result, isHUDAnimationExist("anm_dry") ? "anm_dry" : "");
}

// called from switch2_Idle (state settled at eIdle). Plays the dry-fire one-shot; when it
// ends OnAnimationEnd(eIdle) -> switch2_Idle -> the plain idle/aim. Falls back to idle when
// the weapon has no dry anim, so the HUD always shows something.
void CWeaponMagazined::PlayAnimDryFire()
{
	string_path anim;
	SelectDryFireAnim(anim);
	if (anim[0])
	{
		m_bDryFirePlaying = true;	// block re-triggering until OnAnimationEnd clears it
		// the JAM inspect owns the hands: mark pending so nothing (firemode/aim/reload) can cut it
		// mid-play — a rapid fire+aim spam otherwise chained interruptions and left the dry-fire flag
		// stuck, deadlocking the weapon. switch2_Idle clears pending when it ends. (Empty dry-fire
		// stays spammable -> no pending.)
		if (IsMisfire())
			SetPending(TRUE);
		PlayHUDMotion(anim, TRUE, this, eIdle);
	}
	else
		PlayAnimIdle();
}

void CWeaponMagazined::OnAnimationEnd(u32 state)
{
	m_bDryFirePlaying = false;	// the dry-fire (or whatever replaced it) has ended -> allow fire again
	switch(state)
	{
		case eReload:
		{
			if (!IsTriStateReload())
			{
				bReloadKeyPressed = false;
				bAmmotypeKeyPressed = false;
			}

			if (bMisfireReload)
			{
				bMisfire = false;
				bMisfireReload = false;
			}
			else
				ReloadMagazine();
			SwitchState(eIdle);
		}break;	// End of reload animation
		case eHiding:	SwitchState(eHidden);   break;	// End of Hide
		case eShowing:	SwitchState(eIdle);		break;	// End of Show
		case eActionAnim:	SwitchState(eIdle);	break;	// End of headlamp/NV gesture
		case eFireModeSwitch:	// End of fire-selector gesture: freeze the selector at its last
			m_fire_selector_capturing = false;
			m_fire_selector_hold = (GetCurrentFireMode() != 1);	// hold for any non-single mode
			m_fire_selector_valid = true;	// we just captured a real pose
			SwitchState(eIdle);
			break;
		case eIdle:		switch2_Idle();			break;  // Keep showing idle
	}
	inherited::OnAnimationEnd(state);
}

void CWeaponMagazined::switch2_Idle	()
{
	SetPending			(FALSE);
	// an aim transition is still playing; don't cut it with the idle — UpdateCL hands off
	// to the idle/aim at the transition's wall-clock deadline.
	if (m_dwAimTransitionEndTm && Device.dwTimeGlobal < m_dwAimTransitionEndTm)
		return;

	if (m_bAimOutPending)
	{
		// fire has ended: now actually leave the scope and play the aim-out (GetState()==eIdle
		// here, so OnZoomOut runs its real zoom-out + transition path).
		m_bAimOutPending = false;
		OnZoomOut();
		return;
	}
	if (m_bAimInPending)
	{
		// fire was stopped by aiming: play the aim-in transition now
		m_bAimInPending = false;
		if (!PlayAimTransition(true))
			PlayAnimIdle();
		return;
	}
	if (m_bDryFirePending)
	{
		m_bDryFirePending = false;
		PlayAnimDryFire	();		// one-shot dry-fire instead of the plain idle
		return;
	}
	PlayAnimIdle	();
}

#ifdef DEBUG
#include "ai\stalker\ai_stalker.h"
#include "object_handler_planner.h"
#endif
void CWeaponMagazined::switch2_Fire	()
{
	CInventoryOwner* io		= smart_cast<CInventoryOwner*>(H_Parent());
	CInventoryItem* ii		= smart_cast<CInventoryItem*>(this);
#ifdef DEBUG
	VERIFY2					(io,make_string("no inventory owner, item %s",*cName()));

	if (ii != io->inventory().ActiveItem())
		Msg					("! not an active item, item %s, owner %s, active item %s",*cName(),*H_Parent()->cName(),io->inventory().ActiveItem() ? *io->inventory().ActiveItem()->object().cName() : "no_active_item");

	if ( !(io && (ii == io->inventory().ActiveItem())) ) 
	{
		CAI_Stalker			*stalker = smart_cast<CAI_Stalker*>(H_Parent());
		if (stalker) {
			stalker->planner().show						();
			stalker->planner().show_current_world_state	();
			stalker->planner().show_target_world_state	();
		}
	}
#else
	if (!io)
		return;
#endif // DEBUG

//
//	VERIFY2(
//		io && (ii == io->inventory().ActiveItem()),
//		make_string(
//			"item[%s], parent[%s]",
//			*cName(),
//			H_Parent() ? *H_Parent()->cName() : "no_parent"
//		)
//	);

	m_bStopedAfterQueueFired = false;
	m_bFireSingleShot = true;
	m_iShotNum = 0;

    if((OnClient() || Level().IsDemoPlay())&& !IsWorking())
		FireStart();

}

void CWeaponMagazined::switch2_Empty()
{
	if (psActorFlags.test(AF_AUTORELOAD))
	{
		OnZoomOut();				// auto-reload will draw the mag out; leave aim
		if (!TryReload())
			OnEmptyClick();
		else
			inherited::FireEnd();
	}
	else
	{
		// keep the zoom so the aim dry-fire gesture can play. SwitchState is deferred, so
		// mark the dry-fire pending and let the queued switch2_Idle play it (it would be
		// overwritten if played here, and GetState() isn't eIdle yet).
		m_bDryFirePending = true;
		SwitchState(eIdle);
		OnEmptyClick();
	}
}

void CWeaponMagazined::PlayReloadSound()
{
	if (m_sounds.FindSoundItem("sndReloadMis", false) && isHUDAnimationExist("anm_reload_misfire") && IsMisfire() && bMisfireReload)
		PlaySound("sndReloadMis", get_LastFP());
	else if (m_sounds.FindSoundItem("sndReloadEmpty", false) && isHUDAnimationExist("anm_reload_empty") && iAmmoElapsed == 0)
		PlaySound("sndReloadEmpty", get_LastFP());
	else
		PlaySound("sndReload", get_LastFP());
}

void CWeaponMagazined::switch2_Reload()
{
	CWeapon::FireEnd	();

	PlayAnimReload		();
	PlayReloadSound		();
	SetPending			(TRUE);
}

// ---- headlamp / night-vision toggle gesture played on THIS weapon's HUD (weapon stays out) ----
void CWeaponMagazined::SelectActionAnim(LPCSTR base, string_path& result)
{
	// base = "anm_headlamp_on" / "anm_headlamp_off" / "anm_nv_on" / "anm_nv_off"
	string_path tmp;
	if (IsMisfire())
	{
		strconcat(sizeof(tmp), tmp, base, "_jammed");
		if (isHUDAnimationExist(tmp)) { xr_strcpy(result, tmp); return; }
	}
	if (iAmmoElapsed == 0)
	{
		strconcat(sizeof(tmp), tmp, base, "_empty");
		if (isHUDAnimationExist(tmp)) { xr_strcpy(result, tmp); return; }
	}
	xr_strcpy(result, isHUDAnimationExist(base) ? base : "");
}

bool CWeaponMagazined::PlayHudActionAnim(LPCSTR base)
{
	if (GetState() != eIdle || IsPending())	return false;	// don't interrupt reload/fire/switch
	string_path anim;
	SelectActionAnim	(base, anim);
	if (!anim[0])		return false;					// this weapon has no such gesture -> let caller fall back
	m_action_anim = anim;
	SwitchState			(eActionAnim);
	return true;
}

void CWeaponMagazined::switch2_ActionAnim()
{
	CWeapon::FireEnd	();
	if (m_action_anim.size())
		PlayHUDMotion	(m_action_anim, TRUE, this, GetState());
	SetPending			(TRUE);								// fire/reload locked until the gesture ends
}

// ---- fire-mode selector bone hold (light firemode) ----
void CWeaponMagazined::FireSelectorBoneCallback(CBoneInstance* B)
{
	CWeaponMagazined* w = static_cast<CWeaponMagazined*>(B->callback_param());
	if (w->m_fire_selector_capturing)
		w->m_fire_selector_xform = B->mTransform;			// CAPTURE during the gesture => its last frame
	else if (w->m_fire_selector_hold && w->m_fire_selector_valid)
		B->mTransform = w->m_fire_selector_xform;			// HOLD: freeze the selector at the captured (auto) pose
	// otherwise (single mode): let the base animation drive the selector
}

void CWeaponMagazined::UpdateFireSelectorBone()
{
	if (!m_fire_mode_bone.size())	return;			// feature disabled (no bone configured)
	if (m_fire_selector_cb)			return;			// already bound to the live model
	attachable_hud_item* itm = HudItemData();
	IKinematics* K = (itm && itm->m_model) ? itm->m_model : NULL;
	if (!K)			{ m_fire_selector_cb = false; return; }
	u16 bid = K->LL_BoneID(m_fire_mode_bone.c_str());
	if (bid == BI_NONE)				{ m_fire_mode_bone_id = BI_NONE; return; }	// model has no such bone (silent; retried)
	m_fire_mode_bone_id = bid;
	K->LL_GetBoneInstance(bid).set_callback(bctCustom, FireSelectorBoneCallback, this);
	m_fire_selector_cb = true;
}

void CWeaponMagazined::on_a_hud_attach()
{
	inherited::on_a_hud_attach	();
	m_fire_selector_cb = false;		// new HUD model -> force (re)attach of the selector callback
	UpdateFireSelectorBone		();
	if (m_fire_selector_cb && IsAutoFireMode() && !m_fire_selector_valid)
		SampleFireSelectorAutoPose();	// first pickup in auto: derive the selector's auto pose
}

void CWeaponMagazined::on_b_hud_detach()
{
	DetachFireSelectorBone		();
	inherited::on_b_hud_detach	();
}

void CWeaponMagazined::DetachFireSelectorBone()
{
	if (!m_fire_selector_cb)		return;
	attachable_hud_item* itm = HudItemData();
	if (itm && itm->m_model && m_fire_mode_bone_id != BI_NONE)
		itm->m_model->LL_GetBoneInstance(m_fire_mode_bone_id).reset_callback();
	m_fire_selector_cb = false;
}

// First pickup in auto (no gesture captured yet): derive the selector's auto pose from the LAST FRAME
// of the anm_firemode_1_to_a item animation, so the flip matches the fire mode immediately.
void CWeaponMagazined::SampleFireSelectorAutoPose()
{
	if (m_fire_selector_valid || !m_fire_mode_bone.size())	return;
	attachable_hud_item* itm = HudItemData();
	if (!itm || !itm->m_model)								return;
	IKinematics* K = itm->m_model;
	u16 bid = K->LL_BoneID(m_fire_mode_bone.c_str());
	if (bid == BI_NONE)										return;
	IKinematicsAnimated* ka = K->dcast_PKinematicsAnimated();
	if (!ka)												return;
	// resolve the item-model motion behind the alias (mirrors attachable_hud_item::anim_play)
	player_hud_motion* anm = itm->m_hand_motions.find_motion("anm_firemode_1_to_a");
	if (!anm || anm->m_animations.empty())					return;
	shared_str item_anm = (anm->m_base_name != anm->m_additional_name) ? anm->m_additional_name : anm->m_animations[0].name;
	MotionID M = ka->ID_Cycle_Safe(item_anm);
	if (!M.valid())											return;
	// play the gesture (part 0, channel 0), jump to its last frame, read the selector bone, then restore
	CBlend* B = ka->LL_PlayCycle(0, M, FALSE, NULL, NULL, 0);
	if (B)	{ B->timeCurrent = B->timeTotal - (1.f/30.f); B->blendAmount = 1.f; B->blendPower = 1.f; }	// exact last frame (30 fps), full weight
	K->CalculateBones_Invalidate();
	K->CalculateBones(TRUE);
	m_fire_selector_xform = K->LL_GetBoneInstance(bid).mTransform;
	m_fire_selector_valid = true;
	m_fire_selector_hold  = true;
	// restore the display animation (mirror CHudItem::on_a_hud_attach)
	if (m_current_motion_def)
		PlayHUDMotion_noCB(m_current_motion, FALSE);
}

void CWeaponMagazined::FireModeToken(int mode, string16& out)
{
	if (mode < 0)	xr_strcpy(out, "a");			// auto (-1 / infinite queue)
	else			xr_sprintf(out, "%d", mode);	// single=1 / burst=2,3...
}

void CWeaponMagazined::PlayAnimFireModeSwitch()
{
	LPCSTR a = m_sFireModeAnim.size()
		? m_sFireModeAnim.c_str()
		: (IsAutoFireMode() ? "anm_firemode_1_to_a" : "anm_firemode_a_to_1");
	PlayHUDMotion(a, TRUE, this, eFireModeSwitch);
}

void CWeaponMagazined::switch2_FireModeSwitch()
{
	CWeapon::FireEnd	();
	m_fire_selector_hold		= false;	// release hold so the gesture drives the selector
	m_fire_selector_capturing	= true;		// sample the selector bone each frame of the gesture
	if (m_sounds.FindSoundItem("sndFireModes", false))
		PlaySound		("sndFireModes", get_LastFP());
	PlayAnimFireModeSwitch	();
	SetPending			(TRUE);				// fire/reload locked for the gesture
}

// Called right after m_iQueueSize changed. If we crossed the single<->auto boundary, play the selector
// gesture (which freezes the new pose at its end); if no gesture is possible, just set the hold state.
void CWeaponMagazined::TriggerFireModeSwitchAnim(int oldMode, int newMode)
{
	if (oldMode == newMode)			return;
	// build anm_firemode_<from>_to_<to>; fall back to the single<->auto pair if that exact
	// transition alias isn't defined for this weapon (e.g. only 1_to_a/a_to_1 exist)
	string16 ft, tt;
	FireModeToken(oldMode, ft);
	FireModeToken(newMode, tt);
	string64 anim;
	xr_sprintf(anim, "anm_firemode_%s_to_%s", ft, tt);
	bool nowAuto = IsAutoFireMode();
	if (!isHUDAnimationExist(anim))
		xr_sprintf(anim, "anm_firemode_%s", nowAuto ? "1_to_a" : "a_to_1");
	m_sFireModeAnim = anim;

	if (GetState()==eIdle && !IsPending() && isHUDAnimationExist(anim))
		SwitchState(eFireModeSwitch);
	else
		m_fire_selector_hold = (newMode != 1);	// hold the selector for any non-single mode
}

void CWeaponMagazined::switch2_Hiding()
{
	OnZoomOut();
	CWeapon::FireEnd();
	
	PlaySound			("sndHide",get_LastFP());

	PlayAnimHide		();
	SetPending			(TRUE);
}

void CWeaponMagazined::switch2_Hidden()
{
	CWeapon::FireEnd();

	StopCurrentAnimWithoutCallback();

	signal_HideComplete		();
	RemoveShotEffector		();
}
void CWeaponMagazined::switch2_Showing()
{
	PlaySound			("sndShow",get_LastFP());

	SetPending			(TRUE);
	PlayAnimShow		();
}

bool CWeaponMagazined::Action(s32 cmd, u32 flags) 
{
	if(inherited::Action(cmd, flags)) return true;
	
	//если оружие чем-то занято, то ничего не делать
	if(IsPending()) return false;
	
	switch(cmd) 
	{
	case kWPN_RELOAD:
		{
			if(flags&CMD_START) 
				if (iAmmoElapsed < iMagazineSize || IsMisfire())
				{
					if (!bReloadKeyPressed || !bAmmotypeKeyPressed)
						bReloadKeyPressed = true;
					Reload();
				}
		} 
		return true;
	case kWPN_FIREMODE_PREV:
		{
			if(flags&CMD_START) 
			{
				OnPrevFireMode();
				return true;
			};
		}break;
	case kWPN_FIREMODE_NEXT:
		{
			if(flags&CMD_START) 
			{
				OnNextFireMode();
				return true;
			};
		}break;
	}
	return false;
}

bool CWeaponMagazined::CanAttach(PIItem pIItem)
{
	CScope*				pScope				= smart_cast<CScope*>(pIItem);
	CSilencer*			pSilencer			= smart_cast<CSilencer*>(pIItem);
	CGrenadeLauncher*	pGrenadeLauncher	= smart_cast<CGrenadeLauncher*>(pIItem);

	if(			pScope &&
				 m_eScopeStatus == ALife::eAddonAttachable &&
				(m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonScope) == 0 &&
				(m_sScopeName == pIItem->object().cNameSect()) )
       return true;
	else if(	pSilencer &&
				m_eSilencerStatus == ALife::eAddonAttachable &&
				(m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonSilencer) == 0 &&
				(m_sSilencerName == pIItem->object().cNameSect()) )
       return true;
	else if (	pGrenadeLauncher &&
				m_eGrenadeLauncherStatus == ALife::eAddonAttachable &&
				(m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) == 0 &&
				(m_sGrenadeLauncherName  == pIItem->object().cNameSect()) )
		return true;
	else
		return inherited::CanAttach(pIItem);
}

bool CWeaponMagazined::CanDetach(const char* item_section_name)
{
	if( m_eScopeStatus == ALife::eAddonAttachable &&
	   0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonScope) &&
	   (m_sScopeName	== item_section_name))
       return true;
	else if(m_eSilencerStatus == ALife::eAddonAttachable &&
	   0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonSilencer) &&
	   (m_sSilencerName == item_section_name))
       return true;
	else if(m_eGrenadeLauncherStatus == ALife::eAddonAttachable &&
	   0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) &&
	   (m_sGrenadeLauncherName == item_section_name))
       return true;
	else
		return inherited::CanDetach(item_section_name);
}

bool CWeaponMagazined::Attach(PIItem pIItem, bool b_send_event)
{
	bool result = false;

	CScope*				pScope					= smart_cast<CScope*>(pIItem);
	CSilencer*			pSilencer				= smart_cast<CSilencer*>(pIItem);
	CGrenadeLauncher*	pGrenadeLauncher		= smart_cast<CGrenadeLauncher*>(pIItem);
	
	if(pScope &&
	   m_eScopeStatus == ALife::eAddonAttachable &&
	   (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonScope) == 0 &&
	   (m_sScopeName == pIItem->object().cNameSect()))
	{
		m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonScope;
		result = true;
	}
	else if(pSilencer &&
	   m_eSilencerStatus == ALife::eAddonAttachable &&
	   (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonSilencer) == 0 &&
	   (m_sSilencerName == pIItem->object().cNameSect()))
	{
		m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonSilencer;
		result = true;
	}
	else if(pGrenadeLauncher &&
	   m_eGrenadeLauncherStatus == ALife::eAddonAttachable &&
	   (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) == 0 &&
	   (m_sGrenadeLauncherName == pIItem->object().cNameSect()))
	{
		m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher;
		result = true;
	}

	if(result)
	{
		if (b_send_event && OnServer())
		{
			//уничтожить подсоединенную вещь из инвентаря
//.			pIItem->Drop					();
			pIItem->object().DestroyObject	();
		};

		UpdateAddonsVisibility();
		InitAddons();

		return true;
	}
	else
        return inherited::Attach(pIItem, b_send_event);
}


bool CWeaponMagazined::Detach(const char* item_section_name, bool b_spawn_item)
{
	if(		m_eScopeStatus == ALife::eAddonAttachable &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonScope) &&
			(m_sScopeName == item_section_name))
	{
		m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonScope;
		
		UpdateAddonsVisibility();
		InitAddons();

		return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
	}
	else if(m_eSilencerStatus == ALife::eAddonAttachable &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonSilencer) &&
			(m_sSilencerName == item_section_name))
	{
		m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonSilencer;

		UpdateAddonsVisibility();
		InitAddons();
		return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
	}
	else if(m_eGrenadeLauncherStatus == ALife::eAddonAttachable &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) &&
			(m_sGrenadeLauncherName == item_section_name))
	{
		m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher;

		UpdateAddonsVisibility();
		InitAddons();
		return CInventoryItemObject::Detach(item_section_name, b_spawn_item);
	}
	else
		return inherited::Detach(item_section_name, b_spawn_item);;
}
/*
void CWeaponMagazined::LoadAddons()
{
	m_zoom_params.m_fIronSightZoomFactor = READ_IF_EXISTS( pSettings, r_float, cNameSect(), "ironsight_zoom_factor", 50.0f );

}
*/
void CWeaponMagazined::InitAddons()
{
	m_zoom_params.m_fIronSightZoomFactor = READ_IF_EXISTS( pSettings, r_float, cNameSect(), "ironsight_zoom_factor", 50.0f );
	if ( IsScopeAttached() )
	{
		shared_str scope_tex_name;
		if ( m_eScopeStatus == ALife::eAddonAttachable )
		{
			//m_sScopeName = pSettings->r_string(cNameSect(), "scope_name");
			//m_iScopeX	 = pSettings->r_s32(cNameSect(),"scope_x");
			//m_iScopeY	 = pSettings->r_s32(cNameSect(),"scope_y");

			VERIFY( *m_sScopeName );
			scope_tex_name						= pSettings->r_string(*m_sScopeName, "scope_texture");
			m_zoom_params.m_fScopeZoomFactor	= pSettings->r_float( *m_sScopeName, "scope_zoom_factor");
		}
		else if( m_eScopeStatus == ALife::eAddonPermanent )
		{
			scope_tex_name						= pSettings->r_string(cNameSect(), "scope_texture");
			m_zoom_params.m_fScopeZoomFactor	= pSettings->r_float( cNameSect(), "scope_zoom_factor");
		}
		if ( m_UIScope )
		{
			xr_delete( m_UIScope );
		}

		if ( !g_dedicated_server )
		{
			m_UIScope				= xr_new<CUIWindow>();
			createWpnScopeXML		();
			CUIXmlInit::InitWindow	(*pWpnScopeXml, scope_tex_name.c_str(), 0, m_UIScope);
		}
	}
	else
	{
		if ( m_UIScope )
		{
			xr_delete( m_UIScope );
		}
		
		if ( IsZoomEnabled() )
		{
			m_zoom_params.m_fIronSightZoomFactor = pSettings->r_float( cNameSect(), "scope_zoom_factor" );
		}
	}

	if ( IsSilencerAttached() && SilencerAttachable() )
	{		
		m_sFlameParticlesCurrent	= m_sSilencerFlameParticles;
		m_sSmokeParticlesCurrent	= m_sSilencerSmokeParticles;
		m_sSndShotCurrent			= "sndSilencerShot";

		//подсветка от выстрела
		LoadLights					(*cNameSect(), "silencer_");
		ApplySilencerKoeffs			();
	}
	else
	{
		m_sFlameParticlesCurrent	= m_sFlameParticles;
		m_sSmokeParticlesCurrent	= m_sSmokeParticles;
		m_sSndShotCurrent			= "sndShot";

		//подсветка от выстрела
		LoadLights		(*cNameSect(), "");
		ResetSilencerKoeffs();
	}

	inherited::InitAddons();
}

void CWeaponMagazined::LoadSilencerKoeffs()
{
	if ( m_eSilencerStatus == ALife::eAddonAttachable )
	{
		LPCSTR sect = m_sSilencerName.c_str();
		m_silencer_koef.hit_power		= READ_IF_EXISTS( pSettings, r_float, sect, "bullet_hit_power_k", 1.0f );
		m_silencer_koef.hit_impulse		= READ_IF_EXISTS( pSettings, r_float, sect, "bullet_hit_impulse_k", 1.0f );
		m_silencer_koef.bullet_speed	= READ_IF_EXISTS( pSettings, r_float, sect, "bullet_speed_k", 1.0f );
		m_silencer_koef.fire_dispersion	= READ_IF_EXISTS( pSettings, r_float, sect, "fire_dispersion_base_k", 1.0f );
		m_silencer_koef.cam_dispersion	= READ_IF_EXISTS( pSettings, r_float, sect, "cam_dispersion_k", 1.0f );
		m_silencer_koef.cam_disper_inc	= READ_IF_EXISTS( pSettings, r_float, sect, "cam_dispersion_inc_k", 1.0f );
	}

	clamp( m_silencer_koef.hit_power,		0.0f, 1.0f );
	clamp( m_silencer_koef.hit_impulse,		0.0f, 1.0f );
	clamp( m_silencer_koef.bullet_speed,	0.0f, 1.0f );
	clamp( m_silencer_koef.fire_dispersion,	0.0f, 3.0f );
	clamp( m_silencer_koef.cam_dispersion,	0.0f, 1.0f );
	clamp( m_silencer_koef.cam_disper_inc,	0.0f, 1.0f );
}

void CWeaponMagazined::ApplySilencerKoeffs()
{
	cur_silencer_koef = m_silencer_koef;
}

void CWeaponMagazined::ResetSilencerKoeffs()
{
	cur_silencer_koef.Reset();
}

void CWeaponMagazined::PlayAnimShow()
{
	VERIFY(GetState()==eShowing);
	PlayHUDMotion("anm_show", FALSE, this, GetState());
}

void CWeaponMagazined::PlayAnimHide()
{
	VERIFY(GetState()==eHiding);
	PlayHUDMotion("anm_hide", TRUE, this, GetState());
}

void CWeaponMagazined::PlayAnimReload()
{
	VERIFY(GetState() == eReload);

	if (isHUDAnimationExist("anm_reload_misfire") && IsMisfire())
	{
		PlayHUDMotion("anm_reload_misfire", TRUE, this, GetState());
		bMisfireReload = true;
	}
	else if (isHUDAnimationExist("anm_reload_empty") && iAmmoElapsed == 0)
		PlayHUDMotion("anm_reload_empty", TRUE, this, GetState());
	else
		PlayHUDMotion("anm_reload", TRUE, this, GetState());
}

// direction suffix from the actor's movement while ADS; "" when standing still
LPCSTR CWeaponMagazined::AimWalkDirSuffix()
{
	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if(!pActor)					return "";
	u32 ms = pActor->MovingState();
	if(!(ms&mcAnyMove))			return "";
	if(ms&mcBack)				return "_walk_back";
	if(ms&mcLStrafe)			return "_walk_left";
	if(ms&mcRStrafe)			return "_walk_right";
	return "_walk";				// forward / diagonal-forward default
}

void CWeaponMagazined::SelectAimIdleAnim(string_path& result)
{
	LPCSTR dir = AimWalkDirSuffix();
	if(dir[0])
	{
		xr_sprintf(result, "anm_idle_aim%s", dir);
		if(isHUDAnimationExist(result))		return;
		// fall back through _walk (forward) before the static aim
		if(xr_strcmp(dir, "_walk") != 0 && isHUDAnimationExist("anm_idle_aim_walk"))
			{ xr_strcpy(result, "anm_idle_aim_walk"); return; }
	}
	xr_strcpy(result, "anm_idle_aim");
}

void CWeaponMagazined::PlayAnimAim()
{
	string_path anim;
	SelectAimIdleAnim(anim);
	PlayHUDMotion(anim, TRUE, NULL, GetState());
}

bool CWeaponMagazined::HasMovementIdleVariant()
{
	if(IsZoomed())
		return isHUDAnimationExist("anm_idle_aim_walk");	// directional aim-walk present
	return inherited::HasMovementIdleVariant();				// anm_idle_moving_slow
}

void CWeaponMagazined::PlayAnimIdle()
{
	VERIFY(GetState()==eIdle);
	if(IsZoomed())
	{
		PlayAnimAim();
	}else
		inherited::PlayAnimIdle();
}

// Pick the shoot motion: a dedicated ADS motion when zoomed (Gunslinger-style); falls back to the
// hip-fire "anm_shots" when not zoomed or the weapon has no such alias. Overridden by the GL
// subclass for the _w_gl / _g variants. (A PIP-scope "anm_shots_aim_scope" variant may be added
// later once IX-Ray gains PIP scopes.)
void CWeaponMagazined::SelectShootAnim(string_path& result)
{
	if (IsZoomed() && isHUDAnimationExist("anm_shots_aim"))
		{ xr_strcpy(result, "anm_shots_aim"); return; }
	xr_strcpy(result, "anm_shots");
}

void CWeaponMagazined::PlayAnimShoot()
{
	VERIFY(GetState()==eFire);
	string_path anim;
	SelectShootAnim(anim);
	PlayHUDMotion(anim, FALSE, this, GetState());
}

// ---- aim-in / aim-out (ADS) transition, Gunslinger-style ----
// Pick the transition motion; base impl has no GL variants (see the WGrenade override).
void CWeaponMagazined::SelectAimTransitionAnim(bool bAimIn, string_path& result)
{
	LPCSTR base = bAimIn ? "anm_idle_aim_start" : "anm_idle_aim_end";
	xr_strcpy(result, isHUDAnimationExist(base) ? base : "");
}

// Play the aim-in/out transition as an eIdle-owned motion: when it ends, OnAnimationEnd(eIdle)
// -> switch2_Idle -> PlayAnimIdle picks the aim idle (still zoomed) or the normal idle (zoomed
// out) automatically, so the transition->idle handoff needs no extra state. NO SetPending, so the
// player can interrupt aim-in by firing. Returns false if the weapon has no such motion.
bool CWeaponMagazined::PlayAimTransition(bool bAimIn)
{
	string_path anim;
	SelectAimTransitionAnim(bAimIn, anim);
	if (!anim[0])	return false;
	// transitions now play only at eIdle (fire is stopped/ended first), so state is eIdle here
	u32 t = PlayHUDMotion(anim, TRUE, this, GetState());
	m_bIdleTransitionLock = true;	// don't let a movement change cut the transition
	m_dwAimTransitionEndTm = Device.dwTimeGlobal + t;	// UpdateCL handoff deadline (t=0 -> next frame)
	return true;
}

void CWeaponMagazined::OnZoomIn			()
{
	if (IsJamInspectPlaying())	return;	// can't aim mid jam-inspect

	inherited::OnZoomIn();
	m_bAimOutPending = false;	// re-aiming cancels a deferred aim-out

	if(GetState() == eIdle)
	{
		if(!PlayAimTransition(true))
			PlayAnimIdle();
	}
	else
		// aiming while firing: the input queued SwitchState(eIdle) to stop the fire; play the
		// aim-in transition once we settle at idle (Gunslinger-style: fire stops, then aim-in)
		m_bAimInPending = true;


	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if(pActor)
	{
		CEffectorZoomInertion* S = smart_cast<CEffectorZoomInertion*>	(pActor->Cameras().GetCamEffector(eCEZoom));
		if (!S)	
		{
			S = (CEffectorZoomInertion*)pActor->Cameras().AddCamEffector(xr_new<CEffectorZoomInertion> ());
			S->Init(this);
		};
		S->SetRndSeed			(pActor->GetZoomRndSeed());
		R_ASSERT				(S);
	}
}
void CWeaponMagazined::OnZoomOut		()
{
	if(!IsZoomed())
		return;

	if(IsJamInspectPlaying())	return;	// can't aim-out mid jam-inspect (mirror OnZoomIn)

	if(GetState()==eFire)
	{
		// releasing aim while firing: keep firing aimed and defer the aim-out — it starts only
		// when the fire ends (switch2_Idle re-enters here at eIdle). Gunslinger-style.
		m_bAimOutPending = true;
		m_bAimInPending  = false;
		return;
	}

	inherited::OnZoomOut	();
	m_bAimInPending = false;	// zooming out cancels a deferred aim-in

	if(GetState()==eIdle)
	{
		if(!PlayAimTransition(false))
			PlayAnimIdle		();
	}

	CActor* pActor			= smart_cast<CActor*>(H_Parent());

	if(pActor)
		pActor->Cameras().RemoveCamEffector	(eCEZoom);

}

//переключение режимов стрельбы одиночными и очередями
bool CWeaponMagazined::SwitchMode			()
{
	if(eIdle != GetState() || IsPending()) return false;

	int oldMode = IsAutoFireMode() ? -1 : 1;
	if(SingleShotMode())
		m_iQueueSize = WEAPON_ININITE_QUEUE;
	else
		m_iQueueSize = 1;

	PlaySound	("sndEmptyClick", get_LastFP());
	TriggerFireModeSwitchAnim(oldMode, IsAutoFireMode() ? -1 : 1);

	return true;
}

void	CWeaponMagazined::OnNextFireMode		()
{
	if (!m_bHasDifferentFireModes) return;
	if (GetState() != eIdle) return;
	int oldMode = GetCurrentFireMode();
	m_iCurFireMode = (m_iCurFireMode+1+m_aFireModes.size()) % m_aFireModes.size();
	SetQueueSize(GetCurrentFireMode());
	TriggerFireModeSwitchAnim(oldMode, GetCurrentFireMode());
};

void	CWeaponMagazined::OnPrevFireMode		()
{
	if (!m_bHasDifferentFireModes) return;
	if (GetState() != eIdle) return;
	int oldMode = GetCurrentFireMode();
	m_iCurFireMode = (m_iCurFireMode-1+m_aFireModes.size()) % m_aFireModes.size();
	SetQueueSize(GetCurrentFireMode());
	TriggerFireModeSwitchAnim(oldMode, GetCurrentFireMode());
};

void	CWeaponMagazined::OnH_A_Chield		()
{
	if (m_bHasDifferentFireModes)
	{
		CActor	*actor = smart_cast<CActor*>(H_Parent());
		if (!actor) SetQueueSize(-1);
		else SetQueueSize(GetCurrentFireMode());
	};	
	inherited::OnH_A_Chield();
};

void	CWeaponMagazined::SetQueueSize			(int size)  
{
	m_iQueueSize = size; 
};

float	CWeaponMagazined::GetWeaponDeterioration	()
{
	if (!m_bHasDifferentFireModes || m_iPrefferedFireMode == -1 || u32(GetCurrentFireMode()) <= u32(m_iPrefferedFireMode)) 
		return inherited::GetWeaponDeterioration();
	return m_iShotNum*conditionDecreasePerShot;
};

void CWeaponMagazined::save(NET_Packet &output_packet)
{
	inherited::save	(output_packet);
	save_data		(m_iQueueSize, output_packet);
	save_data		(m_iShotNum, output_packet);
	save_data		(m_iCurFireMode, output_packet);
	// fire-selector hold (so the flipped selector survives save/load)
	output_packet.w_u8	(m_fire_selector_valid ? 1 : 0);
	output_packet.w_u8	(m_fire_selector_hold ? 1 : 0);
	output_packet.w		(&m_fire_selector_xform, sizeof(m_fire_selector_xform));
	output_packet.w_u8	(bMisfire ? 1 : 0);		// remember a jam across save/load
}

void CWeaponMagazined::load(IReader &input_packet)
{
	inherited::load	(input_packet);
	load_data		(m_iQueueSize, input_packet);SetQueueSize(m_iQueueSize);
	load_data		(m_iShotNum, input_packet);
	load_data		(m_iCurFireMode, input_packet);
	m_fire_selector_valid	= (input_packet.r_u8() != 0);
	m_fire_selector_hold	= (input_packet.r_u8() != 0);
	input_packet.r			(&m_fire_selector_xform, sizeof(m_fire_selector_xform));
	bMisfire				= (input_packet.r_u8() != 0);	// restore a jam
}

void CWeaponMagazined::net_Export	(NET_Packet& P)
{
	inherited::net_Export (P);

	P.w_u8(u8(m_iCurFireMode&0x00ff));
}

void CWeaponMagazined::net_Import	(NET_Packet& P)
{
	inherited::net_Import (P);

	m_iCurFireMode = P.r_u8();
	SetQueueSize(GetCurrentFireMode());
}

#include "string_table.h"
void CWeaponMagazined::GetBriefInfo(xr_string& str_name, xr_string& icon_sect_name, xr_string& str_count, string16& fire_mode )
{
	int	AE		= GetAmmoElapsed();
	int	AC		= 0;
	if ( IsGameTypeSingle() )
	{
		AC		= GetCurrentTypeAmmoTotal();
	}
	else
	{
		AC		= GetSuitableAmmoTotal();//mp = all type
	}
	
	if(AE==0 || 0==m_magazine.size() )
		icon_sect_name	= *m_ammoTypes[m_ammoType];
	else
		icon_sect_name	= *m_ammoTypes[m_magazine.back().m_LocalAmmoType];


	string256		sItemName;
	xr_strcpy			(sItemName, *CStringTable().translate(pSettings->r_string(icon_sect_name.c_str(), "inv_name_short")));

	xr_strcpy( fire_mode, sizeof(fire_mode), "" );
	if ( HasFireModes() )
	{
		if (m_iQueueSize == -1)
			xr_strcpy(fire_mode, "A");
		else
			xr_sprintf(fire_mode, "%d", m_iQueueSize);
	}

	str_name		= sItemName;

	{
		if (!unlimited_ammo())
			xr_sprintf			(sItemName, "%d/%d",AE,AC - AE);
		else
			xr_sprintf			(sItemName, "%d/--",AE);

		str_count				= sItemName;
	}
}

bool CWeaponMagazined::install_upgrade_impl( LPCSTR section, bool test )
{
	bool result = inherited::install_upgrade_impl( section, test );
	
	LPCSTR str;
	// fire_modes = 1, 2, -1
	bool result2 = process_if_exists_set( section, "fire_modes", &CInifile::r_string, str, test );
	if ( result2 && !test )
	{
		int ModesCount = _GetItemCount( str );
		m_aFireModes.clear();
		for ( int i = 0; i < ModesCount; ++i )
		{
			string16 sItem;
			_GetItem( str, i, sItem );
			m_aFireModes.push_back( (s8)atoi(sItem) );
		}
		m_iCurFireMode = ModesCount - 1;
	}
	result |= result2;

	result |= process_if_exists( section, "dispersion_start", &CInifile::r_s32, m_iShootEffectorStart, test );

	// sounds (name of the sound, volume (0.0 - 1.0), delay (sec))
	result2 = process_if_exists_set( section, "snd_draw", &CInifile::r_string, str, test );
	if ( result2 && !test ) { m_sounds.LoadSound( section, "snd_draw"	    , "sndShow"		, false, m_eSoundShow		);	}
	result |= result2;

	result2 = process_if_exists_set( section, "snd_holster", &CInifile::r_string, str, test );
	if ( result2 && !test ) { m_sounds.LoadSound( section, "snd_holster"	, "sndHide"		, false, m_eSoundHide		);	}
	result |= result2;

	result2 = process_if_exists_set( section, "snd_shoot", &CInifile::r_string, str, test );
	if ( result2 && !test ) { m_sounds.LoadSound( section, "snd_shoot"	, "sndShot"		, false, m_eSoundShot		);	}
	result |= result2;

	result2 = process_if_exists_set( section, "snd_empty", &CInifile::r_string, str, test );
	if ( result2 && !test ) { m_sounds.LoadSound( section, "snd_empty"	, "sndEmptyClick"	, false, m_eSoundEmptyClick);	}
	result |= result2;

	result2 = process_if_exists_set( section, "snd_reload", &CInifile::r_string, str, test );
	if ( result2 && !test ) { m_sounds.LoadSound( section, "snd_reload"	, "sndReload"		, true, m_eSoundReload	);	}
	result |= result2;

	//snd_shoot1     = weapons\ak74u_shot_1 ??
	//snd_shoot2     = weapons\ak74u_shot_2 ??
	//snd_shoot3     = weapons\ak74u_shot_3 ??

	if ( m_eSilencerStatus == ALife::eAddonAttachable )
	{
		result |= process_if_exists_set( section, "silencer_flame_particles", &CInifile::r_string, m_sSilencerFlameParticles, test );
		result |= process_if_exists_set( section, "silencer_smoke_particles", &CInifile::r_string, m_sSilencerSmokeParticles, test );

		result2 = process_if_exists_set( section, "snd_silncer_shot", &CInifile::r_string, str, test );
		if ( result2 && !test ) { m_sounds.LoadSound( section, "snd_silncer_shot"	, "sndSilencerShot", false, m_eSoundShot	);	}
		result |= result2;
	}

	// fov for zoom mode
	result |= process_if_exists( section, "ironsight_zoom_factor", &CInifile::r_float, m_zoom_params.m_fIronSightZoomFactor, test );

	if( IsScopeAttached() )
	{
		//if ( m_eScopeStatus == ALife::eAddonAttachable )
		{
			result |= process_if_exists( section, "scope_zoom_factor", &CInifile::r_float, m_zoom_params.m_fScopeZoomFactor, test );
		}
	}
	else
	{
		if( IsZoomEnabled() )
		{
			result |= process_if_exists( section, "scope_zoom_factor", &CInifile::r_float, m_zoom_params.m_fIronSightZoomFactor, test );
		}
	}

	return result;
}
