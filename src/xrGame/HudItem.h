#pragma once

class CSE_Abstract;
class CPhysicItem;
class NET_Packet;
class CInventoryItem;
class CMotionDef;

#include "actor_defs.h"
#include "inventory_space.h"
#include "hudsound.h"
#include "InertionData.h"

struct attachable_hud_item;
class motion_marks;

class CHUDState
{
public:
enum EHudStates {
		eIdle		= 0,
		eShowing,
		eHiding,
		eHidden,
		eBore,
		eLastBaseState = eBore,
};

private:
	u32						m_hud_item_state;
	u32						m_nextState;
	u32						m_dw_curr_state_time;
protected:
	u32						m_dw_curr_substate_time;
public:
							CHUDState			()					{SetState(eHidden);}
	IC		u32				GetNextState		() const			{return		m_nextState;}
	IC		u32				GetState			() const			{return		m_hud_item_state;}

	IC		void			SetState			(u32 v)				{m_hud_item_state = v; m_dw_curr_state_time=Device.dwTimeGlobal;ResetSubStateTime();}
	IC		void			SetNextState		(u32 v)				{m_nextState = v;}
	IC		u32				CurrStateTime		() const			{return Device.dwTimeGlobal-m_dw_curr_state_time;}
	IC		void			ResetSubStateTime	()					{m_dw_curr_substate_time=Device.dwTimeGlobal;}
	virtual void			SwitchState			(u32 S)				= 0;
	virtual void			OnStateSwitch		(u32 S)				= 0;
};

class CHudItem :public CHUDState
{
protected:
							CHudItem			();
	virtual					~CHudItem			();
	virtual DLL_Pure*		_construct			();
	
	Flags16					m_huditem_flags;
	enum{
		fl_pending			= (1<<0),
		fl_renderhud		= (1<<1),
	};

	struct{
		const CMotionDef*		m_current_motion_def;
		shared_str				m_current_motion;
		u32						m_dwMotionCurrTm;
		u32						m_dwMotionStartTm;
		u32						m_dwMotionEndTm;
		u32						m_startedMotionState;
		u8						m_started_rnd_anim_idx;
		bool					m_bStopAtEndAnimIsRunning;
		bool					m_bAttachedNoMotion;
	};

	// GS action DOF (ActorDOF.pas + the WeaponAnims *_selector calls): the animation now running has
	// pulled focus onto the hands, and these say how and when to let it back out.
	bool						m_bActionDofActive;
	float						m_fActionDofOutSpeed;
	int							m_iActionDofTimeOffsetMs;	// <0: release that long BEFORE the anim ends
															// >0: that long AFTER it started
	// A hud model is attached a beat AFTER its motion starts -- the item-use phantoms play anm_show
	// with nothing on screen yet -- so a DOF request that arrives too early is parked here and
	// re-issued from UpdateCL once the model is actually up.
	shared_str				m_pending_dof_alias;
	u32						m_pending_dof_until;
			void				StartActionDof		(LPCSTR anim_alias);
			void				StopActionDof		();
	// While aiming, the aim DOF owns the effector and an action must not yank it back (GS checks
	// IsAimNow / IsHolderInAimState before ResetDOF). IsHudItemZoomed is CWeapon's IsZoomed().
			bool				DofHeldByAim		()			{ return IsHudItemZoomed(); }
public:
	virtual void				Load				(LPCSTR section);
	virtual	BOOL				net_Spawn			(CSE_Abstract* DC)				{return TRUE;};
	// the action DOF belongs to an animation on THIS item; if the item goes away mid-animation
	// (an interrupted item-use gesture, whose phantom is simply released) nothing else would ever
	// let the focus back out -- so release it here too, not only when the animation ends.
	virtual void				net_Destroy			()								{ StopActionDof(); };
	virtual void				OnEvent				(NET_Packet& P, u16 type);

	virtual void				OnH_A_Chield		();
	virtual void				OnH_B_Chield		();
	virtual void				OnH_B_Independent	(bool just_before_destroy);
	virtual void				OnH_A_Independent	();
	
	virtual void				PlaySound			(LPCSTR alias, const Fvector& position, bool b_overlap = false);

	virtual bool				Action				(s32 cmd, u32 flags)			{return false;}
			void				OnMovementChanged	(ACTOR_DEFS::EMoveCommand cmd)	;
	
	virtual	u8					GetCurrentHudOffsetIdx ()							{return 0;}

