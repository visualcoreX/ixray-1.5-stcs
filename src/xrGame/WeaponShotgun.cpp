#include "stdafx.h"
#include "weaponshotgun.h"
#include "entity.h"
#include "ParticlesObject.h"
#include "xr_level_controller.h"
#include "inventory.h"
#include "level.h"
#include "actor.h"
#include "object_broker.h"		// READ_IF_EXISTS

CWeaponShotgun::CWeaponShotgun()
{
	m_eSoundClose			= ESoundTypes(SOUND_TYPE_WEAPON_SHOOTING);
	m_eSoundAddCartridge	= ESoundTypes(SOUND_TYPE_WEAPON_SHOOTING);
	bStopReloadSignal		= false;
	m_bReloadEmpty			= false;
	m_bJustAfterReload		= false;
	m_bPreloaded			= false;
	m_bAddCartridgeInOpen	= false;
	m_bEmptyPreloadMode		= false;
	m_bChamberFirstRound	= false;
	m_bTriUnjamming			= false;
	m_bTriInsertDone		= false;
	m_dwTriInsertTm			= 0;
	m_dwTriPhaseTm			= 0;
}

CWeaponShotgun::~CWeaponShotgun()
{
}

void CWeaponShotgun::net_Destroy()
{
	inherited::net_Destroy();
}

void CWeaponShotgun::Load	(LPCSTR section)
{
	inherited::Load		(section);

	if(pSettings->line_exist(section, "tri_state_reload")){
		m_bTriStateReload = !!pSettings->r_bool(section, "tri_state_reload");
	};
	if(m_bTriStateReload){
		m_sounds.LoadSound(section, "snd_open_weapon", "sndOpen", false, m_eSoundOpen);

		m_sounds.LoadSound(section, "snd_add_cartridge", "sndAddCartridge", false, m_eSoundAddCartridge);

		m_sounds.LoadSound(section, "snd_close_weapon", "sndClose", false, m_eSoundClose);

		LoadPhaseAnmSounds	(section);

		// pump feed order: the round chambered on an empty-start reload fires first, then the tube LIFO
		m_bChamberFirstRound = READ_IF_EXISTS(pSettings, r_bool, section, "chamber_first_round", FALSE);
	};

}

// GS per-anim reload sounds: snd_<anim> played when the matching variant plays, else the fixed
// sndOpen/sndAddCartridge/sndClose fall back. Load each that the config actually defines; the
// label is the config key itself ("snd_anm_..."), so PlayReloadPhaseSound can look it up by anim.
// These live in the WEAPON section (that is where our shotgun configs put them), not in the hud one.
void CWeaponShotgun::LoadPhaseAnmSounds(LPCSTR section)
{
	if (!section || !section[0] || !pSettings->section_exist(section))	return;

	static const char* s_open[]  = { "anm_open", "anm_open_empty", "anm_open_first" };
	static const char* s_add[]   = { "anm_add_cartridge", "anm_add_cartridge_empty", "anm_add_cartridge_first",
		"anm_add_cartridge_preloaded", "anm_add_cartridge_empty_preloaded", "anm_add_cartridge_first_preloaded" };
	static const char* s_close[] = { "anm_close", "anm_close_final", "anm_close_first", "anm_close_first_final",
		"anm_close_empty", "anm_close_empty_final", "anm_close_preloaded", "anm_close_preloaded_final",
		"anm_close_first_preloaded", "anm_close_first_preloaded_final", "anm_close_empty_preloaded",
		"anm_close_empty_preloaded_final" };
	string_path key;
	auto load = [&](const char* a, ESoundTypes t)
	{
		strconcat(sizeof(key), key, "snd_", a);
		if (!pSettings->line_exist(section, key))	return;
		if (m_sounds.FindSoundItem(key, false))		return;		// already registered
		m_sounds.LoadSound(section, key, key, false, t);
	};
	for (auto a : s_open)	load(a, m_eSoundOpen);
	for (auto a : s_add)	load(a, m_eSoundAddCartridge);
	for (auto a : s_close)	load(a, m_eSoundClose);
}

