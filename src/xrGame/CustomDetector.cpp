#include "stdafx.h"
#include "customdetector.h"
#include "ui/ArtefactDetectorUI.h"
#include "hudmanager.h"
#include "inventory.h"
#include "level.h"
#include "map_manager.h"
#include "ActorEffector.h"
#include "actor.h"
#include "ui/UIWindow.h"
#include "player_hud.h"
#include "weapon.h"
#include "WeaponMagazined.h"
#include "../xrEngine/motion.h"		// ESMFlags (esmStopAtEnd) - one-shot vs looping companion
#include "WeaponMagazined.h"
#include "WeaponPistol.h"		// CWeaponPistol -- "one-handed" test for detector coexistence (slots are interchangeable now)

ITEM_INFO::ITEM_INFO()
{
	pParticle	= NULL;
	curr_ref	= NULL;
}

ITEM_INFO::~ITEM_INFO()
{
	if(pParticle)
		CParticlesObject::Destroy(pParticle);
}

// GS CanShowDetector (DetectorUtils.pas:208): a weapon may forbid the detector for the DURATION of
// one specific animation, via `disable_detector_<alias>` in its hud section -- the glock uses it so the
// hand puts the detector away while the fire-mode selector is switched. Keyed on the motion actually
// playing, so it releases by itself when that animation ends. Consulted from BOTH the compatibility
// test (item switches) and the per-frame UpdateVisibility, since a fire-mode switch changes neither
// the active item nor the weapon state and would otherwise never be re-evaluated.
bool  CCustomDetector::AnimForbidsDetector(CHudItem* itm)
{
	if (!itm)	return false;
	const shared_str& cm = itm->CurrentMotion();
	if (!cm.size())	return false;
	string_path key;	strconcat(sizeof(key), key, "disable_detector_", cm.c_str());
	return pSettings->line_exist(itm->HudSection(), key) && !!pSettings->r_bool(itm->HudSection(), key);
}

// Is it worth waiting for this item to finish coming up before the detector appears? Only when the
// item actually performs the hand-over (anm_prepare_detector -> anm_draw_detector, CWeaponMagazined::
// BeginDetectorDraw). The KNIFE and the BOLT have no such motion -- they are not even CWeaponMagazined
// -- so waiting would buy nothing and just delay the draw; for them the detector comes out in parallel,
// exactly as it did before the deferral existed.
bool  CCustomDetector::HasDetectorDrawGesture(CHudItem* itm)
{
	if (!itm || !smart_cast<CWeaponMagazined*>(itm))	return false;
	return !!pSettings->line_exist(itm->HudSection(), "anm_prepare_detector");
}

