#include "stdafx.h"

using namespace DirectX;

#include "../xrCDB/frustum.h"

#pragma warning(disable:4995)
// mmsystem.h
#define MMNOSOUND
#define MMNOMIDI
#define MMNOAUX
#define MMNOMIXER
#define MMNOJOY
#include <mmsystem.h>
#pragma warning(default:4995)

#include "x_ray.h"
#include "render.h"

// must be defined before include of FS_impl.h
#define INCLUDE_FROM_ENGINE
#include "../xrCore/FS_impl.h"

#ifdef INGAME_EDITOR
#	include "../include/editor/ide.hpp"
#	include "engine_impl.hpp"
#endif // #ifdef INGAME_EDITOR

#include "igame_persistent.h"
#include "borderless_display.h"
#include "../xrCore/hitch_trace.h"

ENGINE_API CRenderDevice Device;
ENGINE_API BOOL g_bRendering = FALSE; 

BOOL		g_bLoaded = FALSE;
ref_light	precache_light = 0;

BOOL CRenderDevice::Begin	()
{
#ifndef DEDICATED_SERVER

	/*
	HW.Validate		();
	HRESULT	_hr		= HW.pDevice->TestCooperativeLevel();
    if (FAILED(_hr))
	{
		// If the device was lost, do not render until we get it back
		if		(D3DERR_DEVICELOST==_hr)		{
			Sleep	(33);
			return	FALSE;
		}

		// Check if the device is ready to be reset
		if		(D3DERR_DEVICENOTRESET==_hr)
		{
			Reset	();
		}
	}
	*/

	switch (m_pRender->GetDeviceState())
	{
	case IRenderDeviceRender::dsOK:
		break;

	case IRenderDeviceRender::dsLost:
		// If the device was lost, do not render until we get it back
		Sleep(33);
		return FALSE;
		break;

	case IRenderDeviceRender::dsNeedReset:
		// Check if the device is ready to be reset
		Reset();
		break;

	default:
		R_ASSERT(0);
	}

	m_pRender->Begin();

	/*
	CHK_DX					(HW.pDevice->BeginScene());
	RCache.OnFrameBegin		();
	RCache.set_CullMode		(CULL_CW);
	RCache.set_CullMode		(CULL_CCW);
	if (HW.Caps.SceneMode)	overdrawBegin	();
	*/

	FPU::m24r	();
	g_bRendering = 	TRUE;
#endif
	return		TRUE;
}

void CRenderDevice::Clear	()
{
	m_pRender->Clear();
}

extern void CheckPrivilegySlowdown();

#include "xr_input.h"

// ---------------------------------------------------------------------------------------------
// "Press any key" at the end of a load (Call of Pripyat). The precache is held one frame short of
// finishing, so the loading screen -- shaders and all -- is still alive and simply keeps being
// drawn, and the game clock is paused meanwhile. Armed per load by PreCache(frames, true).
// ---------------------------------------------------------------------------------------------
ENGINE_API bool			g_bLoadWaitKey		= false;
ENGINE_API string256	g_sLoadWaitKeyText	= { 0 };
// Raised the moment a load ARMS the gate, i.e. several precache frames before g_bLoadWaitKey itself
// comes up on the last one. Game code needs the early warning: anything that keys off "the precache
// is nearly over" (the intro movie starts at dwPrecacheFrame<=2) would otherwise run behind the
// loading screen while the player has not pressed anything yet.
ENGINE_API bool			g_bLoadWaitKeyPending	= false;

static bool				s_wait_key_armed	= false;	// this load asked for the gate
static bool				s_wait_key_open		= false;	// the gate is up, snapshot below is valid
static u8				s_wait_key_down[256];			// keys already held when it opened
static u8				s_wait_btn_down[3];
static BOOL				s_wait_pause_str	= TRUE;		// bShowPauseString as it was before the gate

extern ENGINE_API BOOL	bShowPauseString;				// defined further down this file

// A key that was ALREADY down when the gate opened must not dismiss it -- the player is quite
// likely leaning on a movement key while the level comes up. Only a fresh press counts, and
// releasing a key arms it for the next press.
static bool				wait_key_pressed	()
{
	if (!pInput)	return true;			// no input device -> never hold the game hostage

	for (int dik = 1; dik < 256; ++dik)
	{
		if (!pInput->iGetAsyncKeyState(dik))	{ s_wait_key_down[dik] = 0; continue; }
		if (!s_wait_key_down[dik])				return true;
	}
	for (int btn = 0; btn < 3; ++btn)
	{
		if (!pInput->iGetAsyncBtnState(btn))	{ s_wait_btn_down[btn] = 0; continue; }
		if (!s_wait_btn_down[btn])				return true;
	}
	return false;
}