// A hud-section change (an upgrade repointing `hud`, e.g. the winchester's TAC grip ->
// [wpn_winchester1300_tac_hud]) re-runs LoadAnmSounds, whose first act is RemoveSounds("snd_anm_") --
// which threw away the per-phase reload sounds above, because they come from the WEAPON section and the
// base pass only re-reads the hud one. The empty reload then fell back to the plain sndOpen (user report
// 2026-08-05: "у tac версии худа winchester не работает звук на анимацию пустой перезарядки"). Re-register
// them after every wipe.
void CWeaponShotgun::LoadAnmSounds()
{
	inherited::LoadAnmSounds	();
	if (m_bTriStateReload)
		LoadPhaseAnmSounds		(*cNameSect());
}

// Play snd_<anim> if the config defined & loaded it for this exact variant; otherwise the fixed fallback
// label (sndOpen/sndAddCartridge/sndClose). Mirrors GS PlaySoundByAnimName's fallback contract.
void CWeaponShotgun::PlayReloadPhaseSound(LPCSTR anim, LPCSTR fallback_label)
{
	string_path key;	strconcat(sizeof(key), key, "snd_", anim);
	if (m_sounds.FindSoundItem(key, false))	PlaySound(key, get_LastFP());
	else									PlaySound(fallback_label, get_LastFP());
}

// Build "<base>[_empty][_preloaded][_final]" and return the most specific alias that actually exists
// (so a config that only defines a subset still resolves). Mirrors GS's anm_open/add/close selectors.
void CWeaponShotgun::SelectTriReloadAnim(LPCSTR base, bool final_close, string_path& out)
{
	// Infix families, tried most-specific first. Emptiness (mutually exclusive per GS): when the mag is
	// currently empty prefer "_first" (the drum/tube "no shell" start, e.g. striker12_reload_noshell_start),
	// then "_empty"; otherwise no emptiness infix. Then optional "_preloaded" (a round seated during the
	// empty open) and "_final" (the close that tops off / chambers). Returns the first alias that exists.
	// GS ModifierStd order: an EMPTY mag -> "_empty"; a non-empty mag that was just reloaded with no shot
	// since -> "_first" (the drum's fresh-round start, replayed every top-off until you fire). Mutually
	// exclusive. Falls through to no emptiness infix when neither applies.
	const char* emps[2]; int ne = 0;
	if (iAmmoElapsed == 0)			emps[ne++] = "_empty";
	else if (m_bJustAfterReload)	emps[ne++] = "_first";
	emps[ne++] = "";
	const char* pres[2]; int np = 0;
	if (m_bPreloaded)		pres[np++] = "_preloaded";
	pres[np++] = "";
	const char* fins[2]; int nf = 0;
	if (final_close)		fins[nf++] = "_final";
	fins[nf++] = "";

	string_path cand;
	for (int e = 0; e < ne; ++e)
		for (int p = 0; p < np; ++p)
			for (int f = 0; f < nf; ++f)
			{
				xr_strcpy(cand, base);
				xr_strcat(cand, emps[e]);
				xr_strcat(cand, pres[p]);
				xr_strcat(cand, fins[f]);
				if (isHUDAnimationExist(cand))	{ xr_strcpy(out, cand); return; }
			}
	xr_strcpy(out, base);	// nothing matched -> the plain base (PlayHUDMotion still guards existence)
}