bool  CCustomDetector::CheckCompatibilityInt(CHudItem* itm, u32* slot_to_activate, bool for_draw)
{
	if(itm==NULL)
		return true;

	CInventoryItem iitm				= itm->item();
	u32 slot						= iitm.GetSlot();
	// One-handed items can be carried together with the detector. Knife/bolt keep their own dedicated
	// slots so the slot test is fine, but the weapon slots are INTERCHANGEABLE now -- a rifle can sit in
	// the former pistol slot -- so gate weapons on the actual TYPE (CWeaponPistol), not on the slot,
	// otherwise a rifle-in-slot-1 wrongly let the detector stay out.
	// GS CanUseDetectorWithItem (DetectorUtils.pas:98) does not look at the item's class at all --
	// it reads a config bool off the item's own section and lets an installed UPGRADE override it:
	//   result := game_ini_r_bool_def(sect,'supports_detector', false);
	//   result := FindBoolValueInUpgradesDef(wpn,'supports_detector', result, true);
	// Ported verbatim, with our old class/slot test kept as the fallback for every weapon whose
	// config predates the key (all the pistols ported before it existed still just work).
	bool bres;
	const shared_str& isect = iitm.object().cNameSect();
	if (pSettings->line_exist(isect, "supports_detector"))
		bres = !!pSettings->r_bool(isect, "supports_detector");
	else
		bres = (slot==KNIFE_SLOT || slot==BOLT_SLOT) || (smart_cast<CWeaponPistol*>(itm) != NULL);
	for (const shared_str& up : iitm.get_upgrades())
	{
		if (!up.size() || !pSettings->section_exist(*up))	continue;
		LPCSTR src = *up;
		if (pSettings->line_exist(*up, "section"))
		{
			LPCSTR e = pSettings->r_string(*up, "section");
			if (e && e[0] && pSettings->section_exist(e) && pSettings->line_exist(e, "supports_detector"))
				src = e;
		}
		if (pSettings->line_exist(src, "supports_detector"))
			bres = !!pSettings->r_bool(src, "supports_detector");
	}

	// The active item can't be held together with the detector (e.g. a rifle in the main slot). Instead
	// of doing nothing, report a slot the detector CAN share so the caller switches to it first and then
	// draws the detector (Gunslinger / GunsXRay behaviour). Prefer the bolt, then the knife, then a
	// pistol -- so a rifle in hand becomes "bolt + detector".
	if(!bres && slot_to_activate)
	{
		// pick a slot the detector CAN share, exactly like GunsXRay: last assignment wins, so a pistol is
		// preferred, then the knife, then the bolt -- whichever the actor actually carries.
		// Priority BOLT > PISTOL > KNIFE (last assignment wins, so list low-to-high). A PISTOL in EITHER
		// interchangeable weapon slot qualifies -- but a rifle there does not, so gate on the pistol type.
		*slot_to_activate = NO_ACTIVE_SLOT;
		if(m_pInventory->ItemFromSlot(KNIFE_SLOT))		*slot_to_activate = KNIFE_SLOT;
		if(smart_cast<CWeaponPistol*>(m_pInventory->ItemFromSlot(PISTOL_SLOT)))	*slot_to_activate = PISTOL_SLOT;
		if(smart_cast<CWeaponPistol*>(m_pInventory->ItemFromSlot(RIFLE_SLOT)))	*slot_to_activate = RIFLE_SLOT;
		if(m_pInventory->ItemFromSlot(BOLT_SLOT))		*slot_to_activate = BOLT_SLOT;
		if(*slot_to_activate != NO_ACTIVE_SLOT)
			bres = true;
	}

	if (bres && AnimForbidsDetector(itm))	bres = false;

	// An item still coming UP owns both hands, so the detector must not come out in the middle of that
	// draw -- but ONLY for a draw (for_draw). Failing the test unconditionally made every item report
	// itself incompatible while it rose, and the hide path acted on that: drawing a KNIFE, which the
	// detector is perfectly happy to share hands with, put the detector away. An item on its way up
	// never justifies holstering an already-drawn detector, so leave that path exactly as it was.
	if(itm->GetState()==CHUDState::eShowing)
	{
		// ...and only for an item that has the gesture to wait FOR (knife/bolt draw in parallel)
		if (for_draw && HasDetectorDrawGesture(itm))	bres = false;
	}
	else if (for_draw)
		// "busy right now" is a reason not to TAKE THE DETECTOR OUT, never a reason to put it away:
		// picking a pistol up holsters the current weapon, so the compatible weapon in hand reported
		// eHiding+pending and the detector was holstered with it (user 2026-08-03, [DETHIDE] log
		// showed state=2 pending=1 supports=true).
		bres = bres && !itm->IsPending();

	if(bres)
	{
		CWeapon* W = smart_cast<CWeapon*>(itm);
		if(W)
		{
			// aiming normally holsters the detector; but if this detector has an aim companion anim
			// it STAYS out and mirrors the ADS pose (Gunslinger-style) instead of hiding
			bool zoom_ok = !W->IsZoomed() || isHUDAnimationExist("anm_wpn_idle_aim");
			bres = bres && (W->GetState()!=CHUDState::eBore) && zoom_ok;
		}
	}
	return bres;
}

bool  CCustomDetector::CheckCompatibility(CHudItem* itm)
{
	if(!inherited::CheckCompatibility(itm) )	
		return false;

	if(!CheckCompatibilityInt(itm))
	{
		HideDetector	(true);
		return			false;
	}
	return true;
}

void CCustomDetector::HideDetector(bool bFastMode)
{
	if(GetState()==eIdle)
	{
		m_bAutoToggle = true;		// engine-driven hide (reload/aim) -> no weapon draw/prepare gesture
		ToggleDetector(bFastMode);
	}
}