static void				wait_key_open		()
{
	ZeroMemory		(s_wait_key_down, sizeof(s_wait_key_down));
	ZeroMemory		(s_wait_btn_down, sizeof(s_wait_btn_down));
	if (pInput)
	{
		for (int dik = 1; dik < 256; ++dik)
			if (pInput->iGetAsyncKeyState(dik))	s_wait_key_down[dik] = 1;
		for (int btn = 0; btn < 3; ++btn)
			if (pInput->iGetAsyncBtnState(btn))	s_wait_btn_down[btn] = 1;
	}
	s_wait_key_open	= true;
	g_bLoadWaitKey	= true;
	// the world is live during a precache (the camera spins through it) -- freeze it, or the actor
	// stands there being shot at while the player reads the loading screen. Sound is left alone:
	// the master volume is already held at 0 for the whole precache.
	Device.Pause	(TRUE, TRUE, FALSE, "load_wait_key");
	// ...but without the "PAUSED" banner over the loading screen -- this is a prompt, not a pause the
	// player asked for. Same thing the main menu does while it holds the game (CMainMenu::Activate).
	s_wait_pause_str	= bShowPauseString;
	bShowPauseString	= FALSE;
}

static void				wait_key_close		()
{
	s_wait_key_armed	= false;
	s_wait_key_open		= false;
	g_bLoadWaitKey		= false;
	g_bLoadWaitKeyPending	= false;
	Device.Pause		(FALSE, TRUE, FALSE, "load_wait_key");
	bShowPauseString	= s_wait_pause_str;
}

void CRenderDevice::End		(void)
{
#ifndef DEDICATED_SERVER


#ifdef INGAME_EDITOR
	bool							load_finished = false;
#endif // #ifdef INGAME_EDITOR
	if (dwPrecacheFrame)
	{
		::Sound->set_master_volume	(0.f);

		// The gate sits on the LAST precache frame: everything is warmed and the level is ready,
		// but the counter never reaches 0, so the finish block below (which destroys the loading
		// shaders) does not run and the screen stays exactly as it is.
		bool	hold					= false;
		if (1==dwPrecacheFrame && s_wait_key_armed && !g_dedicated_server)
		{
			if (!s_wait_key_open)		wait_key_open	();
			hold					= !wait_key_pressed();
			if (!hold)					wait_key_close	();
		}

		if (!hold)					dwPrecacheFrame	--;
		pApp->load_draw_internal	();
		if (0==dwPrecacheFrame)
		{
			// Safety net: the precache is over, so nothing is waiting on a key any more -- even if
			// the gate never actually opened (dedicated server, a load with no precache frames).
			// Whatever the game held back for it must be released here or it never runs.
			g_bLoadWaitKeyPending	= false;

#ifdef INGAME_EDITOR
			load_finished			= true;
#endif // #ifdef INGAME_EDITOR
			//Gamma.Update		();
			m_pRender->updateGamma();

			if(precache_light) precache_light->set_active	(false);
			if(precache_light) precache_light.destroy		();
			::Sound->set_master_volume						(1.f);
			pApp->destroy_loading_shaders					();

			m_pRender->ResourcesDestroyNecessaryTextures	();
			Memory.mem_compact								();
			Msg												("* MEMORY USAGE: %d K",Memory.mem_usage()/1024);
			Msg												("* End of synchronization A[%d] R[%d]",b_is_Active, b_is_Ready);

#ifdef FIND_CHUNK_BENCHMARK_ENABLE
			g_find_chunk_counter.flush();
#endif // FIND_CHUNK_BENCHMARK_ENABLE

			CheckPrivilegySlowdown							();
			
			// A load that finishes while the player is looking at something else used to drop
			// straight into a pause -- the same "window is not in front" case the option covers,
			// so it obeys g_pause_on_minimize as well. With the option off the game simply resumes
			// running behind the other window, which is the whole point of turning it off.
			if(g_pGamePersistent->GameType()==1 && psDeviceFlags.test(rsPauseOnMinimize))//haCk
			{
				WINDOWINFO	wi;
				GetWindowInfo(m_hWnd,&wi);
				if(wi.dwWindowStatus!=WS_ACTIVECAPTION)
					Pause(TRUE,TRUE,TRUE,"application start");
			}
		}
	}

	g_bRendering		= FALSE;
	// end scene
	m_pRender->End();
	//RCache.OnFrameEnd	();
	//Memory.dbg_check		();
    //CHK_DX				(HW.pDevice->EndScene());

	//HRESULT _hr		= HW.pDevice->Present( NULL, NULL, NULL, NULL );
	//if				(D3DERR_DEVICELOST==_hr)	return;			// we will handle this later
	//R_ASSERT2		(SUCCEEDED(_hr),	"Presentation failed. Driver upgrade needed?");
#	ifdef INGAME_EDITOR
		if (load_finished && m_editor)
			m_editor->on_load_finished	();
#	endif // #ifdef INGAME_EDITOR
#endif
}


