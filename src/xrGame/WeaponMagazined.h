#pragma once

#include "weapon.h"
#include "hudsound.h"
#include "ai_sounds.h"

class ENGINE_API CMotionDef;
class CBoneInstance;

//размер очереди считается бесконечность
//заканчиваем стрельбу, только, если кончились патроны
#define WEAPON_ININITE_QUEUE -1


class CWeaponMagazined: public CWeapon
{
private:
	typedef CWeapon inherited;
protected:
	//звук текущего выстрела
	shared_str		m_sSndShotCurrent;

	//дополнительная информация о глушителе
	LPCSTR			m_sSilencerFlameParticles;
	LPCSTR			m_sSilencerSmokeParticles;

	ESoundTypes		m_eSoundShow;
	ESoundTypes		m_eSoundHide;
	ESoundTypes		m_eSoundShot;
	ESoundTypes		m_eSoundEmptyClick;
	ESoundTypes		m_eSoundReload;
	// General
	//кадр момента пересчета UpdateSounds
	u32				dwUpdateSounds_Frame;
protected:

	virtual void	switch2_Idle	();
	virtual void	switch2_Fire	();
	virtual void	switch2_Empty	();
	virtual void	switch2_Reload	();
	virtual void	switch2_Hiding	();
	virtual void	switch2_Hidden	();
	virtual void	switch2_ActionAnim	();
	// Pick the HUD-animation variant for `base` (e.g. "anm_headlamp_on") from the weapon's state.
	// Overridden by the GL subclass to add _w_gl / _g variants. Returns "" if none exists.
	virtual void	SelectActionAnim	(LPCSTR base, string_path& result);
public:
	// Play a one-shot fire-locked HUD gesture (headlamp/NV toggle) on this weapon. Returns
	// false (so the caller can fall back to the generic left-hand animator) if the weapon is
	// busy or the config has no matching anm_* alias.
	virtual bool	PlayHudActionAnim	(LPCSTR base);

	// --- GS controller suicide (wpnpatch ControllerMonster.pas). The actor drives the sequence
	// (CActor::StartControllerSuicide); the weapon only says whether it can be used for it and
	// swaps the shot animation while it runs.
	bool			CanSuicide			() const;
	bool			SuicideByAnimation	() const;	// GS `suicide_by_animation`: anm_suicide, else the hud offset
	bool			HasSuicideHudOffset	() const;	// a non-zero hud_move_suicide_offset to travel to
	// GS GetAmmoInMagCount: the RIFLE magazine in either mode (in GL mode iAmmoElapsed is the grenades)
	virtual int		SuicideRifleAmmo	() const { return iAmmoElapsed; }
	u32				SuicideStart		();	// play anm_suicide, return its lock_time in ms (0 = cannot)
	void			SuicideForceIdle	();	// leave the gesture state so the shot can go out
	virtual void	SuicideShoot		();	// end the gesture and fire the round into one's own head
	void			PlaySuicideSound	();	// snd_suicide, if this weapon has one
	void			SuicideStopFire		();	// release the trigger (the actor is dead now)
	void			SuicideAbort		();	// grab broken -> lower the weapon (anm_stop_suicide)
	bool			m_bSuicideShot;
	bool			m_bNeedFirstShootAnims;	// section `need_first_shoot_anims`
	bool			m_bActionAnimNoCB;	// play the action gesture without a state callback (GS PlayCustomAnim)
protected:
	shared_str		m_action_anim;

