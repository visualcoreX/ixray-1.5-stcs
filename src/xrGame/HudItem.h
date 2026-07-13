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
	};
public:
	virtual void				Load				(LPCSTR section);
	virtual	BOOL				net_Spawn			(CSE_Abstract* DC)				{return TRUE;};
	virtual void				net_Destroy			()								{};
	virtual void				OnEvent				(NET_Packet& P, u16 type);

	virtual void				OnH_A_Chield		();
	virtual void				OnH_B_Chield		();
	virtual void				OnH_B_Independent	(bool just_before_destroy);
	virtual void				OnH_A_Independent	();
	
	virtual void				PlaySound			(LPCSTR alias, const Fvector& position);

	virtual bool				Action				(s32 cmd, u32 flags)			{return false;}
			void				OnMovementChanged	(ACTOR_DEFS::EMoveCommand cmd)	;
	
	virtual	u8					GetCurrentHudOffsetIdx ()							{return 0;}

	BOOL						GetHUDmode			();
	IC BOOL						IsPending			()		const					{ return !!m_huditem_flags.test(fl_pending);}

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
	virtual bool				MovingAnimAllowedNow ()				{return true;}

	virtual void				PlayAnimIdleMoving	();
	virtual void				PlayAnimIdleSprint	();

	// walk_slow: when the actor is walking (not running) inserts "_slow" after the
	// "anm_idle_moving" prefix of any moving-anim name, falling back to the plain
	// name if the slow variant is absent on this HUD. m_bStepSlow is refreshed each
	// TryPlayAnimIdle() and consumed by the PlayAnimIdleMoving() overrides.
	LPCSTR						SelectMovingAnim	(LPCSTR base);
	bool						m_bStepSlow;
	bool						m_bStepCrouch;	// crouch-move: anm_idle_moving_crouch[_slow]

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
	virtual bool				NeedJammedAnim			() { return false; }
	void						MakeJammedName			(LPCSTR name, string_path& out);
protected:

	IC		void				SetPending			(BOOL H)			{ m_huditem_flags.set(fl_pending, H);}
	shared_str					hud_sect;

	//����� ������� ��������� XFORM � FirePos
	u32							dwFP_Frame;
	u32							dwXF_Frame;

	u32							m_animation_slot;

	HUD_SOUND_COLLECTION		m_sounds;
	InertionData				m_current_inertion;
	float						m_fHudFov;
	float						m_fHudFovAim;	// hud_fov while fully aimed (0 => same as m_fHudFov)

private:
	CPhysicItem					*m_object;
	CInventoryItem				*m_item;

public:
	const shared_str&			HudSection				() const		{ return hud_sect;}
	IC CPhysicItem&				object					() const		{ VERIFY(m_object); return(*m_object);}
	IC CInventoryItem&			item					() const		{ VERIFY(m_item); return(*m_item);}
	IC		u32					animation_slot			()				{ return m_animation_slot;}
	InertionData& CurrentInertionData() { return m_current_inertion; }

	virtual void				on_renderable_Render	() = 0;
	virtual void				debug_draw_firedeps		() {};

	virtual CHudItem*			cast_hud_item			()				{ return this; }
};