volatile u32	mt_Thread_marker		= 0x12345678;
void 			mt_Thread	(void *ptr)	{
	while (true) {
		// waiting for Device permission to execute
		Device.mt_csEnter.Enter	();

		if (Device.mt_bMustExit) {
			Device.mt_bMustExit = FALSE;				// Important!!!
			Device.mt_csEnter.Leave();					// Important!!!
			return;
		}
		// we has granted permission to execute
		mt_Thread_marker			= Device.dwFrame;
 
		{
			hitch::zone			hz("mt/parallel");
			for (u32 pit=0; pit<Device.seqParallel.size(); pit++)
				Device.seqParallel[pit]	();
			Device.seqParallel.clear();
		}
		{
			hitch::zone			hz("mt/seqFrameMT");
			Device.seqFrameMT.Process	(rp_Frame);
		}

		// now we give control to device - signals that we are ended our work
		Device.mt_csEnter.Leave	();
		// waits for device signal to continue - to start again
		Device.mt_csLeave.Enter	();
		// returns sync signal to device
		Device.mt_csLeave.Leave	();
	}
}

#include "igame_level.h"
void CRenderDevice::PreCache	(u32 amount, bool wait_user_input)
{
	if (m_pRender->GetForceGPU_REF()) amount=0;
#ifdef DEDICATED_SERVER
	amount = 0;
#endif
	// Msg			("* PCACHE: start for %d...",amount);
	// The gate needs a precache to hang off; with none (or none left) there is no loading screen
	// to hold. Arming is a plain flag, so the two PreCache calls a single load makes still buy
	// exactly one wait.
	if (amount && wait_user_input && !s_wait_key_open)	s_wait_key_armed = g_bLoadWaitKeyPending = true;
	dwPrecacheFrame	= dwPrecacheTotal = amount;
	if (amount && !precache_light && g_pGameLevel && g_loading_events.empty()) {
		precache_light					= ::Render->light_create();
		precache_light->set_shadow		(false);
		precache_light->set_position	(vCameraPosition);
		precache_light->set_color		(255,255,255);
		precache_light->set_range		(5.0f);
		precache_light->set_active		(true);
	}
}


int g_svDedicateServerUpdateReate = 100;

ENGINE_API xr_list<LOADING_EVENT>			g_loading_events;

void CRenderDevice::on_idle		()
{
	if (!b_is_Ready) {
		Sleep	(100);
		return;
	}

	u32 FrameStartTime = TimerGlobal.GetElapsed_ms();

	// first frame actually presented -- i.e. the moment the player sees something
	{
		static bool s_first_frame = true;
		if (s_first_frame)	{ s_first_frame = false; startup_stamp("first frame"); }
	}

	if (psDeviceFlags.test(rsStatistic))	g_bEnableStatGather	= TRUE;
	else									g_bEnableStatGather	= FALSE;
	if(g_loading_events.size())
	{
		if( g_loading_events.front()() )
			g_loading_events.pop_front();
		pApp->LoadDraw				();
		hitch_frame_end				(false);
		return;
	}else 
	{
		hitch::zone					hz("frame/move");
		FrameMove						( );
	}

	// Precache
	if (dwPrecacheFrame)
	{
		float factor					= float(dwPrecacheFrame)/float(dwPrecacheTotal);
		float angle						= PI_MUL_2 * factor;
		vCameraDirection.set			(_sin(angle),0,_cos(angle));	vCameraDirection.normalize	();
		vCameraTop.set					(0,1,0);
		vCameraRight.crossproduct		(vCameraTop,vCameraDirection);

		mView.build_camera_dir			(vCameraPosition,vCameraDirection,vCameraTop);
	}

	// Matrices
	mFullTransform.mul			( mProject,mView	);
	m_pRender->SetCacheXform(mView, mProject);
	//RCache.set_xform_view		( mView				);
	//RCache.set_xform_project	( mProject			);

	XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&mInvFullTransform), 
		XMMatrixInverse(nullptr, XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&mFullTransform))));

	vCameraPosition_saved = vCameraPosition;
	mFullTransform_saved = mFullTransform;

	// *** Resume threads
	// Capture end point - thread must run only ONE cycle
	// Release start point - allow thread to run
	mt_csLeave.Enter			();
	mt_csEnter.Leave			();
	{
		hitch::zone				hz("frame/sleep0");
		Sleep					(0);
	}