	// --- fire-mode selector (single<->auto): light firemode -- play a switch gesture, then HOLD the
	// selector bone's last-frame pose across all other anims (config `fire_mode_bone` names the bone).
	shared_str		m_fire_mode_bone;		// HUD-model bone of the fire selector ("" = feature off)
	u16				m_fire_mode_bone_id;	// resolved id (BI_NONE if absent)
	Fmatrix			m_fire_selector_xform;	// captured/held local transform of the selector bone
	bool			m_fire_selector_hold;	// true (auto) = override bone to xform
	bool			m_fire_selector_capturing;	// true during the switch gesture = sample the bone
	bool			m_fire_selector_valid;	// the captured/restored pose is valid (guard vs identity)
	bool			m_fire_selector_cb;		// callback currently attached to the live HUD model
	bool			m_selector_model_warm;	// the selector bone has been through >=1 NORMAL render calc (model posed)
	u8				m_selector_sample_tries;	// bounded retries of the auto-pose sample (model may not be posed yet on attach)
	void			UpdateFireSelectorBone	();	// (re)attach callback to the current HUD model
	void			DetachFireSelectorBone	();
	void			SampleFireSelectorAutoPose();	// derive the auto selector pose from the anim's last frame
	bool			IsActorSprinting		();	// parent actor currently in the sprint movement state
	bool			DetectorCompanionOut	();	// a detector is out in the left hand (hud idx 1)
public:
	// play the "take out / put away the detector" hand gesture on the weapon (anm_draw_detector /
	// anm_prepare_detector) when the detector is toggled with this weapon in hand. Returns true if played.
	bool			PlayDetectorGesture		(bool draw);
	// two-phase detector DRAW: play anm_prepare_detector (hand goes off-screen) now; when it ends the
	// detector is actually shown + anm_draw_detector (hand returns) plays. Returns true if it deferred.
	bool			BeginDetectorDraw		();
	// HOLSTER: anm_holster_detector (= <pref>_hand_draw) returns the support hand to idle. It plays at
	// the same time as the detector's own holster (anm_hide_fast), NOT before/after it.
	bool			PlayDetectorHandReturn	();
	bool			m_bDetectorDrawPending;	// anm_prepare_detector is playing -> show the detector when it ends
	// anm_prepare_detector's last frames are static, so waiting for its natural end shows a frozen pose
	// (the hitch). Fire the draw early instead - the next motion overrides the static tail. Gunslinger
	// does exactly this via a config lock_time. 0 = inactive.
	u32				m_dwDetectorShowTm;
	void			ArmDetectorShowTimer	();	// call once anm_prepare_detector has actually started
	void			FireDetectorShow		();	// show the detector + play anm_draw_detector now
protected:
	virtual void	on_a_hud_attach			();
	virtual void	on_b_hud_detach			();
	static void		FireSelectorBoneCallback(CBoneInstance* B);
	bool			IsAutoFireMode			() const { return m_iQueueSize == WEAPON_ININITE_QUEUE; }
	// GS NeedShootMix (WeaponAnims.pas ShootAnimMixPatch): whether the shot animation blends into
	// whatever is on screen instead of cutting to it. Keyed per hud section, see the three
	// mix_shoot_after_* switches.
	BOOL			NeedShootMix			() const;
	// GS `need_first_shoot_anims` + IsJustAfterReload: the FIRST shot after a reload gets its own
	// take (`anm_shoot_first` and its aim/scope variants). Opt-in per weapon; GS ships it on the
	// Protecta alone. JustAfterReload is what the weapon class knows -- only the shotguns track it.
	bool			NeedFirstShootAnim		() const { return m_bNeedFirstShootAnims && JustAfterReload(); }
	virtual bool	JustAfterReload			() const { return false; }
	// May a trigger held through an aim in/out transition auto-resume firing at the handoff?
	// Only genuine continuous-auto weapons. NOTE: pistols/shotguns/SVD keep the default
	// m_iQueueSize == WEAPON_ININITE_QUEUE (they gate semi-auto via bWorking in switch2_Fire),
	// so IsAutoFireMode() alone is TRUE for them and must NOT be used here — CWeaponCustomPistol
	// overrides this to false. (Without it, a held pistol self-fires after aiming.)
	virtual bool	CanAutoResumeFire		() const { return IsAutoFireMode(); }
	void			switch2_FireModeSwitch	();
	// play the fire-selector gesture for an oldMode->newMode change (auto = -1 -> token "a",
	// else the mode number): anm_firemode_<from>_to_<to>. Falls back to the single<->auto
	// pair when that specific transition alias is absent.
	void			TriggerFireModeSwitchAnim(int oldMode, int newMode);
	static void		FireModeToken			(int mode, string16& out);	// -1->"a", N->"N"
	shared_str		m_sFireModeAnim;	// transition alias chosen by TriggerFireModeSwitchAnim
	virtual void	PlayAnimFireModeSwitch	();	// picks anm_firemode_<from>_to_<to> (+GL in WGrenade)

	virtual void	switch2_Showing	();
	
