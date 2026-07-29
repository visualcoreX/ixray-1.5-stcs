#ifndef IGame_PersistentH
#define IGame_PersistentH
#pragma once

#include "..\xrServerEntities\gametype_chooser.h"
#ifndef _EDITOR
#include "Environment.h"
#include "IGame_ObjectPool.h"
#endif

class IRenderVisual;
class IMainMenu;
class ENGINE_API CPS_Instance;
//-----------------------------------------------------------------------------------------------------------
class ENGINE_API IGame_Persistent	: 
#ifndef _EDITOR
	public DLL_Pure,
#endif
	public pureAppStart, 
	public pureAppEnd,
	public pureAppActivate, 
	public pureAppDeactivate,
	public pureFrame
{
public:
	union params {
		struct {
			string256	m_game_or_spawn;
			string256	m_game_type;
			string256	m_alife;
			string256	m_new_or_load;
			EGameIDs	m_e_game_type;
		};
		string256		m_params[4];
						params		()	{	reset();	}
		void			reset		()
		{
			for (int i=0; i<4; ++i)
				xr_strcpy	(m_params[i],"");
		}
		void						parse_cmd_line		(LPCSTR cmd_line)
		{
			reset					();
			int						n = _min(4,_GetItemCount(cmd_line,'/'));
			for (int i=0; i<n; ++i) {
				_GetItem			(cmd_line,i,m_params[i],'/');
				_strlwr				(m_params[i]);
			}
		}
	};
	params							m_game_params;
public:
	xr_set<CPS_Instance*>			ps_active;
	xr_vector<CPS_Instance*>		ps_destroy;
	xr_vector<CPS_Instance*>		ps_needtoplay;

	// Gunslinger exo HUD-screen shader constants, filled by CActor each frame, read by the render constant binder.
	// m_actor_params: x=actor_health, y=outfit_cond, z=weapon_cond, w=weapon_loading (all 0..1, or -1 when absent)
	// m_affects: exo "electronics problems" (no such system in IX-Ray -> kept 0 = clean screen); kept for shader compatibility
	Fvector4						hud_actor_params;
	Fvector4						hud_affects;

	// Gunslinger 3D PiP scope lens shader constants (model_scope_lense / models_zoom), filled by CActor each frame.
	// m_hud_params:     x=aspect(h/w), y=aim_factor(0..1), z=scope_abberation, w=lens_visibility(0..1)
	//                   lens output alpha = min(y,w) -> the lens fades in with aim and hides when not aiming
	// m_zoom_deviation: x,y=lens image offset (sway/recoil), z=brightness, w=jitter (0 = static centred lens)
	Fvector4						hud_scope_params;
	Fvector4						hud_zoom_deviation;

	// Gunslinger 3D PiP double-render: true on a "lens frame" -- the whole scene is rendered at the
	// magnified scope FOV with the first-person HUD suppressed, captured into $user$scope, and NOT
	// presented (the screen keeps the previous normal frame). Set each frame by ComputeLensFrame at
	// camera-apply; read by the HUD gate (CActor::OnHUDDraw), the scope capture (CRender::RenderScopeToRT)
	// and the present bridge (dxRenderDeviceRender::End).
	bool							m_bLensFrameNow;

	// True whenever the actor is aiming a lensed scope (BOTH the lens frames and the presented normal frames
	// in between). Drives the present bridge (CRender::PresentBridgeLens): while active, each presented normal
	// frame is saved to rt_scope_save and each lens frame presents that saved frame instead of its own zoomed
	// image -- so Present still fires every frame (DXGI flip needs it) and the screen never flashes the zoom.
	bool							m_bLensAimActive;

	// True once at least one presented NORMAL frame has been saved to rt_scope_save this aim session. A lens
	// frame is only taken (and its saved-frame restored) after this is set, so the FIRST aim frame is always a
	// normal frame -> the present bridge never restores a STALE save from a previous aim (a 1-frame camera pop).
	bool							m_bLensSaveValid;

public:
			void					destroy_particles	(const bool &all_particles);

public:
	virtual void					PreStart			(LPCSTR op);
	virtual void					Start				(LPCSTR op);
	virtual void					Disconnect			();
#ifndef _EDITOR
	IGame_ObjectPool				ObjectPool;
	CEnvironment*					pEnvironment;
	CEnvironment&					Environment()	{return *pEnvironment;};
#endif
	IMainMenu*						m_pMainMenu;	


	virtual bool					OnRenderPPUI_query	() { return FALSE; };	// should return true if we want to have second function called
	virtual void					OnRenderPPUI_main	() {};
	virtual void					OnRenderPPUI_PP		() {};

	virtual	void					OnAppStart			();
	virtual void					OnAppEnd			();
	virtual	void					OnAppActivate		();
	virtual void					OnAppDeactivate		();
	virtual void					OnFrame				();

	// ���������� ������ ����� ���������� ��� ����
	virtual	void					OnGameStart			(); 
	virtual void					OnGameEnd			();

	virtual void					UpdateGameType		() {};
	virtual void					GetCurrentDof		(Fvector3& dof){dof.set(-1.4f, 0.0f, 250.f);};
	virtual void					SetBaseDof			(const Fvector3& dof){};
	virtual void					OnSectorChanged		(int sector){};
	virtual void					OnAssetsChanged		();

	virtual void					RegisterModel		(IRenderVisual* V)
#ifndef _EDITOR
     = 0;
#else
	{}
#endif
	virtual	float					MtlTransparent		(u32 mtl_idx)
#ifndef _EDITOR
	= 0;
#else
	{return 1.f;}
#endif

	IGame_Persistent				();
	virtual ~IGame_Persistent		();

	ICF		u32						GameType			() {return m_game_params.m_e_game_type;};
	virtual void					Statistics			(CGameFont* F)
#ifndef _EDITOR
     = 0;
#else
	{}
#endif
	virtual	void					LoadTitle			(LPCSTR str){}
	virtual bool					CanBePaused			()		{ return true;}
	// Appended at the end of the class on purpose: keeps every existing vtable index unchanged so a
	// stock xrEngine.dll stays ABI-compatible. Called from the R3 forward phase (scene depth bound)
	// so world tracers depth-test against the HUD under MSAA. No-op outside a level.
	virtual void					OnRenderForward		() {};
	// 3D PDA: draw the PDA window (+cursor) into the backbuffer so the renderer can snapshot it into
	// "$user$ui", which the PDA hud model's screen material samples. Returns true if it actually drew
	// (= the PDA phantom is in hand and its window is open) -- the renderer's cue to take the shot.
	// Same shape and reason as OnRenderForward: the RENDERER calls the GAME. The other direction (a
	// new virtual on IRender_interface) does NOT work: that class is ENGINE_API, so outside xrEngine
	// it's dllimport and an inline body becomes an unresolved import at load time
	// ("entry point ?CaptureUIToRT@IRender_interface@@UAEXXZ not found in xrRender_R2.dll").
	virtual bool					OnRenderPdaUI		() { return false; };
	// 3D PiP scope: true while the actor aims through a scope whose lens needs the $user$scope snapshot.
	// The renderer asks the game (same bridge shape as OnRenderPdaUI/OnRenderForward -- render calls game).
	virtual bool					OnRenderScopeActive	() { return false; };

	// 3D PiP double-render: decide whether THIS frame is a lens frame (aiming a lensed scope + throttle) and,
	// if so, output the magnified scope FOV (degrees) to render the world at. Sets m_bLensFrameNow. The engine
	// calls this at camera-apply (CCameraManager::ApplyDevice) and overrides the scene FOV with out_fov.
	virtual bool					ComputeLensFrame	(float& out_fov) { out_fov = 0.f; return false; }
};

class IMainMenu
{
public:
	virtual			~IMainMenu						()													{};
	virtual void	Activate						(bool bActive)										=0; 
	virtual	bool	IsActive						()													=0; 
	virtual	bool	CanSkipSceneRendering			()													=0; 
	virtual void	DestroyInternal					(bool bForce)										=0;
};

extern ENGINE_API	bool g_dedicated_server;
extern ENGINE_API	IGame_Persistent*	g_pGamePersistent;
#endif //IGame_PersistentH