#ifndef DEDICATED_SERVER
	Statistic->RenderTOTAL_Real.FrameStart	();
	Statistic->RenderTOTAL_Real.Begin		();
	const BOOL b_render = may_render();
	if (b_render)							{
		if (Begin())				{

			{
				hitch::zone						hz("frame/render");
				seqRender.Process					(rp_Render);
			}
			if (psDeviceFlags.test(rsCameraPos) || psDeviceFlags.test(rsStatistic) || Statistic->errors.size())	
				Statistic->Show						();
			//	TEST!!!
			//Statistic->RenderTOTAL_Real.End			();
			//	Present goes here
			hitch::zone							hz("frame/end+present");
			End										();
		}
	}
	Statistic->RenderTOTAL_Real.End			();
	Statistic->RenderTOTAL_Real.FrameEnd	();
	Statistic->RenderTOTAL.accum	= Statistic->RenderTOTAL_Real.accum;
#endif // #ifndef DEDICATED_SERVER
	// *** Suspend threads
	// Capture startup point
	// Release end point - allow thread to wait for startup point
	{
		// The secondary thread runs A-Life and the parallel jobs; a long one shows here as a wait.
		hitch::zone							hz("frame/wait_secondary_thread");
		mt_csEnter.Enter					();
	}
	mt_csLeave.Leave						();

	// Ensure, that second thread gets chance to execute anyway
	if (dwFrame!=mt_Thread_marker)			{
		hitch::zone							hz("frame/secondary_jobs_on_main");
		for (u32 pit=0; pit<Device.seqParallel.size(); pit++)
			Device.seqParallel[pit]			();
		Device.seqParallel.clear();
		seqFrameMT.Process					(rp_Frame);
	}

#ifndef DEDICATED_SERVER
	if(!g_pGameLevel || g_pGamePersistent->m_pMainMenu->IsActive())
	#endif // DEDICATED_SERVER
	{
		u32 FrameEndTime = TimerGlobal.GetElapsed_ms();
		u32 FrameTime = (FrameEndTime - FrameStartTime);

		u32 DSUpdateDelta = 1000 / g_svDedicateServerUpdateReate;
		if(FrameTime < DSUpdateDelta) {
			Sleep(DSUpdateDelta - FrameTime - 1);
		}
	}

	UpdateFocusSoundFade			();

	if (!b_is_Active)
		Sleep		(1);

#ifndef DEDICATED_SERVER
	hitch_frame_end(g_pGameLevel && g_pGameLevel->bReady && !g_pGamePersistent->m_pMainMenu->IsActive()
		&& !Paused() && !dwPrecacheFrame && b_is_Active);
#else
	hitch_frame_end(false);
#endif
}

// Frame-to-frame, not on_idle alone: the window message pump between two frames counts as well.
void CRenderDevice::hitch_frame_end(bool report)
{
	static u64		s_last	= 0;
	const u64		now		= CPU::QPC();
	hitch::frame_end		(dwFrame, s_last ? now - s_last : 0, report);
	s_last					= now;
}

// The game kept playing at full volume behind another window: with "pause on minimise" off the whole
// game, with it on still the menu music. Fade the listener gain out while the window is inactive and
// back in when it returns. Real time, not game time: the fade must run the same whatever the time factor or pause.
// The level precache owns the same gain (it mutes the load and restores 1.0 at the end), so stay out
// of its way while it runs; the next frame after it picks the fade up from wherever it is.
void CRenderDevice::UpdateFocusSoundFade()
{
#ifndef DEDICATED_SERVER
	static float	s_gain		= 1.f;
	static u32		s_prev_ms	= 0;
	static bool		s_applied	= false;	// last value we set is s_gain (false: someone else set 1.0)

	// TimerMM, not TimerGlobal: TimerGlobal is a CTimer_paused and stops with the game -- the pause menu
	// froze the fade and its music kept playing behind another window.
	const u32	now_ms		= TimerMM.GetElapsed_ms();
	const float	dt			= s_prev_ms ? float(now_ms - s_prev_ms) * 0.001f : 0.f;
	s_prev_ms				= now_ms;

	// Regardless of "pause on minimise": with it on the game sounds pause, but the menu and pause-screen
	// music is exempt from the pause and kept playing behind another window.
	const bool	muted		= !b_is_Active;
	const float	target		= muted ? 0.f : 1.f;
	const float	fade_time	= muted ? 0.75f : 0.5f;		// seconds for a full swing
	const float	prev		= s_gain;
	if (s_gain < target)	s_gain = _min(target, s_gain + dt / fade_time);
	else if (s_gain > target) s_gain = _max(target, s_gain - dt / fade_time);

	if (dwPrecacheFrame)	{ s_applied = false; return; }
	if (!::Sound)			return;
	if (s_gain != prev || (!s_applied && s_gain < 1.f))
	{
		::Sound->set_master_volume	(s_gain);
		s_applied					= true;
	}
#endif
}

#ifdef INGAME_EDITOR
void CRenderDevice::message_loop_editor	()
{
	m_editor->run			();
	m_editor_finalize		(m_editor);
	xr_delete				(m_engine);
}
#endif // #ifdef INGAME_EDITOR