	virtual void	OnShot			();	
	
	virtual void	OnEmptyClick	();

	virtual void	OnAnimationEnd	(u32 state);
	virtual void	OnStateSwitch	(u32 S);
	virtual bool	NeedJammedAnim	() { return !!IsMisfire(); }	// jammed -> play "_jammed" HUD gestures
	virtual bool	NeedEmptyAnim	() { return iAmmoElapsed == 0; }	// empty mag -> "_empty" (bolt held back)
	// no optic in use -> "_noscope". Keyed on UseScopeAnims (not just IsScopeAttached) so a scope whose
	// section turns scope anims off keeps the plain gestures too, same rule the _scope aliases follow.
	virtual bool	NeedNoScopeAnim	() { return !UseScopeAnims(); }
	virtual void	MakeFireModeName(LPCSTR name, string_path& out);	// GS mask_firemode_<a|N> suffix per fire mode
public:
	// public: CCustomDetector strips this mark off the weapon's alias before mirroring it as a
	// companion (a detector has no per-fire-mode animations)
	virtual LPCSTR	GetFireModeMark	(LPCSTR name);					// the bare mark ("" = none)
protected:
	virtual bool	IsGrenadeMode	() const { return false; }			// GL grenade mode (WGrenade overrides)

	virtual void	UpdateSounds	();

	bool			TryReload		();

protected:
	virtual void	ReloadMagazine();
			void	ApplySilencerKoeffs();
			void	ResetSilencerKoeffs();

	virtual void	state_Fire		(float dt);
	virtual void	state_Misfire	(float dt);
	// hyperburst: swap in base_dispersioned_bullets_speed for the fast rounds
	virtual void	FireBullet		(const Fvector& pos, const Fvector& dir, float fire_disp,
									 const CCartridge& cartridge, u16 parent_id, u16 weapon_id, bool send_hit);
public:
	virtual bool	UseBaseFireDispersion() const { return InBaseDispersionedBurst(); }
					CWeaponMagazined	(ESoundTypes eSoundType=SOUND_TYPE_WEAPON_SUBMACHINEGUN);
	virtual			~CWeaponMagazined	();

	virtual void	Load			(LPCSTR section);
			void	LoadSilencerKoeffs();
	virtual CWeaponMagazined*cast_weapon_magazined	()		 {return this;}

	virtual void	SetDefaults		();
	virtual void	FireStart		();
	virtual void	FireEnd			();
	virtual void	Reload			();
	// shared pre-reload gate (blocks + the "lower the sights first" hand-over). false = do not start the
	// reload now. Called by Reload() and by CWeaponShotgun's tri-state path, which bypasses Reload().
	bool			ReloadGate		();
	

	virtual	void	UpdateCL		();

	// ---- Gunslinger-style HUD-model bone visibility (attachments / ammo count / ammo type / firemode) ----
	// Bones of the first-person model are shown/hidden to match state: individual rounds in the mag,
	// a different bullet mesh per ammo type, a firemode selector, and static per-weapon hides.
	// Config keys live in the weapon's hud section, same names as GS. Re-run only on change.
	// GS use_light_misfire: rolls the per-shot "light misfire" (light strike). If it hits, plays
	// anm_shoot_lightmisfire + sndLightMisfire, does NOT fire the round or jam the weapon, and returns true
	// (state_Fire then stops the shot -- the player just pulls again). Opt-in via use_light_misfire in cfg.
			bool	gwr_TryLightMisfire		();
			void	PlayKickSound			();	// GS snd_kick, at the bayonet stab
			void	gwr_UpdateBones			(bool force = false);
	void			gwr_SetBones			(LPCSTR csv, BOOL show);
	// ...restricted to the entries whose name does (want) / does not (!want) start with pfx -- lets the
	// chamber's case and the magazine's rounds be coloured from two different ammo-bone sections.
	void			gwr_SetBonesFiltered	(LPCSTR csv, BOOL show, LPCSTR pfx, bool want);
	// true if the magazine keeps the round that fires next (the "chamber") at the BACK rather than the
	// last-loaded round -- chamber-first pump shotguns. Lets the ammo-type display read the newest round
	// (index size-2) instead of the pinned chamber. Base: normal push_back order, so false.
	virtual bool	GwrChamberAtBack		() const { return false; }
	// GL/grenade weapons add their own pass (the loaded grenade's bone) and fold GL state into the
	// change-detection so it re-runs when the grenade or its type changes. Base = no GL, no-op.
	virtual int		gwr_GLBonesState		() { return 0; }
	virtual void	gwr_UpdateBonesGL		() {}