void CCustomDetector::ShowDetector(bool bFastMode)
{
	if(GetState()==eHidden)
	{
		m_bEmergencyShow = false;
		m_bAutoToggle = true;		// engine-driven re-show (after reload/aim) -> no weapon gesture
		ToggleDetector(bFastMode);
	}
}

void CCustomDetector::ShowDetectorEmergency()
{
	if(GetState()==eHidden)
	{
		m_bEmergencyShow = true;		// use the anm_show_emergency draw (weapon in the other hand)
		m_bAutoToggle = true;
		ToggleDetector(false);
	}
}

void CCustomDetector::ToggleDetector(bool bFastMode)
{
	m_bFastAnimMode = bFastMode;
	if(GetState()==eHidden)
	{
		PIItem iitem = m_pInventory->ActiveItem();
		CHudItem* itm = (iitem)?iitem->cast_hud_item():NULL;
		u32 slot_to_activate = NO_ACTIVE_SLOT;
		// pressed while the item in hand is still being drawn -> remember it: UpdateVisibility draws
		// the detector as soon as that item is out, so the press is honoured instead of lost.
		// Remember HOW it was requested too: a keypress must still get the weapon's hand gesture when
		// it finally fires, otherwise the detector just appears with the right hand never moving.
		// (knife/bolt play no hand-over gesture -> nothing to wait for, fall through and draw now)
		if(itm && itm->GetState()==CHUDState::eShowing && HasDetectorDrawGesture(itm))
		{
			m_bNeedActivation		= true;
			m_bNeedActivationManual	= !m_bAutoToggle;
			return;
		}
		if(CheckCompatibilityInt(itm, &slot_to_activate, true))
		{
			if(slot_to_activate != NO_ACTIVE_SLOT)
			{
				// the active weapon can't be held with the detector (e.g. a rifle) -> switch to a
				// compatible slot first; UpdateVisibility shows the detector once that item is out
				// (m_bNeedActivation), so one keypress does "holster rifle -> draw bolt + detector".
				m_pInventory->Activate(slot_to_activate);
				m_bNeedActivation		= true;
				m_bNeedActivationManual	= !m_bAutoToggle;
				return;
			}
			// manual draw + the in-hand weapon has anm_prepare_detector -> phase 1: the hand goes
			// off-screen; the detector is actually shown when it ends (weapon -> ShowAfterPrepare)
			if (!m_bAutoToggle)
			{
				attachable_hud_item* w0 = g_player_hud ? g_player_hud->attached_item(0) : NULL;
				CWeaponMagazined* wm = w0 ? smart_cast<CWeaponMagazined*>(w0->m_parent_hud_item) : NULL;
				if (wm && wm->BeginDetectorDraw())
					return;		// deferred; ShowAfterPrepare() plays the _quick draw when the prepare ends
			}
			SwitchState				(eShowing);
			TurnDetectorInternal	(true);
		}
	}else
	if(GetState()==eIdle)
	{
		// Manual holster with a weapon in hand: the detector's own holster (anm_hide_fast) and the
		// weapon's hand-return (anm_holster_detector) play AT THE SAME TIME. No anm_prepare_detector
		// here - the support hand is already off the grip.
		if (!m_bAutoToggle)
		{
			attachable_hud_item* w0 = g_player_hud ? g_player_hud->attached_item(0) : NULL;
			CWeaponMagazined* wm = w0 ? smart_cast<CWeaponMagazined*>(w0->m_parent_hud_item) : NULL;
			if (wm)	wm->PlayDetectorHandReturn();
		}
		SwitchState					(eHiding);
	}

	m_bNeedActivation		= false;
	m_bNeedActivationManual	= false;
}

void CCustomDetector::ShowAfterPrepare()
{
	if(GetState()!=eHidden)	return;
	SwitchState				(eShowing);		// OnStateSwitch plays the detector's own draw + anm_draw_detector
	TurnDetectorInternal	(true);
	m_bNeedActivation		= false;
	m_bNeedActivationManual	= false;
}