void CRenderDevice::message_loop()
{
#ifdef INGAME_EDITOR
	if (editor()) {
		message_loop_editor	();
		return;
	}
#endif // #ifdef INGAME_EDITOR

	MSG						msg;
    PeekMessage				(&msg, NULL, 0U, 0U, PM_NOREMOVE );
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage	(&msg);
			continue;
		}

		on_idle				();
    }
}

// Startup profiling. The engine logs its milestones but never a time, and xrCore buffers the log and
// flushes it in bulk (measured: all ~150 startup lines land in the file in ONE write), so no amount of
// watching the file can tell which startup stage is slow. These stamps are the only way to see it.
// Cheap enough to leave in: five Msg's per process launch.
static CTimer	g_startup_timer;
static bool		g_startup_timer_on	= false;
ENGINE_API void startup_stamp(LPCSTR stage)
{
	if (!g_startup_timer_on)	{ g_startup_timer.Start(); g_startup_timer_on = true; }
	Msg			("* startup [%s]: %u ms", stage, g_startup_timer.GetElapsed_ms());
}

void CRenderDevice::Run			()
{
//	DUMP_PHASE;
	g_bLoaded		= FALSE;
	Log				("Starting engine...");
	startup_stamp	("engine start");
	thread_name		("X-RAY Primary thread");

	// Startup timers and calculate timer delta
	dwTimeGlobal				= 0;
	Timer_MM_Delta				= 0;
	{
		u32 time_mm			= timeGetTime	();
		while (timeGetTime()==time_mm);			// wait for next tick
		u32 time_system		= timeGetTime	();
		u32 time_local		= TimerAsync	();
		Timer_MM_Delta		= time_system-time_local;
	}

	// Start all threads
//	InitializeCriticalSection	(&mt_csEnter);
//	InitializeCriticalSection	(&mt_csLeave);
	mt_csEnter.Enter			();
	mt_bMustExit				= FALSE;
	thread_spawn				(mt_Thread,"X-RAY Secondary thread",0,0);

	// Message cycle
	seqAppStart.Process			(rp_AppStart);
	startup_stamp				("app start done (main menu built)");

	//CHK_DX(HW.pDevice->Clear(0, 0, D3DCLEAR_TARGET, color_xrgb(0, 0, 0), 1, 0));
	m_pRender->ClearTarget		();

	message_loop				();

	seqAppEnd.Process		(rp_AppEnd);

	// Stop Balance-Thread
	mt_bMustExit			= TRUE;
	mt_csEnter.Leave		();
	while (mt_bMustExit)	Sleep(0);
//	DeleteCriticalSection	(&mt_csEnter);
//	DeleteCriticalSection	(&mt_csLeave);
}

void ProcessLoading(RP_FUNC *f);
void CRenderDevice::FrameMove()
{
	dwFrame			++;

	dwTimeContinual	= TimerMM.GetElapsed_ms	();
	if (psDeviceFlags.test(rsConstantFPS))	{
		// 20ms = 50fps
		//fTimeDelta		=	0.020f;			
		//fTimeGlobal		+=	0.020f;
		//dwTimeDelta		=	20;
		//dwTimeGlobal	+=	20;
		// 33ms = 30fps
		fTimeDelta		=	0.033f;			
		fTimeGlobal		+=	0.033f;
		dwTimeDelta		=	33;
		dwTimeGlobal	+=	33;
	} else {
		// Timer
		float fPreviousFrameTime = Timer.GetElapsed_sec(); Timer.Start();	// previous frame
		fTimeDelta = 0.1f * fTimeDelta + 0.9f*fPreviousFrameTime;			// smooth random system activity - worst case ~7% error
		//fTimeDelta = 0.7f * fTimeDelta + 0.3f*fPreviousFrameTime;			// smooth random system activity
		if (fTimeDelta>.1f)    fTimeDelta = .1f;							// limit to 15fps minimum
		if (fTimeDelta <= 0.f) fTimeDelta = EPS_S + EPS_S;					// limit to 15fps minimum
		if(Paused())	fTimeDelta = 0.0f;

//		u64	qTime		= TimerGlobal.GetElapsed_clk();
		fTimeGlobal		= TimerGlobal.GetElapsed_sec(); //float(qTime)*CPU::cycles2seconds;
		u32	_old_global	= dwTimeGlobal;
		dwTimeGlobal	= TimerGlobal.GetElapsed_ms	();	//u32((qTime*u64(1000))/CPU::cycles_per_second);
		dwTimeDelta		= dwTimeGlobal-_old_global;
	}

	// Frame move
	Statistic->EngineTOTAL.Begin	();

	//	TODO: HACK to test loading screen.
	//if(!g_bLoaded) 
		ProcessLoading				(rp_Frame);
	//else
	//	seqFrame.Process			(rp_Frame);
	Statistic->EngineTOTAL.End	();
}