	// ---- Gunslinger world-model animations (WeaponUpdate.pas: ReassignWorldAnims) ----
	// Opt-in per weapon via `use_world_anims` in the WEAPON section. The state picks a base config key
	// (wanm_idle/draw/holster/shoot/reload) in that same section; suffixes are appended only when the
	// resulting KEY exists (GS's AddSuffixIfStringExist), and the key's VALUE is the motion name played
	// on the world visual. Lets the third-person/dropped model animate from wpn_*_animation.omf.
			void	gwr_UpdateWorldAnims	();
			void	gwr_WorldAnimSuffix		(const shared_str& sect, LPCSTR suffix, string128& anm);
	shared_str		m_sLastWorldAnim;		// last motion played on the world model (replay only on change)
	u32				m_dwLastWorldAnimState;	// and the state it was picked for (so a re-fire restarts it)

	int				m_gwr_bones_state[6];	// last {ammo, ammotype, firemode, misfire, gl_state, valid}
	u8				m_gwr_last_fired_type;	// ammo type of the last round fired (chamber-first: the ejecting
	u32				m_gwr_fired_until;		// shell's colour must be the FIRED round, not the next chamber);
											// shown until this wall-clock time (the eject/pump window)
	u8				m_gwr_last_mag_type;	// ammo type last held in the magazine (tracked while non-empty) -- the
											// spent-casing colour after the mag empties by FIRING or UNLOADING

	virtual void	net_Destroy		();
	virtual void	net_Export		(NET_Packet& P);
	virtual void	net_Import		(NET_Packet& P);

	virtual void	OnH_A_Chield		();

	virtual bool	Attach(PIItem pIItem, bool b_send_event);
	virtual bool	Detach(const char* item_section_name, bool b_spawn_item);
	virtual bool	CanAttach(PIItem pIItem);
	virtual bool	CanDetach(const char* item_section_name);

	virtual void	InitAddons();
	// GS hud_when_silencer_is_attached / hud_silencer (WeaponUpdate.pas:803): mounting a silencer swaps the
	// whole HUD section, i.e. the animation set, to the weapon's silencer variant (the groza's is a full
	// 148-alias groza_* -> groza_sil_* set). m_hud_sect_presilencer remembers what was in use so detaching
	// restores it -- including a section an UPGRADE had swapped in.
	// DELIBERATELY UNCONDITIONAL: GS gates the same block on `GetInstalledUpgradesCount(wpn) > 0` merely
	// because it lives inside its upgrade-processing routine, so an un-upgraded weapon keeps the normal
	// set. That reads as a structural artifact rather than a rule, and the user chose not to copy it.
	// `extra_upgrade_sect` is an upgrade EFFECT section to treat as installed on top of m_upgrades:
	// the manager calls install_upgrade() BEFORE add_upgrade(), so during an install the new node is
	// not in m_upgrades yet and a plain recompute would wipe the section that install just chose.
	void			UpdateHudSectionForAddons(LPCSTR extra_upgrade_sect = nullptr);
	// GS PlaySoundByAnimName set, (re)loaded from the CURRENT hud section -- an upgrade that repoints `hud`
	// brings its own snd_anm_* values (the gauss's fast-rpm node -> the gauss_shoot_fast shot sounds).
	// virtual: a subclass may own snd_anm_* sounds that do NOT come from the hud section (the shotgun's
	// per-reload-phase set lives in the WEAPON section) and must re-register them after the wipe
	virtual void	LoadAnmSounds			();
	shared_str		m_anm_snd_sect;			// hud section the snd_anm_* set was loaded from
	// GS restricted_gl_and_sil: this weapon cannot wear the launcher and the silencer at once
	IC bool			GwrRestrictedGLandSil() const
					{ return !!READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "restricted_gl_and_sil", FALSE); }
	// Attaching one of the pair DETACHES the other (GS need_detach_gl / need_detach_sil). Shared, because
	// CWeaponMagazinedWGrenade::Attach handles the launcher itself and never reaches the base class's
	// launcher branch -- putting the rule in only one of them left that direction doing nothing.
	void			GwrEnforceGLSilExclusion(bool silencer_is_the_new_one);

	virtual bool	Action			(s32 cmd, u32 flags);
	bool			IsAmmoAvailable	();
	virtual void	UnloadMagazine	(bool spawn_ammo = true, u32 keep_count = 0);	// keep_count: stop unloading with this many rounds left (GS ammo_in_chamber save)

	virtual void	GetBriefInfo				(xr_string& str_name, xr_string& icon_sect_name, xr_string& str_count, string16& fire_mode);

	bool			bMisfireReload;
	// the reload currently playing is an ammo-TYPE change (anm_reload_ammochange) -> it gets
	// snd_changecartridgetype instead of the reload sound (GS sndChangeCartridgeType)
	bool			m_bAmmoChangeReload;