void CCustomDetector::OnStateSwitch(u32 S)
{
	inherited::OnStateSwitch(S);

	switch(S)
	{
	case eShowing:
		{
			g_player_hud->attach_item	(this);
			m_sounds.PlaySound			("sndShow", Fvector().set(0,0,0), this, true, false);
			LPCSTR show_anm = m_bFastAnimMode ? "anm_show_fast" : "anm_show";
			if (m_bEmergencyShow && isHUDAnimationExist("anm_show_emergency"))
				show_anm = "anm_show_emergency";		// drawn together with a weapon
			PlayHUDMotion				(show_anm, FALSE, this, GetState());
			// the weapon's draw/prepare gestures are driven from the weapon (BeginDetectorDraw ->
			// anm_prepare_detector -> ShowAfterPrepare -> anm_draw_detector), not from here.
			m_bAutoToggle = false;
			SetPending					(TRUE);
		}break;
	case eHiding:
		{
			m_sounds.PlaySound			("sndHide", Fvector().set(0,0,0), this, true, false);
			PlayHUDMotion				(m_bFastAnimMode?"anm_hide_fast":"anm_hide", TRUE, this, GetState());
			m_bAutoToggle = false;
			SetPending					(TRUE);
		}break;
	case eIdle:
		{
			PlayAnimIdle				();
			SetPending					(FALSE);
		}break;
	case eDetActionAnim:
		{
			if (m_action_anim.size())
				PlayHUDMotion			(m_action_anim, TRUE, this, GetState());
			SetPending					(TRUE);
		}break;
}
}

void CCustomDetector::OnAnimationEnd(u32 state)
{
	inherited::OnAnimationEnd	(state);
	switch(state)
	{
	case eShowing:
		{
			SwitchState					(eIdle);
		} break;
	case eDetActionAnim:
		{
			m_bSprintStarted			= false;	// gesture interrupted sprint -> replay the enter after
			SwitchState					(eIdle);	// gesture finished -> back to the working idle
		} break;
	case eIdle:
		{
			// a one-shot idle-slot motion ended (sprint enter/exit transition, or a companion like the
			// dry-fire) -> re-select the idle so it hands off to the sprint loop / walk / normal idle.
			// A one-shot COMPANION needs care: the weapon may still be playing its own one-shot, and the
			// mirror would then just re-pick the companion we've already finished and hold its last
			// frame. Mark it so PlayCompanionAction skips it for this one re-select; any other companion
			// (e.g. the aim idle) is still mirrored normally.
			if (m_bCompanionOneShot)
			{
				m_bCompanionOneShot	= false;
				m_companion_done	= m_current_motion;
			}
			PlayAnimIdle				();
			m_companion_done			= NULL;
		} break;
	case eHiding:
		{
			SwitchState					(eHidden);
			TurnDetectorInternal		(false);
			g_player_hud->detach_item	(this);
		} break;
	}
}

// Companion: while a weapon (idx 0) is out, the detector (left hand) MIRRORS the weapon's current
// motion via its anm_wpn_<action> (aim, directional aim-walk, bolt throw-idle, ...) instead of running
// its own idle -- this stops the detector's own moving idle from overwriting a held companion (e.g.
// walking while aiming or holding a bolt throw). No companion for the weapon's current motion (plain
// hip idle/moving/sprint) -> the detector plays its own idle.
void CCustomDetector::PlayAnimIdle()
{
	attachable_hud_item* w0 = g_player_hud ? g_player_hud->attached_item(0) : NULL;
	CHudItem* wi = (w0 && w0->m_parent_hud_item != this) ? w0->m_parent_hud_item : NULL;
	// CompanionSuppressed(): the weapon is playing something we must NOT mirror (the bolt re-showing
	// itself after a throw). Checked here too, not just in the hook - our companion for the throw ends
	// before that show does, and this mirror would then pick the show up and play the draw late.
	if (wi && !wi->CompanionSuppressed())
	{
		const shared_str& wm = wi->CurrentMotion();
		LPCSTR m = wm.c_str();
		if (m && 0==strncmp(m, "anm_", 4) && PlayCompanionAction(m + 4))
			return;
	}
	inherited::PlayAnimIdle();
}

// The detector was toggled with a weapon in hand -> that weapon plays its draw/prepare-detector
// hand gesture (anm_draw_detector / anm_prepare_detector) so the left hand takes out / puts away
// the detector, coordinated with this detector's own show/hide.
void CCustomDetector::WeaponDetectorGesture(bool draw)
{
	if (!g_player_hud)	return;
	attachable_hud_item* w0 = g_player_hud->attached_item(0);
	CWeaponMagazined* wm = w0 ? smart_cast<CWeaponMagazined*>(w0->m_parent_hud_item) : NULL;
	if (wm)	wm->PlayDetectorGesture(draw);
}

