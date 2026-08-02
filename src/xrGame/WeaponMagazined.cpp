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

extern bool gwr_pda_need_fastzoom();		// ui\UIPdaWnd.cpp
extern int  g_pda_dbg;						// ui\UIPdaWnd.cpp
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
	m_dwAimFireLockTm			= 0;
	m_bAimLockAutoShoot			= false;
	m_bAimLockFirePressed		= false;
	m_dwShootAnimEndTm			= 0;
	for (int i = 0; i < 6; ++i)	m_gwr_bones_state[i] = -0x7fffffff;	// force the first bone update
	m_gwr_last_fired_type = 0;
	m_gwr_fired_until     = 0;
	m_dwLastWorldAnimState= u32(-1);
	m_gwr_last_mag_type   = 0;
	m_bDryFirePending			= false;
	m_bDryFirePlaying			= false;
	m_bLightMisfirePlaying		= false;
	m_bAimInPending				= false;
	m_bAimOutPending			= false;
	m_bTriggerHeld				= false;
	m_bZoomPendingSprint		= false;
	m_bZoomPendingMisfire		= false;
	m_bZoomPendingMisfireIn		= false;
	m_bFirePendingSprint		= false;
	m_bDetectorDrawPending		= false;
	m_dwDetectorShowTm			= 0;

	m_bFireSingleShot			= false;
	m_iShotNum					= 0;
	m_iBaseDispersionedBulletsCount		= 0;
	m_fBaseDispersionedBulletsSpeed		= 0.0f;
	m_fBaseDispersionedBulletsTimeDelta	= 0.0f;
	m_fSingleShootsTimeDelta			= 0.0f;
	m_iQueueSize				= WEAPON_ININITE_QUEUE;
	m_bLockType					= false;
	m_bAmmoInChamber			= false;
	m_bNoJamFire				= false;
	m_dwReloadInsertTm			= 0;
	m_bReloadInsertDone			= false;
	m_bLastEmptyAnim			= false;
	m_iMaxQueueSize				= 0;
	m_fRechargeTime				= 0.f;
	m_bSaveCartridgeInAmmoChange = true;
	bMisfireReload				= false;
	m_bAmmoChangeReload			= false;

	m_fire_mode_bone_id			= BI_NONE;
	m_fire_selector_hold		= false;
	m_fire_selector_capturing	= false;
	m_fire_selector_valid		= false;
	m_fire_selector_cb			= false;
	m_selector_model_warm		= false;
	m_selector_sample_tries		= 0;
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

	// GS no_jam_fire (bm16 / toz34 / rg6 hud sections): on a break-action the failure is a DUD, not a
	// stuck action -- the roll happens BEFORE the shot, so the round isn't spent, the hammer just falls
	// on nothing (anm_shoot_jammed_<n> = *_dry_empty). The weapon still counts as jammed and needs its
	// revival; it simply never eats the shot. See state_Fire.
	m_bNoJamFire		= READ_IF_EXISTS(pSettings, r_bool, HudSection(), "no_jam_fire", FALSE);

	// Gunslinger ammo_in_chamber (+1): the config ammo_mag_size counts the mag PLUS the chambered round, so a
	// reload from empty loads mag_size-1 (nothing chambered), and a reload with a round still chambered (weapon
	// not empty) keeps it and takes a full mag on top = mag_size. See ReloadMagazine wrap in OnAnimationEnd.
	m_bAmmoInChamber	= READ_IF_EXISTS(pSettings, r_bool, section, "ammo_in_chamber", FALSE);
	m_iMaxQueueSize		= (int)READ_IF_EXISTS(pSettings, r_u32, section, "max_queue_size", 0);
	m_fRechargeTime		= READ_IF_EXISTS(pSettings, r_float, section, "recharge_time", 0.0f);
	m_bSaveCartridgeInAmmoChange = READ_IF_EXISTS(pSettings, r_bool, section, "save_cartridge_in_ammochange", TRUE);

	// Sounds
	m_sounds.LoadSound(section,"snd_draw", "sndShow"		, false, m_eSoundShow		);
	m_sounds.LoadSound(section,"snd_holster", "sndHide"		, false, m_eSoundHide		);
	m_sounds.LoadSound(section,"snd_shoot", "sndShot"		, false, m_eSoundShot		);
	m_sounds.LoadSound(section,"snd_empty", "sndEmptyClick"	, false, m_eSoundEmptyClick	);
	// GS snd_jammed_click: a JAM (клин) clicks with its own sound, NOT the empty-magazine click. Optional --
	// if a weapon doesn't define it, a jam stays silent (GS pistols do exactly this), never the empty click.
	if (WeaponSoundExist(section, "snd_jammed_click"))
		m_sounds.LoadSound(section, "snd_jammed_click", "sndJammedClick", false, m_eSoundEmptyClick);
	// GS PlaySoundByAnimName: load every `snd_anm_*` the HUD section defines, keyed by the config name
	// itself, so CHudItem::PlayHUDMotion can look the sound up by the alias it just resolved. Covers the
	// gauss MUI toggle (snd_anm_changefiremode_from_1_to_a/_from_a_to_1) and anything else a config keys.
	LoadAnmSounds();

	// GS snd_jam: the sound of the shot that JAMS (played with anm_shoot_jammed, in place of the
	// breechblock rack). Different from snd_jammed_click, which is the pull on an already-jammed gun.
	if (WeaponSoundExist(section, "snd_jam"))
		m_sounds.LoadSound(section, "snd_jam", "sndJam", false, m_eSoundEmptyClick);
	m_sounds.LoadSound(section,"snd_reload", "sndReload"	, true, m_eSoundReload		);

	if (WeaponSoundExist(section, "snd_reload_empty") && isHUDAnimationExist("anm_reload_empty"))
		m_sounds.LoadSound(section,"snd_reload_empty", "sndReloadEmpty"	, true, m_eSoundReload);
	
	if (WeaponSoundExist(section, "snd_reload_jammed") && HasJammedReloadAnim())
		m_sounds.LoadSound(section, "snd_reload_jammed", "sndReloadMis", true, m_eSoundReload);
	// GS snd_changecartridgetype: the ammo-TYPE change reload (anm_reload_ammochange) sounds different
	if (WeaponSoundExist(section, "snd_changecartridgetype") && isHUDAnimationExist("anm_reload_ammochange"))
		m_sounds.LoadSound(section, "snd_changecartridgetype", "sndChangeCartridge", true, m_eSoundReload);
	// GS snd_reload_jammed_last: the revival when the gun is also empty (shotguns clear a jam through their
	// own anm_reload_jammed_last -- see CWeaponShotgun::PlayAnimUnjamWeapon)
	if (WeaponSoundExist(section, "snd_reload_jammed_last") && isHUDAnimationExist("anm_reload_jammed_last"))
		m_sounds.LoadSound(section, "snd_reload_jammed_last", "sndReloadMisLast", true, m_eSoundReload);
	// ...and its detector companion (GS snd_reload_jammed_last_detector -> sndReloadJammedLastDetector):
	// same reason as the plain jam-clear below, the one-handed motion has its own length
	if (WeaponSoundExist(section, "snd_reload_jammed_last_detector") && isHUDAnimationExist("anm_reload_jammed_last_detector"))
		m_sounds.LoadSound(section, "snd_reload_jammed_last_detector", "sndReloadMisLastDet", true, m_eSoundReload);
	// the jam-clear with a detector is a whole different motion (anm_reload_misfire_detector), so it
	// needs its own sound - the plain one is cut for the normal revival and runs ahead of this one
	if (WeaponSoundExist(section, "snd_reload_jammed_detector") && isHUDAnimationExist("anm_reload_jammed_detector"))
		m_sounds.LoadSound(section, "snd_reload_jammed_detector", "sndReloadMisDet", true, m_eSoundReload);

	if (WeaponSoundExist(section, "snd_changefiremode"))	// fire-selector switch sound (optional)
		m_sounds.LoadSound(section, "snd_changefiremode", "sndFireModes", false, m_eSoundEmptyClick);

	// GS light misfire (use_light_misfire): the "click, no bang" light-strike sound. Optional.
	if (WeaponSoundExist(section, "snd_light_misfire"))
		m_sounds.LoadSound(section, "snd_light_misfire", "sndLightMisfire", false, m_eSoundEmptyClick);

	// GS snd_kick: the bayonet stab's own sound. GS reaches it through its generic snd_<anim> lookup for
	// anm_kick; we play it explicitly at the stab (CWeaponMagazined::PlayKickSound). Mechanical type, like
	// the other gesture sounds -- it is not a gunshot and should not alert AI as one.
	if (WeaponSoundExist(section, "snd_kick"))
		m_sounds.LoadSound(section, "snd_kick", "sndKick", false, m_eSoundReload);

	// pump/bolt rack (GS snd_breechblock): layered on each shot for weapons that define it (pump shotguns,
	// bolt-actions). Optional -- absent = no-op. Loaded as a mechanical (reload-type) sound so it doesn't
	// alert AI like a second gunshot; it plays right after the shot which already does.
	if (WeaponSoundExist(section, "snd_breechblock"))
		m_sounds.LoadSound(section, "snd_breechblock", "sndBreechblock", false, m_eSoundReload);

	// GS laser designator toggle sounds (optional; mechanical/reload-type so they don't alert AI)
	if (WeaponSoundExist(section, "snd_laser_on"))
		m_sounds.LoadSound(section, "snd_laser_on", "sndLaserOn", false, m_eSoundReload);
	if (WeaponSoundExist(section, "snd_laser_off"))
		m_sounds.LoadSound(section, "snd_laser_off", "sndLaserOff", false, m_eSoundReload);
	// GS weapon flashlight toggle sounds (snd_torch_on/off on the weapon; distinct from the actor headlamp)
	if (WeaponSoundExist(section, "snd_torch_on"))
		m_sounds.LoadSound(section, "snd_torch_on", "sndFlashOn", false, m_eSoundReload);
	if (WeaponSoundExist(section, "snd_torch_off"))
		m_sounds.LoadSound(section, "snd_torch_off", "sndFlashOff", false, m_eSoundReload);

	m_sSndShotCurrent = "sndShot";
		
	//звуки и партиклы глушителя, еслит такой есть
	// NOTE: also load them when the silencer comes from an UPGRADE (base silencer_status may be 0 at Load,
	// only becoming attachable after install_upgrade -- which never re-runs LoadSounds). Gate on the config
	// key existing so the silenced-shot sound is ready when the upgrade later attaches the silencer.
	if ( m_eSilencerStatus == ALife::eAddonAttachable || m_eSilencerStatus == ALife::eAddonPermanent
		|| WeaponSoundExist(section, "snd_silncer_shot") )
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

	// AN-94 hyperburst. count/speed are the vanilla SoC keys (dropped in CS), time_delta is what
	// Gunslinger's AN94Patch.pas adds on top -- it is the whole point of the feature: the interval
	// to the next round inside the fast part of the queue, in seconds (= 1/rpm of the hyperburst).
	// All optional: no key -> count stays 0 -> nothing below ever triggers.
	m_iBaseDispersionedBulletsCount		= READ_IF_EXISTS(pSettings, r_u8,	 section, "base_dispersioned_bullets_count",		0);
	m_fBaseDispersionedBulletsSpeed		= READ_IF_EXISTS(pSettings, r_float, section, "base_dispersioned_bullets_speed",		0.0f);
	m_fBaseDispersionedBulletsTimeDelta	= READ_IF_EXISTS(pSettings, r_float, section, "base_dispersioned_bullets_time_delta",	0.0f);
	// GS gives some weapons their own rate in single-shot mode (glock17, gsh18, sr1m, stechkin,
	// p90, vintorez, svu_uniq); same patch, so it lives here.
	m_fSingleShootsTimeDelta			= READ_IF_EXISTS(pSettings, r_float, section, "singleshoots_time_delta",				0.0f);

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

bool CWeaponMagazined::IsActorSprinting()
{
	CActor* a = smart_cast<CActor*>(H_Parent());
	if (!a)	return false;
	CEntity::SEntityState st;
	a->g_State(st);
	return !!st.bSprint;
}