void ProcessLoading				(RP_FUNC *f)
{
	// Device.seqFrame.Process(rp_Frame), with every subscriber timed under its class name for the
	// hitch tracer. Mirrors CRegistrator::Process exactly, including its early return.
	CRegistrator<pureFrame>& S			= Device.seqFrame;
	S.in_process						= true;
	if (!S.R.empty())
	{
		if (S.R[0].Prio==REG_PRIORITY_CAPTURE)	rp_Frame(S.R[0].Object);
		else
		{
			for (u32 i=0; i<S.R.size(); i++)
			{
				if (S.R[i].Prio==REG_PRIORITY_INVALID)	continue;
				pureFrame*		obj		= (pureFrame*)S.R[i].Object;
				hitch::zone		hz		(typeid(*obj).name());
				obj->OnFrame			();
			}
		}
		if (S.changed)	S.Resort		();
		S.in_process					= false;
	}
	g_bLoaded							= TRUE;
}

ENGINE_API BOOL bShowPauseString = TRUE;
#include "IGame_Persistent.h"

void CRenderDevice::Pause(BOOL bOn, BOOL bTimer, BOOL bSound, LPCSTR reason)
{
	static int snd_emitters_ = -1;

#ifdef DEBUG
//	Msg("pause [%s] timer=[%s] sound=[%s] reason=%s",bOn?"ON":"OFF", bTimer?"ON":"OFF", bSound?"ON":"OFF", reason);
#endif // DEBUG

#ifndef DEDICATED_SERVER	

	if(bOn)
	{
		if(!Paused())						
			bShowPauseString				= 
#ifdef INGAME_EDITOR
				editor() ? FALSE : 
#endif // #ifdef INGAME_EDITOR
				TRUE;

		if( bTimer && (!g_pGamePersistent || g_pGamePersistent->CanBePaused()) )
			g_pauseMngr.Pause				(TRUE);
	
		if (bSound && ::Sound) {
			snd_emitters_ =					::Sound->pause_emitters(true);
#ifdef DEBUG
//			Log("snd_emitters_[true]",snd_emitters_);
#endif // DEBUG
		}
	}else
	{
		if( bTimer && /*g_pGamePersistent->CanBePaused() &&*/ g_pauseMngr.Paused() )
		{
			fTimeDelta						= EPS_S + EPS_S;
			g_pauseMngr.Pause				(FALSE);
		}
		
		if(bSound)
		{
			if(snd_emitters_>0) //avoid crash
			{
				snd_emitters_ =				::Sound->pause_emitters(false);
#ifdef DEBUG
//				Log("snd_emitters_[false]",snd_emitters_);
#endif // DEBUG
			}else {
#ifdef DEBUG
				Log("Sound->pause_emitters underflow");
#endif // DEBUG
			}
		}
	}

#endif

}

BOOL CRenderDevice::Paused()
{
	return g_pauseMngr.Paused();
};

// Draw even when the window is not focused, unless the pause option is on -- otherwise the
// picture freezes on its last frame while the world keeps running behind it. A minimised window
// is always skipped: there is no client area to present into.
BOOL CRenderDevice::may_render() const
{
	return (b_is_Active || (!b_is_Minimized && !psDeviceFlags.test(rsPauseOnMinimize)));
}

// THE RECTANGLE THE PICTURE OCCUPIES, IN THE COORDINATES GetCursorPos REPORTS.
//
// Those are not back buffer pixels, and nothing guarantees they match them. In a window the
// picture is the client area, wherever it sits and whatever size it is (a back buffer larger than
// the desktop is stretched into it). In exclusive fullscreen it is the whole monitor -- and the
// monitor, as this process sees it, can be SMALLER than the mode: at a DSR / DLDSR resolution
// Windows may run that mode at a different scale, and a process that is not per-monitor aware
// is then handed scaled coordinates (3840 wide comes back as 2560). Dividing the pointer by the
// back buffer width, as CUICursor did, stopped the cursor two thirds of the way across.
// Both the confinement below and the UI cursor take their rectangle from here.
bool pointer_screen_rect(RECT& r)
{
	HWND hWnd			= Device.m_hWnd;
	if (!hWnd)			return false;

	bool ok				= false;
	if (psDeviceFlags.is(rsFullscreen))
	{
		MONITORINFO		mi;
		mi.cbSize		= sizeof(mi);
		if (GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi))
		{
			r			= mi.rcMonitor;
			ok			= true;
		}
	}
	if (!ok)
	{
		RECT			rc;
		if (!GetClientRect(hWnd, &rc))	return false;
		POINT			tl = { rc.left,  rc.top    };
		POINT			br = { rc.right, rc.bottom };
		ClientToScreen	(hWnd, &tl);
		ClientToScreen	(hWnd, &br);
		SetRect			(&r, tl.x, tl.y, br.x, br.y);
	}
	if ((r.right <= r.left) || (r.bottom <= r.top))	return false;

	// one line whenever it changes: the first thing to look at if the cursor and the picture
	// disagree again
	static RECT			s_last	= { 0, 0, 0, 0 };
	static u32			s_w		= 0, s_h = 0;
	if (!EqualRect(&s_last, &r) || (s_w != Device.dwWidth) || (s_h != Device.dwHeight))
	{
		s_last			= r;
		s_w				= Device.dwWidth;
		s_h				= Device.dwHeight;
		Msg				("* pointer space: %dx%d at (%d,%d), back buffer %dx%d, %s",
						r.right - r.left, r.bottom - r.top, r.left, r.top, s_w, s_h,
						psDeviceFlags.is(rsFullscreen) ? "fullscreen" : "window");
	}
	return				true;
}