	BOOL						GetHUDmode			();
	IC BOOL						IsPending			()		const					{ return !!m_huditem_flags.test(fl_pending);}
	// Attached to the hands with no motion of its own yet: the frame between a finished holster and
	// the start of the next draw. Rendering there shows the model at its bind pose (or, if parked on
	// an idle, at a pose that has nothing to do with the draw), so it is simply not drawn.
	IC bool						HudSilentFrame		()		const					{ return m_bAttachedNoMotion; }
	void						ClearSilentFrame	()								{ m_bAttachedNoMotion = false; }

	// --- Gunslinger-style locks (single, uniform queries) ---------------------------------------------
	// Two lock concepts, mirroring GS's WpnBuf:
	//   ACTION lock == our IsPending() (GS _lock_remain_time / CanStartAction): while set, no new action
	//     (reload / aim / gesture / fire) may start. Already one clean gate -- nothing to consolidate.
	//   SHOOT lock == IsShootLocked() (GS SetShootLockTime): firing is blocked for a timed window while
	//     other actions stay allowed. Simple timed fire-blocks call SetShootLock(ms); each feature keeps
	//     its own resume/defer. Weapons OVERRIDE IsShootLocked() to fold in their bespoke fire-lock
	//     deadlines (aim in/out, sprint-exit) so every caller has ONE "is firing locked right now?" query.
	void						SetShootLock		(u32 ms);
	virtual bool				IsShootLocked		() const;

	virtual bool				ActivateItem		();
	virtual void				DeactivateItem		();
	virtual void				SendDeactivateItem	();
	virtual void				OnActiveItem		()				{};
	virtual void				OnHiddenItem		()				{};
	virtual void				SendHiddenItem		();			//same as OnHiddenItem but for client... (sends message to a server)...
	virtual void				OnMoveToRuck		(EItemPlace prev);

	bool						IsHidden			()	const		{	return GetState() == eHidden;}						// Does weapon is in hidden state
	bool						IsHiding			()	const		{	return GetState() == eHiding;}
	bool						IsShowing			()	const		{	return GetState() == eShowing;}

	virtual void				SwitchState			(u32 S);
	virtual void				OnStateSwitch		(u32 S);

	virtual void				OnAnimationEnd		(u32 state);
	virtual void				OnMotionMark		(u32 state, const motion_marks&){};

	virtual void				PlayAnimIdle		();
	virtual void				PlayAnimBore		();
	bool						TryPlayAnimIdle		();
	bool						TryPlayBlowoutAnim	();		// GS emission glitch anim; safe to poll every frame (fires once, only in eIdle)
	virtual bool				MovingAnimAllowedNow ()				{return true;}

	virtual void				PlayAnimIdleMoving	();
	virtual void				PlayAnimIdleSprint	();
	// The looping sprint-idle anim base for THIS item (subclasses add their suffix: GL "_g"/"_w_gl",
	// bm16 shell "_0".."_2"). The enter/exit transitions are derived from it (MakeSprintVariant), so
	// all the suffix logic lives in one override and start/loop/end stay centralized.
	virtual LPCSTR				SprintLoopBase		()	{ return "anm_idle_sprint"; }
	// Companion (left-hand) anims: when THIS is the active weapon (hud idx 0) and it plays a motion,
	// mirror it on an out detector (idx 1) via its anm_wpn_<action>. Base is a no-op; CCustomDetector
	// overrides PlayCompanionAction to actually play it. See Gunslinger's StartCompanionAnimIfNeeded.
	// bRestart: the weapon just (re)started this motion, so replay the companion even if it's the one
	// already playing (one-shots like dry/shots must re-trigger). The detector's own idle mirror passes
	// false so a mere refresh can't restart/jerk a companion that's already running.
	virtual bool				PlayCompanionAction	(LPCSTR action, bool bRestart = false)	{ return false; }
	void						TryPlayDetectorCompanion(const shared_str& weaponMotion);
	// While set, NOTHING this item plays drives the detector companion - not through the PlayHUDMotion
	// hook, and not through the detector's own idle mirror (CCustomDetector::PlayAnimIdle reads our
	// CurrentMotion, so a one-shot flag wouldn't hold: the detector re-picks the motion later, when its
	// own companion ends). For an anm_show that isn't a draw the player asked for: after a throw the bolt
	// re-shows itself (CMissile eThrowEnd -> eShowing) to put the next one in hand.
	bool						m_bSuppressCompanion;
	bool						CompanionSuppressed	() const	{ return m_bSuppressCompanion; }
	const shared_str&			CurrentMotion		() const	{ return m_current_motion; }
	// build "anm_idle_sprint_<which><suffix>" from a loop base "anm_idle_sprint<suffix>"
	void						MakeSprintVariant	(LPCSTR loop_base, LPCSTR which, string_path& out);
	bool						HasSprintExitAnim	();	// the item has a (suffix-correct) sprint-exit anim

