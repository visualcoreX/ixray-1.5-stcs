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
	void			UpdateFireSelectorBone	();	// (re)attach callback to the current HUD model
	void			DetachFireSelectorBone	();
	void			SampleFireSelectorAutoPose();	// derive the auto selector pose from the anim's last frame
	virtual void	on_a_hud_attach			();
	virtual void	on_b_hud_detach			();
	static void		FireSelectorBoneCallback(CBoneInstance* B);
	bool			IsAutoFireMode			() const { return m_iQueueSize == WEAPON_ININITE_QUEUE; }
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

	virtual void	UpdateSounds	();

	bool			TryReload		();

protected:
	virtual void	ReloadMagazine();
			void	ApplySilencerKoeffs();
			void	ResetSilencerKoeffs();

	virtual void	state_Fire		(float dt);
	virtual void	state_Misfire	(float dt);
public:
					CWeaponMagazined	(ESoundTypes eSoundType=SOUND_TYPE_WEAPON_SUBMACHINEGUN);
	virtual			~CWeaponMagazined	();

	virtual void	Load			(LPCSTR section);
			void	LoadSilencerKoeffs();
	virtual CWeaponMagazined*cast_weapon_magazined	()		 {return this;}

	virtual void	SetDefaults		();
	virtual void	FireStart		();
	virtual void	FireEnd			();
	virtual void	Reload			();
	

	virtual	void	UpdateCL		();
	virtual void	net_Destroy		();
	virtual void	net_Export		(NET_Packet& P);
	virtual void	net_Import		(NET_Packet& P);

	virtual void	OnH_A_Chield		();

	virtual bool	Attach(PIItem pIItem, bool b_send_event);
	virtual bool	Detach(const char* item_section_name, bool b_spawn_item);
	virtual bool	CanAttach(PIItem pIItem);
	virtual bool	CanDetach(const char* item_section_name);

	virtual void	InitAddons();

	virtual bool	Action			(s32 cmd, u32 flags);
	bool			IsAmmoAvailable	();
	virtual void	UnloadMagazine	(bool spawn_ammo = true);

	virtual void	GetBriefInfo				(xr_string& str_name, xr_string& icon_sect_name, xr_string& str_count, string16& fire_mode);

	bool			bMisfireReload;

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
	virtual void	SelectShootAnim		(string_path& result);	// hip / ADS / scope shoot motion
	// dry-fire ("pull the trigger, nothing happens") on empty/jammed. GL subclass overrides.
	virtual void	SelectDryFireAnim	(string_path& result);
			void	PlayAnimDryFire		();
	bool			m_bDryFirePending;	// switch2_Idle should play the dry-fire, not the idle
	bool			m_bDryFirePlaying;	// a dry-fire gesture is in progress -> FireStart ignored
	bool			m_bAimInPending;	// aim pressed mid-fire: play aim-in once fire stops (switch2_Idle)
	bool			m_bAimOutPending;	// aim released mid-fire: keep aiming, play aim-out when fire ends
	bool			m_bTriggerHeld;		// trigger currently pressed (FireStart..FireEnd); resume fire after a transition
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

	virtual	int		ShotsFired			() { return m_iShotNum; }
	virtual float	GetWeaponDeterioration	();
	virtual bool	WeaponSoundExist	(LPCSTR section, LPCSTR sound_name);
};