// Pointer confinement. The exclusive DirectInput mouse used to do this implicitly; in a window
// it is no longer exclusive (that is what stopped Windows eating the first click after alt-tab),
// so without a clip the pointer walks off onto a second monitor mid-game. Recomputed from the
// current client rect every time, because the window it was first computed for -- the small
// default one at startup -- is not the window that ends up on screen.
void CRenderDevice::UpdateCursorClip()
{
#ifndef DEDICATED_SERVER
	if (!b_is_Active || b_is_Minimized || !m_hWnd)	{ ClipCursor(NULL); return; }
	RECT scr;
	if (!pointer_screen_rect(scr))					{ ClipCursor(NULL); return; }
	ClipCursor(&scr);
#endif
}

// A BORDERLESS WINDOW AT A RESOLUTION THE DESKTOP DOES NOT HAVE.
//
// That is a DSR / DLDSR mode: the driver lists it like any other, but it is larger than the panel.
// A window of that size simply hangs off the screen, and the driver's downscale -- the whole point
// of DLDSR -- only runs while the DESKTOP is in that mode. So for as long as the game has the focus
// the desktop is switched to it (CDS_FULLSCREEN: temporary, never written to the registry, undone
// by Windows itself if the process dies) and it is given back on alt-tab and on exit. There is
// still no exclusive mode; the device stays windowed throughout.
// A resolution that fits the desktop is left exactly as it was: a window of that size, centred,
// and the desktop untouched.
// If the switch is refused, the window is fitted to the desktop instead and the present stretches
// the picture into it -- plain downsampling, but never a window larger than the screen.
static bool	s_display_switched	= false;

static bool desktop_mode(DWORD which, DEVMODE& dm)
{
	ZeroMemory			(&dm, sizeof(dm));
	dm.dmSize			= sizeof(dm);
	return				!!EnumDisplaySettings(NULL, which, &dm);
}

static bool borderless_wanted()
{
	return				!g_dedicated_server && !psDeviceFlags.is(rsFullscreen) && psDeviceFlags.test(rsBorderless);
}

// Larger than the desktop the user actually runs -- which, once it has been switched, is the
// registry mode, the one ChangeDisplaySettingsEx(NULL) goes back to.
static bool exceeds_desktop(u32 w, u32 h)
{
	DEVMODE				dm;
	if (!desktop_mode(s_display_switched ? ENUM_REGISTRY_SETTINGS : ENUM_CURRENT_SETTINGS, dm))
		return			false;
	return				(w > dm.dmPelsWidth) || (h > dm.dmPelsHeight);
}

void borderless_restore_display()
{
	if (!s_display_switched)	return;
	s_display_switched	= false;
	ChangeDisplaySettingsEx	(NULL, NULL, NULL, 0, NULL);
}

static bool switch_display(u32 w, u32 h)
{
	DEVMODE				cur;
	const bool			have_cur = desktop_mode(ENUM_CURRENT_SETTINGS, cur);
	if (have_cur && (cur.dmPelsWidth == w) && (cur.dmPelsHeight == h))
		return			true;			// already there

	DEVMODE				dm;
	ZeroMemory			(&dm, sizeof(dm));
	dm.dmSize			= sizeof(dm);
	dm.dmPelsWidth		= w;
	dm.dmPelsHeight		= h;
	dm.dmFields			= DM_PELSWIDTH | DM_PELSHEIGHT;
	// Keep the refresh rate: left out, Windows is free to pick the mode's default and a 144 Hz
	// desktop comes back as 60. Only if the new mode has that rate at all.
	if (have_cur && (cur.dmDisplayFrequency > 1))
	{
		dm.dmDisplayFrequency	= cur.dmDisplayFrequency;
		dm.dmFields				|= DM_DISPLAYFREQUENCY;
		if (DISP_CHANGE_SUCCESSFUL != ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_FULLSCREEN | CDS_TEST, NULL))
			dm.dmFields			&= ~DM_DISPLAYFREQUENCY;
	}

	const LONG			r = ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_FULLSCREEN, NULL);
	if (DISP_CHANGE_SUCCESSFUL != r)
	{
		Msg				("! borderless: the desktop refused %dx%d (%d) -- fitting the window to it instead", w, h, r);
		return			false;
	}
	Msg					("* borderless: desktop switched to %dx%d", w, h);
	s_display_switched	= true;
	return				true;
}

