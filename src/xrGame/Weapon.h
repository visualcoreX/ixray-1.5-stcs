#pragma once

#include "PhysicsShell.h"
#include "weaponammo.h"
#include "PHShellCreator.h"

#include "ShootingObject.h"
#include "hud_item_object.h"
#include "Actor_Flags.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "../xrEngine/Render.h"		// ref_light / ref_glow for the weapon flashlight
#include "firedeps.h"
#include "game_cl_single.h"
#include "first_bullet_controller.h"

#include "CameraRecoil.h"

class CEntity;
class ENGINE_API CMotionDef;
class CSE_ALifeItemWeapon;
class CSE_ALifeItemWeaponAmmo;
class CWeaponMagazined;
class CParticlesObject;
class CUIWindow;

class CWeapon : public CHudItemObject,
				public CShootingObject
{
private:
	typedef CHudItemObject inherited;

public:
							CWeapon				();
	virtual					~CWeapon			();

	// Generic
	virtual void			Load				(LPCSTR section);

	virtual BOOL			net_Spawn			(CSE_Abstract* DC);
	virtual void			net_Destroy			();
	virtual void			net_Export			(NET_Packet& P);
	virtual void			net_Import			(NET_Packet& P);
	
	virtual CWeapon			*cast_weapon			()					{return this;}
	virtual CWeaponMagazined*cast_weapon_magazined	()					{return 0;}


	//serialization
	virtual void			save				(NET_Packet &output_packet);
	virtual void			load				(IReader &input_packet);
	virtual BOOL			net_SaveRelevant	()								{return inherited::net_SaveRelevant();}

	virtual void			UpdateCL			();
	virtual void			shedule_Update		(u32 dt);

	virtual void			renderable_Render	();
	virtual void			render_hud_mode		();
	virtual bool			need_renderable		();

	virtual void			render_item_ui		();
	virtual bool			render_item_ui_query();

	virtual void			OnH_B_Chield		();
	virtual void			OnH_A_Chield		();
	virtual void			OnH_B_Independent	(bool just_before_destroy);
	virtual void			OnH_A_Independent	();
	virtual void			OnEvent				(NET_Packet& P, u16 type);// {inherited::OnEvent(P,type);}

	virtual	void			Hit					(SHit* pHDS);

	virtual void			reinit				();
	virtual void			reload				(LPCSTR section);
	virtual void			create_physic_shell	();
	virtual void			activate_physic_shell();
	virtual void			setup_physic_shell	();

	virtual void			SwitchState			(u32 S);

	virtual void			OnActiveItem		();
	virtual void			OnHiddenItem		();
	virtual void			SendHiddenItem		();	//same as OnHiddenItem but for client... (sends message to a server)...

public:
	virtual bool			can_kill			() const;
	virtual CInventoryItem	*can_kill			(CInventory *inventory) const;
	virtual const CInventoryItem *can_kill		(const xr_vector<const CGameObject*> &items) const;
	virtual bool			ready_to_kill		() const;
	virtual bool			NeedToDestroyObject	() const; 
	virtual ALife::_TIME_ID	TimePassedAfterIndependant() const;
protected:
	//время удаления оружия
	ALife::_TIME_ID			m_dwWeaponRemoveTime;
	ALife::_TIME_ID			m_dwWeaponIndependencyTime;

	virtual bool			IsHudModeNow		();
public:
	void					signal_HideComplete	();
	virtual bool			Action(s32 cmd, u32 flags);

	enum EWeaponStates {
		eFire		= eLastBaseState+1,
		eFire2,
		eReload,
		eMisfire,
		eSwitch,
		eActionAnim,		// one-shot HUD gesture (headlamp / night-vision toggle), fire-locked
		eFireModeSwitch,	// single<->auto fire-selector gesture, fire-locked

	};
	enum EWeaponSubStates{
		eSubstateReloadBegin		=0,
		eSubstateReloadInProcess,
		eSubstateReloadEnd,
	};

	IC BOOL					IsValid				()	const		{	return iAmmoElapsed;						}
	// Does weapon need's update?
	BOOL					IsUpdating			();


	BOOL					IsMisfire			() const;
	BOOL					CheckForMisfire		();


	BOOL					AutoSpawnAmmo		() const		{ return m_bAutoSpawnAmmo; };
	bool					IsTriStateReload	() const		{ return m_bTriStateReload;}
	EWeaponSubStates		GetReloadState		() const		{ return (EWeaponSubStates)m_sub_state;}
protected:
	bool					m_bTriStateReload;
	bool					m_bZoomKeyHeld;		// aim key held (physical): set on kWPN_ZOOM CMD_START, cleared on CMD_STOP
	u8						m_sub_state;
	// a misfire happens, you'll need to rearm weapon
	bool					bMisfire;
	// guarantees at least one clean shot right after a jam is cleared (no back-to-back jams)
	bool					m_bMisfireCooldown;

	BOOL					m_bAutoSpawnAmmo;

public:
			bool IsGrenadeLauncherAttached	() const;
			bool IsScopeAttached			() const;
			bool IsSilencerAttached			() const;

	virtual bool GrenadeLauncherAttachable();
	virtual bool ScopeAttachable();
	virtual bool SilencerAttachable();
			
	ALife::EWeaponAddonStatus	get_GrenadeLauncherStatus	() const { return m_eGrenadeLauncherStatus; }
	ALife::EWeaponAddonStatus	get_ScopeStatus				() const { return m_eScopeStatus; }
	ALife::EWeaponAddonStatus	get_SilencerStatus			() const { return m_eSilencerStatus; }

	virtual bool UseScopeTexture();
	virtual bool IsGrenadeMode() const { return false; }	// CWeaponMagazinedWGrenade overrides -> aiming the GL, not the weapon
	// Gunslinger IsLensedScopeInstalled: an attached scope flagged need_lens_frame uses the 3D PiP lens
	// (weapon stays rendered, no 2D full-screen scope texture) instead of the vanilla 2D scope zoom.
	bool IsLensedScope() const;
	// Gunslinger `collimator`: the THIRD scope mode. A red-dot/collimator has its reticle on the MODEL, so
	// it must neither hide the weapon behind a 2D scope texture nor spin up the PiP lens -- and being 1x it
	// must not zoom the world either. Keeps the weapon and the HUD visible, aim FOV from scope_hud_fov_aim.
	bool IsCollimatorScope() const;
	float GetLensFOV() const;	// 3D PiP double-render: magnified world FOV (deg) for the lens frame; 0 = disabled
	bool UseScopeAnims() const;	// Gunslinger use_scope_anims: play the "_scope" anim variants while a scope is attached
	float ZoomMouseSenseKoef() const;	// Gunslinger zoom_mouse_sense_koef: look-sensitivity multiplier while scoped
	float ScaleSenseByLensStep(float k) const;	// ...scaled by the current magnification on a variable-power optic

	// Gunslinger scope reticle / night-vision illumination (scope_brightness_plus/minus). Stepped 0..steps
	// between min/max_night_brightness (config, /3 like GS). cur_value feeds the lens shader (m_zoom_deviation.z,
	// NV scopes) and gates the illuminated-reticle bones (scope_illum_bones, day scopes like the PSO).
	void  ChangeScopeIllum(int delta);				// +/-1 step (reloads params from the current scope, plays sound)
	void  ApplyScopeIllumUI();						// GS switchable_zoom_wnd: pick the crosshair level static
	void  ResetScopeIllumToDefault();				// set the step to the scope's default_brightness_step (on attach)
	float ScopeIllumValue() const	{ return m_scope_illum_value; }
	float ScopeIllumJitter() const	{ return m_scope_illum_jitter; }
private:
	void  LoadScopeIllumParams();					// read min/max/steps/jitter from the attached scope (or weapon) section
	float m_scope_illum_value;						// current brightness 0..max
	float m_scope_illum_jitter;
	int   m_scope_illum_step;						// current (active scope's) step, clamped to steps
	int   m_scope_illum_steps;
	int   m_scope_illum_step_by_scope[16];			// PER-SCOPE saved step (index = m_cur_scope; -1 = never set -> use default_brightness_step). Each scope keeps its own brightness.
	float m_scope_illum_min, m_scope_illum_max;

	// ---- GS variable magnification (min_lens_factor / max_lens_factor / lens_factor_levels_count) ----
	// A dual/variable-power optic (ELCAN 1.8x<->10x) steps its LENS magnification with the mouse wheel
	// while aiming. Runtime only (not saved): re-derived from the scope section on attach.
	int   m_lens_step;								// current step, 0..m_lens_steps
	int   m_lens_steps;								// lens_factor_levels_count (0 = fixed scope_lens_factor)
	float m_lens_min, m_lens_max;
	int   m_lens_step_by_scope[16];					// per-scope remembered step (-1 = not set yet)
	// GS lens_speed: the step is a TARGET, the lens travels to it (position units per second, 0 = instant)
	// and ticks snd_scope_zoom_gyro every lens_gyro_sound_period while it moves (WeaponAdditionalBuffer.pas:747).
	float m_fLensSpeed;
	float m_fLensPos;								// smoothed position 0..1 (m_lens_step/m_lens_steps is the target)
	float m_fLensGyroPeriod;
	u32   m_dwLensGyroSndTm;

	// ---- GS scope electronics (the gauss's nv / detector upgrade nodes) ----
	shared_str m_sScopeNV;							// scope_nightvision -> a PPE section ([scope_nightvision_gauss])
	shared_str m_sScopeDetector;					// scope_alive_detector -> a params section ([scope_detector])
	float m_fScopeNVMinFactor;						// scope_nightvision_min_factor: PPE strength at brightness step 0
	bool  m_bScopeNVActive;

	// ---- GS alter zoom (alter_zoom_allowed): a SECOND aim pose toggled by a key while aiming -------
	// The ELCAN's magnifier: the eye moves to the other optic, so the aim offset (and hud fov) swap.
	bool  m_bAlterZoom;
	// GS _is_alter_zoom_last: which of the two aim poses the last aim ENDED in, so the next aim resumes
	// it (GS IsLastZoomAlter). Runtime only, like the lens step -- not in the save stream.
	bool  m_bAlterZoomLast;
	float m_fAlterZoomFactor;						// 0 = normal pose, 1 = alter pose; ramps over alter_zoom_time
public:
	// GS variable magnification: step the lens power; true if this scope actually has steps (so the
	// caller can consume the mouse wheel instead of letting it switch weapons).
	bool			ChangeLensStep		(int delta);
	void			LoadLensFactorParams();
	void			ResetLensStepToDefault();
	bool			HasLensSteps		() const { return m_lens_steps > 0; }
	// GS alter zoom
	bool			IsAlterZoomAllowed	() const;
	bool			IsAlterZoom			() const { return m_bAlterZoom; }
	void			ToggleAlterZoom		();
	// eased 0..1 blend toward the alter pose (cubic ease-in-out, i.e. cubic-bezier(.42,0,.58,1)); the
	// aim offset and hud fov interpolate with it instead of snapping.
	float			AlterZoomBlend		() const;
	// GS GetZoomLensVisibilityFactor: 1 = PiP lens fully visible, 0 = off (alter pose / no lensed scope);
	// cross-fades with the alter-pose ramp. Drives the lens shader alpha and the $user$scope capture.
	float			LensVisibility		() const;
	void			UpdateAlterZoomBlend(float dt);

	//обновление видимости для косточек аддонов
			void UpdateAddonsVisibility();
			void UpdateHUDAddonsVisibility();
	//инициализация свойств присоединенных аддонов
	virtual void InitAddons();

	// ---- Gunslinger world-model bone visibility (xr_BoneUtils.pas) -------------------------------
	// GS's SetWeaponModelBoneStatus applies EVERY bone change to the world model unconditionally, and
	// to the hud model only while the weapon is the actor's active item -- so the same show_bones/
	// hide_bones lists drive both. Silent on a missing bone: the world .ogf need not carry every bone
	// the hud model has. gwr_UpdateWorldBones re-applies the static + per-upgrade lists to the world
	// visual, so a holstered/dropped/NPC weapon still shows its upgrade meshes.
	static	void gwr_SetWorldBone		(IKinematics* K, LPCSTR bone, BOOL show);
			void gwr_SetWorldBonesCSV	(IKinematics* K, LPCSTR csv,  BOOL show);
	// force=true for the event-driven calls (spawn / addon / upgrade change), which must always re-apply.
	// force=false is the per-frame call: it early-outs unless the attachment state actually changed, so
	// runtime toggles (scope on/off, laser, flashlight, bayonet) reach the world model without re-reading
	// the whole config every frame for every weapon in the level.
			void gwr_UpdateWorldBones	(IKinematics* K, bool force = true);
			// parse a comma-separated bone list without touching any model (so callers can tell which bones
			// an upgrade explicitly named and undo the recursive show_bones collateral)
	static	void gwr_CollectBoneNames	(LPCSTR csv, xr_vector<shared_str>& out);
	u32			 m_gwr_world_bones_sig;	// last applied attachment-state signature (u32(-1) = never)

	// ---- laser designator (Gunslinger LAM port) --------------------------------------------------
	// laser_params_section on the weapon -> a [<section>] with laserdot_attach_bone, laserdot_particle_0,
	// laserdot_attach_offset_*. Installed via an upgrade (laser_installed=true in its property section);
	// each frame the dot is ray-cast from the laser bone and a particle placed at the hit point.
public:
			bool			IsLaserInstalled	() const { return m_bLaserInstalled; }
			bool			IsLaserEnabled		() const { return m_bLaserEnabled; }
			void			SetLaserEnabled		(bool e) { m_bLaserEnabled = e; }
			void			ScheduleLaserToggle	(bool e, u32 delay_ms);	// GS anm_laser_on/off: the beam flips mid-gesture (lock_time_start)
			void			UpdateLaserDot		();		// per RENDER frame (from CActor::UpdateCL, after the HUD transform is fresh)
			void			PlaceLaserDot		(const Fvector& dot, int idx);	// create/move the dot particle
			void			UpdateCollimatorGlitch();			// GS: hide the active scope's collimator_sights_bones reticle during an emission (per render frame)
protected:
			void			LoadLaserParams		();		// from cNameSect() at Load
			void			StopLaserDot		();
			float			TraceLaserAsView	(const Fvector& pos, const Fvector& dir, float range, CObject* ignore);	// GS TraceAsView: ray passes vision-transparent statics (alpha)
			bool			m_bLaserInstalled;
			bool			m_bLaserEnabled;
			bool			m_bBayonetInstalled;		// GS bayonet upgrade -> quick-kick stabs with the weapon's own anm_kick (ak74_bayonet)
			// Whether an attached silencer / GL takes the blade away. Per weapon, because it is a
			// question of geometry, not a rule: on the AK the bayonet and the GP-25 share the barrel,
			// but the AN-94's knife sits clear of the launcher and Gunslinger keeps it mounted.
			// Config (weapon section), both default TRUE = the old hardcoded behaviour.
			bool			m_bBayonetBlockedBySilencer;
			bool			m_bBayonetBlockedByGL;
			shared_str		m_sBayonetBone;		// `bayonet_bone`, default "knife" (the AK family's blade)
	public:
			bool			IsBayonetInstalled() const	{ return m_bBayonetInstalled; }
			// A silencer or GL on the barrel removes the bayonet blade + disables its unique stab (falls
			// back to the generic knife kick). So the bayonet is "active" only when installed AND unobstructed.
			bool			IsBayonetActive() const
			{
				return m_bBayonetInstalled
					&& !(m_bBayonetBlockedBySilencer && IsSilencerAttached())
					&& !(m_bBayonetBlockedByGL       && IsGrenadeLauncherAttached());
			}
	protected:
			u32				m_dwLaserToggleAt;			// Device time to apply the pending toggle (0 = none)
			bool			m_bLaserPendingState;
			// GL-mode switch window (set by CWeaponMagazinedWGrenade::PlayAnimModeSwitch) so the laser dot can
			// fade out/in relative to the raise/lower anim like GS (laser_switch_time_to_gl/from_gl), not instantly.
			u32				m_dwGLSwitchStartTm;
			u32				m_dwGLSwitchEndTm;
			shared_str		m_sLaserBone;				// laserdot_attach_bone
			shared_str		m_sLaserRayBones;			// laser_ray_bones (the visible beam bones, e.g. "line, line2")
			xr_vector<shared_str>	m_LaserParticles;	// laserdot_particle_0..N (distance-switched dot)
			xr_vector<float>		m_LaserSwitchDist;	// laserdot_dist_1..N: switch to particle j+1 at >= [j]
			xr_vector<float>		m_LaserScaleDist;	// laserdot_dist_treshold_N (camera-pull remap segments)
			xr_vector<float>		m_LaserScaleMul;	// laserdot_dist_scale_N (per-segment multiplier)
			int				m_iLaserParticleIdx;		// currently-created particle index (-1 = none)
			Fvector			m_vLaserOffset;				// laserdot_attach_offset_*
			Fvector			m_vLaserWorldOffset;		// laserdot_world_attach_offset_*: origin in the WEAPON's own
													// space, used when the dot is placed off the world model
													// (no HUD item) instead of the hud bone -- GS WeaponUpdate.pas:200
			float			m_fLaserCosHudTreshold;		// cos(laserdot_hud_treshold): dot hidden when the laser deviates from the view by more
			BOOL			m_bLaserCorrection;			// laserdot_correction: on = GS camera-pull constant-size dot; off = real-depth dot + distance-switched particles
			float			m_fLaserHudRecalcKoef;		// hud_recalc_koef (hud section): hip-mode dir widening, m = koef/psHUD_FOV (GS CorrectDirFromWorldToHud)
			float			m_fLaserZeroDist;			// laserdot_zero_dist: >0 = boresight the hip ray so the dot hits screen center at this range (aiming); near dot still converges to the device
			float			m_fLaserHudPointKoef;		// laserdot_hud_point_koef: strength of the world->hud POSITION reprojection so the
													// real-depth dot converges onto the `line` bone on screen (0 = off, 1 = full cos ratio). Live-tunable.
			float			m_fLaserSurfacePull;		// laserdot_surface_pull: real-depth dot distance factor off the surface (GS 0.85; 1.0 = on the object)
			CParticlesObject* m_pLaserDot;

	// ---- weapon-mounted flashlight (Gunslinger LightUtils port) ----------------------------------
	// flash_params_section on the weapon -> spot + omni + optional glow attached to the `flash` bone;
	// installed by the same upgrade as the laser (flashlight_installed=true), toggled by kWPN_FLASHLIGHT.
public:
			bool			IsFlashlightInstalled	() const { return m_bFlashInstalled; }
			bool			IsFlashlightEnabled		() const { return m_bFlashEnabled; }
			void			SetFlashlightInstalled	(bool i) { m_bFlashInstalled = i; }
			void			ScheduleFlashlightToggle(bool e, u32 delay_ms);	// GS anm_torch_on/off: light flips at lock_time_start
			void			UpdateFlashlight		();		// per RENDER frame (from CActor::UpdateCL, after the HUD transform is fresh)
protected:
			void			LoadFlashlightParams	();		// from cNameSect() at Load
			void			StopFlashlight			();
			bool			m_bFlashInstalled;
			bool			m_bFlashEnabled;
			u32				m_dwFlashToggleAt;			// Device time to apply the pending toggle (0 = none)
			bool			m_bFlashPendingState;
			shared_str		m_sFlashBone;				// torch_light_bone (attach bone on the HUD model)
			Fvector			m_vFlashOffset;				// torch_attach_offset_*
			Fvector			m_vFlashWorldOffset;		// torch_world_attach_offset_*: spot origin in the WEAPON's
													// own space for the world model (NPC-held / dropped / no HUD)
			Fvector			m_vFlashOmniWorldOffset;	// torch_omni_world_attach_offset_* (defaults to the above)
			bool			m_bFlashHudModeNow;			// which mode the lights were last created//set for
			Fcolor			m_FlashColor;				// spot colour (r2 set)
			float			m_fFlashRange;				// torch_r2_range
			float			m_fFlashCone;				// torch_spot_angle (radians)
			shared_str		m_sFlashSpotTex;			// torch_spot_texture
			Fcolor			m_FlashOmniColor;			// torch_r2_omni_color
			float			m_fFlashOmniRange;			// torch_r2_omni_range
			bool			m_bFlashGlow;				// create_glow
			shared_str		m_sFlashGlowTex;			// torch_glow_texture
			float			m_fFlashGlowRadius;			// torch_glow_radius
			ref_light		m_pFlashSpot;
			ref_light		m_pFlashOmni;
			ref_glow		m_pFlashGlowObj;
			// draw/holster fade (task): the mounted light ramps in over the show anim and out over the hide
			// anim instead of popping on/off. 0=dark, 1=full; scales the spot/omni/glow colour each frame.
			float			m_fFlashFade;
public:

	//для отоброажения иконок апгрейдов в интерфейсе
	// Inventory-icon offset of the mounted optic. GS keeps this in the SCOPE's own section, because each
	// optic sits in a different place on the weapon's icon; the weapon-level scope_x/scope_y is only the
	// fallback for the legacy single-scope path. (Used solely by CUIWeaponCellItem.)
	int	GetScopeX();
	int	GetScopeY();
	int	GetSilencerX() {return m_iSilencerX;}
	int	GetSilencerY() {return m_iSilencerY;}
	int	GetGrenadeLauncherX() {return m_iGrenadeLauncherX;}
	int	GetGrenadeLauncherY() {return m_iGrenadeLauncherY;}

	const shared_str& GetGrenadeLauncherName	()		const {return m_sGrenadeLauncherName;}
	const shared_str& GetScopeName				()		const {return m_sScopeName;}
	const shared_str& GetSilencerName			()		const {return m_sSilencerName;}

	// ---- Gunslinger multi-scope: the weapon lists compatible per-weapon scope SECTIONS via `scopes_sect`;
	// each section's `scope_name` names the addon item that selects it. GetCurrentScopeSection() returns the
	// section of the scope currently attached (its `bones`/offsets/zoom/lens), or the single scope_name /
	// weapon section as a fallback for the legacy single-scope path. ----
	shared_str		GetCurrentScopeSection		()		const;
	shared_str		GetAttachedScopeName		()		const;	// item section of the ATTACHED scope (for detach/icon), not the default scope_name
	int				ScopeIndexByItem			(LPCSTR item_sect)	const;	// index into m_scopes whose scope_name==item_sect, else -1
	bool			IsScopeItem					(LPCSTR item_sect)	const	{ return ScopeIndexByItem(item_sect) >= 0; }

	IC void	ForceUpdateAmmo						()		{ m_dwAmmoCurrentCalcFrame = 0; }

	u8		GetAddonsState						()		const		{return m_flagsAddOnState;};
	void	SetAddonsState						(u8 st)	{m_flagsAddOnState=st;}//dont use!!! for buy menu only!!!

	bool bReloadKeyPressed;
	bool bAmmotypeKeyPressed;

protected:
	//состояние подключенных аддонов
	u8 m_flagsAddOnState;

	//возможность подключения различных аддонов
	ALife::EWeaponAddonStatus	m_eScopeStatus;
	ALife::EWeaponAddonStatus	m_eSilencerStatus;
	ALife::EWeaponAddonStatus	m_eGrenadeLauncherStatus;

	//названия секций подключаемых аддонов
	shared_str		m_sScopeName;
	shared_str		m_sSilencerName;
	shared_str		m_sGrenadeLauncherName;

	// GS multi-scope: per-weapon scope sections from `scopes_sect`, and the index of the attached one
	// (0xFF = none / fall back to m_sScopeName). Persisted in save_data/load_data.
	xr_vector<shared_str>	m_scopes;
	u8						m_cur_scope;

	//смещение иконов апгрейдов в инвентаре
	int	m_iScopeX, m_iScopeY;
	int	m_iSilencerX, m_iSilencerY;
	int	m_iGrenadeLauncherX, m_iGrenadeLauncherY;

protected:

	struct SZoomParams
	{
		bool			m_bZoomEnabled;			//разрешение режима приближения
		bool			m_bHideCrosshairInZoom;
		bool			m_bZoomDofEnabled;

		bool			m_bIsZoomModeNow;		//когда режим приближения включен
		float			m_fCurrentZoomFactor;	//текущий фактор приближения
		float			m_fZoomRotateTime;		//время приближения
	
		float			m_fIronSightZoomFactor;	//коэффициент увеличения прицеливания
		float			m_fScopeZoomFactor;		//коэффициент увеличения прицела

		float			m_fZoomRotationFactor;
		
		Fvector			m_ZoomDof;
		Fvector4		m_ReloadDof;

	} m_zoom_params;
	
	CUIWindow*				m_UIScope;

	InertionData	m_base_inertion;
	InertionData	m_zoom_inertion;

public:

	// GS CanStartAimNow (WeaponAdditionalBuffer.pas:919): with the LAUNCHER RAISED, aiming is refused when
	// `prohibit_aim_for_grenade_mode` is set -- read from the ATTACHED SCOPE's section when there is one,
	// else from the hud section. That is how the Groza stops you sighting a mounted optic through a
	// raised GP-25. Opt-in per scope/weapon, so nothing else changes.
			bool			AimProhibitedByGrenadeMode	()	const;
	IC bool					IsZoomEnabled		()	const		{return m_zoom_params.m_bZoomEnabled && !AimProhibitedByGrenadeMode();}
	virtual	void			ZoomInc				(){};
	virtual	void			ZoomDec				(){};
	virtual void			OnZoomIn			();
	virtual void			OnZoomOut			();
	IC		bool			IsZoomed			()	const		{return m_zoom_params.m_bIsZoomModeNow;};
	// GS inertion: blend hip->aim params by the zoom transition factor; zoom_inertion kills sway in ADS
	virtual	float			GetInertionAimFactor() const		{ float f = GetZoomRotationFactor(); clamp(f, 0.f, 1.f); return f; }
	virtual	bool			InertionZoomedNow	() const		{ return IsZoomed(); }
	IC		bool			IsZoomKeyHeld		()	const		{return m_bZoomKeyHeld;}	// aim key physically held (CMD_START..CMD_STOP)
	virtual bool			IsHudItemZoomed		()				{return IsZoomed();}	// see CHudItem
	CUIWindow*				ZoomTexture			();	


			bool			ZoomHideCrosshair	()				{
				extern int g_dbg_zoom_hide_crosshair;	// debug console override: -1 = per-weapon config, 0 = force show, 1 = force hide (not saved)
				if (g_dbg_zoom_hide_crosshair >= 0)		return g_dbg_zoom_hide_crosshair != 0;
				return m_zoom_params.m_bHideCrosshairInZoom || ZoomTexture();
			}

	IC float				GetZoomFactor		() const		{return m_zoom_params.m_fCurrentZoomFactor;}
	IC void					SetZoomFactor		(float f) 		{m_zoom_params.m_fCurrentZoomFactor = f;}

	virtual	float			CurrentZoomFactor	();
	//показывает, что оружие находится в соостоянии поворота для приближенного прицеливания
			bool			IsRotatingToZoom	() const		{	return (m_zoom_params.m_fZoomRotationFactor<1.f);}
	// 0..1 aim-in/out rotation progress (ramps over zoom_rotate_time); used to ease the
	// iron-sight FOV in sync with the weapon rotating into aim (no instant FOV snap).
	IC		float			GetZoomRotationFactor() const		{	return m_zoom_params.m_fZoomRotationFactor;}

	virtual	u8				GetCurrentHudOffsetIdx ();

	virtual float			Weight				() const;
	virtual u32				Cost				() const;

public:
    virtual EHandDependence		HandDependence		()	const		{	return eHandDependence;}
			bool				IsSingleHanded		()	const		{	return m_bIsSingleHanded; }

public:
	IC		LPCSTR			strap_bone0			() const {return m_strap_bone0;}
	IC		LPCSTR			strap_bone1			() const {return m_strap_bone1;}
	IC		void			strapped_mode		(bool value) {m_strapped_mode = value;}
	IC		bool			strapped_mode		() const {return m_strapped_mode;}

protected:
	LPCSTR					m_strap_bone0;
	LPCSTR					m_strap_bone1;
	Fmatrix					m_StrapOffset;
	bool					m_strapped_mode;
	bool					m_can_be_strapped;

	Fmatrix					m_Offset;
	// 0-используется без участия рук, 1-одна рука, 2-две руки
	EHandDependence			eHandDependence;
	bool					m_bIsSingleHanded;

public:
	//загружаемые параметры
	Fvector					vLoadedFirePoint;
	Fvector					vLoadedFirePoint2;

private:
	firedeps				m_current_firedeps;

protected:
	virtual void			UpdateFireDependencies_internal	();
	virtual void			UpdatePosition			(const Fmatrix& transform);	//.
	virtual void			UpdateXForm				();
	virtual void			UpdateHudAdditonal		(Fmatrix&);
	virtual float			GetHudFov				();	// eases hud_fov -> hud_fov_aim while aiming
	IC		void			UpdateFireDependencies	()			{ if (dwFP_Frame==Device.dwFrame) return; UpdateFireDependencies_internal(); };

	virtual void			LoadFireParams		(LPCSTR section);
public:	
	IC		const Fvector&	get_LastFP				()			{ UpdateFireDependencies(); return m_current_firedeps.vLastFP;	}
	IC		const Fvector&	get_LastFP2				()			{ UpdateFireDependencies(); return m_current_firedeps.vLastFP2;	}
	IC		const Fvector&	get_LastFD				()			{ UpdateFireDependencies(); return m_current_firedeps.vLastFD;	}
	IC		const Fvector&	get_LastSP				()			{ UpdateFireDependencies(); return m_current_firedeps.vLastSP;	}

	virtual const Fvector&	get_CurrentFirePoint	()			{ return get_LastFP();				}
	virtual const Fvector&	get_CurrentFirePoint2	()			{ return get_LastFP2();				}
	virtual const Fmatrix&	get_ParticlesXFORM		()			{ UpdateFireDependencies(); return m_current_firedeps.m_FireParticlesXForm;	}
	virtual void			ForceUpdateFireParticles();
	virtual void			debug_draw_firedeps		();

protected:
	virtual void			SetDefaults				();
	
	virtual bool			MovingAnimAllowedNow	();
	virtual void			OnStateSwitch			(u32 S);
	virtual void			OnAnimationEnd			(u32 state);

	//трассирование полета пули
	virtual	void			FireTrace			(const Fvector& P, const Fvector& D);
	virtual float			GetWeaponDeterioration	();

	virtual void			FireStart			() {CShootingObject::FireStart();}
	virtual void			FireEnd				();

	virtual void			Reload				();
			void			StopShooting		();
    

	// обработка визуализации выстрела
	virtual void			OnShot				(){};
	virtual void			AddShotEffector		();
	virtual void			RemoveShotEffector	();
	virtual	void			ClearShotEffector	();
	virtual	void			StopShotEffector	();

public:
	float					GetFireDispersion	(bool with_cartridge)			;
	float					GetFireDispersion	(float cartridge_k)				;
	// dispersion of the weapon ITSELF, without the shooter's accuracy term -- vanilla SoC's
	// GetBaseDispersion, used by the AN-94 hyperburst for the rounds it fires "into one hole"
	float					GetBaseDispersion	(float cartridge_k)				;
	// while true, FireTrace uses GetBaseDispersion instead of the shooter's accumulated
	// dispersion (see CWeaponMagazined's base_dispersioned_bullets_* block)
	virtual bool			UseBaseFireDispersion() const						{ return false; }
	virtual	int				ShotsFired			() { return 0; }
	virtual	int				GetCurrentFireMode	() { return 1; }

	//параметы оружия в зависимоти от его состояния исправности
	float					GetConditionDispersionFactor	() const;
	float					GetConditionMisfireProbability	() const;
	virtual	float			GetConditionToShow				() const;

public:
	CameraRecoil			cam_recoil;			// simple mode (walk, run)
	CameraRecoil			zoom_cam_recoil;	// using zoom =(ironsight or scope)

protected:
	//фактор увеличения дисперсии при максимальной изношености 
	//(на сколько процентов увеличится дисперсия)
	float					fireDispersionConditionFactor;
	//вероятность осечки при максимальной изношености
	float					misfireProbability;
	float					misfireConditionK;
	// Gunslinger misfire model: linear jam probability between two condition points. Above misfireStartCondition
	// -> no jams; at start -> misfireStartProb; at (and below) misfireEndCondition -> misfireEndProb. Active only
	// when misfireStartCondition > 0 (config has misfire_start_condition); otherwise the legacy K-model is used.
	float					misfireStartCondition;
	float					misfireEndCondition;
	float					misfireStartProb;
	float					misfireEndProb;
	//увеличение изношености при выстреле
	float					conditionDecreasePerShot;
	float					conditionDecreasePerShotQueue;	// GS condition_queue_shot_dec: wear per shot while firing an auto/burst queue (0 = same as single)
	
	struct SPDM
	{
		float					m_fPDM_disp_base			;
		float					m_fPDM_disp_vel_factor		;
		float					m_fPDM_disp_accel_factor	;
		float					m_fPDM_disp_crouch			;
		float					m_fPDM_disp_crouch_no_acc	;
	};
	SPDM					m_pdm;
	
	float					m_crosshair_inertion;
	first_bullet_controller	m_first_bullet_controller;
protected:
	//для отдачи оружия
	Fvector					m_vRecoilDeltaAngle;

	//для сталкеров, чтоб они знали эффективные границы использования 
	//оружия
	float					m_fMinRadius;
	float					m_fMaxRadius;

protected:	
	//для второго ствола
			void			StartFlameParticles2();
			void			StopFlameParticles2	();
			void			UpdateFlameParticles2();
protected:
	shared_str				m_sFlameParticles2;
	//объект партиклов для стрельбы из 2-го ствола
	CParticlesObject*		m_pFlameParticles2;

public:
	IC int					GetAmmoElapsed		()	const		{	return /*int(m_magazine.size())*/iAmmoElapsed;}
	IC int					GetAmmoMagSize		()	const		{	return iMagazineSize;						}
	int						GetSuitableAmmoTotal		(bool use_item_to_spawn = false)  const;
	int						GetCurrentTypeAmmoTotal		()  const;
	int						GetAmmoCountByType			(u32 type) const;	// rounds of a SPECIFIC ammo-type index available in the inventory (belt+ruck), excluding the magazine

	void					SetAmmoElapsed		(int ammo_count);

	virtual void			OnMagazineEmpty		();
			void			SpawnAmmo			(u32 boxCurr = 0xffffffff, 
													LPCSTR ammoSect = NULL, 
													u32 ParentID = 0xffffffff);
	virtual bool			SwitchAmmoType		(u32 flags);

	virtual	float			Get_PDM_Base		()	const	{ return m_pdm.m_fPDM_disp_base			; };
	virtual	float			Get_PDM_Vel_F		()	const	{ return m_pdm.m_fPDM_disp_vel_factor		; };
	virtual	float			Get_PDM_Accel_F		()	const	{ return m_pdm.m_fPDM_disp_accel_factor	; };
	virtual	float			Get_PDM_Crouch		()	const	{ return m_pdm.m_fPDM_disp_crouch			; };
	virtual	float			Get_PDM_Crouch_NA	()	const	{ return m_pdm.m_fPDM_disp_crouch_no_acc	; };
	virtual	float			GetCrosshairInertion()	const	{ return m_crosshair_inertion; };
			float			GetFirstBulletDisp	()	const	{ return m_first_bullet_controller.get_fire_dispertion(); };
protected:
	int						iAmmoElapsed;		// ammo in magazine, currently
	int						iMagazineSize;		// size (in bullets) of magazine

	//для подсчета в GetSuitableAmmoTotal
	mutable int				iAmmoCurrent;
	mutable u32				m_dwAmmoCurrentCalcFrame;	//кадр на котором просчитали кол-во патронов
	bool					m_bAmmoWasSpawned;

	virtual bool			IsNecessaryItem	    (const shared_str& item_sect);

public:
	xr_vector<shared_str>	m_ammoTypes;

	CWeaponAmmo*			m_pAmmo;
	u32						m_ammoType;
	shared_str				m_ammoName;
	BOOL					m_bHasTracers;
	u8						m_u8TracerColorID;
	u32						m_set_next_ammoType_on_reload;
	// Multitype ammo support
	xr_vector<CCartridge>	m_magazine;
	CCartridge				m_DefaultCartridge;
	float					m_fCurrentCartirdgeDisp;

		bool				unlimited_ammo				();
	IC	bool				can_be_strapped				() const {return m_can_be_strapped;};

	LPCSTR					GetCurrentAmmo_ShortName	();

protected:
	u32						m_ef_main_weapon_type;
	u32						m_ef_weapon_type;

public:
	virtual u32				ef_main_weapon_type	() const;
	virtual u32				ef_weapon_type		() const;

protected:
	// This is because when scope is attached we can't ask scope for these params
	// therefore we should hold them by ourself :-((
	float					m_addon_holder_range_modifier;
	float					m_addon_holder_fov_modifier;

public:
	virtual	void			modify_holder_params		(float &range, float &fov) const;
	virtual bool			use_crosshair				()	const {return true;}
			bool			show_crosshair				();
			bool			show_indicators				();
	virtual BOOL			ParentMayHaveAimBullet		();
	virtual BOOL			ParentIsActor				();
	
private:
	virtual	bool			install_upgrade_ammo_class	( LPCSTR section, bool test );
			bool			install_upgrade_disp		( LPCSTR section, bool test );
			bool			install_upgrade_hit			( LPCSTR section, bool test );
			bool			install_upgrade_addon		( LPCSTR section, bool test );
protected:
	virtual bool			install_upgrade_impl		( LPCSTR section, bool test );

private:
	float					m_hit_probability[egdCount];

public:
	const float				&hit_probability			() const;
	
	virtual void				DumpActiveParams			(shared_str const & section_name, CInifile & dst_ini) const;
	virtual shared_str const	GetAnticheatSectionName		() const { return cNameSect(); };
};