// Called by the weapon when its aim state flips, so the companion idle re-selects immediately
// (the detector's own idle only refreshes on movement/state changes, which would lag the aim).
void CCustomDetector::RefreshCompanionIdle()
{
	if (GetState()==eIdle && !IsPending())
		PlayAnimIdle();
}

// Play the companion anim matching the weapon's action (anm_wpn_<action>) if this detector defines
// it. Driven from CHudItem::PlayHUDMotion (the weapon side). Only overlays when the detector is idle
// (don't stomp its own show/hide). One-shots route back to the (aim-aware) idle via OnAnimationEnd.
// strip a weapon-state infix (e.g. "_empty", "_jammed") in place so empty/jammed weapon anims map to
// the base companion (anm_idle_aim_empty -> idle_aim, anm_idle_aim_empty_start -> idle_aim_start).
static void companion_strip(char* s, const char* sub)
{
	char* p; size_t sl = xr_strlen(sub);
	while ((p = strstr(s, sub)) != NULL)
		memmove(p, p + sl, xr_strlen(p + sl) + 1);
}

bool CCustomDetector::PlayCompanionAction(LPCSTR action, bool bRestart)
{
	if (!IsWorking() || GetState()!=eIdle || IsPending())	return false;
	// SPRINT is NOT mirrored as a companion. Like Gunslinger (where the detector's sprint is its OWN
	// isdetector movement dispatch, and only AIM is a companion), we let the detector run its own sprint
	// enter/loop/exit -- a single driver, reacting to the same actor movement events as the weapon, so no
	// double enter/exit or phantom exit. The per-frame phase-lock in player_hud::update keeps the two
	// sprint loops aligned. Returning false here makes PlayAnimIdle fall through to the own dispatch.
	if (strstr(action, "sprint"))	return false;
	// Prefer the EXACT companion for the weapon's motion, so a weapon-state variant this detector
	// actually has is mirrored as-is (empty mag: anm_dry_empty -> anm_wpn_dry_empty, the short dry -
	// matching the weapon instead of playing the full one). Only when it has no such variant do we
	// strip the state infix and fall back to the base companion (anm_dry_aim_empty -> anm_wpn_dry_aim,
	// anm_idle_aim_empty_start -> anm_wpn_idle_aim_start).
	string256 alias;
	xr_sprintf(alias, "anm_wpn_%s", action);
	if (!isHUDAnimationExist(alias))
	{
		string256 clean;
		xr_strcpy(clean, action);
		companion_strip(clean, "_empty");
		companion_strip(clean, "_jammed");
		// ...and the FIRE MODE mark. The weapon's alias carries mask_firemode_<a|N> (anm_shoot ->
		// anm_shoot_auto), but a detector mirrors an ACTION, not a fire mode, and no detector defines a
		// per-mode companion -- so in auto every companion silently failed to resolve. The mark is per
		// weapon, so ask the weapon itself instead of hardcoding "_auto"/"_triple".
		attachable_hud_item* wh = g_player_hud ? g_player_hud->attached_item(0) : NULL;
		CWeaponMagazined* wm = wh ? smart_cast<CWeaponMagazined*>(wh->m_parent_hud_item) : NULL;
		if (wm)
		{
			string64 marked;	xr_sprintf(marked, "anm_%s", action);
			LPCSTR mark = wm->GetFireModeMark(marked);
			if (mark && mark[0])	companion_strip(clean, mark);
		}
		xr_sprintf(alias, "anm_wpn_%s", clean);
		if (!isHUDAnimationExist(alias))	return false;
	}
	// A one-shot companion that JUST ended must not be re-picked by the idle mirror: the weapon can
	// still be playing its own one-shot (dry), so we'd re-select the motion we already finished and
	// freeze on its last frame. Fall through -> our own idle/moving plays instead.
	if (!bRestart && m_companion_done.size() && 0 == xr_strcmp(m_companion_done.c_str(), alias))
		return false;
	// Already playing this exact companion -> don't restart it, UNLESS the weapon itself just
	// (re)started the motion (bRestart, set only by the PlayHUDMotion hook). Without the guard, the
	// PlayAnimIdle mirror restarted it on every movement change and double-played the draw / jerked the
	// loop; without the bRestart escape, a repeated one-shot (dry-fire spam) only ever played once.
	if (!bRestart && 0 == xr_strcmp(m_current_motion.c_str(), alias))	return true;
	PlayHUDMotion(alias, TRUE, this, eIdle);
	// a loop keeps running by itself after OnAnimationEnd; a one-shot holds its last frame, so we must
	// route back to our own idle when it finishes (see OnAnimationEnd)
	// (read the flag directly: CMotionDef::StopAtEnd() isn't const, m_current_motion_def is)
	m_bCompanionOneShot = (m_current_motion_def && (m_current_motion_def->flags & esmStopAtEnd) != 0);
	return true;
}