// Read lock_time_start_/lock_time_end_/lock_time_ for the just-played <anim> and arm the shell-insert
// and phase-advance timers. inserts_shell = this phase seats a round (add_cartridge, or open when
// add_cartridge_in_open). If no lock_time is configured, leaves m_dwTriPhaseTm=0 -> OnAnimationEnd drives it.
void CWeaponShotgun::ArmTriReloadPhase(LPCSTR anim, bool inserts_shell)
{
	m_sTriCurAnim		= anim;
	m_bTriInsertDone	= !inserts_shell;
	m_dwTriInsertTm		= 0;
	m_dwTriPhaseTm		= 0;

	string_path key;
	strconcat(sizeof(key), key, "lock_time_start_", anim);
	float ls = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, -1.0f);
	strconcat(sizeof(key), key, "lock_time_end_", anim);
	float le = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, -1.0f);
	strconcat(sizeof(key), key, "lock_time_", anim);
	float single = READ_IF_EXISTS(pSettings, r_float, HudSection(), key, -1.0f);

	u32 now = Device.dwTimeGlobal;
	if (inserts_shell && ls >= 0.0f)
	{
		m_dwTriInsertTm	= now + u32(ls * 1000.0f);					// seat the shell mid-anim
		m_dwTriPhaseTm	= now + u32((ls + (le >= 0.0f ? le : 0.0f)) * 1000.0f);	// then advance
	}
	else if (single >= 0.0f)
	{
		m_dwTriPhaseTm	= now + u32(single * 1000.0f);				// timed phase, shell (if any) at phase end
	}
	// else: no lock_time -> m_dwTriPhaseTm stays 0 -> OnAnimationEnd(eReload) advances (legacy behaviour)
}

void CWeaponShotgun::switch2_Fire	()
{
	inherited::switch2_Fire	();
	bWorking = false;
	m_bJustAfterReload = false;	// a shot was fired -> the drum is no longer "just reloaded" (clears _first)
}

bool CWeaponShotgun::SwitchAmmoType(u32 flags)
{
	if (IsTriStateReload() && iAmmoElapsed == iMagazineSize)
		return false;

	return inherited::SwitchAmmoType(flags);
}

bool CWeaponShotgun::Action(s32 cmd, u32 flags) 
{
	if (inherited::Action(cmd, flags))
		return true;

	// GS CWeaponShotgun__Action_OnStopReload (WeaponAmmoCounter.pas:351): the fire key cuts a tri-state
	// reload short -- but NOT while the gun is jammed. The jam-clear gesture must play out, otherwise the
	// trigger pull (which on a jammed gun only produces the dry click) would abort the only way to unjam.
	if (m_bTriStateReload && cmd == kWPN_FIRE && flags & CMD_START && GetState() == eReload && IsMisfire())
		return true;

	if (m_bTriStateReload && cmd == kWPN_FIRE && flags & CMD_START && GetState() == eReload && (m_sub_state == eSubstateReloadInProcess || m_sub_state == eSubstateReloadBegin))//���������� �����������
	{
		bStopReloadSignal = true;
		return true;
	}
	return false;
}

void CWeaponShotgun::OnAnimationEnd(u32 state)
{
	if (!m_bTriStateReload || state != eReload)
	{
		bStopReloadSignal = false;
		return inherited::OnAnimationEnd(state);
	}

	// When a lock_time is configured the phase is advanced by the UpdateCL timer, so ignore the natural
	// anim end (it comes later than lock_time_end -- the tail is meant to be cut). Only drive from the
	// anim end when there's no lock_time for this phase (legacy behaviour).
	if (m_dwTriPhaseTm != 0)
		return;

	AdvanceTriReload();
}