static void sync_display(u32 w, u32 h, bool active)
{
	// ChangeDisplaySettingsEx sends messages to our own window before it returns
	static bool			busy = false;
	if (busy)			return;
	busy				= true;
	if (borderless_wanted() && active && exceeds_desktop(w, h))
		switch_display				(w, h);
	else
		borderless_restore_display	();
	busy				= false;
}

static bool window_has_focus(HWND hWnd)
{
	return				Device.b_is_Active || (GetForegroundWindow() == hWnd);
}

void borderless_sync_display(HWND hWnd)
{
	sync_display		(psCurrentVidMode[0], psCurrentVidMode[1], window_has_focus(hWnd));
}

// Centred on the primary screen and never larger than it, the picture's proportions kept.
static void place_window(HWND hWnd, u32 w, u32 h, UINT flags)
{
	RECT				desktop;
	GetClientRect		(GetDesktopWindow(), &desktop);
	const int			dw = desktop.right;
	const int			dh = desktop.bottom;
	int					ww = int(w);
	int					wh = int(h);
	if ((dw > 0) && (dh > 0) && ((ww > dw) || (wh > dh)))
	{
		const float		k = _min(float(dw) / float(ww), float(dh) / float(wh));
		ww				= _min(dw, iFloor(float(ww) * k + .5f));
		wh				= _min(dh, iFloor(float(wh) * k + .5f));
	}
	SetWindowPos		(hWnd, HWND_NOTOPMOST, (dw - ww) / 2, (dh - wh) / 2, ww, wh, flags);
}

void borderless_place_window(HWND hWnd, u32 w, u32 h)
{
	sync_display		(w, h, window_has_focus(hWnd));
	place_window		(hWnd, w, h, SWP_SHOWWINDOW | SWP_NOCOPYBITS | SWP_FRAMECHANGED);
}

// Focus gained or lost. An ordinary borderless window -- one that fits the desktop -- is not
// touched here at all; only the oversized one takes the desktop with it and hands it back.
static void borderless_on_activation(HWND hWnd, bool active)
{
	if (!Device.b_is_Ready || !hWnd || !borderless_wanted())	return;
	const u32			w = Device.dwWidth;
	const u32			h = Device.dwHeight;
	if (active ? !exceeds_desktop(w, h) : !s_display_switched)	return;

	sync_display		(w, h, active);
	if (!IsIconic(hWnd))
		place_window	(hWnd, w, h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
}

void CRenderDevice::OnWM_Activate(WPARAM wParam, LPARAM lParam)
{
	u16 fActive						= LOWORD(wParam);
	BOOL fMinimized					= (BOOL) HIWORD(wParam);
	// ShowCursor keeps a COUNTER, not a flag: every FALSE decrements and every TRUE increments,
	// and the pointer is drawn only while it is >= 0. Stepping it once per activate/deactivate
	// assumes those arrive in exact pairs; alt-tabbing out of a borderless window does not
	// oblige, and once the count has drifted to -2 a single TRUE leaves it at -1 -- no cursor
	// until something else happens to nudge it. Drive it to the state we want instead.
	struct win_cursor { static void show(bool bShow) {
		int c;
		if (bShow)	{ do { c = ShowCursor(TRUE);  } while (c <  0); }
		else		{ do { c = ShowCursor(FALSE); } while (c >= 0); }
	} };

	Device.b_is_Minimized			= fMinimized;
	BOOL bActive					= ((fActive!=WA_INACTIVE) && (!fMinimized))?TRUE:FALSE;
	
	if (bActive!=Device.b_is_Active)
	{
		Device.b_is_Active			= bActive;

		if (Device.b_is_Active)	
		{
			Device.seqAppActivate.Process(rp_AppActivate);
#ifndef DEDICATED_SERVER
#	ifdef INGAME_EDITOR
			if (!editor())
#	endif // #ifdef INGAME_EDITOR
				win_cursor::show	(false);
#endif // #ifndef DEDICATED_SERVER
			borderless_on_activation(m_hWnd, true);
			Device.UpdateCursorClip	();
		}else	
		{
			Device.seqAppDeactivate.Process(rp_AppDeactivate);
			borderless_on_activation(m_hWnd, false);
			ClipCursor				(NULL);
			win_cursor::show		(true);
		}
	}
}