// One-shot torch/NV toggle gesture played on the detector's HUD (left hand). Detectors have no
// magazine/GL, so only the plain alias (anm_headlamp_on/off, anm_nv_on/off) is used.
bool CCustomDetector::PlayHudActionAnim(LPCSTR base)
{
	if (!IsWorking() || GetState() != eIdle || IsPending())	return false;	// not shown/idle -> skip
	if (!isHUDAnimationExist(base))							return false;	// this detector has no such gesture
	m_action_anim = base;
	SwitchState		(eDetActionAnim);
	return true;
}

void CCustomDetector::UpdateXForm()
{
	CInventoryItem::UpdateXForm();
}

void CCustomDetector::OnActiveItem()
{
	return;
}

void CCustomDetector::OnHiddenItem()
{
}

CCustomDetector::CCustomDetector() 
{
	m_ui				= NULL;
	m_bFastAnimMode		= false;
	m_bNeedActivation	= false;
	m_bNeedActivationManual	= false;
	m_bAutoToggle		= false;
	m_bCompanionOneShot	= false;
}

CCustomDetector::~CCustomDetector() 
{
	m_artefacts.destroy		();
	TurnDetectorInternal	(false);
	xr_delete				(m_ui);
}

BOOL CCustomDetector::net_Spawn(CSE_Abstract* DC)
{
	TurnDetectorInternal(false);
	m_bEmergencyShow = false;
	return		(inherited::net_Spawn(DC));
}

void CCustomDetector::Load(LPCSTR section) 
{
	inherited::Load			(section);

	m_fAfDetectRadius		= pSettings->r_float(section,"af_radius");
	m_fAfVisRadius			= pSettings->r_float(section,"af_vis_radius");
	m_artefacts.load		(section, "af");

	m_sounds.LoadSound( section, "snd_draw", "sndShow");
	m_sounds.LoadSound( section, "snd_holster", "sndHide");
}


void CCustomDetector::shedule_Update(u32 dt) 
{
	inherited::shedule_Update(dt);
	
	if( !IsWorking() )			return;

	Position().set(H_Parent()->Position());

	Fvector						P; 
	P.set						(H_Parent()->Position());
	m_artefacts.feel_touch_update(P,m_fAfDetectRadius);
}


bool CCustomDetector::IsWorking()
{
	return m_bWorking && H_Parent() && H_Parent()==Level().CurrentViewEntity();
}

void CCustomDetector::UpfateWork()
{
	UpdateAf				();
	m_ui->update			();
}