public:
	virtual bool	SwitchMode				();
	virtual bool	SingleShotMode			()			{return 1 == m_iQueueSize;}
	virtual void	SetQueueSize			(int size);
	IC		int		GetQueueSize			() const	{return m_iQueueSize;};
	virtual bool	StopedAfterQueueFired	()			{return m_bStopedAfterQueueFired; }
	virtual void	StopedAfterQueueFired	(bool value){m_bStopedAfterQueueFired = value; }

protected:
	//максимальный размер очереди, которой можно стрельнуть
	int				m_iQueueSize;
	//количество реально выстреляных патронов
	int				m_iShotNum;
	//после какого патрона, при непрерывной стрельбе, начинается отдача (сделано из-зи Абакана)
	int				m_iShootEffectorStart;
	Fvector			m_vStartPos, m_vStartDir;

	// --- AN-94 hyperburst (Gunslinger AN94Patch.pas + the vanilla SoC base_dispersioned_* keys) ---
	// The first <count> rounds of a queue leave the barrel at their own (much higher) rate, at their
	// own muzzle speed, from the aim point captured when the queue started and with the barrel's own
	// dispersion. count = 0 (key absent) turns the whole thing off, so other weapons are untouched.
	int				m_iBaseDispersionedBulletsCount;
	float			m_fBaseDispersionedBulletsSpeed;
	float			m_fBaseDispersionedBulletsTimeDelta;	// sec between those rounds (= 1/rpm)
	// GS singleshoots_time_delta: own rate for the single-shot fire mode
	float			m_fSingleShootsTimeDelta;
	// `shot_queue` (weapon section, default true): may a trigger pull made DURING the post-shot gap be
	// remembered and fired when the gap ends? Turn it off on a weapon whose gap is long enough that the
	// queued round would be a surprise (the gauss: recharge_time = 3 s). The wpn_shot_queue console flag
	// is the global master switch on top of this.
	bool			m_bShotQueue;
	// true while the shot being fired belongs to the fast part of the queue (m_iShotNum is already
	// incremented by then, so the first round is 1)
	IC bool			InBaseDispersionedBurst() const
	{
		return m_iBaseDispersionedBulletsCount > 0 && m_iShotNum > 0 && m_iShotNum <= m_iBaseDispersionedBulletsCount;
	}
	// delay until the NEXT round, GS AN94_RPM_Patch
	float			CurrentShotTimeDelta	() const;
	//флаг того, что мы остановились после того как выстреляли
	//ровно столько патронов, сколько было задано в m_iQueueSize
	bool			m_bStopedAfterQueueFired;
	//флаг того, что хотя бы один выстрел мы должны сделать
	//(даже если очень быстро нажали на курок и вызвалось FireEnd)
	bool			m_bFireSingleShot;
	//режимы стрельбы
	bool			m_bHasDifferentFireModes;
	xr_vector<s8>	m_aFireModes;
	int				m_iCurFireMode;
	int				m_iPrefferedFireMode;

	//переменная блокирует использование
	//только разных типов патронов
	bool m_bLockType;
	bool m_bAmmoInChamber;	// Gunslinger ammo_in_chamber: cfg mag_size = real mag + 1 (chambered round)
	int  m_iMaxQueueSize;	// GS max_queue_size: hard cap of rounds per trigger pull (0 = none)
	float m_fRechargeTime;	// GS recharge_time (sec): minimum gap between shots on top of rpm (gauss capacitor); upgradeable
	bool m_bNoJamFire;		// GS no_jam_fire (hud section): the jam is a DUD rolled BEFORE the shot -- the
							// round is not spent and the fire cycle isn't a stuck action (bm16/toz34/rg6)
	bool m_bSaveCartridgeInAmmoChange;	// GS save_cartridge_in_ammochange (default true): keep the OLD-type chambered round when swapping ammo type

	// ---- GS autoaim (WeaponEvents.pas:2467 IsShotNeededNow) ------------------------------------
	// NOT aim assist -- an INTERLOCK on the trigger. The gauss's guard/safari/ideal nodes hold the
	// shot back until something worth shooting is under the crosshair (or until autoaim_time runs
	// out). Every key lives on the weapon section and is upgradeable.
	shared_str m_sAutoAimModes;		// autoaim_modes: the fire modes the interlock applies to ("a" = the infinite/MUI queue)
	// autoaim_time in MILLISECONDS: >0 fire anyway after this long, <0 wait for a target, 0 = off.
	// GS reads the two sources with DIFFERENT units and we have to match it: the weapon section goes
	// through `floor(r_single * 1000)` (WeaponAdditionalBuffer.pas:482, i.e. seconds) while an upgrade's
	// value is taken RAW by FindIntValueInUpgradesDef -> game_ini_r_int_def (HudItemUtils.pas:399), i.e.
	// already milliseconds. So the safari node's `autoaim_time = 10` means 10 ms, not 10 seconds.
	float m_fAutoAimTimeMs;
	bool  m_bAutoAimOnlyAlive;		// autoaim_only_alive: only a thermovisor-visible target counts (crow yes, bloodsucker no)
	bool  m_bAutoAimIgnoreDead;		// autoaim_ignore_dead: a corpse is not a target
	bool  m_bAutoAimShotCancel;		// autoaim_shot_cancellation: no target -> drop the shot instead of holding the trigger
	bool  m_bAutoAimAfterRelease;	// autoaim_shot_after_key_released: the timer only starts once the trigger is RELEASED
	// NOT a GS key. GS fires on the very first frame the ray connects, and that shot can go wide -- the ray
	// catches a limb swinging past or the edge of a model. `autoaim_confirm_time` (ms, weapon section,
	// default 50) makes the SAME object have to stay under the crosshair that long before it counts.
	int   m_iAutoAimConfirmMs;
	u32   m_dwAutoAimOnTargetSince;	// when the current object came under the crosshair; 0 = nothing there
	u16   m_wAutoAimTargetId;		// which object that is, so sweeping onto another one restarts the wait
	u32   m_dwAutoAimStartTm;		// when the wait started; 0 = not counting
	u32   m_dwAutoAimActorState;	// last seen actor movement state (re-pick the idle while the shot waits)
	// ms; 0 = the interlock is off in the CURRENT fire mode (GS WpnBuf.GetAutoAimPeriod)
	int   gwr_AutoAimPeriod		() const;
	// false = this shot must not happen now. Holds the weapon in eFire (fShotTimeCounter = 0) while
	// waiting, or forces it negative to let state_Fire's tail cancel the shot -- exactly what GS's
	// SetShootLockTime(0)/(-1) do to the same field.
	bool  gwr_IsShotNeededNow	(const Fvector& pos, const Fvector& dir);

	// eFire with nothing left to do: the trigger is up, no shot is queued and the shot animation has
	// finished -- the weapon is only sitting out `fShotTimeCounter` (rpm, or recharge_time: 3 s on the
	// gauss). Anything that must not wait for that cooldown asks here. See OnZoomOut / UpdateCL.
	bool  gwr_FireCycleIdle		() const;
	// If so, close the fire cycle right now -- the same two steps state_Fire's tail would take once the
	// counter went negative. `fShotTimeCounter` is deliberately left alone: it keeps draining at eIdle,
	// so this can never be used to skip a recharge.
	void  gwr_EndIdleFireCycle	();

