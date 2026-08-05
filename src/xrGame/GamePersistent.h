#ifndef GamePersistentH
#define GamePersistentH
#pragma once

#include "../xrEngine/IGame_Persistent.h"
class CMainMenu;
class CUICursor;
class CParticlesObject;
class CUISequencer;
class ui_core;

class CGamePersistent: 
	public IGame_Persistent, 
	public IEventReceiver
{
	// ambient particles
	CParticlesObject*	ambient_particles; 
	u32					ambient_sound_next_time		[20]; //max snd channels
	u32					ambient_effect_next_time;
	u32					ambient_effect_stop_time;

	float				ambient_effect_wind_start;
	float				ambient_effect_wind_in_time;
	float				ambient_effect_wind_end;
	float				ambient_effect_wind_out_time;
	bool				ambient_effect_wind_on;

	bool				m_bPickableDOF;

	CUISequencer*		m_intro;
	EVENT				eQuickLoad;
	Fvector				m_dof		[4];	// 0-dest 1-current 2-from 3-original
	// GS ActorDOF.pas: the blend rate is NOT a constant. Every transition carries its own speed --
	// vanilla's hardcoded "reach the target in 0.2s" is only GS's `default_dof_speed`, and aiming in
	// (3 = 0.33s) / out (1 = 1.0s) are deliberately different from it and from each other.
	float				m_dof_speed;
	bool				m_dof_changed;		// GS _dof_changed: a Restore with nothing to undo is a no-op

	fastdelegate::FastDelegate0<> m_intro_event;

	void xr_stdcall		start_logo_intro		();
	void xr_stdcall		update_logo_intro		();
	void xr_stdcall		start_game_intro		();
	void xr_stdcall		update_game_intro		();

#ifdef DEBUG
	u32					m_frame_counter;
	u32					m_last_stats_frame;
#endif

	void				WeathersUpdate			();
	void				UpdateDof				();

public:
	ui_core*			m_pUI_core;
	IReader*			pDemoFile;
	u32					uTime2Change;
	EVENT				eDemoStart;

						CGamePersistent			();
	virtual				~CGamePersistent		();

	virtual void		Start					(LPCSTR op);
	virtual void		Disconnect				();

	virtual	void		OnAppActivate			();
	virtual void		OnAppDeactivate			();

	virtual void		OnAppStart				();
	virtual void		OnAppEnd				();
	virtual	void		OnGameStart				();
	virtual void		OnGameEnd				();
	virtual void		OnFrame					();
	virtual void		OnEvent					(EVENT E, u64 P1, u64 P2);

	virtual void		UpdateGameType			();

	virtual void		RegisterModel			(IRenderVisual* V);
	virtual	float		MtlTransparent			(u32 mtl_idx);
	virtual	void		Statistics				(CGameFont* F);

	virtual bool		OnRenderPPUI_query		();
	virtual void		OnRenderPPUI_main		();
	virtual void		OnRenderPPUI_PP			();
	virtual void		OnRenderForward			();
	virtual bool		OnRenderPdaUI			();	// 3D PDA: draw the window for the $user$ui snapshot
	virtual bool		OnRenderScopeActive		();	// 3D PiP scope: true while aiming through a lensed scope
	virtual bool		ComputeLensFrame		(float& out_fov);	// 3D PiP double-render: decide lens frame + magnified FOV
	virtual	void		LoadTitle				(LPCSTR str);

	virtual bool		CanBePaused				();

			void		SetPickableEffectorDOF	(bool bSet);
			void		SetEffectorDOF			(const Fvector& needed_dof, float speed);
			void		RestoreEffectorDOF		(float speed);
			void		SetEffectorDOF			(const Fvector& needed_dof);	// base speed
			void		RestoreEffectorDOF		();							// "out" speed
			bool		DofChanged				() const	{ return m_dof_changed; }

	// GS DOF tuning, read once from [gunslinger_base] (gunslinger_params.ltx) under GS's own key
	// names, with GS's own fallbacks. Speeds are 1/seconds: 5 = the vanilla 0.2s, 1 = a full second.
	struct SDofDefaults
	{
		Fvector	zoom;			// default_zoom_dof_near/_focus/_far		0.5 / 0.8 / 10000
		Fvector	action;			// default_action_dof_near/_focus/_far	0   / 0.5 / 5
		float	speed;			// default_dof_speed		5	(base / pickable)
		float	speed_in;		// default_dof_speed_in		3	(into aim, into an action)
		float	speed_out;		// default_dof_speed_out	1	(back out)
		float	time_offset;	// default_dof_time_offset	-0.5 s (see CHudItem::UpdateCL)
	};
	static const SDofDefaults&	DofDefaults	();

	virtual void		GetCurrentDof			(Fvector3& dof);
	virtual void		SetBaseDof				(const Fvector3& dof);
	virtual void		OnSectorChanged			(int sector);
	virtual void		OnAssetsChanged			();
};

IC CGamePersistent&		GamePersistent()		{ return *((CGamePersistent*) g_pGamePersistent);			}

#endif //GamePersistentH