void CCustomDetector::UpdateVisibility()
{


	//check visibility
	attachable_hud_item* i0		= g_player_hud->attached_item(0);
	if(i0 && HudItemData())
	{
		CWeapon* wpn			= smart_cast<CWeapon*>(i0->m_parent_hud_item);
		if(wpn)
		{
			u32 state			= wpn->GetState();
			bool bClimb			= ( (Actor()->MovingState()&mcClimb) != 0 );
			// aiming normally holsters the detector; a companion detector (has aim companion) stays
			// out & mirrors the ADS pose. Reload still hides it: the pistol plays its own one-handed
			// _detector reload (which shows the detector in-hand), so the real detector must be hidden.
			bool companion		= isHUDAnimationExist("anm_wpn_idle_aim");
			bool zoom_hides		= wpn->IsZoomed() && !companion;
			if(bClimb || zoom_hides || state==CWeapon::eReload || state==CWeapon::eSwitch
				|| AnimForbidsDetector(i0->m_parent_hud_item))
			{
				HideDetector		(true);
				m_bNeedActivation	= true;
				// engine-driven hide (aim/reload/climb) -> the matching re-show is engine-driven too,
				// so drop any pending manual request: the right hand is busy with that action, not
				// with handing the detector over.
				m_bNeedActivationManual	= false;
			}
		}
	}else
	if(m_bNeedActivation)
	{
		attachable_hud_item* i0_		= g_player_hud->attached_item(0);
		bool bClimb					= ( (Actor()->MovingState()&mcClimb) != 0 );
		if(!bClimb)
		{
			// Show the detector only once the item in hand is actually COMPATIBLE with it (GunsXRay-style).
			// The old check tested only zoom/reload/switch, which is TRUE while an incompatible weapon is
			// still HOLSTERING (eHiding) -> ShowDetector fired too early, CheckCompatibilityInt(pending
			// weapon) returned false, and ToggleDetector's tail reset m_bNeedActivation, so the deferred
			// draw was lost (the substitute item came out but the detector never did).
			// for_draw: this IS the draw decision, so an item still rising holds the detector back
			// until its motion ends -- that is the whole point of the deferral.
			CHudItem* huditem		= (i0_)?i0_->m_parent_hud_item : NULL;
			bool bChecked			= !huditem || CheckCompatibilityInt(huditem, 0, true);
			// A pending request only makes sense while the detector is actually away. ShowDetector()
			// is a no-op when it isn't, but ToggleDetector() is NOT -- called with the detector already
			// out it would TOGGLE, i.e. put it away by itself. Consume the stale request instead.
			if(bChecked && GetState()!=eHidden)
			{
				m_bNeedActivation		= false;
				m_bNeedActivationManual	= false;
			}
			else if(bChecked)
			{
				if (m_bNeedActivationManual)
				{
					// the press was deferred: replay it as a MANUAL toggle now that the right hand is
					// free, so ToggleDetector takes the !m_bAutoToggle path -> the weapon plays
					// anm_prepare_detector and the detector appears from ShowAfterPrepare with
					// anm_draw_detector. ShowDetector() would set m_bAutoToggle and skip both.
					m_bNeedActivationManual	= false;
					m_bAutoToggle			= false;
					ToggleDetector			(true);		// fast anims: something is in the other hand
				}
				else
					ShowDetector		(true);
			}
		}
	}
}

void CCustomDetector::UpdateCL() 
{
	inherited::UpdateCL();

	UpdateVisibility		();

	if( !IsWorking() )		return;
	UpfateWork				();
}

void CCustomDetector::OnH_A_Chield() 
{
	inherited::OnH_A_Chield		();
}

void CCustomDetector::OnH_B_Independent(bool just_before_destroy) 
{
	inherited::OnH_B_Independent(just_before_destroy);
	
	m_artefacts.clear			();
}


void CCustomDetector::OnMoveToRuck(EItemPlace prev)
{
	inherited::OnMoveToRuck	(prev);
	if(GetState()==eIdle)
	{
		SwitchState					(eHidden);
		g_player_hud->detach_item	(this);
	}
	TurnDetectorInternal	(false);
}

void CCustomDetector::OnMoveToSlot()
{
	inherited::OnMoveToSlot	();
}

void CCustomDetector::TurnDetectorInternal(bool b)
{
	m_bWorking				= b;
	if(b && m_ui==NULL)
	{
		CreateUI			();
	}else
	{
//.		xr_delete			(m_ui);
	}

	UpdateNightVisionMode	(b);
}



#include "game_base_space.h"
void CCustomDetector::UpdateNightVisionMode(bool b_on)
{
}

BOOL CAfList::feel_touch_contact	(CObject* O)
{
	CLASS_ID	clsid			= O->CLS_ID;
	TypesMapIt it				= m_TypesMap.find(clsid);

	bool res					 = (it!=m_TypesMap.end());
	if(res)
	{
		CArtefact*	pAf				= smart_cast<CArtefact*>(O);
		
		if(pAf->GetAfRank()>m_af_rank)
			res = false;
	}
	return						res;
}