	// walk_slow: when the actor is walking (not running) inserts "_slow" after the
	// "anm_idle_moving" prefix of any moving-anim name, falling back to the plain
	// name if the slow variant is absent on this HUD. m_bStepSlow is refreshed each
	// TryPlayAnimIdle() and consumed by the PlayAnimIdleMoving() overrides.
	LPCSTR						SelectMovingAnim	(LPCSTR base);
	bool						m_bStepSlow;
	bool						m_bStepCrouch;	// crouch-move: anm_idle_moving_crouch[_slow]
	// sprint enter/exit transitions (Gunslinger-style): true once anm_idle_sprint_start has played,
	// cleared after anm_idle_sprint_end. Gates the one-shot start/end vs the looping anm_idle_sprint.
	bool						m_bSprintStarted;
	// one-shot blend-in (Accrue) override for the NEXT PlayHUDMotion only (0 = use the motion's baked blend).
	// A LOWER accrue = slower/softer mix-in. Used so the sprint enter transition eases in smoothly instead of
	// the anim's fast baked blend. Consumed (reset to 0) by attachable_hud_item::anim_play.
	float						m_fNextBlendAccrue;
	// true while the sprint-START one-shot (anm_idle_sprint_start) is actually on screen -- guards it from
	// being replaced by the loop by a same-frame re-entry (aim-transition handoff). Cleared when the loop plays.
	bool						m_bSprintStartRunning;
	// previous-frame actor sprint state: a false->true edge means sprint began fresh, so the enter
	// transition (anm_idle_sprint_start) must replay even if m_bSprintStarted was left set by a prior
	// state (e.g. aiming out straight into a sprint) -- otherwise the loop would snap in without the start.
	bool						m_bPrevSprint;
	// resolved name of the sprint LOOP we started, and whether that motion is cyclic (self-running).
	// A cyclic motion keeps going on its own, so re-playing it only cross-fades the hands back to
	// frame 0; PlayAnimIdleSprint uses these to skip that restart. Empty = no loop of ours playing.
	shared_str					m_sprint_loop_motion;
	bool						m_bSprintLoopCyclic;
	// wall-clock deadline while the sprint-EXIT anim (anm_idle_sprint_end) is playing, minus a few
	// cut frames: fire/aim is deferred until then so the exit plays first, then the action resumes.
	u32							m_dwSprintExitEndTm;
	// wall-clock deadline for a plain timed shoot-lock (GS SetShootLockTime): firing blocked until then,
	// other actions allowed. Set via SetShootLock(); 0 = no lock. Weapons OR their own fire-lock deadlines
	// into IsShootLocked() on top of this.
	u32							m_dwShootLockTm;

	// true iff this HUD has a movement-dependent idle (slow-walk, or directional
	// aim-walk for weapons) -> OnMovementChanged re-plays it immediately on a
	// speed/direction change instead of waiting for the current cycle to end.
	virtual bool				HasMovementIdleVariant();
	// set while an aim in/out transition (an eIdle-owned motion) is playing so the
	// movement refresh does not cut it short; cleared when that motion ends.
	bool						m_bIdleTransitionLock;

	virtual void				UpdateCL			();
	virtual void				renderable_Render	();


	virtual void				UpdateHudAdditonal	(Fmatrix&);


	virtual	void				UpdateXForm			()						= 0;

	u32							PlayHUDMotion		(const shared_str& M, BOOL bMixIn, CHudItem*  W, u32 state);
	u32							PlayHUDMotion_noCB	(const shared_str& M, BOOL bMixIn);
	// read-and-clear the one-shot blend-in override for the next HUD motion (see m_fNextBlendAccrue)
	float						ConsumeNextBlendAccrue()	{ float v = m_fNextBlendAccrue; m_fNextBlendAccrue = 0.f; return v; }
	void						StopCurrentAnimWithoutCallback();

	IC void						RenderHud				(BOOL B)	{ m_huditem_flags.set(fl_renderhud, B);}
	IC BOOL						RenderHud				()			{ return m_huditem_flags.test(fl_renderhud);}
	attachable_hud_item*		HudItemData				();
	virtual void				on_a_hud_attach			();
	virtual void				on_b_hud_detach			();
	virtual void				render_hud_mode			()					{};
	virtual bool				need_renderable			()					{return true;};
	virtual void				render_item_3d_ui		()					{}
	virtual bool				render_item_3d_ui_query	()					{return false;}