// The tri-state phase transition (open -> add x N -> close -> idle). Called either from the UpdateCL
// lock_time timer or, when no lock_time is set, from OnAnimationEnd. Does NOT itself seat a round unless
// the insert timer never fired (no lock_time_start), matching the legacy add-at-anim-end behaviour.
void CWeaponShotgun::AdvanceTriReload()
{
	// GS CWeaponMagazined__OnAnimationEnd_anm_open, jammed branch (WeaponAmmoCounter.pas:372): the jam-clear
	// gesture ENDS the cycle -- clear the misfire, reset the substate and go straight back to idle. It does
	// NOT continue into add_cartridge/close: topping the tube up is a separate reload the player asks for.
	if (m_bTriUnjamming)
	{
		m_bTriUnjamming		= false;
		bMisfire			= false;
		bStopReloadSignal	= false;
		bReloadKeyPressed	= false;
		bAmmotypeKeyPressed	= false;
		m_dwTriInsertTm		= 0;
		m_dwTriPhaseTm		= 0;
		m_sub_state			= eSubstateReloadBegin;
		// GS first_state_after_jammed (default true): after the revival the gun is "just handled" again, so
		// the next anims use the _first family exactly like after a reload.
		if (READ_IF_EXISTS(pSettings, r_bool, HudSection(), "first_state_after_jammed", TRUE))
			m_bJustAfterReload = true;
		SetPending			(FALSE);
		SwitchState			(eIdle);
		return;
	}

	switch(m_sub_state)
	{
		case eSubstateReloadBegin:
		{
			// open finished. add_cartridge_in_open already seated a shell (at the insert timer).
			if (bStopReloadSignal || m_magazine.size() >= (u32)iMagazineSize || !HaveCartridgeInInventory(1))
				m_sub_state = eSubstateReloadEnd;
			else
				m_sub_state = eSubstateReloadInProcess;
			SwitchState(eReload);
		}break;

		case eSubstateReloadInProcess:
		{
			if (!m_bTriInsertDone)		// no lock_time_start -> seat the shell now, at the anim end
			{
				AddCartridge(1);
				m_bTriInsertDone = true;
			}
			if (bStopReloadSignal || m_magazine.size() >= (u32)iMagazineSize || !HaveCartridgeInInventory(1))
				m_sub_state = eSubstateReloadEnd;
			SwitchState(eReload);
		}break;

		case eSubstateReloadEnd:
		{
			bStopReloadSignal = false;
			bReloadKeyPressed = false;
			bAmmotypeKeyPressed = false;
			m_dwTriInsertTm = 0;
			m_dwTriPhaseTm  = 0;
			m_bJustAfterReload = true;	// reloaded, no shot since -> next reload plays the _first family
			SwitchState(eIdle);
		}break;
	};
}

void CWeaponShotgun::UpdateCL()
{
	inherited::UpdateCL();

	if (!m_bTriStateReload || GetState() != eReload)
		return;

	// seat the shell mid-anim at lock_time_start (GS OnAddCartridge)
	if (m_dwTriInsertTm && Device.dwTimeGlobal >= m_dwTriInsertTm)
	{
		m_dwTriInsertTm = 0;
		if (!m_bTriInsertDone)
		{
			AddCartridge(1);
			m_bTriInsertDone = true;
		}
	}

	// advance to the next phase at lock_time_start+lock_time_end (the anim tail is cut) -- GS lock_time_end
	if (m_dwTriPhaseTm && Device.dwTimeGlobal >= m_dwTriPhaseTm)
	{
		m_dwTriPhaseTm = 0;
		AdvanceTriReload();
	}
}

void CWeaponShotgun::Reload()
{
	if (!m_bTriStateReload)		{ inherited::Reload(); return; }	// the magazined path runs the gate itself

	// "is there anything to do at all" FIRST (same test TriStateReload makes), so a reload press with an
	// empty backpack does not drop the sights for nothing.
	if (!IsMisfire() && !HaveCartridgeInInventory(1))	return;

	// The tri-state (pump/break-action) reload starts here and NEVER goes through CWeaponMagazined::Reload,
	// so it has to run the shared pre-reload gate itself: the jam-inspect / light-misfire blocks, the
	// detector check and -- the visible one -- "reload pressed while aiming lowers the sights first, then
	// blends into the reload". Without it every shotgun jumped straight into the reload from the ADS pose.
	if (!ReloadGate())			return;

	TriStateReload();
}

void CWeaponShotgun::TriStateReload()
{
	// GS CWeaponShotgun_Needreload (WeaponAmmoCounter.pas:407): `jammed OR have cartridges`. Clearing a jam
	// needs no spare shells and no room in the tube -- without this a jammed shotgun with an empty backpack
	// (or a full magazine) could not be touched at all: reload did nothing and the gun stayed jammed forever.
	if( !IsMisfire() && !HaveCartridgeInInventory(1) )return;
	CWeapon::Reload		();
	// GS tri-state options live in the HUD section (valid now that the weapon is in hand)
	m_bAddCartridgeInOpen	= READ_IF_EXISTS(pSettings, r_bool, HudSection(), "add_cartridge_in_open", FALSE);
	m_bEmptyPreloadMode		= READ_IF_EXISTS(pSettings, r_bool, HudSection(), "empty_preload_mode", FALSE);
	m_bReloadEmpty		= (iAmmoElapsed == 0);	// remember for the whole reload (the _empty family)
	m_bPreloaded		= false;
	m_bTriUnjamming		= false;
	m_dwTriInsertTm		= 0;
	m_dwTriPhaseTm		= 0;
	m_sub_state			= eSubstateReloadBegin;
	SwitchState			(eReload);
}

