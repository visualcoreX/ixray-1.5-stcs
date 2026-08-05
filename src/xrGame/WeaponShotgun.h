#pragma once

#include "weaponcustompistol.h"
#include "script_export_space.h"

class CWeaponShotgun :	public CWeaponCustomPistol
{
	typedef CWeaponCustomPistol inherited;
public:
					CWeaponShotgun		();
	virtual			~CWeaponShotgun		();

	virtual void	Load				(LPCSTR section);
	// per-reload-phase snd_anm_* (WEAPON section). Re-registered on every hud-section change, because
	// CWeaponMagazined::LoadAnmSounds wipes the whole snd_anm_ family and only re-reads the hud section.
	virtual void	LoadAnmSounds		();
	void			LoadPhaseAnmSounds	(LPCSTR section);

	virtual void	net_Destroy			();
	virtual void	net_Export			(NET_Packet& P);
	virtual void	net_Import			(NET_Packet& P);

	virtual void	Reload				();
	virtual void	switch2_Fire		();
	void			switch2_StartReload ();
	void			switch2_AddCartgidge();
	void			switch2_EndReload	();

	virtual void	PlayAnimOpenWeapon	();
	virtual void	PlayAnimAddOneCartridgeWeapon();
	void			PlayAnimCloseWeapon	();
	// GS anm_open_selector jammed branch: the jam-clear gesture that REPLACES the open phase.
	// Returns the motion length (0 = the weapon has no revival motion configured).
	u32				PlayAnimUnjamWeapon	();

	virtual bool	Action(s32 cmd, u32 flags);
	virtual bool	SwitchAmmoType(u32 flags);
	virtual void	UpdateCL			();
	// reloaded & no shot since & mag not empty -> the "_first" anim family (idle/gestures), like the
	// _first reload variant. PlayHUDMotion rewrites <anim> -> <anim>_first when this is true.
	virtual bool	NeedFirstAnim		() { return m_bJustAfterReload && iAmmoElapsed > 0; }
	virtual bool	JustAfterReload		() const { return m_bJustAfterReload; }	// feeds the shot's _first take
	// chamber-first pumps pin the chambered (fires-next) round at m_magazine.back(); the display must read
	// the newest LOADED round (size-2) instead. See CWeaponShotgun::AddCartridge chamber-first insert.
	virtual bool	GwrChamberAtBack	() const { return m_bChamberFirstRound; }

	bool			bStopReloadSignal;

protected:
	virtual void	OnAnimationEnd		(u32 state);
	void			TriStateReload		();
	virtual void	OnStateSwitch		(u32 S);

	bool			HaveCartridgeInInventory(u8 cnt);
	virtual u8		AddCartridge		(u8 cnt);

	// ---- Gunslinger-style tri-state reload: per-shell insert with lock_time phasing + variants ----
	// build "<base>[_empty][_preloaded][_final]" picking the most specific alias that exists in config
	void			SelectTriReloadAnim	(LPCSTR base, bool final_close, string_path& out);
	// play snd_<anim> for this reload variant if configured, else the fixed fallback label (GS-style)
	void			PlayReloadPhaseSound(LPCSTR anim, LPCSTR fallback_label);
	// read lock_time_start_/lock_time_end_/lock_time_ for <anim> and arm the insert + phase-end timers
	void			ArmTriReloadPhase	(LPCSTR anim, bool inserts_shell);
	// do the phase transition (what OnAnimationEnd(eReload) used to do); driven by the lock timer
	void			AdvanceTriReload	();

	bool			m_bReloadEmpty;			// mag was empty when this reload started (_empty family)
	bool			m_bJustAfterReload;		// reloaded and NO shot fired since -> the "_first" reload family
											// (GS IsJustAfterReload: set on reload end, cleared on the next shot)
	bool			m_bPreloaded;			// a shell got preloaded during the empty open (empty_preload_mode)
	bool			m_bAddCartridgeInOpen;	// config add_cartridge_in_open: a shell is seated during anm_open
	bool			m_bEmptyPreloadMode;	// config empty_preload_mode
	bool			m_bChamberFirstRound;	// config chamber_first_round: on an empty-start reload the round
											// seated into the chamber (first loaded) fires FIRST, then the tube
											// feeds LIFO (last-loaded next). Off = plain LIFO. Pump-shotgun only.
	bool			m_bTriUnjamming;		// the current "open" phase is the jam-clear gesture (anm_reload_jammed):
											// it ends the reload instead of going on to add/close (GS)
	bool			m_bTriInsertDone;		// the shell for the current phase has been added
	u32				m_dwTriInsertTm;		// wall-clock to add the shell (lock_time_start); 0 = none
	u32				m_dwTriPhaseTm;			// wall-clock to advance to the next phase; 0 = fall back to OnAnimationEnd
	shared_str		m_sTriCurAnim;			// alias currently playing (for the lock_time_* lookup)

	ESoundTypes		m_eSoundOpen;
	ESoundTypes		m_eSoundAddCartridge;
	ESoundTypes		m_eSoundClose;

	DECLARE_SCRIPT_REGISTER_FUNCTION
};
add_to_type_list(CWeaponShotgun)
#undef script_type_list
#define script_type_list save_type_list(CWeaponShotgun)