public:
	// true while the jam (misfire) inspect gesture is on screen -> block aim / headlamp / NV /
	// detector so nothing interrupts it (it owns the hands until it finishes)
	bool			IsJamInspectPlaying	() const { return m_bDryFirePlaying && !!IsMisfire(); }
	virtual void	OnZoomIn			();
	virtual void	OnZoomOut			();
			void	OnNextFireMode		();
			void	OnPrevFireMode		();
			bool	HasFireModes		() { return m_bHasDifferentFireModes; };
	virtual	int		GetCurrentFireMode	() { return m_aFireModes[m_iCurFireMode]; };	

	virtual void	save				(NET_Packet &output_packet);
	virtual void	load				(IReader &input_packet);

protected:
	virtual bool	install_upgrade_impl( LPCSTR section, bool test );

protected:
	virtual bool	AllowFireWhileWorking() {return false;}

	//виртуальные функции для проигрывания анимации HUD
	virtual void	PlayAnimShow		();
	virtual void	PlayAnimHide		();
	virtual void	PlayAnimReload		();
	virtual void	PlayAnimIdle		();
	virtual void	PlayAnimShoot		();
	// GS: re-assign the shoot motion to its "_jammed" variant on the shot that jams (false = no such alias)
	bool			PlayJammedShootAnim	(bool force = false);
	// the magazine fill of a reload, once per reload: at the animation's end, or at the config's
	// lock_time_start_<alias> when it has one (GS) so the loaded round shows up mid-animation
	void			DoReloadInsert		();
	void			ArmReloadLockTimes	();	// read lock_time_start_/lock_time_end_ for the played alias
	u32				m_dwReloadInsertTm;		// wall clock of that fill; 0 = none pending
	bool			m_bReloadInsertDone;	// guard so the timer and OnAnimationEnd can't both fill
	bool			m_bReloadInsertHandTimed;	// insert time came from a per-alias lock_time_start_, not the generic fallback
	bool			m_bLastEmptyAnim;		// last NeedEmptyAnim(): flips -> re-pick the idle at once
	virtual void	SelectJammedShootBase(string_path& out);	// base alias that gets the "_jammed" token
	virtual void	SelectShootAnim		(string_path& result);	// hip / ADS / scope shoot motion
	// dry-fire ("pull the trigger, nothing happens") on empty/jammed. GL subclass overrides.
	virtual void	SelectDryFireAnim	(string_path& result);
	// does this weapon have a jam-clear reload motion? (the double-barrels name theirs
	// anm_reload_jammed_<shells>, so the plain alias test misses them)
	virtual bool	HasJammedReloadAnim	() { return !!isHUDAnimationExist("anm_reload_jammed"); }
			void	PlayAnimDryFire		();
	bool			m_bDryFirePending;	// switch2_Idle should play the dry-fire, not the idle
	bool			m_bDryFirePlaying;	// a dry-fire gesture is in progress -> FireStart ignored
	bool			m_bLightMisfirePlaying;	// a light-misfire (light strike) gesture is in progress -> owns the hands until it ends
	// unified "is firing locked right now?" query (GS SetShootLockTime): folds the base SetShootLock timer
	// together with this weapon's bespoke fire-lock deadlines (aim in/out, sprint-exit).
	virtual bool	IsShootLocked		() const override;
	bool			m_bAimInPending;	// aim pressed mid-fire: play aim-in once fire stops (switch2_Idle)
	bool			m_bAimOutPending;	// aim released mid-fire: keep aiming, play aim-out when fire ends
	// reload pressed while aiming: lower the sights FIRST, then blend the reload into the tail of
	// that transition. (GS just forbids reloading while aimed; this is the variant the user asked for.)
	bool			m_bReloadAfterAimOut;
	u32				m_dwReloadAfterAimAt;	// wall clock to start it; 0 = as soon as the sights are down
	// set by UpdateCL for the one Reload() call the schedule above has just come due for, so
	// ReloadGate lets it through instead of scheduling it all over again (see the gate)
	bool			m_bReloadAimOutDue;
	// GS CanReloadNow: a reload may not start until the shot's own cycle (the pump/bolt work) is over.
	// Wall clock at which the queued reload may go; 0 = nothing waiting on the shot cycle.
	u32				m_dwReloadAfterShotAt;
	u32				m_dwLastShotTm;			// when the last round left the barrel (GS RegisterShot)
	// GS anm_shots_selector (WeaponAnims.pas:956) arms a LOCK from `lock_time_<the shot anim that played>`,
	// and CanLeaveAimNow waits for that lock -- never for the animation. Deadline, 0 = the config keys no
	// lock_time for this shot (true of every weapon GS ships) -> the sights come down at once.
	u32				m_dwShotLockUntil;