void CWeaponShotgun::OnStateSwitch	(u32 S)
{
	if(!m_bTriStateReload || S != eReload)
	{
		bStopReloadSignal = false;
		inherited::OnStateSwitch(S);
		return;
	}

	CWeapon::OnStateSwitch(S);

	// JAM CLEAR (GS): the open phase of a jammed gun is the revival gesture, so it must be reached whatever
	// the magazine/inventory look like -- the "nothing to load" shortcut below would otherwise drop straight
	// to switch2_EndReload (close only) and the misfire would never be cleared.
	if (IsMisfire() && !m_bTriUnjamming)
	{
		m_sub_state = eSubstateReloadBegin;		// whatever the cycle was doing, the jam comes first
		switch2_StartReload();
		return;
	}

	if( m_magazine.size() == (u32)iMagazineSize || !HaveCartridgeInInventory(1) ){
			switch2_EndReload		();
			m_sub_state = eSubstateReloadEnd;
			return;
	};

	switch (m_sub_state)
	{
	case eSubstateReloadBegin:
		if( HaveCartridgeInInventory(1) )
			switch2_StartReload	();
		break;
	case eSubstateReloadInProcess:
			if( HaveCartridgeInInventory(1) )
				switch2_AddCartgidge	();
		break;
	case eSubstateReloadEnd:
			switch2_EndReload		();
		break;
	};
}

void CWeaponShotgun::switch2_StartReload()
{
	// GS anm_open_selector (WeaponAnims.pas:765): while jammed the open phase plays the revival instead --
	// anm_reload_jammed[_last] with its own sound; AdvanceTriReload then ends the cycle (no add/close).
	if (IsMisfire())
	{
		m_bTriUnjamming		= true;
		m_bTriInsertDone	= true;					// no shell is seated by the revival
		m_dwTriInsertTm		= 0;
		// no phase timer: like GS the revival runs to its own end (a config lock_time_anm_reload_jammed is
		// already honoured by PlayHUDMotion, which shortens the motion end -> OnAnimationEnd).
		m_dwTriPhaseTm		= 0;
		// ...unless the weapon has no revival motion at all: then there is no anim end to wait for, so
		// advance on the next frame instead of leaving the gun stuck in eReload (jammed forever again).
		if (0 == PlayAnimUnjamWeapon())				// selects the variant -> m_sTriCurAnim
			m_dwTriPhaseTm	= Device.dwTimeGlobal + 1;
		SetPending			(TRUE);
		return;
	}

	PlayAnimOpenWeapon	();							// selects the variant -> m_sTriCurAnim
	PlayReloadPhaseSound(m_sTriCurAnim.c_str(), "sndOpen");
	// empty + preload mode: anm_open_empty seats one round into the chamber -> the follow-up phases use
	// their _preloaded variants and don't double-count it.
	if (m_bReloadEmpty && m_bEmptyPreloadMode)
		m_bPreloaded = true;
	ArmTriReloadPhase	(m_sTriCurAnim.c_str(), m_bAddCartridgeInOpen);	// open only seats a shell if add_cartridge_in_open
	SetPending			(TRUE);
}

void CWeaponShotgun::switch2_AddCartgidge	()
{
	PlayAnimAddOneCartridgeWeapon();
	PlayReloadPhaseSound(m_sTriCurAnim.c_str(), "sndAddCartridge");
	ArmTriReloadPhase	(m_sTriCurAnim.c_str(), true);	// this phase seats a round
	SetPending			(TRUE);
}

void CWeaponShotgun::switch2_EndReload	()
{
	SetPending			(FALSE);
	PlayAnimCloseWeapon	();
	PlayReloadPhaseSound(m_sTriCurAnim.c_str(), "sndClose");
	ArmTriReloadPhase	(m_sTriCurAnim.c_str(), false);	// close seats nothing; timer (or anim end) -> idle
}