	virtual bool				CheckCompatibility		(CHudItem*)			{return true;}

	// hud_fov (weapon-render FOV as a fraction of world FOV). Weapons ease it toward
	// m_fHudFovAim while aiming (Gunslinger hud_fov_zoom_factor), so override.
	virtual float				GetHudFov();

	bool						isHUDAnimationExist		(LPCSTR anim_name);
	// while the weapon is jammed, every HUD gesture uses its "_jammed" variant (stuck bolt).
	// NeedJammedAnim() is overridden by CWeaponMagazined to return IsMisfire(); MakeJammedName
	// inserts "_jammed" before a trailing GL/shell suffix (falls back to the base if absent).
	// 3D PDA: this item takes its idle from the PDA cursor direction (hud config `pda_cursor_anims`),
	// i.e. anm_idle_<dir> instead of the plain anm_idle. See g_pda_cursor_dir / CUIPdaWnd::Update.
	bool						m_bPdaCursorAnims;
	bool						UsesPdaCursorAnims		() const	{ return m_bPdaCursorAnims; }
	// GS play_blowout_anim: the emission glitch (anm_blowout) plays ONCE per surge, not on a loop -- this
	// latches after the first play and re-arms only when the surge passes (electronics level drops back).
	bool						m_bBlowoutPlayed;
	// Wall clock until which anm_blowout is still playing. The PDA cursor loop calls PlayAnimIdle repeatedly;
	// without this it would replay the cursor idle over the glitch a frame after it starts. See UIPdaWnd.
	u32							m_dwBlowoutUntil;
	bool						IsBlowoutAnimPlaying	() const	{ return Device.dwTimeGlobal < m_dwBlowoutUntil; }

	// ---- ppe over a slice of a hud motion (Gunslinger's NV blackout), keyed per alias ----
	shared_str					m_show_ppe_sect;	// effector SECTION (holds pp_eff_name)
	u32							m_show_ppe_at;		// wall clock; 0 = nothing pending
	u32							m_show_ppe_off_at;
	bool						m_show_ppe_on;
	void						ArmPPE					(const shared_str& alias);
	void						UpdateShowPPE			();
	u32							MotionEndTm				() const	{ return m_dwMotionEndTm; }	// wall clock; 0 when nothing is running
	u32							MotionStartTm			() const	{ return m_dwMotionStartTm; }	// ...and when it began
	// CHudItem knows nothing about aiming; CWeapon overrides this with IsZoomed(). Only used to pick
	// the PDA's aim-variant idles (anm_idle_aim<dir>) from inside TryPlayAnimIdle.
	virtual bool				IsHudItemZoomed			()			{ return false; }
	virtual bool				NeedJammedAnim			() { return false; }
	// NeedEmptyAnim() is overridden by CWeaponMagazined to return iAmmoElapsed==0 -> PlayHUDMotion
	// rewrites the alias to its "_empty" variant (bolt/slide held back) when the weapon has one.
	virtual bool				NeedEmptyAnim			() { return false; }
	// "_empty" applies ONLY when the weapon is not also jammed: GS precedence is jammed > empty, and the
	// two tokens do not combine (no anm_x_jammed_empty exists anywhere). Any selector that hands
	// PlayHUDMotion a ready-made "_empty" alias must gate on THIS, not on the ammo count -- otherwise it
	// defeats the rewrite, which can only add a token, never replace the one already in the name.
	bool						UseEmptyAnimOnly		() { return NeedEmptyAnim() && !NeedJammedAnim(); }
	// NeedFirstAnim() is overridden by CWeaponShotgun (just reloaded, no shot since, mag not empty) ->
	// PlayHUDMotion rewrites the alias to its "_first" variant (the drum's fresh-round idle/gestures).
	// Order in PlayHUDMotion mirrors GS ModifierStd: jammed > empty > first.
	virtual bool				NeedFirstAnim			() { return false; }
	// GS's INVERTED scope convention (assault/oc14 huds.ltx anm_switch family): the BARE alias is the
	// with-a-scope-mounted animation and "<alias>_noscope" is the plain one -- the opposite of the
	// "_scope" infix used by the aim/shoot aliases. CWeapon returns !UseScopeAnims(). PlayHUDMotion
	// appends the token last, after the firemode mark, the state token and any GL suffix, so it matches
	// names like anm_switch_jammed_w_gl_noscope.
	virtual bool				NeedNoScopeAnim			() { return false; }
	void						MakeStateName			(LPCSTR name, LPCSTR infix, string_path& out);
	void						MakeJammedName			(LPCSTR name, string_path& out);
	// GS firemode selector (GetFireModeStateMark): CWeaponMagazined appends the mask_firemode_<a|N>
	// value so each fire mode plays its own baked-selector anim variant. Default: no mark (copy base).
	virtual void				MakeFireModeName		(LPCSTR name, string_path& out) { xr_strcpy(out, name); }
	// The bare mark for `name` ("" = none). PlayHUDMotion needs it to build base+mark+state in ONE
	// step: gating on the intermediate base+mark (which many configs never author) loses the mark.
	virtual LPCSTR				GetFireModeMark			(LPCSTR /*name*/) { return ""; }
	// splits the trailing state/variant token run ("_empty", "_last", "_jammed", "_sil", GL suffixes...)
	// off an alias so the firemode mark can be inserted where GS puts it: base + mark + tail.
	// virtual: the double-barrels (CWeaponBM16) also carry a trailing loaded-shell count, which GS's
	// ModifierBM16 appends LAST -- anm_reload_jammed_1, i.e. the state token goes in FRONT of it.
	virtual bool				SplitStateSuffix		(LPCSTR name, string_path& stem, string_path& tail);
	// Does the alias PlayHUDMotion would settle on for state token <st_tok> ("_jammed", ...) exist?
	// Same candidate order it uses, so callers can test before deciding to (re)assign a motion.
	bool						HasStateVariant			(LPCSTR alias, LPCSTR st_tok);
protected:

	IC		void				SetPending			(BOOL H)			{ m_huditem_flags.set(fl_pending, H);}
	shared_str					hud_sect;

	//����� ������� ��������� XFORM � FirePos
	u32							dwFP_Frame;
	u32							dwXF_Frame;

	u32							m_animation_slot;
	shared_str					m_actor_anim_group;		// xrMPE `actor_anim_group`, empty when unused
	shared_str					m_actor_torso_anim;		// xrMPE `actor_anim_group`, empty when unused

	HUD_SOUND_COLLECTION		m_sounds;
	InertionData				m_current_inertion;		// hip params (per-weapon inertion_* over [gunslinger_base] defaults)
	InertionData				m_aim_inertion;			// aim params (inertion_aim_* over aim defaults)
	InertionData				m_gl_inertion;			// grenade-mode params (inertion_gl_*, defaults = the resolved aim set, like GS)
	InertionData				m_blend_inertion;		// scratch for the hip<->aim lerp returned by CurrentInertionData
	bool						m_bHudInertion;			// hud_inertion (hud section, GS default true): master switch
	bool						m_bZoomInertion;		// zoom_inertion (GS code default false, GS base config true): kill inertion while zoomed
	bool						m_bDisableBore;			// disable_bore (hud section, GS default true): never play the idle "bore"/fidget anim
	float						m_fHudFov;
	float						m_fHudFovAim;	// hud_fov while fully aimed (0 => same as m_fHudFov)
	float						m_fHudFovAimScope;	// hud_fov while aiming a 3D-PiP lensed scope only (0 => use m_fHudFovAim)

private:
	CPhysicItem					*m_object;
	CInventoryItem				*m_item;

public:
	const shared_str&			HudSection				() const		{ return hud_sect;}
	IC CPhysicItem&				object					() const		{ VERIFY(m_object); return(*m_object);}
	IC CInventoryItem&			item					() const		{ VERIFY(m_item); return(*m_item);}
	IC		u32					animation_slot			()				{ return m_animation_slot;}
	// Named actor torso set for this item, empty = use the numbered animation_slot. Virtual because a
	// launcher rifle swaps between two of them (see CWeaponMagazinedWGrenade).
	virtual const shared_str&	ActorAnimGroup			() const		{ return m_actor_anim_group; }
	const shared_str&			ActorTorsoAnim			() const		{ return m_actor_torso_anim; }
	InertionData&				CurrentInertionData		();				// GS UpdateInertion: hip<->aim blend by zoom factor, honors hud_inertion/zoom_inertion
	virtual float				GetInertionAimFactor	() const		{ return 0.f; }	// CWeapon: zoom rotation factor
	virtual bool				InertionZoomedNow		() const		{ return false; }
	virtual bool				InertionGrenadeModeNow	() const		{ return false; }	// CWeaponMagazinedWGrenade: m_bGrenadeMode

	virtual void				on_renderable_Render	() = 0;
	virtual void				debug_draw_firedeps		() {};

	virtual CHudItem*			cast_hud_item			()				{ return this; }
};