void CWeaponMagazined::FireStart		()
{
	m_bTriggerHeld = true;	// trigger pressed (held until FireEnd); used to resume fire after a transition

	// A light-misfire strike owns the trigger for its whole gesture: the plain shoot-lock deadline set in
	// gwr_TryLightMisfire (survives the StopShooting/switch2_Idle that clears pending). No round, no queued
	// shot -- the player just pulls again once it ends. Base (CHudItem) query only, so the aim/sprint
	// fire-locks folded into the CWeaponMagazined override still hit their own defer blocks below.
	if (CHudItem::IsShootLocked())	return;

	// let the jam (misfire) dry-fire gesture finish before another trigger pull; the empty
	// dry-fire stays spammable (each click re-triggers it)
	if ((m_bDryFirePending || m_bDryFirePlaying) && IsMisfire())	return;

	// Aim in/out: block firing only for the short lock_time, not the whole transition animation
	// (Gunslinger-style). After the lock a shot is allowed even mid-transition and cuts it.
	if (m_dwAimFireLockTm && Device.dwTimeGlobal < m_dwAimFireLockTm)
	{
		// The player actually pressed fire DURING the lock window -> remember it so the shot comes out
		// the moment the lock ends (autoshoot). This is a FRESH press inside the lock, NOT the sticky
		// m_bTriggerHeld (which lingers true on pistols/shotguns/SVD and caused a self-shot on aim in/out).
		m_bAimLockFirePressed = true;
		return;
	}

	// exiting sprint: pressing fire clears the actor's sprint (ActorInput) and the weapon plays the
	// sprint-out anim first; hold the shot until that exit is (almost) done, then the UpdateCL handoff
	// resumes it (m_bFirePendingSprint - a dedicated flag, NOT m_bTriggerHeld which sticks true on
	// pistols and caused a self-fire on every later sprint-exit). Only when the weapon has an exit anim.
	if ((m_dwSprintExitEndTm && Device.dwTimeGlobal < m_dwSprintExitEndTm)
		|| (IsActorSprinting() && HasSprintExitAnim()))
	{
		m_bFirePendingSprint = true;
		return;
	}

	// Past the fire lock now: a real shot -- or a jam dry-fire -- is about to be issued, and it cuts any
	// aim in/out transition still on screen. Clear the deferred transition handoff so switch2_Idle won't
	// swallow the follow-up anim (it early-returns while m_dwAimTransitionEndTm is pending, which left the
	// jam dry-fire stuck as m_bDryFirePending and froze the hands) and the UpdateCL fallback won't stomp
	// the new anim. Matches Gunslinger: once fire is processed the transition anim is replaced at once.
	m_dwAimTransitionEndTm	= 0;
	m_bIdleTransitionLock	= false;

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
	{//misfire -- the "weapon jammed" message is raised with the dry-fire gesture (PlayAnimDryFire),
	 // like GS's OnEmptyClick, not here: a pull that cannot even play the gesture says nothing.
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
	m_bFirePendingSprint = false;	// releasing fire cancels a sprint-deferred shot (set before any shot, so it clears cleanly)
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

	// same for the light-misfire strike (m_bLightMisfirePlaying): block reload while the click gesture
	// plays, exactly like the normal-misfire inspect above. Light misfire keeps the round chambered so no
	// reload is needed anyway -- this just stops a reload from cutting the strike short.
	if (m_bLightMisfirePlaying)
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
		// GS DISABLE_AUTOAMMOCHANGE: don't auto-switch to another ammo type while the gun still holds rounds
		// and no explicit type change was requested -- pressing reload then does nothing (only an explicit
		// ammo-type change reloads). An empty gun, or a requested change, still searches for any ammo.
		else if (m_set_next_ammoType_on_reload != u32(-1) || iAmmoElapsed == 0)
			for(u32 i = 0; i < m_ammoTypes.size(); ++i)
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

	// No reload started (e.g. DISABLE_AUTOAMMOCHANGE blocked it, or genuinely no ammo). Clear the pressed-key
	// latches so a poisoned bReloadKeyPressed doesn't block the next SwitchAmmoType (which bails if it's set).
	bReloadKeyPressed	= false;
	bAmmotypeKeyPressed	= false;
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

void CWeaponMagazined::UnloadMagazine(bool spawn_ammo, u32 keep_count)
{
	xr_map<LPCSTR, u16> l_ammo;

	while(m_magazine.size() > keep_count)
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
	const bool typechange = !m_bLockType && !m_magazine.empty() &&
		(!m_pAmmo || xr_strcmp(m_pAmmo->cNameSect(), *m_magazine.back().m_ammoSect));
	// GS save_cartridge_in_ammochange: keep ONE round of the OLD ammo type as the chamber (next-to-fire) across
	// a type swap. Swap first<->last so the chambered round (back) moves to front, keep it through the unload,
	// refill with the new type, then swap back at the end so the old round returns to the back (fires first).
	// ...but NOT in grenade-launcher mode: the GL holds exactly iMagazineSize (1) grenades and has no
	// chamber, so "keep one round of the old type" kept the ONLY grenade -- the refill loop below then
	// found iAmmoElapsed already == iMagazineSize and loaded nothing, so changing the grenade type
	// silently did nothing and the launcher still held (and showed) the previous grenade. Same
	// !IsGrenadeMode() guard the ammo_in_chamber capacity tweak in OnAnimationEnd already carries.
	const bool save_chamber = typechange && m_bAmmoInChamber && m_bSaveCartridgeInAmmoChange
		&& !IsGrenadeMode() && iAmmoElapsed > 0;
	if (save_chamber)
	{
		std::swap(m_magazine.front(), m_magazine.back());
		UnloadMagazine(true, 1);	// keep the old-type chamber round (now at front)
	}
	else if (typechange)
		UnloadMagazine();			// GS PerformUnloadAmmo: a type change ejects the whole magazine (no mixed barrels)

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

	// GS ammo_in_chamber save: return the preserved OLD-type round to the chamber (back = fires next) after the
	// new-type rounds have all been loaded (incl. the recursive top-up above).
	if (save_chamber && m_magazine.size() >= 2)
		std::swap(m_magazine.front(), m_magazine.back());

	VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeaponMagazined::OnStateSwitch	(u32 S)
{
	inherited::OnStateSwitch(S);
	// an animated action (reload / med-gesture / firemode switch) interrupts the sprint idle -> forget
	// that sprint was "entered" so, when the action ends and we return to sprinting, the enter
	// transition (anm_idle_sprint_start) replays instead of snapping straight into the loop.
	if (S==eReload || S==eActionAnim || S==eFireModeSwitch)
	{
		m_bSprintStarted = false;
		// ...and drop any pending dry-fire/jam-inspect: a reload (or other action) supersedes it. Otherwise
		// the flag set by an empty-click/misfire survives the reload and the NEXT switch2_Idle (e.g. when you
		// start moving) spuriously plays anm_fakeshoot[_jammed] right after reloading.
		m_bDryFirePending = false;
	}
	switch (S)
	{
	case eIdle:
		switch2_Idle	();
		break;
	case eFire:
		switch2_Fire	();
		break;
	case eMisfire:
		// GS patches the stock jam hint OUT (WeaponEvents.pas OnJammedHintShow is an empty stub): nothing
		// is announced at the MOMENT of the jam -- the failed shot animation is the feedback. The message
		// comes later, when the player pulls the trigger on the jammed weapon (see PlayAnimDryFire).
		break;
	case eReload:
		// Reset the once-per-reload guard HERE, not in switch2_Reload: CWeaponMagazinedWGrenade's
		// grenade-mode switch2_Reload never chains to the parent, so a GL reload used to keep the flag
		// from the previous magazine reload -- the animation played and DoReloadInsert did nothing.
		m_bReloadInsertDone	= false;
		m_dwReloadInsertTm	= 0;
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


// ---- Gunslinger-style bone visibility (hud + world) ----
// Comma-separated bone list -> set them all shown/hidden. GS's SetWeaponModelBoneStatus
// (xr_BoneUtils.pas) drives BOTH models from one list: the hud model only while the weapon is the
// actor's active item, and the world model unconditionally -- so the third-person/dropped visual
// tracks the same dynamic state (ammo, scope, laser, flash, bayonet) the hud does. Silent on both, so
// a bone a given model lacks is skipped, not an assert (one config lists bones across variants).
void CWeaponMagazined::gwr_SetBones(LPCSTR csv, BOOL show)
{
	attachable_hud_item* hi = HudItemData();
	IKinematics* K = smart_cast<IKinematics*>(Visual());
	if ((!hi && !K) || !csv || !csv[0])	return;
	string256 name;
	LPCSTR p = csv;
	while (*p)
	{
		while (*p == ' ' || *p == ',')	++p;			// skip separators/space
		LPCSTR s = p;
		while (*p && *p != ',')			++p;
		u32 n = (u32)(p - s);
		while (n && s[n-1] == ' ')		--n;			// trim trailing space
		if (n && n < sizeof(name))
		{
			strncpy_s(name, sizeof(name), s, n);  name[n] = 0;
			if (hi)	hi->set_bone_visible(name, show, TRUE);
			gwr_SetWorldBone(K, name, show);
		}
	}
}

// GS AddSuffixIfStringExist (WeaponUpdate.pas:666): append the suffix only if the resulting CONFIG KEY
// exists in the section -- so a weapon that has no `_empty`/`_jammed`/GL world variant just keeps the
// base key instead of resolving to a missing motion.
void CWeaponMagazined::gwr_WorldAnimSuffix(const shared_str& sect, LPCSTR suffix, string128& anm)
{
	if (!suffix || !suffix[0])	return;
	string128 candidate;
	xr_sprintf(candidate, "%s%s", anm, suffix);
	if (pSettings->line_exist(sect, candidate))
		xr_strcpy(anm, candidate);
}

// GS ReassignWorldAnims (WeaponUpdate.pas:676). Drives the WORLD model's animation (wpn_*_animation.omf)
// from weapon state, so a holstered/dropped/NPC weapon isn't frozen. Opt-in: `use_world_anims` in the
// weapon section, plus wanm_* keys whose VALUES are motion names in that omf.
void CWeaponMagazined::gwr_UpdateWorldAnims()
{
	const shared_str& sect = cNameSect();
	if (!pSettings->line_exist(sect, "use_world_anims") || !pSettings->r_bool(sect, "use_world_anims"))	return;

	IKinematicsAnimated* KA = smart_cast<IKinematicsAnimated*>(Visual());
	if (!KA)	return;

	u32 st = GetState();
	string128 anm;
	switch (st)
	{
	case eShowing:	xr_strcpy(anm, "wanm_draw");	break;
	case eHiding:	xr_strcpy(anm, "wanm_holster");	break;
	case eFire:		xr_strcpy(anm, "wanm_shoot");	break;
	case eReload:	xr_strcpy(anm, "wanm_reload");	break;
	case eIdle:
	default:		xr_strcpy(anm, "wanm_idle");	break;
	}
	// last round in the mag gets its own shoot variant (slide locks back)
	if (st == eFire && iAmmoElapsed <= 0)		gwr_WorldAnimSuffix(sect, "_last", anm);
	// state key not configured for this weapon -> fall back to idle, like GS
	if (!pSettings->line_exist(sect, anm))		xr_strcpy(anm, "wanm_idle");

	if (IsMisfire())							gwr_WorldAnimSuffix(sect, "_jammed", anm);
	else if (iAmmoElapsed <= 0 && st != eFire)	gwr_WorldAnimSuffix(sect, "_empty", anm);
	// NOTE: GS also has a `_first` variant (IsFirstShotAnimationNeeded && IsJustAfterReload); we have no
	// equivalent flag, and since the suffix is only taken when its key exists, omitting it is harmless.

	// GS GetFiremodeSuffix (WeaponUpdate.pas:451): a LITERAL "_a" for the infinite queue, else "_<N>" --
	// NOT the hud's mask_firemode_* value (that mark is for hud aliases). Same convention as the
	// firemode_bones_<a|N> keys above.
	{
		string64 sfx;
		if (m_iQueueSize == WEAPON_ININITE_QUEUE)	xr_strcpy(sfx, "_a");
		else										xr_sprintf(sfx, "_%d", m_iQueueSize);
		gwr_WorldAnimSuffix(sect, sfx, anm);
	}

	if (IsGrenadeMode())
		gwr_WorldAnimSuffix(sect, "_g", anm);
	else if (m_eGrenadeLauncherStatus == ALife::eAddonPermanent ||
			(m_eGrenadeLauncherStatus == ALife::eAddonAttachable && IsGrenadeLauncherAttached()))
		gwr_WorldAnimSuffix(sect, "_w_gl", anm);

	if (!pSettings->line_exist(sect, anm))	return;
	shared_str motion = pSettings->r_string(sect, anm);
	if (!motion.size())						return;
	// GS gates the replay on its force-reassign flag (set on state change); equivalently, replay when the
	// resolved motion changes OR the state does -- the latter so re-entering eFire restarts the shot cycle.
	if (motion == m_sLastWorldAnim && st == m_dwLastWorldAnimState)	return;

	MotionID M = KA->ID_Cycle_Safe(*motion);
	if (!M.valid())							return;
	KA->PlayCycle(M, TRUE);
	m_sLastWorldAnim		= motion;
	m_dwLastWorldAnimState	= st;
}

void CWeaponMagazined::gwr_UpdateBones(bool force)
{
	if (!GetHUDmode())			return;			// first-person model only
	if (!HudItemData())			return;
	const shared_str& sect = HudSection();

	// ---- per-barrel ammo state, with reload phasing (GS's reload state machine, distilled) ----
	// The rounds you SEE aren't just a count of one type: EACH barrel/slot carries its own cartridge
	// with its own type (GS reads the mag vector per cartridge -- ammo_params_use_last_cartridge_type).
	// And during a reload the old rounds eject first (spent shells, old type) before fresh rounds of
	// the type being LOADED are inserted -- split at ammo_reload_insert_mark.
	// gwr_barrel_state[i] encodes slot i as (loaded ? 1 : 0) + (type<<1), or -1 for "not a slot".
	int  bstate[16];
	int  bcount = (iMagazineSize < 16) ? iMagazineSize : 16;
	// Tri-state (shotgun tube/drum) reloads insert rounds ONE AT A TIME at their own lock_time; the
	// display must follow the live magazine (each shell/type appears exactly when it's actually added),
	// NOT this single-shot reload-phasing (which flips the whole mag at ammo_reload_insert_mark -> the
	// count/type changed too early). So skip the phasing for tri-state and read the real m_magazine.
	bool reloading = (GetState() == eReload && !IsMisfire() && !IsTriStateReload());
	bool inserted  = false;
	u32  reload_type = m_ammoType;
	if (reloading)
	{
		reload_type = (m_set_next_ammoType_on_reload != u32(-1)) ? m_set_next_ammoType_on_reload : m_ammoType;
		u32 s = m_dwMotionStartTm, e = m_dwMotionEndTm, now = Device.dwTimeGlobal;
		float progress = (e > s) ? float(now - s) / float(e - s) : 1.0f;
		clamp(progress, 0.0f, 1.0f);
		// The OLD->NEW shell swap is timed by a config fraction of the reload anim (per-variant: _ammochange
		// vs plain, _1 partial vs _2 full). Before the mark: current (old) rounds shown; after: all barrels
		// the new type. (Not GS-exact -- GS restructures ReloadMagazine to defer the insert -- but works.)
		bool ammochange = !m_magazine.empty() && (m_set_next_ammoType_on_reload != u32(-1));	// pending-set (GS), not != m_ammoType: the fallback may have set m_ammoType to the new type already
		LPCSTR cnt = (m_magazine.size()==1) ? "_1" : "_2";
		string128 mk;
		float mark = 0.4f;
		xr_sprintf(mk, "ammo_reload_insert_mark%s%s", ammochange ? "_ammochange" : "", cnt);
		if (pSettings->line_exist(sect, mk))				mark = pSettings->r_float(sect, mk);
		else { xr_sprintf(mk, "ammo_reload_insert_mark%s", ammochange ? "_ammochange" : "");
			if (pSettings->line_exist(sect, mk))			mark = pSettings->r_float(sect, mk);
			else if (pSettings->line_exist(sect, "ammo_reload_insert_mark"))	mark = pSettings->r_float(sect, "ammo_reload_insert_mark"); }
		inserted = (progress >= mark);
	}
	// Which type the single-colour ammo display shows. A chamber-first pump pins the chambered (fires-next)
	// round at the back, so eff_type must be context-dependent:
	//  - DURING RELOAD: show the round being LOADED = the newest one (index size-2; the chamber sits behind
	//    it at the back). Otherwise the display freezes on the chamber's colour while you load a different type.
	//  - IDLE / FIRING: show the round that fires NEXT = the chamber = back(). So the shell that ejects on
	//    each shot matches the round actually fired (chamber first, then the tube LIFO).
	u32 last_type;
	if (m_gwr_fired_until && Device.dwTimeGlobal < m_gwr_fired_until && GetState() != eReload)
																			last_type = (u32)m_gwr_last_fired_type;					// eject window: the shell just FIRED (pop happens before the eject anim, so back() is already the next round). Any per-type-shell weapon.
	else if (m_magazine.empty())											last_type = m_ammoType;
	else if (GwrChamberAtBack() && m_magazine.size() >= 2 && GetState() == eReload)
																			last_type = (u32)m_magazine[m_magazine.size()-2].m_LocalAmmoType;	// chamber-first reload: chamber pinned at back, so the round being loaded is one before it
	else																	last_type = (u32)m_magazine.back().m_LocalAmmoType;		// idle: back() = fires-next (winchester chamber / spas12 last-loaded LIFO)

	// Per-barrel weapons (toz34/bm16): the blanket "fill every barrel with the loaded type" over-shows when
	// FEWER rounds than capacity actually load -- a single-round reload (empty gun, one shell on hand) or a
	// type change with <2 on hand. Predict the real fill: a same-type top-up keeps the loaded rounds, a type
	// change ejects them (kept=0); then only as many fresh rounds as are on hand seat -- the rest stay EMPTY
	// (the upper barrel keeps its spent-shell visual). NOTE: no mixing -- kept rounds are the same type.
	const bool per_barrel = !!READ_IF_EXISTS(pSettings, r_bool, sect, "use_per_barrel_ammo_bones", FALSE);
	int gwr_kept = 0, gwr_new = 0;
	if (per_barrel && reloading)
	{
		const bool typechange = !m_magazine.empty() && (reload_type != (u32)m_magazine.back().m_LocalAmmoType);
		gwr_kept = typechange ? 0 : (int)m_magazine.size();
		gwr_new  = GetAmmoCountByType(reload_type);
		const int room = iMagazineSize - gwr_kept;
		if (gwr_new > room)	gwr_new = room;
		if (gwr_new < 0)	gwr_new = 0;
	}
	// Spent-casing colour for the empty barrels BEFORE the insert mark. For an empty magazine last_type falls
	// back to m_ammoType -- but an ammo change (old type exhausted) or a re-selected type has already flipped
	// m_ammoType, so the casings would show the wrong colour. Track the type the magazine LAST held (updated
	// while it has rounds) so the spent casings keep it after the mag empties by firing OR unloading.
	if (!m_magazine.empty())	m_gwr_last_mag_type = (u8)m_magazine.back().m_LocalAmmoType;
	const u32 spent_type = m_magazine.empty() ? (u32)m_gwr_last_mag_type : last_type;

	// GS ammo_in_chamber: one loaded round rides in the CHAMBER, not the magazine, so the magazine bones show
	// one fewer than the total loaded -- the chamber round (back(), fires next) is excluded from the mag stack.
	// So the last remaining round reads as chambered (empty mag), and a full load shows mag_size-1 in the mag
	// + 1 chambered. Only weapons that declare ammo_in_chamber; per-barrel/tri-state weapons don't use it, and
	// grenade mode has no chamber. mag_visible(total) = rounds to actually show in the magazine.
	const int chamber = (m_bAmmoInChamber && !IsGrenadeMode() && !per_barrel) ? 1 : 0;
	auto mag_visible = [chamber](int total) -> int { return (total > chamber) ? (total - chamber) : 0; };

	for (int i = 0; i < 16; ++i)
	{
		if (i >= bcount)					{ bstate[i] = -1; continue; }
		bool loaded; u32 t;
		if (per_barrel && reloading)
		{
			// before the insert mark: the live (pre-reload) magazine; after it: the predicted final fill
			if (!inserted)
			{
				if (i < (int)m_magazine.size())	{ loaded = true;  t = (u32)m_magazine[i].m_LocalAmmoType; }
				else							{ loaded = false; t = spent_type; }	// spent casing keeps the FIRED colour, not the re-selected type
			}
			else if (i < gwr_kept)				{ loaded = true;  t = (u32)m_magazine[i].m_LocalAmmoType; }
			else if (i < gwr_kept + gwr_new)	{ loaded = true;  t = reload_type; }
			else								{ loaded = false; t = reload_type; }	// empty barrel: shell colour matches the type being loaded (blue bullet -> blue shell)
		}
		else if (reloading && inserted)			{ loaded = (i < mag_visible(iMagazineSize)); t = reload_type; }	// all filled (minus chamber), new type
		else if (i < mag_visible((int)m_magazine.size()))	{ loaded = true;  t = (u32)m_magazine[i].m_LocalAmmoType; }
		else									{ loaded = false; t = last_type; }			// spent shell keeps the current colour
		bstate[i] = (loaded ? 1 : 0) | (int(t) << 1);
	}

	// Also the flat count/type (for the single-type advanced & count modes).
	int eff_count = mag_visible((reloading && inserted) ? iMagazineSize : iAmmoElapsed);
	u32 eff_type  = (reloading && inserted) ? reload_type : last_type;

	(void)force;
	// NOTE: apply EVERY frame, not on-change. The HUD model's bone visibility is reset by the
	// per-frame bone recalc (CWeapon::UpdateCL re-applies the stock addon bones every frame for the
	// same reason), so setting it once and gating on change just gets wiped a frame later. set_bone_visible
	// no-ops when a bone is already in the wanted state, so re-applying a handful of bones is cheap.

	// Static hides/shows: hide every optional attachment bone the model carries (all the scopes,
	// mags, rails, lights this variant doesn't use), then show the base set. GS keeps these in the
	// WEAPON section (GetSection), not the hud section -- without its upgrade system, def_hide_bones
	// is what stops the model showing every attachment mesh at once.
	const shared_str& wsect = cNameSect();
	if (pSettings->line_exist(wsect, "def_hide_bones"))	gwr_SetBones(pSettings->r_string(wsect, "def_hide_bones"), FALSE);
	if (pSettings->line_exist(wsect, "def_show_bones"))	gwr_SetBones(pSettings->r_string(wsect, "def_show_bones"), TRUE);

	// Per-upgrade bone visibility (GS-style): each installed upgrade shows/hides model bones (e.g. the mag45
	// upgrade shows the extended `mag45` bone and hides `mag30`). GS keeps these in the upgrade's EFFECT
	// section (up_sect_*), reached from the installed node via its `section` key; older CS upgrades put them
	// straight on the node itself -- so read BOTH. Applied AFTER the def_* pass (installed upgrade overrides
	// the base set) in install order, so a child (mag60) can hide the bone its parent (mag45) showed. Within
	// each source: hide_bones + hide_bones_override first, then show_bones, so show wins on overlap.
	// `shown` records every bone an upgrade NAMED in show_bones. set_bone_visible ultimately calls
	// LL_SetBoneVisible(..., bRecursive = TRUE), so revealing a parent also reveals its whole subtree --
	// e.g. the winchester tactical handle shows `bolt_foregrip_rail` and drags the `flash`/`laser` device
	// bones (its children) out with it, even though those belong to the LATER flashlaser upgrade. So after
	// this pass any def_hide_bones entry that nobody named explicitly is hidden again (see below).
	// Seeded with def_show_bones: a config may list the same bone in BOTH def lists (the winchester has
	// `handler` in each) and rely on show winning, so those count as explicitly shown too -- otherwise the
	// re-hide pass below would undo def_show_bones (it hid the pistol grip). An upgrade's hide_bones still
	// wins, because that pass runs before and this one only ever hides.
	xr_vector<shared_str> shown;
	if (pSettings->line_exist(wsect, "def_show_bones"))
		gwr_CollectBoneNames(pSettings->r_string(wsect, "def_show_bones"), shown);
	for (const shared_str& up : m_upgrades)
	{
		if (!up.size())	continue;
		shared_str esect = pSettings->line_exist(up, "section") ? (shared_str)pSettings->r_string(up, "section") : up;
		const shared_str srcs[2] = { esect, up };
		for (const shared_str& s : srcs)
		{
			if (!s.size())	continue;
			if (pSettings->line_exist(s, "hide_bones"))				gwr_SetBones(pSettings->r_string(s, "hide_bones"), FALSE);
			if (pSettings->line_exist(s, "hide_bones_override"))	gwr_SetBones(pSettings->r_string(s, "hide_bones_override"), FALSE);
			if (pSettings->line_exist(s, "show_bones"))
			{
				LPCSTR csv = pSettings->r_string(s, "show_bones");
				gwr_SetBones(csv, TRUE);
				gwr_CollectBoneNames(csv, shown);
			}
		}
	}

	// Addon-conditional OVERRIDES (GS WeaponUpdate.pas:598-608). A SEPARATE PASS AFTER the loop above,
	// exactly like GS: these hide a part the upgrade itself shows -- the groza's `grip` under its
	// silencer, the l85/sig550 bayonet under theirs -- so running them inline would just be undone by
	// that same upgrade's `show_bones` two lines later (which is precisely what happened: grip stayed on).
	for (const shared_str& up : m_upgrades)
	{
		if (!up.size())	continue;
		shared_str esect = pSettings->line_exist(up, "section") ? (shared_str)pSettings->r_string(up, "section") : up;
		const shared_str srcs[2] = { esect, up };
		for (const shared_str& s : srcs)
		{
			if (!s.size())	continue;
			if (IsSilencerAttached() && pSettings->line_exist(s, "hide_bones_override_when_silencer_attached"))
				gwr_SetBones(pSettings->r_string(s, "hide_bones_override_when_silencer_attached"), FALSE);
			if (IsScopeAttached() && pSettings->line_exist(s, "hide_bones_override_when_scope_attached"))
				gwr_SetBones(pSettings->r_string(s, "hide_bones_override_when_scope_attached"), FALSE);
			if ((m_eGrenadeLauncherStatus == ALife::eAddonPermanent || IsGrenadeLauncherAttached())
				&& pSettings->line_exist(s, "hide_bones_override_when_gl_attached"))
				gwr_SetBones(pSettings->r_string(s, "hide_bones_override_when_gl_attached"), FALSE);
		}
	}

	// Undo the recursive collateral: re-hide every def_hide_bones entry that no installed upgrade actually
	// asked for. def_hide_bones is the baseline "this variant does not carry that attachment", so a bone
	// must be NAMED by an upgrade (or by the scope/device passes below) to stay visible.
	if (pSettings->line_exist(wsect, "def_hide_bones"))
	{
		xr_vector<shared_str> hide_list;
		gwr_CollectBoneNames(pSettings->r_string(wsect, "def_hide_bones"), hide_list);
		for (const shared_str& b : hide_list)
			if (std::find(shown.begin(), shown.end(), b) == shown.end())
			{
				if (HudItemData())	HudItemData()->set_bone_visible(b, FALSE, TRUE);
				gwr_SetWorldBone(smart_cast<IKinematics*>(Visual()), *b, FALSE);
			}
	}

	// GS `def_hide_bones_override_when_gl_attached` (WeaponUpdate.pas:617): parts the launcher takes the
	// place of -- the l85's handguard/handguard_rail/tac_handler. Applied after the upgrade pass, so it
	// wins over an upgrade that shows them, and only while the GL is really on (or welded on permanently).
	if ((m_eGrenadeLauncherStatus == ALife::eAddonPermanent || IsGrenadeLauncherAttached())
		&& pSettings->line_exist(wsect, "def_hide_bones_override_when_gl_attached"))
		gwr_SetBones(pSettings->r_string(wsect, "def_hide_bones_override_when_gl_attached"), FALSE);

	// ---- scope bones (Gunslinger ProcessScope): reveal the ATTACHED scope's own model bone. Every scope bone
	// starts hidden via def_hide_bones (applied above each frame), so we only SHOW the active scope's `bones`
	// (from its per-weapon section) while a scope is on -- on detach def_hide_bones re-hides it. Falls back to
	// the weapon's single scope_bones key for the legacy single-scope path.
	shared_str cur_scope = GetCurrentScopeSection();
	// Hide EVERY listed scope's bones first, then reveal only the attached one's. Relying on def_hide_bones
	// alone is not enough: it runs before the upgrade pass, and a rail upgrade that lists the scope mounts in
	// its own show_bones (winchester: `show_bones = rail, scope1, scope2, scope3, scope4`) would otherwise
	// leave every optic on the model at once.
	for (const shared_str& sc : m_scopes)
		if (sc.size() && sc != cur_scope && pSettings->line_exist(*sc, "bones"))
			gwr_SetBones(pSettings->r_string(*sc, "bones"), FALSE);

	LPCSTR scope_bones = (cur_scope.size() && pSettings->line_exist(*cur_scope, "bones"))
							? pSettings->r_string(*cur_scope, "bones")
							: (pSettings->line_exist(wsect, "scope_bones") ? pSettings->r_string(wsect, "scope_bones") : nullptr);
	if (scope_bones)
		gwr_SetBones(scope_bones, IsScopeAttached() ? TRUE : FALSE);
	else if (cur_scope.size() && !IsScopeAttached() && pSettings->line_exist(*cur_scope, "bones"))
		gwr_SetBones(pSettings->r_string(*cur_scope, "bones"), FALSE);


	// GS overriding_hide_bones / overriding_show_bones (per scope section): what the MOUNT does to the
	// rest of the weapon while this optic is on -- e.g. the l85's optics hide the carry handle + iron
	// sights and reveal the rail they sit on. Only while the scope is actually attached; on detach the
	// def_show_bones/def_hide_bones pass above restores the stock configuration.
	if (cur_scope.size() && IsScopeAttached())
	{
		if (pSettings->line_exist(*cur_scope, "overriding_hide_bones"))
			gwr_SetBones(pSettings->r_string(*cur_scope, "overriding_hide_bones"), FALSE);
		if (pSettings->line_exist(*cur_scope, "overriding_show_bones"))
			gwr_SetBones(pSettings->r_string(*cur_scope, "overriding_show_bones"), TRUE);
	}

	// GS scope reticle illumination: show the glowing-reticle bones only while the scope is attached AND the
	// illumination is on (brightness > 0), toggled by scope_brightness_plus/minus. These bones must be listed
	// in def_hide_bones so they start off. Per-scope (active section's scope_illum_bones) with weapon fallback.
	LPCSTR illum_bones = (cur_scope.size() && pSettings->line_exist(*cur_scope, "scope_illum_bones"))
							? pSettings->r_string(*cur_scope, "scope_illum_bones")
							: (pSettings->line_exist(wsect, "scope_illum_bones") ? pSettings->r_string(wsect, "scope_illum_bones") : nullptr);
	if (illum_bones)
		gwr_SetBones(illum_bones, (IsScopeAttached() && ScopeIllumValue() > 0.f) ? TRUE : FALSE);

	// laser ray bones follow the toggle AND the emission: shown by the upgrade's show_bones above; hidden here
	// when the laser is switched off, OR when an emission's electronics-problems level suppresses the dot (GS
	// ProcessLaserdot hides ray_bones together with the dot -> the whole beam disappears, not just the far dot).
	// This runs EVERY frame (see the note above), so it is the authority; CWeapon::UpdateLaserDot only owns the
	// dot particle. Uses the full ray-bone list (line, line2) with the single attach bone as a fallback.
	if (m_bLaserInstalled)
	{
		extern float g_electronics_problems;
		const float laser_lvl = READ_IF_EXISTS(pSettings, r_float, "gwr_blowout", "laser_disabling_level", 8.f);
		const bool  surge_off = m_bLaserEnabled && (laser_lvl > 0.f) && (g_electronics_problems >= laser_lvl);
		LPCSTR rb = m_sLaserRayBones.size() ? m_sLaserRayBones.c_str() : (m_sLaserBone.size() ? m_sLaserBone.c_str() : nullptr);
		if (rb && (!m_bLaserEnabled || surge_off))
			gwr_SetBones(rb, FALSE);
	}

	// GS bayonet: the blade (shown by the bayonet upgrade's show_bones) is REMOVED when a silencer or GL is
	// on the barrel -- and the unique stab is disabled with it (ActorInput falls back to the knife kick).
	// The blade's bone name is per weapon (`bayonet_bone`): the AK family calls it `knife`, the l85 `bayonet`.
	if (m_bBayonetInstalled && !IsBayonetActive())
		gwr_SetBones(m_sBayonetBone.size() ? m_sBayonetBone.c_str() : "knife", FALSE);

	// flashlight glow bone: the physical `flash` DEVICE stays visible (shown by the upgrade's show_bones);
	// the glowing lens toggles WITH the on/off state -- shown when on, hidden when off. flashlight_bone picks
	// which bone that is: default `flash` (legacy: the whole device toggled), the ak74 sets it to `light` so
	// only the lens glow appears/disappears while the device stays put.
	if (m_bFlashInstalled)
	{
		LPCSTR fb = pSettings->line_exist(cNameSect(), "flashlight_bone") ? pSettings->r_string(cNameSect(), "flashlight_bone") : "flash";
		gwr_SetBones(fb, m_bFlashEnabled ? TRUE : FALSE);
	}

	// ---- PER-BARREL ammo bones: each slot shows its own cartridge's type (double-barrels etc.) ----
	// Bone name is rebuilt as <loaded/empty prefix><type name><barrel suffix>, so mixed loads read
	// per-barrel-correctly (barrel 1 red, barrel 2 blue). Reuses GS's exact bone names via the config.
	if (READ_IF_EXISTS(pSettings, r_bool, sect, "use_per_barrel_ammo_bones", FALSE))
	{
		LPCSTR lpre = READ_IF_EXISTS(pSettings, r_string, sect, "ammo_bone_loaded_prefix", "bullet_");
		LPCSTR epre = READ_IF_EXISTS(pSettings, r_string, sect, "ammo_bone_empty_prefix",  "shell_");
		if (!lpre)	lpre = "bullet_";
		if (!epre)	epre = "shell_";
		int types = (int)m_ammoTypes.size();
		string128 tname[8], bsuf[16];
		// Colour per CURRENT ammo type. Prefer the NAME-keyed ammo_bone_type_<ammo_section>, same rule as
		// ammo_params_section_<ammo_section> below: the colour then tracks the CARTRIDGE, not its position
		// in ammo_class. An upgrade that drops a cartridge (bm16 rifled barrel removes buckshot) shifts the
		// positional indices, which would otherwise recolour the survivors. Positional is the fallback.
		for (int t = 0; t < types && t < 8; ++t)
		{
			LPCSTR v = NULL;
			string128 k;
			strconcat(sizeof(k), k, "ammo_bone_type_", *m_ammoTypes[t]);
			if (pSettings->line_exist(sect, k))		v = pSettings->r_string(sect, k);
			if (!v)
			{
				string64 kp; xr_sprintf(kp, "ammo_bone_type_%d", t);
				// an empty ltx value ("key =") reads back as NULL, so guard it -- xr_strcpy(NULL) is the crash
				v = READ_IF_EXISTS(pSettings, r_string, sect, kp, "");
			}
			xr_strcpy(tname[t], v ? v : "");
		}
		for (int b = 0; b < bcount; ++b)
		{
			string64 k; xr_sprintf(k, "ammo_bone_barrel_%d", b);
			LPCSTR v = READ_IF_EXISTS(pSettings, r_string, sect, k, "");	// "" for the no-suffix first barrel
			xr_strcpy(bsuf[b], v ? v : "");
		}
		// The hide pass must cover EVERY colour the config declares, not just the ones this weapon currently
		// accepts: when an upgrade drops a cartridge, the bones of the dropped one would never be hidden and
		// every colour would show at once. So collect the declared set -- all positional slots (scanned
		// unconditionally, NOT bounded by `types`) plus the colours resolved for the current types.
		string128 hname[24];
		int hcount = 0;
		for (int pass = 0; pass < 2; ++pass)
			for (int t = 0; t < 8; ++t)
			{
				LPCSTR v;
				if (pass == 0)
				{
					string64 kp; xr_sprintf(kp, "ammo_bone_type_%d", t);
					v = READ_IF_EXISTS(pSettings, r_string, sect, kp, "");
				}
				else
					v = (t < types) ? tname[t] : "";
				if (!v || !v[0] || hcount >= 24)	continue;
				bool dup = false;
				for (int i = 0; i < hcount; ++i)
					if (0 == xr_strcmp(hname[i], v))	{ dup = true; break; }
				if (!dup)	xr_strcpy(hname[hcount++], v);
			}
		// hide every loaded+empty bone of every declared colour in every barrel, then show what each slot wants
		string256 nm;
		for (int b = 0; b < bcount; ++b)
			for (int i = 0; i < hcount; ++i)
			{
				xr_sprintf(nm, "%s%s%s", lpre, hname[i], bsuf[b]);	HudItemData()->set_bone_visible(nm, FALSE, TRUE);
				xr_sprintf(nm, "%s%s%s", epre, hname[i], bsuf[b]);	HudItemData()->set_bone_visible(nm, FALSE, TRUE);
			}
		for (int b = 0; b < bcount; ++b)
		{
			if (bstate[b] < 0)	continue;
			bool loaded = (bstate[b] & 1) != 0;
			int  t = bstate[b] >> 1;
			if (t < 0 || t >= types || t >= 8 || !tname[t][0])	continue;
			xr_sprintf(nm, "%s%s%s", loaded ? lpre : epre, tname[t], bsuf[b]);
			HudItemData()->set_bone_visible(nm, TRUE, TRUE);
		}
	}
	// ---- ammo TYPE bones: a named configuration of bones per (ammo type, round count) ----
	// hud[ammo_params_section_<type>] (or the generic ammo_params_section) -> a section with
	// all_bones (hidden first) + configuration_<count> (shown). Different bullet meshes per ammo.
	else if (READ_IF_EXISTS(pSettings, r_bool, sect, "use_advanced_ammo_bones", FALSE))
	{
		int cnt = eff_count;					// reload-phased (see the effective-state block above)
		LPCSTR bsect = NULL;
		string128 key;
		// Prefer a NAME-keyed section (ammo_params_section_<ammo_section>) so the shell colour tracks the
		// ammo TYPE, not its position in ammo_class -- an upgrade that drops a cartridge (e.g. barrel-mod
		// removes buckshot) shifts the positional indices and would otherwise recolour the survivors.
		if (eff_type < (u32)m_ammoTypes.size())
		{
			strconcat(sizeof(key), key, "ammo_params_section_", *m_ammoTypes[eff_type]);
			if (pSettings->line_exist(sect, key))			bsect = pSettings->r_string(sect, key);
		}
		if (!bsect)
		{
			xr_sprintf(key, "ammo_params_section_%d", eff_type);		// positional fallback (legacy)
			if (pSettings->line_exist(sect, key))			bsect = pSettings->r_string(sect, key);
			else if (pSettings->line_exist(sect, "ammo_params_section"))	bsect = pSettings->r_string(sect, "ammo_params_section");
		}
		if (bsect)
		{
			if (IsMisfire() && READ_IF_EXISTS(pSettings, r_bool, bsect, "additional_ammo_bone_when_jammed", FALSE))	++cnt;
			if (pSettings->line_exist(bsect, "all_bones"))	gwr_SetBones(pSettings->r_string(bsect, "all_bones"), FALSE);
			// Drum/tube "closing round": visible ONLY when the mag is completely full. The full count
			// varies with the mag-capacity upgrade, so a dedicated configuration_full covers any size
			// instead of hard-coding configuration_<N>. Falls back to configuration_<count> otherwise.
			if (eff_count >= iMagazineSize && pSettings->line_exist(bsect, "configuration_full"))
				gwr_SetBones(pSettings->r_string(bsect, "configuration_full"), TRUE);
			else
			{
				xr_sprintf(key, "configuration_%d", cnt);
				if (pSettings->line_exist(bsect, key))		gwr_SetBones(pSettings->r_string(bsect, key), TRUE);
			}
		}
	}
	// ---- ammo COUNT bones: one bone per round in the mag ----
	else if (READ_IF_EXISTS(pSettings, r_bool, sect, "use_ammo_bones", FALSE))
	{
		LPCSTR pre  = READ_IF_EXISTS(pSettings, r_string, sect, "ammo_bones_prefix", "");
		LPCSTR preH = READ_IF_EXISTS(pSettings, r_string, sect, "ammo_hide_bones_prefix", "");
		LPCSTR preV = READ_IF_EXISTS(pSettings, r_string, sect, "ammo_var_bones_prefix", "");
		int start = READ_IF_EXISTS(pSettings, r_s32, sect, "start_ammo_bone_index", 0);
		int last  = READ_IF_EXISTS(pSettings, r_s32, sect, "end_ammo_bone_index", 0);
		int fin   = start + eff_count - 1;		// reload-phased count
		if (IsMisfire() && READ_IF_EXISTS(pSettings, r_bool, sect, "additional_ammo_bone_when_jammed", FALSE))	++fin;
		if (pSettings->line_exist(sect, "ammo_divisor_up"))
			fin = start + (int)ceilf(float(fin - start + 1) / pSettings->r_s32(sect, "ammo_divisor_up")) - 1;
		else if (pSettings->line_exist(sect, "ammo_divisor_down"))
			fin = start + (int)floorf(float(fin - start + 1) / pSettings->r_s32(sect, "ammo_divisor_down")) - 1;
		if (fin > last)	fin = last;

		string256 nm;
		for (int i = start; i <= last; ++i)
		{
			bool on = (i <= fin);
			if (pre[0])  { xr_sprintf(nm, "%s%d", pre,  i); HudItemData()->set_bone_visible(nm, on,  TRUE); }
			if (preH[0]) { xr_sprintf(nm, "%s%d", preH, i); HudItemData()->set_bone_visible(nm, !on, TRUE); }
		}
		if (preV[0])
			for (int i = start - 1; i <= last; ++i) { xr_sprintf(nm, "%s%d", preV, i); HudItemData()->set_bone_visible(nm, (i == fin), TRUE); }
	}

	// ---- firemode selector bones ----
	if (pSettings->line_exist(sect, "firemode_bones_total"))
	{
		gwr_SetBones(pSettings->r_string(sect, "firemode_bones_total"), FALSE);
		string128 key;
		if (m_iQueueSize == WEAPON_ININITE_QUEUE)	xr_strcpy(key, "firemode_bones_a");
		else										xr_sprintf(key, "firemode_bones_%d", m_iQueueSize);
		if (pSettings->line_exist(sect, key))	gwr_SetBones(pSettings->r_string(sect, key), TRUE);
	}

	gwr_UpdateBonesGL();	// GL/grenade weapons: show the loaded grenade's bone (no-op otherwise)
}

void CWeaponMagazined::UpdateCL			()
{
	inherited::UpdateCL	();
	float dt = Device.fTimeDelta;

	gwr_UpdateBones();		// show/hide HUD-model bones for ammo count / type / firemode (on change)
	gwr_UpdateWorldAnims();	// GS ReassignWorldAnims: drive the world model from wpn_*_animation.omf (opt-in)

	// The idle animation is chosen ONCE, when the idle starts, so a magazine emptied from the outside
	// (unloaded at a technician / in the inventory / an upgrade that unloads) kept the loaded idle on
	// screen until whatever was playing ran out. Re-pick it the moment the loaded/empty state flips,
	// while the weapon is genuinely idle and owns no action -- the same test switch2_Idle would make.
	{
		const bool empty_now = NeedEmptyAnim();
		if (empty_now != m_bLastEmptyAnim)
		{
			m_bLastEmptyAnim = empty_now;
			if (GetState() == eIdle && GetNextState() == eIdle && !IsPending()
				&& !m_bDryFirePending && !m_bDryFirePlaying && !m_bLightMisfirePlaying)
				PlayAnimIdle();
		}
	}

	// aim-lock auto-shoot: the fire lock just ended and the player pressed fire DURING it with autoshoot
	// enabled -> fire now, so a shot queued inside the transition comes out on its own. Opt-in per weapon
	// (m_bAimLockAutoShoot). Keyed off m_bAimLockFirePressed (a fresh press captured in FireStart during
	// the lock), NOT the sticky m_bTriggerHeld -- that lingered true on pistols/shotguns/SVD and made them
	// self-fire on every aim in/out.
	if (m_dwAimFireLockTm && Device.dwTimeGlobal >= m_dwAimFireLockTm)
	{
		// Only auto-shoot a REAL round: never let it produce a jam dry-fire or an empty click by itself
		// (the "AK74 plays a dry anim on its own" bug), and never self-launch a grenade -- FireStart()
		// here is the base bullet path, wrong in grenade mode.
		bool auto_fire = m_bAimLockAutoShoot && m_bAimLockFirePressed
			&& !IsMisfire() && iAmmoElapsed > 0 && !IsGrenadeMode();
		m_dwAimFireLockTm		= 0;
		m_bAimLockAutoShoot		= false;
		m_bAimLockFirePressed	= false;
		if (auto_fire && GetState()==eIdle)
			FireStart();
	}

	// Held-aim resume: a shot on a slow-firing gun blocks aim-in (Weapon.cpp kWPN_ZOOM needs !IsPending),
	// so pressing aim mid-shot was dropped and you had to release + re-press after the shot. Instead, if
	// the aim key is STILL physically held and the weapon is idle again, enter ADS now. Not auto-aim --
	// gated on the physical key (m_bZoomKeyHeld, cleared on release).
	if (m_bZoomKeyHeld && IsZoomEnabled() && !IsZoomed() && !IsPending()
		&& GetState()==eIdle && !IsJamInspectPlaying())
	{
		OnZoomIn();
	}

	// aim in/out transition handoff fallback: when its OnAnimationEnd didn't fire on time
	// (unreliable while moving), force the switch to the aim idle / moving idle at the
	// transition's wall-clock deadline so the one-shot transition can't stick/loop.
	if(m_dwAimTransitionEndTm && Device.dwTimeGlobal >= m_dwAimTransitionEndTm)
	{
		m_bIdleTransitionLock	= false;
		m_dwAimTransitionEndTm	= 0;
		if(GetState()==eIdle)
			// switch2_Idle, NOT PlayAnimIdle: switch2_Idle's own head early-returns while this
			// transition is running, so anything it defers (m_bDryFirePending / m_bAimIn|OutPending /
			// the shoot-anim tail) is still queued here. Going straight to the idle dropped those on
			// the floor -- a jam taken WHILE the aim-out transition played left m_bDryFirePending set
			// forever, and FireStart (the `(m_bDryFirePending || m_bDryFirePlaying) && IsMisfire()`
			// gate) then ignored every trigger pull until the weapon was re-drawn (the show anim's
			// switch2_Idle finally consumed it). The timer is already cleared above, so the re-entry
			// cannot hit that guard again, and switch2_Idle ends in PlayAnimIdle anyway.
			switch2_Idle();
			// NOTE: no auto-resume of fire here. A trigger held through the aim transition must
			// NEVER make the weapon fire by itself (user requirement, all weapons) - the player
			// has to release and press fire again. (Previously resumed continuous-auto here, which
			// caused a stray shot after aim in/out, esp. right after clearing a jam.)
	}

	// GS lock_time_start_<reload alias>: seat the rounds mid-animation (see ArmReloadLockTimes) so the
	// ammo bones show the loaded round exactly when the hands put it in, not a second later at the end.
	if (m_dwReloadInsertTm && Device.dwTimeGlobal >= m_dwReloadInsertTm)
	{
		m_dwReloadInsertTm = 0;
		if (GetState() == eReload)
			DoReloadInsert();
	}

	// shoot-anim -> idle handoff: switch2_Idle deferred the idle so a longer shoot variant (de_shoot2)
	// could finish; now that its anim has ended, play the idle (re-runs switch2_Idle so any aim-out /
	// dry-fire pending is honoured too). Self-expiring, so the weapon can never stick on the last frame.
	if (m_dwShootAnimEndTm && Device.dwTimeGlobal >= m_dwShootAnimEndTm)
	{
		m_dwShootAnimEndTm = 0;
		if (GetState()==eIdle && GetNextState()==eIdle)
			switch2_Idle();
	}

	// light-misfire strike ended: replay the aim press/release that was blocked during it (task). The
	// strike owns the hands via m_bLightMisfirePlaying; once it clears (OnAnimationEnd) we resume the
	// deferred intent, mirroring the sprint-exit aim handoff above.
	if (m_bZoomPendingMisfire && !m_bLightMisfirePlaying
		&& GetState()==eIdle && GetNextState()==eIdle)
	{
		m_bZoomPendingMisfire = false;
		if (m_bZoomPendingMisfireIn)	{ if (!IsZoomed())	OnZoomIn(); }
		else							{ if (IsZoomed())	OnZoomOut(); }
	}

	// sprint-exit handoff: fire/aim pressed during sprint played the sprint-out anim; when it's
	// (almost) done, resume the deferred action - fire if the trigger is still held, aim if the aim
	// press is still pending. Unlike the aim-transition handoff this IS user-requested (the player
	// actively pressed fire/aim to leave sprint), so it may resume the action.
	if (m_dwSprintExitEndTm && Device.dwTimeGlobal >= m_dwSprintExitEndTm && !IsActorSprinting())
	{
		m_dwSprintExitEndTm = 0;
		if (m_bZoomPendingSprint)
		{
			m_bZoomPendingSprint = false;
			if (!IsZoomed())	OnZoomIn();
		}
		else if (m_bFirePendingSprint && GetState()==eIdle)
		{
			m_bFirePendingSprint = false;
			FireStart();
		}
		else
			m_bFirePendingSprint = false;
	}

	// detector draw phase 2, fired just before anm_prepare_detector's static tail would show
	if (m_dwDetectorShowTm && Device.dwTimeGlobal >= m_dwDetectorShowTm)
		FireDetectorShow();

	// Sample the auto selector pose here (NOT at attach): only once the bone callback has run during a
	// real render (m_selector_model_warm), so the forced CalculateBones reads the actually-posed bone.
	// This fixes the "first weapon spawned has a shifted selector" case — the freshly pooled HUD model
	// is cold at attach, so an attach-time sample committed a wrong-but-sane pose that then stuck.
	// ...and only at a fully settled idle: the sample force-plays anm_firemode_1_to_a for one calc,
	// which would visibly disrupt an in-progress firemode-switch gesture (the selector "clicks"/jumps).
	// If a switch happens first it captures a valid pose anyway, so gating to idle loses nothing.
	if (m_fire_selector_cb && m_selector_model_warm && IsAutoFireMode() && !m_fire_selector_valid
		&& m_fire_mode_bone.size() && m_selector_sample_tries < 60
		&& GetState()==eIdle && GetNextState()==eIdle && !m_fire_selector_capturing)
	{
		++m_selector_sample_tries;
		SampleFireSelectorAutoPose();
	}

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
	if (m_sounds.FindSoundItem("sndReloadMisDet", false))
		m_sounds.SetPosition("sndReloadMisDet", P);
	if (m_sounds.FindSoundItem("sndReloadMisLast", false))
		m_sounds.SetPosition("sndReloadMisLast", P);
	if (m_sounds.FindSoundItem("sndReloadMisLastDet", false))
		m_sounds.SetPosition("sndReloadMisLastDet", P);
}

// One question for "is firing locked right now?" (GS SetShootLockTime). Folds the base plain shoot-lock
// (CHudItem::SetShootLock) together with this weapon's bespoke fire-lock deadlines: the aim in/out lock
// (m_dwAimFireLockTm, shot allowed to cut the transition after lock_time) and the sprint-exit lock
// (m_dwSprintExitEndTm, fire deferred until the sprint-out anim is nearly done). Each keeps its own
// deadline because their resume/defer differs (aim autoshoot vs sprint handoff) -- only the query unifies.
bool CWeaponMagazined::IsShootLocked() const
{
	if (inherited::IsShootLocked())	return true;
	if (m_dwAimFireLockTm   && Device.dwTimeGlobal < m_dwAimFireLockTm)		return true;
	if (m_dwSprintExitEndTm && Device.dwTimeGlobal < m_dwSprintExitEndTm)	return true;
	return false;
}

bool CWeaponMagazined::gwr_TryLightMisfire()
{
	LPCSTR sect = cNameSect().c_str();
	if (!READ_IF_EXISTS(pSettings, r_bool, sect, "use_light_misfire", FALSE))	return false;

	// GS disable_light_misfires_with_detector (WeaponEvents.pas:843): some pistols (GS p99/glock17/fiveseven/
	// gsh18) suppress the light strike entirely while a detector is out in the off-hand -- the one-hand jerk
	// gesture reads badly next to the held detector. Per-weapon opt-in HUD flag; gated on a detector actually
	// being out (DetectorCompanionOut). Of our light-misfire pistols only the walther sets it (matches GS).
	if (DetectorCompanionOut()
		&& READ_IF_EXISTS(pSettings, r_bool, HudSection(), "disable_light_misfires_with_detector", FALSE))
		return false;

	// GS WeaponEvents.pas light-misfire probability -- its OWN condition curve, separate from the jam misfire.
	const float sc   = READ_IF_EXISTS(pSettings, r_float, sect, "light_misfire_start_condition",    1.f);
	const float ec   = READ_IF_EXISTS(pSettings, r_float, sect, "light_misfire_end_condition",      0.f);
	const float sp   = READ_IF_EXISTS(pSettings, r_float, sect, "light_misfire_start_probability",  1.f);
	const float ep   = READ_IF_EXISTS(pSettings, r_float, sect, "light_misfire_end_probability",    0.f);
	const float cond = GetCondition();
	float prob;
	if      (cond < ec)		prob = ep;
	else if (cond > sc)		prob = 0.f;
	else if (sc > ec)		prob = ep + cond * (sp - ep) / (sc - ec);	// GS formula, verbatim
	else					prob = 0.f;

	// GS GATE (the important part): OnWeaponJam -- and therefore the light strike -- runs ONLY once a jam has
	// actually been rolled. A light misfire is a FRACTION of jams, not an independent per-shot event: the jam
	// probability (misfire_start/end_prob, ~0.007-0.05) is low, and OF those jams ~sp..ep become light strikes.
	// So scale by the jam probability (the very GetConditionMisfireProbability the post-shot CheckForMisfire
	// rolls). Without this the strike rolled its own high curve (0.4-0.9) EVERY shot -> far more frequent than
	// in GS. Effective rate now = P_jam * P_light, matching GS. (Real jams still roll independently post-shot.)
	prob *= GetConditionMisfireProbability();
	if (prob <= 0.f || ::Random.randF(1.f) >= prob)	return false;		// no light misfire this trigger pull

	// LIGHT MISFIRE (light strike): no bullet, no jam (bMisfire untouched). Play anm_shoot_lightmisfire
	// (GS: _aim when zoomed, +_scope with an optic; PlayHUDMotion still adds firemode suffixes) + the click
	// sound. The round stays chambered -> the player just pulls the trigger again.
	string_path anim;	xr_strcpy(anim, "anm_shoot_lightmisfire");
	if (IsZoomed())
	{
		string_path aim;	xr_sprintf(aim, "%s_aim", anim);
		shared_str sc2 = GetCurrentScopeSection();
		if (IsScopeAttached() && sc2.size())
		{
			string_path scp;	xr_sprintf(scp, "%s_scope", aim);
			if (isHUDAnimationExist(scp))	xr_strcpy(aim, scp);
		}
		if (isHUDAnimationExist(aim))	xr_strcpy(anim, aim);
	}
	if (isHUDAnimationExist(anim))
	{
		// Block re-firing until the strike gesture ends. This MUST be the shoot-lock DEADLINE, not a pending
		// flag: state_Fire calls StopShooting() right after this, and StopShooting -> SwitchState(eIdle) ->
		// switch2_Idle clears SetPending a frame later (deferred net event), so a pending flag never survives
		// the strike. The deadline is immune -- nothing clears it, it just expires with the anim. FireStart
		// gates on CHudItem::IsShootLocked(). Not a jam (bMisfire stays false) so the idle returns normally.
		m_bLightMisfirePlaying = true;
		u32 t = PlayHUDMotion(anim, TRUE, this, eIdle);	// returns the motion length (ms)
		// GS locks re-fire for lock_time_<anim> (config, seconds -- PM/PB use 1.0), NOT the whole anim; fall
		// back to the anim length when the key is absent. Matches GS MakeLockByConfigParam(lock_time_+anim).
		string128 lk;	xr_sprintf(lk, "lock_time_%s", anim);
		if (pSettings->line_exist(HudSection(), lk))
			t = u32(pSettings->r_float(HudSection(), lk) * 1000.0f);
		SetShootLock(t);
	}
	if (m_sounds.FindSoundItem("sndLightMisfire", false))
		PlaySound("sndLightMisfire", get_LastFP());
	return true;
}

// GS snd_kick -- the bayonet stab's sound, played when the stab anim starts (ActorInput's quick-kick
// branch). Silent no-op for a weapon without the key, so nothing else changes.
void CWeaponMagazined::PlayKickSound()
{
	if (m_sounds.FindSoundItem("sndKick", false))
		PlaySound("sndKick", get_LastFP());
}

// Interval to the next round -- Gunslinger's AN94_RPM_Patch (AN94Patch.pas), which hooks the
// engine right where it loads fOneShotTime and can substitute two config values:
//   * single-shot mode      -> singleshoots_time_delta (that weapon's own single-fire rate)
//   * inside a queue        -> base_dispersioned_bullets_time_delta, but only while fewer than
//                              (count - 1) rounds have been fired, i.e. with count = 2 only the
//                              very first shot -> exactly ONE hyper-fast interval, then rpm again.
// GS compares the queue size UNSIGNED, so auto (-1) takes the queue branch too: the first rounds
// of any burst are hyper-fast, exactly like the real AN-94.
float CWeaponMagazined::CurrentShotTimeDelta() const
{
	if (m_iQueueSize >= 0 && m_iQueueSize <= 1)
		return (m_fSingleShootsTimeDelta > 0.0f) ? m_fSingleShootsTimeDelta : fOneShotTime;

	if (m_iBaseDispersionedBulletsCount > 0 && m_fBaseDispersionedBulletsTimeDelta > 0.0f &&
		m_iShotNum < m_iBaseDispersionedBulletsCount - 1)
		return m_fBaseDispersionedBulletsTimeDelta;

	return fOneShotTime;
}

void CWeaponMagazined::FireBullet(const Fvector& pos, const Fvector& dir, float fire_disp,
								  const CCartridge& cartridge, u16 parent_id, u16 weapon_id, bool send_hit)
{
	// The hyperburst rounds get their own muzzle velocity (base_dispersioned_bullets_speed).
	// Swap-and-restore around the single call instead of vanilla's save/restore pair, which never
	// restored when the queue ended exactly on the last fast round (2-round cut-off with count=2).
	const float saved_speed = m_fStartBulletSpeed;
	if (InBaseDispersionedBurst() && m_fBaseDispersionedBulletsSpeed > 0.0f)
		m_fStartBulletSpeed = m_fBaseDispersionedBulletsSpeed;

	inherited::FireBullet(pos, dir, fire_disp, cartridge, parent_id, weapon_id, send_hit);

	m_fStartBulletSpeed = saved_speed;
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
				(m_iQueueSize<0 || m_iShotNum<m_iQueueSize) &&
				// GS max_queue_size: a hard cap on rounds per trigger pull, independent of the fire
				// mode. The gauss's second "mode" is really the MUI toggle (fire_modes = 1, -1), and
				// GS keeps it from firing full auto with max_queue_size = 1. 0/absent = no cap.
				(m_iMaxQueueSize<=0 || m_iShotNum<m_iMaxQueueSize)
			   )
		{
			m_bFireSingleShot		= false;

			// GS AN94_RPM_Patch: the delay to the NEXT round is not always 60/rpm -- see
			// CurrentShotTimeDelta. m_iShotNum is still the count of rounds already fired here,
			// which is exactly what the GS patch compares against.
			fShotTimeCounter		+=	CurrentShotTimeDelta();

			// GS recharge_time: the capacitor charge the gauss needs between shots (base.ltx: 3 s, cut
			// by the fast_conders / ionistori upgrades). It is a floor on the gap to the next round, so
			// a weapon with an rpm-derived delta shorter than the charge waits for the charge instead.
			if (m_fRechargeTime > 0.f && fShotTimeCounter < m_fRechargeTime)
				fShotTimeCounter	=	m_fRechargeTime;

			++m_iShotNum;

			// GS light misfire: a light strike BEFORE the shot -- no bullet fired, no jam, play the click
			// gesture and stop; the round stays chambered so the player just pulls again.
			if (gwr_TryLightMisfire())
			{
				m_dwShootAnimEndTm	= m_dwMotionEndTm;	// let the light-misfire anim finish before the idle
				StopShooting		();
				return;
			}

			// GS no_jam_fire: the jam is rolled BEFORE the shot on these weapons, so the round is NOT
			// spent -- the trigger falls on a dud. Same probability model (CheckForMisfire), same eMisfire
			// -> jammed idle flow; only the timing differs, and the post-shot roll below is skipped so the
			// chance isn't doubled. GS additionally skips FireEnd here (CheckForMisfire calls it), which
			// on a break-action is invisible: one pull = one hammer fall either way.
			if (m_bNoJamFire && CheckForMisfire())
			{
				if (PlayJammedShootAnim())
				{
					m_dwShootAnimEndTm	= m_dwMotionEndTm;
					if (m_sounds.FindSoundItem("sndJam", false))
						PlaySound("sndJam", get_LastFP());
				}
				else
					m_dwShootAnimEndTm	= 0;
				StopShooting			();
				return;
			}

			OnShot					();
			// remember when this shot's anim ends so switch2_Idle won't clip it (GS: let anm_shoot*
			// finish). Set HERE, not in OnShot -- CWeaponPistol/etc. override OnShot without calling
			// inherited, but state_Fire is the shared fire loop. OnShot -> PlayAnimShoot -> PlayHUDMotion
			// just set m_dwMotionEndTm to the (randomly picked) shot variant's end.
			m_dwShootAnimEndTm		= m_dwMotionEndTm;

			// The hyperburst rounds all fly from the aim point captured when the queue started
			// (vanilla SoC gated this on base_dispersioned_bullets_count, CS on dispersion_start --
			// take whichever covers more rounds, so neither behaviour is lost).
			if (m_iShotNum > _max(m_iShootEffectorStart, m_iBaseDispersionedBulletsCount))
				FireTrace		(p1,d);
			else
				FireTrace		(m_vStartPos, m_vStartDir);

			// Ammo bones the instant the round is spent (FireTrace just decremented iAmmoElapsed):
			// the fired barrel's live-round bone flips to the empty shell now, on the shot frame,
			// instead of a frame later when UpdateCL's change-detection would otherwise catch it.
			// This is Gunslinger's forced ProcessAmmo right after the shot (WeaponAnims), same idea.
			gwr_UpdateBones			(true);

			// jam (misfire) can only occur AFTER a shot has actually been fired -
			// the fired round leaves the barrel, then the action jams (stovepipe /
			// failure-to-eject). Rolled here (post-shot) instead of before the shot so
			// a fresh trigger pull never jams in place of firing. Skip the roll when the
			// shot emptied the magazine (nothing left to chamber -> just empty, not jammed).
			if( !m_bNoJamFire && !m_magazine.empty() && CheckForMisfire() )
			{
				// GS OnWeaponJam + anm_shots_selector's "_jammed" modifier: the shot that jams has its OWN
				// animation (the case caught in the ejection port) and it plays to the END -- the jammed
				// idle only takes over afterwards. CheckForMisfire already set bMisfire, so re-assigning
				// the shoot motion now resolves to the _jammed variant (GS does the same through
				// SetAnimForceReassignStatus). Weapons with no jammed shot variant keep the old behaviour:
				// drop the deadline so switch2_Idle shows the jammed idle at once.
				if (PlayJammedShootAnim())
				{
					m_dwShootAnimEndTm	= m_dwMotionEndTm;
					if (m_sounds.FindSoundItem("sndJam", false))
						PlaySound("sndJam", get_LastFP());
				}
				else
					m_dwShootAnimEndTm = 0;
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
	// The jam itself is announced by the jamming shot (snd_jam + anm_shoot_jammed, armed in state_Fire).
	// The jammed CLICK belongs to a trigger pull on an already-jammed gun (GS OnWeaponJam), so only fall
	// back to it when the weapon has no snd_jam.
	if (!m_sounds.FindSoundItem("sndJam", false))
		OnEmptyClick		();
	// bMisfire BEFORE the state switch so switch2_Idle/PlayAnimIdle already sees the jam and picks the
	// jammed idle. m_dwShootAnimEndTm is deliberately NOT cleared here: the jamming shot's own animation
	// (anm_shoot_jammed) has to play to its end first, exactly like GS -- switch2_Idle waits for that
	// deadline and only then commits to the jammed idle. state_Fire already zeroes it for weapons that
	// have no jammed shoot variant, so those still show the jam at once.
	bMisfire				= true;
	SwitchState				(eIdle);

	UpdateSounds			();
}

void CWeaponMagazined::SetDefaults	()
{
	CWeapon::SetDefaults		();
}


void CWeaponMagazined::OnShot()
{
	// remember the round we're about to fire (still at the back; FireTrace pops it AFTER OnShot). Its type
	// colours the ejecting shell for the eject/pump window, so a chamber-first pump ejects the fired shell's
	// colour and not the next chambered round's.
	if (!m_magazine.empty())
	{
		m_gwr_last_fired_type = (u8)m_magazine.back().m_LocalAmmoType;
		m_gwr_fired_until     = Device.dwTimeGlobal + 600;
	}

	// Sound
	PlaySound					(m_sSndShotCurrent.c_str(), get_LastFP());

	// pump/bolt rack (GS breechblock) -- layered on the shot for weapons that define snd_breechblock
	if (m_sounds.FindSoundItem("sndBreechblock", false))
		PlaySound				("sndBreechblock", get_LastFP());

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
	// GS OnEmptyClick: a JAM (misfire/клин) is NOT an empty magazine -- it must not play the empty-mag click.
	// Play the dedicated jammed-click (snd_jammed_click) if the weapon has one, else stay silent (GS pistols
	// do this: the anm_shoot_jammed/anm_fakeshoot gesture is the feedback). Only a truly empty mag clicks.
	if (IsMisfire())
	{
		if (m_sounds.FindSoundItem("sndJammedClick", false))
			PlaySound("sndJammedClick", get_LastFP());
		return;
	}
	PlaySound	("sndEmptyClick",get_LastFP());
}

// dry-fire gesture on an empty/jammed trigger pull. aim: anm_dry_aim[_empty]; hip:
// anm_dry_empty (empty) / anm_dry (jammed). GL subclass overrides for _w_gl/_g.
// JAMMED WINS OVER EMPTY (GS OnEmptyClick, WeaponEvents.pas:957-971): GS hands the selector the BARE
// anm_fakeshoot[_aim] in both cases -- the two branches differ only in the sound/message -- and lets
// ModifierStd pick the state token, where jammed outranks empty. Picking "_empty" here ourselves broke
// that: with a jammed AND empty gun the candidate anm_fakeshoot_jammed_empty does not exist, so
// PlayHUDMotion fell back to the _empty alias and the jam pose snapped to the empty (slide-locked) one.
void CWeaponMagazined::SelectDryFireAnim(string_path& result)
{
	bool empty = (iAmmoElapsed == 0) && !NeedJammedAnim();
	if (IsZoomed())
	{
		if (empty && isHUDAnimationExist("anm_fakeshoot_aim_empty"))	{ xr_strcpy(result, "anm_fakeshoot_aim_empty"); return; }
		if (isHUDAnimationExist("anm_fakeshoot_aim"))					{ xr_strcpy(result, "anm_fakeshoot_aim"); return; }
	}
	if (empty && isHUDAnimationExist("anm_fakeshoot_empty"))			{ xr_strcpy(result, "anm_fakeshoot_empty"); return; }
	xr_strcpy(result, isHUDAnimationExist("anm_fakeshoot") ? "anm_fakeshoot" : "");
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
		// ...but if the motion doesn't actually run (0 length), no OnAnimationEnd will ever arrive to
		// clear the flag+pending -> the weapon would be frozen with no way to even reload out of the jam.
		if (0 == PlayHUDMotion(anim, TRUE, this, eIdle))
		{
			m_bDryFirePlaying = false;
			SetPending(FALSE);
			PlayAnimIdle();
		}
		// GS OnEmptyClick (WeaponEvents.pas:945): the "weapon jammed" line is sent ONLY once the click
		// gesture actually started, and only for the actor's own weapon -- Messenger.SendMessage
		// ('gunsl_msg_weapon_jammed', gd_novice), which is GS's small message line, not the stock
		// centred hint the engine used to throw at the moment of the jam.
		else if (IsMisfire() && smart_cast<CActor*>(H_Parent()) && Level().CurrentViewEntity() == H_Parent())
			HUD().GetUI()->AddInfoMessage("gun_jammed");
	}
	else
		PlayAnimIdle();
}

void CWeaponMagazined::OnAnimationEnd(u32 state)
{
	const bool was_dryfire = m_bDryFirePlaying;
	const bool was_lightmisfire = m_bLightMisfirePlaying;
	m_bDryFirePlaying = false;	// the dry-fire (or whatever replaced it) has ended -> allow fire again
	m_bLightMisfirePlaying = false;
	// The JAM inspect / dry-fire / light-misfire plays with state eIdle and SetPending(TRUE) to own the hands.
	// There is no eIdle case below, so that pending was never cleared -> the weapon froze (no input, couldn't
	// even reload to unjam) until re-drawn. Route these back through switch2_Idle (as the design intended:
	// "switch2_Idle clears pending when it ends") so it clears pending + plays the (jammed / normal) idle.
	if ((was_dryfire || was_lightmisfire) && state == eIdle)
	{
		switch2_Idle();
		return;
	}
	switch(state)
	{
		case eReload:
		{
			if (!IsTriStateReload())
			{
				bReloadKeyPressed = false;
				bAmmotypeKeyPressed = false;
			}

			DoReloadInsert();		// no-op when the lock_time_start timer already did it
			SwitchState(eIdle);
		}break;	// End of reload animation
		case eHiding:	SwitchState(eHidden);   break;	// End of Hide
		case eShowing:	SwitchState(eIdle);		break;	// End of Show
		case eActionAnim:
			// the draw's phase 2 normally already fired off the timer; run it here only if it somehow
			// didn't, so the detector can never stay stuck hidden
			if (m_bDetectorDrawPending)
				FireDetectorShow();
			else
				SwitchState(eIdle);							// End of headlamp/NV gesture, or the hand-return
			break;
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

	// GS: aiming while firing INTERRUPTS the shoot anim -> play the aim-in transition now, ahead of the
	// "let the shoot anim finish" guard below. Otherwise the camera zooms immediately but the hands only
	// snap into the ADS pose once the shoot anim runs out ("zoom first, pose later"). The aim-in motion
	// blends over the shoot anim, so the cut is smooth. Skipped mid light-misfire strike so the click
	// gesture isn't cut -- the deferred aim replays once the strike ends (see UpdateCL).
	if (m_bAimInPending && !m_bLightMisfirePlaying)
	{
		m_dwShootAnimEndTm = 0;
		m_bAimInPending = false;
		if (!PlayAimTransition(true))
			PlayAnimIdle();
		return;
	}

	// GS: releasing aim right after a shot leaves the scope AT ONCE -- the aim-out transition blends over
	// the tail of the shoot anim instead of waiting for it to run out (snappier, matching Gunslinger). Same
	// pre-guard placement as aim-in above so it isn't held back by the "let the shoot anim finish" guard.
	// Also skipped during a light-misfire strike (replayed once the strike ends).
	if (m_bAimOutPending && !m_bLightMisfirePlaying)
	{
		m_dwShootAnimEndTm = 0;
		m_bAimOutPending = false;
		OnZoomOut();
		return;
	}

	// a shoot / light-misfire anim is still on screen (the fire-rate timing ended before the anim did --
	// e.g. a longer random variant like de_shoot2, or the click gesture): let it finish before the idle.
	// UpdateCL replays switch2_Idle at m_dwShootAnimEndTm. Mirrors Gunslinger's CanAssignIdleAnimNow.
	if (m_dwShootAnimEndTm && Device.dwTimeGlobal < m_dwShootAnimEndTm)
		return;
	m_dwShootAnimEndTm = 0;	// committing to the idle now

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
	// an ammo-TYPE change has its own sound (GS snd_changecartridgetype); the flag is set by PlayAnimReload
	if (m_bAmmoChangeReload && m_sounds.FindSoundItem("sndChangeCartridge", false))
	{
		PlaySound("sndChangeCartridge", get_LastFP());
		return;
	}
	if (m_sounds.FindSoundItem("sndReloadMis", false) && HasJammedReloadAnim() && IsMisfire() && bMisfireReload)
	{
		// mirror PlayAnimReload's choice: with a detector out the jam-clear plays its own (differently
		// timed) motion, so it gets its own sound - otherwise the plain one runs ahead of the animation.
		// Same for the empty-magazine "_last" revival (GS MagazinedWeaponReloadSoundSelector,
		// WeaponSoundSelector.pas:27-37 -- it picks sndReloadJammedLast on GetAmmoInMagCount<=0).
		const bool det	= DetectorCompanionOut();
		const bool last	= (0 == iAmmoElapsed);
		if (last && det && isHUDAnimationExist("anm_reload_jammed_last_detector")
			&& m_sounds.FindSoundItem("sndReloadMisLastDet", false))
			PlaySound("sndReloadMisLastDet", get_LastFP());
		else if (last && isHUDAnimationExist("anm_reload_jammed_last")
			&& m_sounds.FindSoundItem("sndReloadMisLast", false))
			PlaySound("sndReloadMisLast", get_LastFP());
		else if (det && isHUDAnimationExist("anm_reload_jammed_detector")
			&& m_sounds.FindSoundItem("sndReloadMisDet", false))
			PlaySound("sndReloadMisDet", get_LastFP());
		else
			PlaySound("sndReloadMis", get_LastFP());
	}
	else if (m_sounds.FindSoundItem("sndReloadEmpty", false) && isHUDAnimationExist("anm_reload_empty") && iAmmoElapsed == 0)
		PlaySound("sndReloadEmpty", get_LastFP());
	else
		PlaySound("sndReload", get_LastFP());
}

// The magazine fill at the end of a reload -- or, when the config gives the animation a GS
// `lock_time_start_<alias>`, at that point INSIDE the animation (see ArmReloadLockTimes).
// Runs exactly once per reload.
void CWeaponMagazined::DoReloadInsert()
{
	if (m_bReloadInsertDone)	return;
	m_bReloadInsertDone = true;
	m_bAmmoChangeReload = false;	// consumed with the reload it belonged to

	// GS CWeaponMagazined__OnAnimationEnd_DoReload (WeaponAmmoCounter.pas:71): while the weapon is
	// JAMMED the reload runs with the magazine capacity clamped to what is already loaded -- i.e.
	// the revival clears the jam and loads NOTHING, and a pending ammo-type change is dropped
	// (SetAmmoTypeChangingStatus $FF). Keyed on IsMisfire() itself, not on bMisfireReload: the
	// double-barrels build their reload alias in CWeaponBM16::PlayAnimReload, which never sets that
	// flag, so a jam cleared on their SECOND barrel handed the player a free extra shell.
	if (bMisfireReload || IsMisfire())
	{
		bMisfire = false;
		bMisfireReload = false;
		m_set_next_ammoType_on_reload = u32(-1);
		return;
	}

	// Gunslinger ammo_in_chamber: reloading an EMPTY weapon (nothing chambered) fills to mag_size-1;
	// a non-empty one keeps its chambered round so a full mag on top makes mag_size. iAmmoElapsed here
	// is still the pre-reload count. Temporarily lower the capacity for the empty case (GS SetMagCapacity).
	// ...but NOT in grenade-launcher mode: the GL holds exactly iMagazineSize (1) grenades with no
	// chamber concept, so the -1 here would make it reload to 0 (anim plays, nothing loads).
	const int saved_mag = iMagazineSize;
	if (m_bAmmoInChamber && !IsGrenadeMode() && iAmmoElapsed == 0 && iMagazineSize > 0)
		iMagazineSize -= 1;
	ReloadMagazine();
	iMagazineSize = saved_mag;
}

// GS MakeLockByConfigParam for the reload: `lock_time_start_<alias>` = when the rounds actually go in
// (the ammo bones follow, so you SEE the round seated mid-animation instead of at the very end), and
// `lock_time_end_<alias>` = how long after that the state still runs before the idle takes over (the
// dead tail is cut). Absent keys = the old behaviour, everything happens at the animation's end.
// Tri-state shotguns are untouched: their phases have their own timers (CWeaponShotgun).
void CWeaponMagazined::ArmReloadLockTimes()
{
	m_dwReloadInsertTm	= 0;
	m_bReloadInsertDone	= false;
	if (IsTriStateReload())					return;

	LPCSTR anim = CurrentMotion().c_str();	// the alias PlayHUDMotion actually settled on
	if (!anim || !anim[0])					return;

	string128 key;
	xr_sprintf(key, "lock_time_start_%s", anim);
	float ls = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, -1.0f);
	if (ls < 0.0f)							return;

	m_dwReloadInsertTm = Device.dwTimeGlobal + u32(ls * 1000.0f);

	xr_sprintf(key, "lock_time_end_%s", anim);
	float le = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, -1.0f);
	if (le >= 0.0f)
	{
		u32 end = m_dwMotionStartTm + u32((ls + le) * 1000.0f);
		if (end < m_dwMotionEndTm)	m_dwMotionEndTm = end;		// shorten only, like the plain lock_time
	}
}

void CWeaponMagazined::switch2_Reload()
{
	CWeapon::FireEnd	();

	PlayAnimReload		();
	ArmReloadLockTimes	();		// must follow PlayAnimReload: it reads the alias that was played
	PlayReloadSound		();
	SetPending			(TRUE);
}

// ---- headlamp / night-vision toggle gesture played on THIS weapon's HUD (weapon stays out) ----
void CWeaponMagazined::SelectActionAnim(LPCSTR base, string_path& result)
{
	// base = "anm_headlamp_on" / "anm_nv_on" / "anm_kick" ...
	// Return the BARE base and let PlayHUDMotion resolve the variants: it applies the firemode (_auto) mark
	// FIRST, then _jammed/_empty -> anm_X_auto_jammed, the order the config uses. Pre-resolving _jammed/_empty
	// HERE reversed that: MakeFireModeName then appended _auto to an already-_jammed name (anm_X_jammed_auto,
	// which doesn't exist -> falls back to the single-token base), so the _auto weapon-model pose (the
	// fire-selector flag) snapped back to single during the gesture/kick when jammed in auto mode.
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

	// Only HERE has anm_prepare_detector actually started, so only now is m_dwMotionEndTm valid. (It
	// must NOT be read back in BeginDetectorDraw: PlayHudActionAnim only SwitchState()s, so the value
	// there is still the previous anim's - already in the past - which fired the timer instantly and
	// overrode the prepare before it ever showed.)
	if (m_bDetectorDrawPending)
		ArmDetectorShowTimer();
}

void CWeaponMagazined::ArmDetectorShowTimer()
{
	// anm_prepare_detector ends on a couple of STATIC frames; holding them is the visible hitch. Fire
	// phase 2 a touch early so its motion overrides that dead tail (Gunslinger's config lock_time does
	// the same). lock_time_anm_prepare_detector = seconds from the anim's start; -1 = use the default cut.
	u32 now = Device.dwTimeGlobal;
	float lt = READ_IF_EXISTS(pSettings, r_float, HudSection(), "lock_time_anm_prepare_detector", -1.f);
	if (lt >= 0.f)
		m_dwDetectorShowTm = now + (u32)(lt * 1000.f);
	else
	{
		const u32 cut = 66;		// ms = the 2 static tail frames @30fps
		m_dwDetectorShowTm = (m_dwMotionEndTm > now + cut) ? (m_dwMotionEndTm - cut) : m_dwMotionEndTm;
	}
}

// A HUD selector bone's LOCAL transform is a rigid frame with a small translation (bones sit close
// to their parent). Reject anything non-finite or wildly offset -> that's an un-posed / not-yet-ready
// model read, and holding it would fling the selector across the screen. (root cause of the random
// "selector flies off on spawn" -- the forced CalculateBones at attach sometimes ran before the model
// was posed.)
static bool SelectorXformSane(const Fmatrix& m)
{
	const float* f = &m.m[0][0];
	for (int i = 0; i < 16; ++i)
		if (!_finite(f[i]))	return false;
	// local bone offset should be modest; a garbage/identity-from-wrong-context read is far off
	if (m.c.magnitude() > 3.0f)	return false;
	// basis must be non-degenerate (a zeroed matrix reads finite + zero translation but is invalid)
	if (m.i.magnitude() < 0.1f || m.j.magnitude() < 0.1f || m.k.magnitude() < 0.1f)	return false;
	return true;
}

// ---- fire-mode selector bone hold (light firemode) ----
// GS GetFireModeStateMark (WeaponAnims.pas:38): append the mask_firemode_<a|N> value to the anim so
// each fire mode plays its own baked-selector variant (anm_idle -> anm_idle_auto). The mark is inserted
// before a trailing GL suffix (MakeStateName), matching GS's base+mark then ModifierGL order. Only for
// weapons that configure mask_firemode_* AND have the resulting motion -- otherwise the base plays and
// the legacy fire_mode_bone hold (if configured) drives the selector instead.
void CWeaponMagazined::MakeFireModeName(LPCSTR name, string_path& out)
{
	xr_strcpy(out, name);
	// GS ModifierStd (L303): the firemode-switch transition itself never gets the mark (leftstr(18)!=
	// 'anm_changefiremode') -- it IS what moves the selector, so it must not be suffixed.
	if (0 == strncmp(name, "anm_changefiremode", 18))	return;
	string64 key;
	if (m_iQueueSize == WEAPON_ININITE_QUEUE)	xr_strcpy(key, "mask_firemode_a");
	else										xr_sprintf(key, "mask_firemode_%d", m_iQueueSize);
	if (!pSettings->line_exist(HudSection(), key))	return;
	LPCSTR mark = pSettings->r_string(HudSection(), key);
	if (!mark || !mark[0])	return;			// empty mark (e.g. single mode) = no suffix

	// GS name order is  base + firemode mark + every trailing state token
	// (anm_reload_auto_empty, anm_fakeshoot_auto_empty, anm_shoot_triple_last,
	// anm_shoot_aim_triple_last_sil, anm_reload_auto_jammed_last, anm_idle_auto_jammed_w_gl).
	// Plenty of call sites hand us an alias that ALREADY carries those tokens -- anm_reload_empty
	// (empty reload), anm_fakeshoot_empty (empty click / dry fire), anm_shoot_last / anm_shoot_aim_last
	// (the shot that empties the mag), anm_reload_empty_w_gl, anm_reload_empty_detector, CWeaponPistol's
	// anm_*_empty family. Appending the mark to those builds anm_reload_empty_auto / anm_shoot_last_auto,
	// which no config defines, so we fell back to the UNMARKED motion -- i.e. the single-fire variant --
	// and the weapon model's fire-selector flag visibly snapped to "1" on the last shot, the empty
	// click and the empty/jam reload while in burst or auto. So try the GS position first: strip the
	// whole trailing token run and insert the mark ahead of it. Both attempts stay existence-gated,
	// and the plain append remains as the fallback.
	string_path stem;
	string_path tail;
	if (SplitStateSuffix(name, stem, tail))
	{
		string_path tmp;
		strconcat(sizeof(tmp), tmp, stem, mark, tail);
		if (isHUDAnimationExist(tmp))
		{
			xr_strcpy(out, tmp);
			return;
		}
	}

	string_path marked;
	MakeStateName(name, mark, marked);
	if (isHUDAnimationExist(marked))
		xr_strcpy(out, marked);
}

// The bare mask_firemode_<a|N> mark for this alias, "" when the weapon/mode has none. PlayHUDMotion
// composes base + mark + state itself (see the comment there).
LPCSTR CWeaponMagazined::GetFireModeMark(LPCSTR name)
{
	// GS ModifierStd (L303): the firemode-switch transition itself never gets the mark -- it IS what
	// moves the selector, so it must not be suffixed.
	if (name && 0 == strncmp(name, "anm_changefiremode", 18))	return "";
	string64 key;
	if (m_iQueueSize == WEAPON_ININITE_QUEUE)	xr_strcpy(key, "mask_firemode_a");
	else										xr_sprintf(key, "mask_firemode_%d", m_iQueueSize);
	if (!pSettings->line_exist(HudSection(), key))	return "";
	LPCSTR mark = pSettings->r_string(HudSection(), key);
	return (mark && mark[0]) ? mark : "";
}

void CWeaponMagazined::FireSelectorBoneCallback(CBoneInstance* B)
{
	CWeaponMagazined* w = static_cast<CWeaponMagazined*>(B->callback_param());
	w->m_selector_model_warm = true;	// bone got calculated -> model is posed; safe to sample the auto pose now
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
	for (int i = 0; i < 6; ++i)	m_gwr_bones_state[i] = -0x7fffffff;	// fresh model -> re-apply bone visibility
	m_fire_selector_cb = false;		// new HUD model -> force (re)attach of the selector callback
	m_selector_sample_tries = 0;	// allow the auto-pose sample to retry on this fresh model
	m_selector_model_warm = false;	// fresh (possibly cold pooled) model -> wait for a real render before sampling
	UpdateFireSelectorBone		();
	// do NOT sample here: at attach the (freshly pooled) HUD model may not be posed yet, so a forced
	// CalculateBones reads a wrong pose (the "first spawned weapon has a shifted selector" bug). UpdateCL
	// samples once the bone callback reports the model has been rendered (m_selector_model_warm).
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
	player_hud_motion* anm = itm->m_hand_motions.find_motion("anm_changefiremode_from_1_to_a");	// GS alias name
	if (!anm || anm->m_animations.empty())					return;
	shared_str item_anm = (anm->m_base_name != anm->m_additional_name) ? anm->m_additional_name : anm->m_animations[0].name;
	MotionID M = ka->ID_Cycle_Safe(item_anm);
	if (!M.valid())											return;
	// play the gesture (part 0, channel 0), jump to its last frame, read the selector bone, then restore
	CBlend* B = ka->LL_PlayCycle(0, M, FALSE, NULL, NULL, 0);
	if (B)	{ B->timeCurrent = B->timeTotal - (1.f/30.f); B->blendAmount = 1.f; B->blendPower = 1.f; }	// exact last frame (30 fps), full weight
	K->CalculateBones_Invalidate();
	K->CalculateBones(TRUE);
	Fmatrix sampled = K->LL_GetBoneInstance(bid).mTransform;
	// restore the display animation (mirror CHudItem::on_a_hud_attach) BEFORE bailing on a bad read,
	// so a failed sample never leaves the firemode gesture posed on screen
	if (m_current_motion_def)
		PlayHUDMotion_noCB(m_current_motion, FALSE);
	// only commit a SANE pose; a garbage read (model not posed yet) is rejected so the hold never
	// flings the selector. Left invalid -> UpdateCL retries next frames until the model is ready.
	if (!SelectorXformSane(sampled))
		return;
	m_fire_selector_xform = sampled;
	m_fire_selector_valid = true;
	m_fire_selector_hold  = true;
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
		: (IsAutoFireMode() ? "anm_changefiremode_from_1_to_a" : "anm_changefiremode_from_a_to_1");	// GS alias names
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
	// build anm_changefiremode_from_<from>_to_<to> (GS alias names); fall back to the single<->auto pair
	// if that exact transition alias isn't defined for this weapon (e.g. only 1_to_a/a_to_1 exist)
	string16 ft, tt;
	FireModeToken(oldMode, ft);
	FireModeToken(newMode, tt);
	string64 anim;
	xr_sprintf(anim, "anm_changefiremode_from_%s_to_%s", ft, tt);
	bool nowAuto = IsAutoFireMode();
	if (!isHUDAnimationExist(anim))
		xr_sprintf(anim, "anm_changefiremode_from_%s", nowAuto ? "1_to_a" : "a_to_1");
	// With the detector in the left hand GS plays a DIFFERENT, one-handed switch motion -- the same
	// `_detector` variant convention the reload already uses (anm_reload_detector). The glock authors
	// all four (anm_changefiremode_from_1_to_a_empty_detector and friends). Existence-gated, so a
	// weapon without them keeps the normal motion. The state token is appended after this by
	// PlayHUDMotion, and SplitStateSuffix moves it in FRONT of the trailing `_detector`.
	// "will this alias actually resolve to something right now?" -- it is NOT enough that SOME variant
	// is authored: PlayHUDMotion appends only the token matching the CURRENT state, so accepting an
	// alias that exists solely as ..._empty_detector and then firing it on a loaded weapon asks
	// anim_play for the bare name and hard-fails (fatal: "has no motion alias defined").
	auto authored = [&](LPCSTR a) {
		if (isHUDAnimationExist(a))						return true;
		if (NeedJammedAnim() && HasStateVariant(a, "_jammed"))	return true;
		if (NeedEmptyAnim()  && HasStateVariant(a, "_empty"))	return true;
		return false;
	};
	if (DetectorCompanionOut())
	{
		string64 det;	strconcat(sizeof(det), det, anim, "_detector");
		if (authored(det))	xr_strcpy(anim, det);
	}
	m_sFireModeAnim = anim;

	if (GetState()==eIdle && !IsPending() && authored(anim))
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
			// Gunslinger: no fire-mode switching while aiming (ADS) -- the selector can't be worked with the
			// weapon shouldered. Swallow the key so it does nothing until the player lowers the sights.
			if(flags&CMD_START && !IsZoomed())
			{
				OnPrevFireMode();
				return true;
			};
		}break;
	case kWPN_FIREMODE_NEXT:
		{
			if(flags&CMD_START && !IsZoomed())
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
				(m_sScopeName == pIItem->object().cNameSect() ||
				 IsScopeItem(pIItem->object().cNameSect().c_str())) )	// GS multi-scope: any listed scope item
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
	   (m_sScopeName == item_section_name || IsScopeItem(item_section_name)))
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

// GS restricted_gl_and_sil (WeaponEvents.pas:329 / 351): the launcher and the silencer are mutually
// exclusive on this weapon, and GS does NOT merely refuse the attach -- it detaches the other one
// (need_detach_gl / need_detach_sil). The removed addon comes back as an inventory item.
void CWeaponMagazined::GwrEnforceGLSilExclusion(bool silencer_is_the_new_one)
{
	if (!GwrRestrictedGLandSil())	return;
	if (silencer_is_the_new_one)
	{
		if (IsGrenadeLauncherAttached() && m_sGrenadeLauncherName.size())
			Detach(*m_sGrenadeLauncherName, true);
	}
	else
	{
		if (IsSilencerAttached() && m_sSilencerName.size())
			Detach(*m_sSilencerName, true);
	}
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
	   (m_sScopeName == pIItem->object().cNameSect() ||
	    IsScopeItem(pIItem->object().cNameSect().c_str())))
	{
		m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonScope;
		// GS multi-scope: remember WHICH scope this is so its section drives bones/offsets/zoom/lens.
		int si = ScopeIndexByItem(pIItem->object().cNameSect().c_str());
		m_cur_scope = (si >= 0) ? (u8)si : 0xFF;
		// GS default_brightness_step: start the reticle/NV illumination at the scope's default level (night
		// scopes start bright, not at the dim step 0).
		ResetScopeIllumToDefault();
		// GS variable magnification: pick up this scope's lens steps (ELCAN 1.8x<->10x) and its start step.
		ResetLensStepToDefault();
		result = true;
	}
	else if(pSilencer &&
	   m_eSilencerStatus == ALife::eAddonAttachable &&
	   (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonSilencer) == 0 &&
	   (m_sSilencerName == pIItem->object().cNameSect()))
	{
		m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonSilencer;
		GwrEnforceGLSilExclusion(true);
		result = true;
	}
	else if(pGrenadeLauncher &&
	   m_eGrenadeLauncherStatus == ALife::eAddonAttachable &&
	   (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher) == 0 &&
	   (m_sGrenadeLauncherName == pIItem->object().cNameSect()))
	{
		m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher;
		GwrEnforceGLSilExclusion(false);
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
			(m_sScopeName == item_section_name || IsScopeItem(item_section_name)))
	{
		m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonScope;
		m_cur_scope = 0xFF;		// GS multi-scope: no scope active

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
// GS ReassignHudSection: the HUD section -- i.e. the whole animation set -- is recomputed from the
// weapon's `hud` baseline plus what is currently installed/attached. Two GS sites, both reproduced:
//   * WeaponUpdate.pas:518 -- per INSTALLED UPGRADE: an upgrade may carry its own `hud`, and its own
//     `hud_when_silencer_is_attached` + `hud_silencer` for the combination. That is how the groza's LAM
//     node reaches wpn_groza_lam_silencer_hud (laser installed AND silencer on). GS's `skip_reassign`
//     sentinel means "keep whatever we have", NOT a section name.
//   * WeaponUpdate.pas:803 -- weapon-level `hud_when_silencer_is_attached` + `hud_silencer`, used only
//     when no upgrade claimed the section (GS's hud_overriden flag).
// Recomputed from scratch every time rather than saved/restored, so attach, detach and upgrade installs
// all converge on the same answer in any order.
// GS PlaySoundByAnimName: load every `snd_anm_*` the CURRENT HUD section defines, keyed by the config name
// itself, so CHudItem::PlayHUDMotion can look the sound up by the alias it just resolved. Covers the gauss
// MUI toggle (snd_anm_changefiremode_from_1_to_a/_from_a_to_1) and anything else a config keys.
// Re-run whenever hud_sect changes: an upgrade may repoint `hud` at a section with its OWN sounds (the
// gauss's fast-rpm node -> [wpn_gauss_hud_fastrpm], whose shot sounds are the gauss_shoot_fast set).
void CWeaponMagazined::LoadAnmSounds()
{
	const shared_str& hs = HudSection();
	if (!hs.size() || !pSettings->section_exist(hs))	return;
	if (m_anm_snd_sect == hs)						return;	// already loaded from this section

	m_anm_snd_sect = hs;
	m_sounds.RemoveSounds("snd_anm_");
	CInifile::Sect& S = pSettings->r_section(hs);
	for (const auto& it : S.Data)
	{
		LPCSTR key = it.first.c_str();
		if (0 != strncmp(key, "snd_anm_", 8))	continue;
		if (m_sounds.FindSoundItem(key, false))	continue;	// already loaded (shotgun phases)
		m_sounds.LoadSound(*hs, key, key, false, m_eSoundReload);
	}
}

void CWeaponMagazined::UpdateHudSectionForAddons(LPCSTR extra_upgrade_sect)
{
	const shared_str& wsect = cNameSect();
	if (!pSettings->line_exist(wsect, "hud"))	return;

	shared_str target		= pSettings->r_string(wsect, "hud");	// baseline
	bool upgrade_overrode	= false;

	// installed upgrades, newest last so it wins
	xr_vector<shared_str> srcs;
	for (const shared_str& up : m_upgrades)
	{
		if (!up.size())	continue;
		srcs.push_back(pSettings->line_exist(up, "section") ? (shared_str)pSettings->r_string(up, "section") : up);
	}
	if (extra_upgrade_sect && extra_upgrade_sect[0])	srcs.push_back(extra_upgrade_sect);

	for (const shared_str& s : srcs)
	{
		if (!s.size() || !pSettings->section_exist(*s))	continue;

		if (IsSilencerAttached()
			&& !!READ_IF_EXISTS(pSettings, r_bool, *s, "hud_when_silencer_is_attached", FALSE)
			&& pSettings->line_exist(*s, "hud_silencer"))
		{
			target = pSettings->r_string(*s, "hud_silencer");
			upgrade_overrode = true;
		}
		else if (pSettings->line_exist(*s, "hud"))
		{
			LPCSTR h = pSettings->r_string(*s, "hud");
			if (h && h[0] && 0 != xr_strcmp(h, "skip_reassign"))	// sentinel: leave the section alone
			{
				target = h;
				upgrade_overrode = true;
			}
		}
	}

	if (!upgrade_overrode && IsSilencerAttached()
		&& !!READ_IF_EXISTS(pSettings, r_bool, wsect, "hud_when_silencer_is_attached", FALSE)
		&& pSettings->line_exist(wsect, "hud_silencer"))
		target = pSettings->r_string(wsect, "hud_silencer");

	if (target.size() && pSettings->section_exist(*target) && hud_sect != target)
		hud_sect = target;

	// unconditional: the base install_upgrade_impl may already have set hud_sect straight off the upgrade's
	// `hud`, so the compare above sees no change even though the sound set must follow the new section.
	LoadAnmSounds();
}

void CWeaponMagazined::InitAddons()
{
	UpdateHudSectionForAddons();
	m_zoom_params.m_fIronSightZoomFactor = READ_IF_EXISTS( pSettings, r_float, cNameSect(), "ironsight_zoom_factor", 50.0f );
	if ( IsScopeAttached() )
	{
		shared_str scope_tex_name;
		if ( m_eScopeStatus == ALife::eAddonAttachable )
		{
			//m_sScopeName = pSettings->r_string(cNameSect(), "scope_name");
			//m_iScopeX	 = pSettings->r_s32(cNameSect(),"scope_x");
			//m_iScopeY	 = pSettings->r_s32(cNameSect(),"scope_y");

			// GS multi-scope: read the texture + zoom from the ATTACHED scope's section (falls back to the
			// single scope_name for legacy weapons).
			shared_str sc = GetCurrentScopeSection();
			VERIFY( sc.size() );
			scope_tex_name						= pSettings->r_string(*sc, "scope_texture");
			m_zoom_params.m_fScopeZoomFactor	= pSettings->r_float( *sc, "scope_zoom_factor");
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
			ApplyScopeIllumUI		();		// pick the reticle level for the current brightness step
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
	// The PDA opens straight to the face, so draw it with the anim that ENDS there (pda_aim_draw)
	// instead of the lowered pda_draw -- otherwise it has to draw low, settle, and only then zoom,
	// which is the "it takes ages to come up" lag. Gunslinger does exactly this swap in
	// anm_show_selector (WeaponAnims.pas): anm_show -> anm_show_fastzoom while its _need_pda_zoom is
	// set. The zoom itself still lands on eIdle, same as GS -- there's just far less to wait for.
	bool fast = (m_bPdaCursorAnims && gwr_pda_need_fastzoom() && isHUDAnimationExist("anm_show_fastzoom"));
	PlayHUDMotion(fast ? "anm_show_fastzoom" : "anm_show", FALSE, this, GetState());
	if (g_pda_dbg && m_bPdaCursorAnims)
		Msg("~ pda: PlayAnimShow t=%d  fast=%d  ends_in=%d ms  obj=%d",
			Device.dwTimeGlobal, fast, (int)MotionEndTm() - (int)Device.dwTimeGlobal, ID());
}

void CWeaponMagazined::PlayAnimHide()
{
	VERIFY(GetState()==eHiding);
	PlayHUDMotion("anm_hide", TRUE, this, GetState());
}

bool CWeaponMagazined::DetectorCompanionOut()
{
	// idx 1 (left hand) is the detector slot; non-null means a detector is out
	return g_player_hud && g_player_hud->attached_item(1) != NULL;
}

bool CWeaponMagazined::PlayDetectorGesture(bool draw)
{
	// the left hand takes the detector out (anm_draw_detector) / puts it away (anm_prepare_detector)
	LPCSTR base = draw ? "anm_draw_detector" : "anm_prepare_detector";
	if (!isHUDAnimationExist(base))	return false;
	return PlayHudActionAnim(base);	// one-shot gesture (eActionAnim), returns to idle after
}

bool CWeaponMagazined::BeginDetectorDraw()
{
	// phase 1: the hand goes off-screen (anm_prepare_detector). OnAnimationEnd(eActionAnim) then shows
	// the detector + plays anm_draw_detector (hand returns). No prepare anim -> caller shows directly.
	if (!isHUDAnimationExist("anm_prepare_detector"))	return false;
	if (!PlayHudActionAnim("anm_prepare_detector"))		return false;
	m_bDetectorDrawPending = true;
	// the show timer is armed in switch2_ActionAnim, once the prepare has really started
	return true;
}

void CWeaponMagazined::FireDetectorShow()
{
	// DRAW phase 2: the detector's own draw (anm_show_fast) and anm_draw_detector start together. The
	// detector is still HIDDEN (not on the HUD), so find it via the slot.
	m_dwDetectorShowTm = 0;
	if (!m_bDetectorDrawPending)	return;
	m_bDetectorDrawPending = false;

	CActor* a = smart_cast<CActor*>(H_Parent());
	PIItem di = a ? a->inventory().ItemFromSlot(DETECTOR_SLOT) : NULL;
	CCustomDetector* det = di ? smart_cast<CCustomDetector*>(di) : NULL;
	if (det)	det->ShowAfterPrepare();
	// via SelectActionAnim so the _jammed/_empty variant is used: these aliases carry the weapon-model
	// motion in their 2nd token, and the plain one would snap the slide back to its normal idle pose.
	// (anm_prepare_detector/anm_holster_detector get this for free - they go through PlayHudActionAnim.)
	string_path draw_anim;
	SelectActionAnim("anm_draw_detector", draw_anim);
	if (draw_anim[0])
	{
		// Blend by default, like Gunslinger: it plays both anm_prepare_detector and anm_draw_detector
		// through PlayHudAnim(..., bMixIn=TRUE) and hands off purely on the config lock_time. We cut the
		// prepare mid-motion, so a hard cut pops (the hand is still moving at the cut frame) - the blend
		// carries it into anm_draw_detector's first frame. mix_anm_draw_detector=false forces a hard cut.
		BOOL mix = READ_IF_EXISTS(pSettings, r_bool, HudSection(), "mix_anm_draw_detector", TRUE) ? TRUE : FALSE;
		PlayHUDMotion(draw_anim, mix, this, eActionAnim);				// stay eActionAnim
	}
	else
		SwitchState(eIdle);
}

bool CWeaponMagazined::PlayDetectorHandReturn()
{
	// HOLSTER: the support hand comes back to idle (anm_holster_detector = <pref>_hand_draw). Started
	// together with the detector's own holster so the two play at the same time. There is NO
	// anm_prepare_detector on the holster - the hand is already off the grip.
	if (!isHUDAnimationExist("anm_holster_detector"))	return false;
	return PlayHudActionAnim("anm_holster_detector");	// eActionAnim -> back to idle when it ends
}

void CWeaponMagazined::PlayAnimReload()
{
	VERIFY(GetState() == eReload);

	// with a detector in the left hand pistols use a special one-handed reload (anm_reload*_detector,
	// e.g. pm_det_reload / pm_det_reload_full) so the left hand keeps holding the detector
	bool det = DetectorCompanionOut();

	if (isHUDAnimationExist("anm_reload_jammed") && IsMisfire())
	{
		// GS anm_reload selector (WeaponAnims.pas:771-774): the jam clear takes a "_last" variant when the
		// magazine is ALSO empty -- the revival ends with the bolt held back instead of chambering a round.
		// GS token order is _jammed then _last, with _detector outermost (anm_reload_jammed_last_detector).
		// The firemode mark and _noscope are layered on by PlayHUDMotion (_last is already in its suffix
		// table), so anm_reload_auto_jammed_last / _last_noscope resolve on their own.
		const bool last = (0 == iAmmoElapsed);
		if (last && det && isHUDAnimationExist("anm_reload_jammed_last_detector"))
			PlayHUDMotion("anm_reload_jammed_last_detector", TRUE, this, GetState());
		else if (last && isHUDAnimationExist("anm_reload_jammed_last"))
			PlayHUDMotion("anm_reload_jammed_last", TRUE, this, GetState());
		else if (det && isHUDAnimationExist("anm_reload_jammed_detector"))
			PlayHUDMotion("anm_reload_jammed_detector", TRUE, this, GetState());
		else
			PlayHUDMotion("anm_reload_jammed", TRUE, this, GetState());
		bMisfireReload = true;
	}
	// GS anm_reload selector (WeaponAnims.pas:1035): with rounds still loaded and an ammo-type change
	// pending, the reload is a CHANGE -- its own motion (pull the old round out, put the new one in).
	// Precedence is GS's: jammed > empty > ammochange, so an empty gun just reloads normally.
	else if (iAmmoElapsed > 0 && m_set_next_ammoType_on_reload != u32(-1)
		&& (isHUDAnimationExist("anm_reload_ammochange")
			|| (det && isHUDAnimationExist("anm_reload_ammochange_detector"))))
	{
		if (det && isHUDAnimationExist("anm_reload_ammochange_detector"))
			PlayHUDMotion("anm_reload_ammochange_detector", TRUE, this, GetState());
		else
			PlayHUDMotion("anm_reload_ammochange", TRUE, this, GetState());
		m_bAmmoChangeReload = true;		// PlayReloadSound: snd_changecartridgetype, not the reload sound
	}
	else if (iAmmoElapsed == 0)
	{
		if (det && isHUDAnimationExist("anm_reload_empty_detector"))
			PlayHUDMotion("anm_reload_empty_detector", TRUE, this, GetState());
		else if (isHUDAnimationExist("anm_reload_empty"))
			PlayHUDMotion("anm_reload_empty", TRUE, this, GetState());
		else if (det && isHUDAnimationExist("anm_reload_detector"))
			PlayHUDMotion("anm_reload_detector", TRUE, this, GetState());
		else
			PlayHUDMotion("anm_reload", TRUE, this, GetState());
	}
	else if (det && isHUDAnimationExist("anm_reload_detector"))
		PlayHUDMotion("anm_reload_detector", TRUE, this, GetState());
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
	// GS aim-move direction suffixes (anm_idle_aim_moving_<dir>), so GS weapon configs drop in as-is
	if(ms&mcBack)				return "_moving_back";
	if(ms&mcLStrafe)			return "_moving_left";
	if(ms&mcRStrafe)			return "_moving_right";
	return "_moving_forward";	// forward / diagonal-forward default
}

void CWeaponMagazined::SelectAimIdleAnim(string_path& result)
{
	LPCSTR dir = AimWalkDirSuffix();		// "" or "_moving_<dir>"

	// GS use_scope_anims: the "_scope" infix goes right after "anm_idle_aim", BEFORE the moving direction
	// (anm_idle_aim_scope[_moving_<dir>]) -- NOT at the very end. And the scope aim-walk variants only exist
	// with the firemode mark (anm_idle_aim_scope_moving_auto), which PlayHUDMotion re-applies -- so test each
	// candidate WITH that mark, not the bare name (else the scope aim-walk is never found and the weapon drops
	// to the iron-sight aim-walk pose while moving+aiming).
	auto exists = [&](LPCSTR nm)->bool {
		if (isHUDAnimationExist(nm))	return true;
		string_path marked;	MakeFireModeName(nm, marked);
		return (0 != xr_strcmp(marked, nm)) && isHUDAnimationExist(marked);
	};

	string_path cand;
	// 1) MOVING + scope: the scope aim-walk (GS order, firemode-aware). In SINGLE fire mode the scope aim-walk
	// often doesn't exist (only the _auto variant is authored) -- do NOT drop to the static scope aim here
	// (that freezes the weapon mid-stride -> looks like it shakes against the walk bob); fall through to the
	// animated non-scope aim-walk below.
	if (UseScopeAnims() && dir[0])
	{
		xr_sprintf(cand, "anm_idle_aim_scope%s", dir);				// anm_idle_aim_scope_moving_<dir>
		if (exists(cand))	{ xr_strcpy(result, cand); return; }
		if (exists("anm_idle_aim_scope_moving"))	{ xr_strcpy(result, "anm_idle_aim_scope_moving"); return; }	// forward, implicit
	}
	// 2) MOVING: the animated non-scope aim-walk (also the fallback for a scope with no moving variant)
	if (dir[0])
	{
		xr_sprintf(cand, "anm_idle_aim%s", dir);
		if (isHUDAnimationExist(cand))	{ xr_strcpy(result, cand); return; }
		if (xr_strcmp(dir, "_moving_forward") != 0 && isHUDAnimationExist("anm_idle_aim_moving_forward"))
			{ xr_strcpy(result, "anm_idle_aim_moving_forward"); return; }
	}
	// 3) STATIC aim: the scope aim pose, else the iron-sight aim idle
	if (UseScopeAnims() && exists("anm_idle_aim_scope"))	{ xr_strcpy(result, "anm_idle_aim_scope"); return; }
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
		return isHUDAnimationExist("anm_idle_aim_moving_forward");	// directional aim-move present
	return inherited::HasMovementIdleVariant();				// anm_idle_moving_slow
}

void CWeaponMagazined::PlayAnimIdle()
{
	VERIFY(GetState()==eIdle);
	// the 3D PDA picks its own idle from the cursor direction, aim variants included, so it must
	// reach CHudItem::TryPlayAnimIdle instead of being sent straight to the plain aim idle here
	if(IsZoomed() && !m_bPdaCursorAnims)
	{
		PlayAnimAim();
	}else
		inherited::PlayAnimIdle();
}

// Pick the shoot motion: a dedicated ADS motion when zoomed (Gunslinger-style); falls back to the
// hip-fire "anm_shoot" when not zoomed or the weapon has no such alias. Overridden by the GL
// subclass for the _w_gl / _g variants. Gunslinger name order (WeaponAnims.pas anm_shots): the "_scope"
// suffix goes right after "_aim" (before "_last") while firing through an attached use_scope_anims scope.
void CWeaponMagazined::SelectShootAnim(string_path& result)
{
	// Last chambered round -> the dedicated shot that leaves the bolt/slide locked back
	// (anm_shot_l hip / anm_shots_aim_last ADS). Lives here rather than in CWeaponPistol so every
	// magazined weapon gets it; existence-gated, so anything without those aliases is unchanged.
	bool last = (iAmmoElapsed <= 1);
	if (IsZoomed() && isHUDAnimationExist("anm_shoot_aim"))
	{
		if (UseScopeAnims())
		{
			if (last && isHUDAnimationExist("anm_shoot_aim_scope_last"))
				{ xr_strcpy(result, "anm_shoot_aim_scope_last"); return; }
			if (isHUDAnimationExist("anm_shoot_aim_scope"))
				{ xr_strcpy(result, "anm_shoot_aim_scope"); return; }
		}
		if (last && isHUDAnimationExist("anm_shoot_aim_last"))
			{ xr_strcpy(result, "anm_shoot_aim_last"); return; }
		xr_strcpy(result, "anm_shoot_aim");
		return;
	}
	if (last && isHUDAnimationExist("anm_shoot_last"))
		{ xr_strcpy(result, "anm_shoot_last"); return; }
	xr_strcpy(result, "anm_shoot");
}

void CWeaponMagazined::PlayAnimShoot()
{
	VERIFY(GetState()==eFire);
	string_path anim;
	SelectShootAnim(anim);
	PlayHUDMotion(anim, FALSE, this, GetState());
}

// Base alias for the shot that jams. "_last" is deliberately NOT applied: GS's modifier precedence is
// jammed > last, so the jammed variant replaces it. Overridden by the double-barrels (shell count).
void CWeaponMagazined::SelectJammedShootBase(string_path& out)
{
	if (IsZoomed() && isHUDAnimationExist("anm_shoot_aim"))
	{
		if (UseScopeAnims() && isHUDAnimationExist("anm_shoot_aim_scope"))
			xr_strcpy(out, "anm_shoot_aim_scope");
		else
			xr_strcpy(out, "anm_shoot_aim");
		return;
	}
	xr_strcpy(out, "anm_shoot");
}

// The shot that JAMS re-assigns the shoot motion to its "_jammed" variant, so the failure is shown by the
// firing animation itself (GS anm_shots_selector).
// Returns false when the weapon authors no jammed shot variant -- then the running shoot anim is kept.
bool CWeaponMagazined::PlayJammedShootAnim()
{
	if (GetState() != eFire)	return false;

	// OPT-IN per weapon (`use_jammed_shoot_anim`). GS re-assigns the shot animation on every gun, but our
	// configs carry the `_jammed` shot variants EVERYWHERE (they came with the bulk GS alias transfer), so
	// the "does this weapon author one?" gate below turned the feature on for the whole arsenal instead of
	// the shotgun it was ported for -- and on a GL rifle it also pulled the hands out of the launcher pose.
	// Weapons that want it say so in their config; everyone else keeps the plain shot + jammed idle.
	if (!READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "use_jammed_shoot_anim", FALSE))	return false;

	string_path base;	SelectJammedShootBase(base);
	if (!base[0] || !HasStateVariant(base, "_jammed"))	return false;

	// NeedJammedAnim() is true now (bMisfire), so PlayHUDMotion picks the _jammed variant itself
	return (0 != PlayHUDMotion(base, FALSE, this, GetState()));
}

// ---- aim-in / aim-out (ADS) transition, Gunslinger-style ----
// Pick the transition motion; base impl has no GL variants (see the WGrenade override).
void CWeaponMagazined::SelectAimTransitionAnim(bool bAimIn, string_path& result)
{
	LPCSTR base = bAimIn ? "anm_idle_aim_start" : "anm_idle_aim_end";
	// The PDA's open-zoomed aim-in isn't a normal aim-in: it's the 2nd half of the split draw
	// (pda_aim_draw_2ndpart), so it continues from where anm_show_fastzoom stopped instead of
	// snapping to the aim pose. GS does the same _fastzoom suffix in WeaponAnims.pas.
	if (bAimIn && m_bPdaCursorAnims && gwr_pda_need_fastzoom())
	{
		if (isHUDAnimationExist("anm_idle_aim_start_fastzoom"))
		{
			xr_strcpy(result, "anm_idle_aim_start_fastzoom");
			return;
		}
	}
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

	// Block firing only for lock_time (GS's values), not the whole transition, so the shot can come
	// out before the aim-in/out animation ends -- pressing fire then just cuts the transition.
	LPCSTR key = bAimIn ? "lock_time_anm_idle_aim_start" : "lock_time_anm_idle_aim_end";
	float lock = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, bAimIn ? 0.2f : 0.25f);
	m_dwAimFireLockTm = Device.dwTimeGlobal + u32(lock * 1000.0f);
	m_bAimLockFirePressed = false;	// fresh lock window -> only a press made DURING it counts for autoshoot

	// a fire press made during this lock fires the moment it ends (GS's autoshoot_<anim>). GS enables
	// this on ~all weapons (43/45), so it's the default here; a weapon can turn it off in config.
	LPCSTR askey = bAimIn ? "autoshoot_anm_idle_aim_start" : "autoshoot_anm_idle_aim_end";
	m_bAimLockAutoShoot = !!READ_IF_EXISTS(pSettings, r_bool, HudSection(), askey, TRUE);
	return true;
}

void CWeaponMagazined::OnZoomIn			()
{
	if (IsJamInspectPlaying())	return;	// can't aim mid jam-inspect

	// task: aiming is blocked while a light-misfire strike plays -- remember the aim press and replay it
	// when the strike ends (UpdateCL), so the click gesture and the aim-in never overlap.
	if (m_bLightMisfirePlaying)
	{
		m_bZoomPendingMisfire	= true;
		m_bZoomPendingMisfireIn	= true;
		return;
	}

	// GS CanAimNow (WeaponAdditionalBuffer.pas:896): block aiming while an active detector is out AND not in
	// eIdle -- i.e. mid draw/hide/reload-companion. After a reload with the detector out it re-draws (non-idle),
	// so the aim is blocked until the detector settles back to idle. Hard block (the press is ignored), like GS.
	if (DetectorCompanionOut())
	{
		attachable_hud_item* i1 = g_player_hud->attached_item(1);
		CCustomDetector* det = i1 ? smart_cast<CCustomDetector*>(i1->m_parent_hud_item) : nullptr;
		if (det && det->GetState() != CCustomDetector::eIdle)
			return;
	}

	// exiting sprint: pressing aim clears the actor's sprint (ActorInput) and the weapon plays the
	// sprint-out anim first; defer the aim-in until it's (almost) done, then the UpdateCL handoff
	// re-triggers OnZoomIn. Only when the weapon has an exit anim (else nothing to defer to).
	if ((m_dwSprintExitEndTm && Device.dwTimeGlobal < m_dwSprintExitEndTm)
		|| (IsActorSprinting() && HasSprintExitAnim()))
	{
		m_bZoomPendingSprint = true;
		return;
	}

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
	// the companion detector mirrors the aim transition via the CHudItem::PlayHUDMotion hook
	// (weapon plays anm_idle_aim_start/anm_idle_aim -> detector plays anm_wpn_*), no explicit call here.
}
void CWeaponMagazined::OnZoomOut		()
{
	m_bZoomPendingSprint = false;	// releasing aim cancels a sprint-deferred aim-in

	if(!IsZoomed())
		return;

	// task: a light-misfire strike blocks the aim-out too -- defer it so it doesn't run on top of the
	// click gesture; UpdateCL replays it when the strike ends.
	if (m_bLightMisfirePlaying)
	{
		m_bZoomPendingMisfire	= true;
		m_bZoomPendingMisfireIn	= false;
		return;
	}

	if(IsJamInspectPlaying())
	{
		// releasing aim mid jam-inspect: the inspect owns the hands (non-interruptible), but we must
		// NOT drop the release — with hold-to-aim the OnZoomOut fires once on key-up and would leave
		// the weapon stuck zoomed until re-pressed. Defer the aim-out instead: switch2_Idle replays
		// OnZoomOut at eIdle right after the inspect ends (m_bDryFirePlaying cleared), returning to idle.
		m_bAimOutPending = true;
		m_bAimInPending  = false;
		return;
	}

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
	// aim-out transition + return-to-own-idle are handled by the companion hook + PlayAnimIdle mirror
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
	// Gunslinger: firing an auto/burst queue wears the gun faster per shot (condition_queue_shot_dec) than a
	// single aimed shot (condition_shot_dec). Active only when the config defines a distinct queue value.
	if (conditionDecreasePerShotQueue != conditionDecreasePerShot)
	{
		const bool queue = (m_iQueueSize == WEAPON_ININITE_QUEUE) || (m_iQueueSize > 1);
		return queue ? conditionDecreasePerShotQueue : conditionDecreasePerShot;
	}

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
	// never trust a saved pose that isn't sane (old/garbage save) -> drop it so the hold can't fling
	// the selector; it'll be re-sampled (auto) or re-captured on the next firemode switch
	if (m_fire_selector_valid && !SelectorXformSane(m_fire_selector_xform))
	{
		m_fire_selector_valid	= false;
		m_selector_sample_tries	= 0;
	}
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
	// the base class may have set hud_sect straight off the upgrade's `hud`; re-derive it so an
	// upgrade's silencer-specific variant (and the skip_reassign sentinel) get their say. `section`
	// is the effect section being installed and is NOT in m_upgrades yet -- pass it explicitly.
	if ( !test )	UpdateHudSectionForAddons( section );
	
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
		// ...and the LIVE queue must follow, or the weapon keeps firing in whatever mode it was in
		// before the upgrade while m_iCurFireMode already points at the new one. The first press of
		// the selector then plays its animation and "does nothing" (it only moves the index back to
		// the mode the gun was really in) -- the glock's autofire/3-round upgrades showed exactly
		// that. Non-actor holders keep the AI's infinite queue, same rule as OnH_A_Chield.
		m_bHasDifferentFireModes = (ModesCount > 1);
		if (smart_cast<CActor*>(H_Parent()))	SetQueueSize(GetCurrentFireMode());
		else if (m_bHasDifferentFireModes)		SetQueueSize(WEAPON_ININITE_QUEUE);
	}
	result |= result2;

	result |= process_if_exists( section, "dispersion_start", &CInifile::r_s32, m_iShootEffectorStart, test );

	// GS recharge_time -- the gauss's fast_conders / ionistori nodes shorten the charge (-1.0 / -0.3)
	result |= process_if_exists( section, "recharge_time", &CInifile::r_float, m_fRechargeTime, test );

	// AN-94 hyperburst keys are upgradeable like any other ballistic value
	result |= process_if_exists( section, "base_dispersioned_bullets_count",      &CInifile::r_s32,   m_iBaseDispersionedBulletsCount,     test );
	result |= process_if_exists( section, "base_dispersioned_bullets_speed",      &CInifile::r_float, m_fBaseDispersionedBulletsSpeed,     test );
	result |= process_if_exists( section, "base_dispersioned_bullets_time_delta", &CInifile::r_float, m_fBaseDispersionedBulletsTimeDelta, test );
	result |= process_if_exists( section, "singleshoots_time_delta",              &CInifile::r_float, m_fSingleShootsTimeDelta,            test );

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