void CWeaponShotgun::PlayAnimOpenWeapon()
{
	VERIFY(GetState()==eReload);
	string_path anim;	SelectTriReloadAnim("anm_open", false, anim);
	// bMixIn TRUE: open is the FIRST motion of a tri-state reload, i.e. the one that follows the
	// aim-out when reload is pressed from the sights. With FALSE it hard-cut every running blend, so
	// the aim-out tail was killed outright and reload_aim_out_accrue had nothing to ramp -- the seam
	// stayed a snap on pump guns while it was smooth everywhere else (CWeaponMagazined::PlayAnimReload
	// plays TRUE for all of its variants, and so does the jam-clear PlayAnimUnjamWeapon here).
	// The inner phases (add_cartridge / close) deliberately keep FALSE: they chain inside the reload
	// cycle, where the hard cut is what keeps the shell-by-shell loop crisp.
	PlayHUDMotion(anim,TRUE,this,GetState());
	m_sTriCurAnim = anim;
}
void CWeaponShotgun::PlayAnimAddOneCartridgeWeapon()
{
	VERIFY(GetState()==eReload);
	string_path anim;	SelectTriReloadAnim("anm_add_cartridge", false, anim);
	PlayHUDMotion(anim,FALSE,this,GetState());
	m_sTriCurAnim = anim;
	m_bPreloaded = false;	// GS SetPreloadedStatus(false): only the first insert after an empty open is "_preloaded"
}
// The jam-clear gesture, GS anm_open_selector's jammed branch: anm_reload_jammed, or anm_reload_jammed_last
// when the gun is also empty (the last round is what jammed). Sound: snd_reload_jammed[_last] (labels
// sndReloadMis/sndReloadMisLast), or the per-anim snd_<alias> when the config defines one.
u32 CWeaponShotgun::PlayAnimUnjamWeapon()
{
	VERIFY(GetState()==eReload);
	string_path anim;	xr_strcpy(anim, "anm_reload_jammed");
	LPCSTR snd = "sndReloadMis";
	if (iAmmoElapsed == 0 && isHUDAnimationExist("anm_reload_jammed_last"))
	{
		xr_strcpy(anim, "anm_reload_jammed_last");
		if (m_sounds.FindSoundItem("sndReloadMisLast", false))	snd = "sndReloadMisLast";
	}
	if (!isHUDAnimationExist(anim))	xr_strcpy(anim, "anm_reload");	// GS ModifierStd fallback
	if (!isHUDAnimationExist(anim))	return 0;						// nothing to play at all

	u32 t = PlayHUDMotion(anim, TRUE, this, GetState());
	m_sTriCurAnim = anim;

	// per-anim sound first (snd_anm_reload_jammed...), else the fixed jam-clear label; a weapon that defines
	// neither stays silent rather than borrowing sndOpen (which is the pump/drum sound, wrong for a revival).
	string_path key;	strconcat(sizeof(key), key, "snd_", anim);
	if (m_sounds.FindSoundItem(key, false))			PlaySound(key, get_LastFP());
	else if (m_sounds.FindSoundItem(snd, false))	PlaySound(snd, get_LastFP());
	return t;
}

void CWeaponShotgun::PlayAnimCloseWeapon()
{
	VERIFY(GetState()==eReload);
	// the close that tops the magazine off gets the _final variant (chambers/racks the last round);
	// a close after stopping early (mag not full) uses the plain anm_close.
	bool final_close = (m_magazine.size() >= (u32)iMagazineSize);
	string_path anim;	SelectTriReloadAnim("anm_close", final_close, anim);
	PlayHUDMotion(anim,FALSE,this,GetState());
	m_sTriCurAnim = anim;
	m_bPreloaded = false;	// consumed by the close too (open-empty then immediate close, no tube shell added)
}