public:
	virtual bool	AimBlockedByShot	() const override
	{ return m_dwShotLockUntil != 0 && Device.dwTimeGlobal < m_dwShotLockUntil; }
protected:
	bool			m_bTriggerHeld;		// trigger currently pressed (FireStart..FireEnd); resume fire after a transition
	bool			m_bAimLockFirePressed;	// fire pressed DURING the aim fire-lock -> autoshoot when it ends (robust vs m_bTriggerHeld)
	bool			m_bZoomPendingSprint;	// aim pressed during sprint: aim-in once the sprint-exit anim is (almost) done
	bool			m_bFirePendingSprint;	// fire pressed during sprint: fire once the sprint-exit anim is (almost) done
	// aiming is blocked during a light-misfire strike (task): an aim press/release that arrives while the
	// click gesture plays is remembered here and replayed by UpdateCL once the strike ends (like the sprint defer).
	bool			m_bZoomPendingMisfire;	// an aim press/release was deferred past a light-misfire strike
	bool			m_bZoomPendingMisfireIn;// true = the deferred intent was aim-IN, false = aim-OUT
	virtual void	PlayReloadSound		();
	virtual void	PlayAnimAim			();
	// directional aim-walk: picks anm_idle_aim / _walk / _walk_back / _walk_left /
	// _walk_right by movement, with isHUDAnimationExist fallback to the static aim.
	// The GL subclass overrides for the _w_gl / _g variants.
	virtual void	SelectAimIdleAnim	(string_path& result);
	LPCSTR			AimWalkDirSuffix	();	// "", "_walk", "_walk_back/left/right"
	virtual bool	HasMovementIdleVariant();

	// aim-in / aim-out (ADS) transition. SelectAimTransitionAnim is overridden by the GL
	// subclass for _w_gl / _g variants; PlayAimTransition returns false if no motion exists.
	virtual void	SelectAimTransitionAnim	(bool bAimIn, string_path& result);
			bool	PlayAimTransition		(bool bAimIn);
	// wall-clock deadline for the aim transition (0=none). CHudItem::OnAnimationEnd is
	// unreliable while moving (fires seconds late), which lets the one-shot transition
	// stick/loop = the ADS "snap"; UpdateCL forces the handoff to the aim/moving idle here.
	u32				m_dwAimTransitionEndTm;
	// A shoot anim is playing until this wall-clock time -> don't let switch2_Idle cut it with the idle
	// (Gunslinger CanAssignIdleAnimNow: no idle while an anm_shoot* motion is running). Lets longer random
	// shoot variants (e.g. de_shoot2) play to the end instead of being clipped by the fire-rate timing.
	u32				m_dwShootAnimEndTm;
	// Firing is blocked only for a short lock (Gunslinger's lock_time_anm_idle_aim_start/_end),
	// NOT for the whole transition -- so you can fire before the aim-in/out animation finishes.
	u32				m_dwAimFireLockTm;
	// GS's autoshoot_anm_idle_aim_start/_end: a trigger held THROUGH the lock fires the instant the
	// lock ends, instead of needing a re-press. On by default (GS sets it on ~all weapons); a weapon
	// can disable it in config. NOTE this fires at the short LOCK end (~0.2s), not the full transition.
	bool			m_bAimLockAutoShoot;
public:
	// The aim transition runs at eIdle and deliberately WITHOUT SetPending (so firing can cut it),
	// which makes it indistinguishable from "settled" to anyone testing state alone -- ask this instead.
	bool			IsAimTransitionPlaying	() const
	{ return m_dwAimTransitionEndTm && Device.dwTimeGlobal < m_dwAimTransitionEndTm; }
protected:

	virtual	int		ShotsFired			() { return m_iShotNum; }
	virtual float	GetWeaponDeterioration	();
	virtual bool	WeaponSoundExist	(LPCSTR section, LPCSTR sound_name);
};