bool CWeaponShotgun::HaveCartridgeInInventory		(u8 cnt)
{
	if (unlimited_ammo()) return true;
	m_pAmmo = NULL;
	if(m_pInventory) 
	{
		//���������� ����� � ��������� ������� �������� ����
		m_pAmmo = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(*m_ammoTypes[m_ammoType]));

		// GS DISABLE_AUTOAMMOCHANGE: only fall back to another ammo type when the gun is empty or an explicit
		// type change was requested; with rounds loaded and no requested change, reload does nothing.
		if(!m_pAmmo && (m_set_next_ammoType_on_reload != u32(-1) || m_magazine.empty()))
		{
			for(u32 i = 0; i < m_ammoTypes.size(); ++i)
			{
				//��������� ������� ���� ���������� �����
				m_pAmmo = smart_cast<CWeaponAmmo*>(m_pInventory->GetAny(*m_ammoTypes[i]));
				if(m_pAmmo)
				{
					m_ammoType = i;
					break;
				}
			}
		}
	}
	return (m_pAmmo!=NULL)&&(m_pAmmo->m_boxCurr>=cnt) ;
}

u8 CWeaponShotgun::AddCartridge		(u8 cnt)
{
	if(IsMisfire())	bMisfire = false;

	if(m_set_next_ammoType_on_reload != u32(-1)){
		m_ammoType						= m_set_next_ammoType_on_reload;
		m_set_next_ammoType_on_reload	= u32(-1);

	}

	if( !HaveCartridgeInInventory(1) )
		return 0;

	VERIFY((u32)iAmmoElapsed == m_magazine.size());


	if (m_DefaultCartridge.m_LocalAmmoType != m_ammoType)
		m_DefaultCartridge.Load(*m_ammoTypes[m_ammoType], u8(m_ammoType));
	CCartridge l_cartridge = m_DefaultCartridge;
	while(cnt)// && m_pAmmo->Get(l_cartridge)) 
	{
		if (!unlimited_ammo())
		{
			if (!m_pAmmo->Get(l_cartridge)) break;
		}
		--cnt;
		++iAmmoElapsed;
		l_cartridge.m_LocalAmmoType = u8(m_ammoType);
		// pump chamber-first feed: a round loaded into an EMPTY gun goes to the chamber = m_magazine.back()
		// (fire pops the back -> it fires first). Any round loaded while a round is already chambered is a
		// TUBE round -> insert it just under the chambered one (before the back) so the chambered round still
		// fires first and the tube feeds LIFO. Based on the LIVE state, so it survives multi-cycle / ammo-type
		// switches (each switch restarts the reload). Off (default) or empty gun -> plain push_back.
		if (m_bChamberFirstRound && !m_magazine.empty())
			m_magazine.insert(m_magazine.end() - 1, l_cartridge);
		else
			m_magazine.push_back(l_cartridge);
//		m_fCurrentCartirdgeDisp = l_cartridge.m_kDisp;
	}
	m_ammoName = (m_pAmmo) ? m_pAmmo->m_nameShort : NULL;

	VERIFY((u32)iAmmoElapsed == m_magazine.size());

	//�������� ������� ��������, ���� ��� ������
	if(m_pAmmo && !m_pAmmo->m_boxCurr && OnServer()) 
		m_pAmmo->SetDropManual(TRUE);

	return cnt;
}

void	CWeaponShotgun::net_Export	(NET_Packet& P)
{
	inherited::net_Export(P);	
	P.w_u8(u8(m_magazine.size()));	
	for (u32 i=0; i<m_magazine.size(); i++)
	{
		CCartridge& l_cartridge = *(m_magazine.begin()+i);
		P.w_u8(l_cartridge.m_LocalAmmoType);
	}
}

void	CWeaponShotgun::net_Import	(NET_Packet& P)
{
	inherited::net_Import(P);	
	u8 AmmoCount = P.r_u8();
	for (u32 i=0; i<AmmoCount; i++)
	{
		u8 LocalAmmoType = P.r_u8();
		if (i>=m_magazine.size()) continue;
		CCartridge& l_cartridge = *(m_magazine.begin()+i);
		if (LocalAmmoType == l_cartridge.m_LocalAmmoType) continue;
#ifdef DEBUG
		Msg("! %s reload to %s", *l_cartridge.m_ammoSect, *(m_ammoTypes[LocalAmmoType]));
#endif
		l_cartridge.Load(*(m_ammoTypes[LocalAmmoType]), LocalAmmoType); 
	}
}
