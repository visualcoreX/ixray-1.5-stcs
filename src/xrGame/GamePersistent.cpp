#include "pch_script.h"
#include "gamepersistent.h"
#include "../xrEngine/fmesh.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/gamemtllib.h"
#include "../Include/xrRender/Kinematics.h"
#include "profiler.h"
#include "MainMenu.h"
#include "UICursor.h"
#include "ui/UIBtnHint.h"
#include "ui/UIPdaWnd.h"
#include "UI.h"
#include "HUDManager.h"
extern bool g_pda_rt_pass;
extern bool gwr_pda_screen_active();
#include "game_base_space.h"
#include "level.h"
#include "Level_Bullet_Manager.h"
#include "ParticlesObject.h"
#include "actor.h"
#include "Weapon.h"
#include "inventory.h"
#include "game_base_space.h"
#include "stalker_animation_data_storage.h"
#include "stalker_velocity_holder.h"
#include "IXRayGameConstants.h"

#include "ActorEffector.h"
#include "actor.h"
#include "spectator.h"
#include "string_table.h"
#include "Actor_Flags.h"
#include "discord_rpc.h"

// Call of Pripyat's "press any key when the level is ready" gate. The engine holds the last
// precache frame (CRenderDevice::End) when PreCache was told to; this decides whether it should be
// and hands over the already-translated hint, because the string table lives on this side of the
// DLL boundary. Single player only -- in MP everyone else is already in the level and waiting.
bool arm_load_keypress_gate()
{
	if (!psActorFlags.test(AF_KEYPRESS_ON_START))					return false;
	if (!g_pGamePersistent || g_pGamePersistent->GameType() != 1)	return false;	// 1 = eGameIDSingle

	xr_strcpy	(g_sLoadWaitKeyText, *CStringTable().translate("ui_st_press_any_key"));
	return		true;
}

#ifndef MASTER_GOLD
#	include "custommonster.h"
#endif // MASTER_GOLD

#ifndef _EDITOR
#	include "ai_debug.h"
#endif // _EDITOR

#ifdef DEBUG_MEMORY_MANAGER
	static	void *	ode_alloc	(size_t size)								{ return Memory.mem_alloc(size,"ODE");			}
	static	void *	ode_realloc	(void *ptr, size_t oldsize, size_t newsize)	{ return Memory.mem_realloc(ptr,newsize,"ODE");	}
	static	void	ode_free	(void *ptr, size_t size)					{ return xr_free(ptr);							}
#else // DEBUG_MEMORY_MANAGER
	static	void *	ode_alloc	(size_t size)								{ return xr_malloc(size);			}
	static	void *	ode_realloc	(void *ptr, size_t oldsize, size_t newsize)	{ return xr_realloc(ptr,newsize);	}
	static	void	ode_free	(void *ptr, size_t size)					{ return xr_free(ptr);				}
#endif // DEBUG_MEMORY_MANAGER

CGamePersistent::CGamePersistent(void)
{
	m_bPickableDOF				= false;
	m_dof_speed					= 5.f;		// = the vanilla 0.2s until the first transition sets its own
	m_dof_changed				= false;
	m_game_params.m_e_game_type	= eGameIDNoGame;
	ambient_effect_next_time	= 0;
	ambient_effect_stop_time	= 0;
	ambient_particles			= 0;

	ambient_effect_wind_start	= 0.f;
	ambient_effect_wind_in_time	= 0.f;
	ambient_effect_wind_end		= 0.f;
	ambient_effect_wind_out_time= 0.f;
	ambient_effect_wind_on		= false;

	ZeroMemory					(ambient_sound_next_time, sizeof(ambient_sound_next_time));
	

	m_pUI_core					= NULL;
	m_pMainMenu					= NULL;
	m_intro						= NULL;
	m_intro_event.bind			(this,&CGamePersistent::start_logo_intro);
#ifdef DEBUG
	m_frame_counter				= 0;
	m_last_stats_frame			= u32(-2);
#endif
	// 
	dSetAllocHandler			(ode_alloc		);
	dSetReallocHandler			(ode_realloc	);
	dSetFreeHandler				(ode_free		);

	// 
	BOOL	bDemoMode	= (0!=strstr(Core.Params,"-demomode "));
	if (bDemoMode)
	{
		string256	fname;
		LPCSTR		name	=	strstr(Core.Params,"-demomode ") + 10;
		sscanf				(name,"%s",fname);
		R_ASSERT2			(fname[0],"Missing filename for 'demomode'");
		Msg					("- playing in demo mode '%s'",fname);
		pDemoFile			=	FS.r_open	(fname);
		Device.seqFrame.Add	(this);
		eDemoStart			=	Engine.Event.Handler_Attach("GAME:demo",this);	
		uTime2Change		=	0;
	} else {
		pDemoFile			=	NULL;
		eDemoStart			=	NULL;
	}

	eQuickLoad				= Engine.Event.Handler_Attach("Game:QuickLoad",this);
	Fvector3* DofValue		= Console->GetFVectorPtr("r2_dof");
	SetBaseDof				(*DofValue);
}

CGamePersistent::~CGamePersistent(void)
{	
	FS.r_close					(pDemoFile);
	Device.seqFrame.Remove		(this);
	Engine.Event.Handler_Detach	(eDemoStart,this);
	Engine.Event.Handler_Detach	(eQuickLoad,this);
}

void CGamePersistent::RegisterModel(IRenderVisual* V)
{
	// Check types
	switch (V->getType()){
	case MT_SKELETON_ANIM:
	case MT_SKELETON_RIGID:{
		u16 def_idx		= GMLib.GetMaterialIdx("default_object");
		R_ASSERT2		(GMLib.GetMaterialByIdx(def_idx)->Flags.is(SGameMtl::flDynamic),"'default_object' - must be dynamic");
		IKinematics* K	= smart_cast<IKinematics*>(V); VERIFY(K);
		int cnt = K->LL_BoneCount();
		for (u16 k=0; k<cnt; k++){
			CBoneData& bd	= K->LL_GetData(k); 
			if (*(bd.game_mtl_name)){
				bd.game_mtl_idx	= GMLib.GetMaterialIdx(*bd.game_mtl_name);
				R_ASSERT2(GMLib.GetMaterialByIdx(bd.game_mtl_idx)->Flags.is(SGameMtl::flDynamic),"Required dynamic game material");
			}else{
				bd.game_mtl_idx	= def_idx;
			}
		}
	}break;
	}
}

extern void clean_game_globals	();
extern void init_game_globals	();

void CGamePersistent::OnAppStart()
{
	// load game materials
	GMLib.Load					();
	init_game_globals			();
	__super::OnAppStart			();
	m_pUI_core					= xr_new<ui_core>();
	m_pMainMenu					= xr_new<CMainMenu>();
}


void CGamePersistent::OnAppEnd	()
{
	// let Discord drop the presence while the process is still healthy
	DiscordRPC().Shutdown		();

	if(m_pMainMenu->IsActive())
		m_pMainMenu->Activate(false);

	xr_delete					(m_pMainMenu);
	xr_delete					(m_pUI_core);

	__super::OnAppEnd			();

	clean_game_globals			();

	GMLib.Unload				();

}

void CGamePersistent::Start		(LPCSTR op)
{
	__super::Start				(op);
	m_intro_event.bind			(this,&CGamePersistent::start_game_intro);
}

void CGamePersistent::Disconnect()
{
	// destroy ambient particles
	CParticlesObject::Destroy(ambient_particles);

	__super::Disconnect			();
	// stop all played emitters
	::Sound->stop_emitters		();
	m_game_params.m_e_game_type	= eGameIDNoGame;
}

#include "xr_level_controller.h"

void CGamePersistent::OnGameStart()
{
	__super::OnGameStart		();
	
	UpdateGameType				();
	GameConstants::LoadConstants();

	// exit_Zone never runs if the zone goes offline (or the level changes) with the actor still
	// inside it, and a stuck m_bPickableDOF blocks every DOF request for the rest of the session
	m_bPickableDOF				= false;
	m_dof_changed				= false;
}

LPCSTR GameTypeToString(EGameIDs gt, bool bShort)
{
	switch(gt)
	{
	case eGameIDSingle:
		return "single";
		break;
	case eGameIDDeathmatch:
		return (bShort)?"dm":"deathmatch";
		break;
	case eGameIDTeamDeathmatch:
		return (bShort)?"tdm":"teamdeathmatch";
		break;
	case eGameIDArtefactHunt:
		return (bShort)?"ah":"artefacthunt";
		break;
	case eGameIDCaptureTheArtefact:
		return (bShort)?"cta":"capturetheartefact";
		break;
	case eGameIDDominationZone:
		return (bShort)?"dz":"dominationzone";
		break;
	case eGameIDTeamDominationZone:
		return (bShort)?"tdz":"teamdominationzone";
		break;
	default :
//		R_ASSERT	(0);
		return		"---";
	}
}

EGameIDs ParseStringToGameType(LPCSTR str)
{
	if (!xr_strcmp(str, "single")) 
		return eGameIDSingle;
	else
		if (!xr_strcmp(str, "deathmatch") || !xr_strcmp(str, "dm")) 
			return eGameIDDeathmatch;
		else
			if (!xr_strcmp(str, "teamdeathmatch") || !xr_strcmp(str, "tdm")) 
				return eGameIDTeamDeathmatch;
			else
				if (!xr_strcmp(str, "artefacthunt") || !xr_strcmp(str, "ah")) 
					return eGameIDArtefactHunt;
				else
					if (!xr_strcmp(str, "capturetheartefact") || !xr_strcmp(str, "cta")) 
						return eGameIDCaptureTheArtefact;
					else
						if (!xr_strcmp(str, "dominationzone")) 
							return eGameIDDominationZone;
						else
							if (!xr_strcmp(str, "teamdominationzone")) 
								return eGameIDTeamDominationZone;
							else 
								return eGameIDNoGame; //EGameIDs
}

void CGamePersistent::UpdateGameType			()
{
	__super::UpdateGameType		();

	m_game_params.m_e_game_type = ParseStringToGameType(m_game_params.m_game_type);


	if (m_game_params.m_e_game_type == eGameIDSingle)
		g_current_keygroup = _sp;
	else
		g_current_keygroup = _mp;
}

void CGamePersistent::OnGameEnd	()
{
	__super::OnGameEnd					();

	xr_delete							(g_stalker_animation_data_storage);
	xr_delete							(g_stalker_velocity_holder);
}

void CGamePersistent::WeathersUpdate()
{
	if (g_pGameLevel && !g_dedicated_server)
	{
		CActor* actor				= smart_cast<CActor*>(Level().CurrentViewEntity());
		BOOL bIndoor				= TRUE;
		if (actor) bIndoor			= actor->renderable_ROS()->get_luminocity_hemi()<0.05f;

		int data_set				= (Random.randF()<(1.f-Environment().CurrentEnv->weight))?0:1; 
		
		CEnvDescriptor* const current_env	= Environment().Current[0]; 
		VERIFY						(current_env);

		CEnvDescriptor* const _env	= Environment().Current[data_set]; 
		VERIFY						(_env);

		CEnvAmbient* env_amb		= _env->env_ambient;
		if (env_amb) {
			CEnvAmbient::SSndChannelVec& vec	= current_env->env_ambient->get_snd_channels();
			CEnvAmbient::SSndChannelVecIt I		= vec.begin();
			CEnvAmbient::SSndChannelVecIt E		= vec.end();
			
			for (u32 idx=0; I!=E; ++I,++idx) {
				CEnvAmbient::SSndChannel& ch	= **I;
				R_ASSERT						(idx<20);
				if(ambient_sound_next_time[idx]==0)//first
				{
					ambient_sound_next_time[idx] = Device.dwTimeGlobal + ch.get_rnd_sound_first_time();
				}else
				if(Device.dwTimeGlobal > ambient_sound_next_time[idx])
				{
					ref_sound& snd					= ch.get_rnd_sound();

					Fvector	pos;
					float	angle		= ::Random.randF(PI_MUL_2);
					pos.x				= _cos(angle);
					pos.y				= 0;
					pos.z				= _sin(angle);
					pos.normalize		().mul(ch.get_rnd_sound_dist()).add(Device.vCameraPosition);
					pos.y				+= 10.f;
					snd.play_at_pos		(0,pos);

#ifdef DEBUG
					if (!snd._handle() && strstr(Core.Params,"-nosound"))
						continue;
#endif // DEBUG

					VERIFY							(snd._handle());
					u32 _length_ms					= iFloor(snd.get_length_sec()*1000.0f);
					ambient_sound_next_time[idx]	= Device.dwTimeGlobal + _length_ms + ch.get_rnd_sound_time();
//					Msg("- Playing ambient sound channel [%s] file[%s]",ch.m_load_section.c_str(),snd._handle()->file_name());
				}
			}
/*
			if (Device.dwTimeGlobal > ambient_sound_next_time)
			{
				ref_sound* snd			= env_amb->get_rnd_sound();
				ambient_sound_next_time	= Device.dwTimeGlobal + env_amb->get_rnd_sound_time();
				if (snd)
				{
					Fvector	pos;
					float	angle		= ::Random.randF(PI_MUL_2);
					pos.x				= _cos(angle);
					pos.y				= 0;
					pos.z				= _sin(angle);
					pos.normalize		().mul(env_amb->get_rnd_sound_dist()).add(Device.vCameraPosition);
					pos.y				+= 10.f;
					snd->play_at_pos	(0,pos);
				}
			}
*/
			// start effect
			if ((FALSE==bIndoor) && (0==ambient_particles) && Device.dwTimeGlobal>ambient_effect_next_time){
				CEnvAmbient::SEffect* eff			= env_amb->get_rnd_effect(); 
				if (eff){
					Environment().wind_gust_factor	= eff->wind_gust_factor;
					ambient_effect_next_time		= Device.dwTimeGlobal + env_amb->get_rnd_effect_time();
					ambient_effect_stop_time		= Device.dwTimeGlobal + eff->life_time;
					ambient_effect_wind_start		= Device.fTimeGlobal;
					ambient_effect_wind_in_time		= Device.fTimeGlobal + eff->wind_blast_in_time;
					ambient_effect_wind_end			= Device.fTimeGlobal + eff->life_time/1000.f;
					ambient_effect_wind_out_time	= Device.fTimeGlobal + eff->life_time/1000.f + eff->wind_blast_out_time;
					ambient_effect_wind_on			= true;
										
					ambient_particles				= CParticlesObject::Create(eff->particles.c_str(),FALSE,false);
					Fvector pos; pos.add			(Device.vCameraPosition,eff->offset); 
					ambient_particles->play_at_pos	(pos);
					if (eff->sound._handle())		eff->sound.play_at_pos(0,pos);


					Environment().wind_blast_strength_start_value=Environment().wind_strength_factor;
					Environment().wind_blast_strength_stop_value=eff->wind_blast_strength;

					if (Environment().wind_blast_strength_start_value==0.f)
					{
						Environment().wind_blast_start_time.set(0.f,eff->wind_blast_direction.x,eff->wind_blast_direction.y,eff->wind_blast_direction.z);
					}
					else
					{
						Environment().wind_blast_start_time.set(0.f,Environment().wind_blast_direction.x,Environment().wind_blast_direction.y,Environment().wind_blast_direction.z);
					}
					Environment().wind_blast_stop_time.set(0.f,eff->wind_blast_direction.x,eff->wind_blast_direction.y,eff->wind_blast_direction.z);
				}
			}
		}
		if (Device.fTimeGlobal>=ambient_effect_wind_start && Device.fTimeGlobal<=ambient_effect_wind_in_time && ambient_effect_wind_on)
		{
			float delta=ambient_effect_wind_in_time-ambient_effect_wind_start;
			float t;
			if (delta!=0.f)
			{
				float cur_in=Device.fTimeGlobal-ambient_effect_wind_start;
				t=cur_in/delta;
			}
			else
			{
				t=0.f;
			}
			Environment().wind_blast_current.slerp(Environment().wind_blast_start_time,Environment().wind_blast_stop_time,t);

			Environment().wind_blast_direction.set(Environment().wind_blast_current.x,Environment().wind_blast_current.y,Environment().wind_blast_current.z);
			Environment().wind_strength_factor=Environment().wind_blast_strength_start_value+t*(Environment().wind_blast_strength_stop_value-Environment().wind_blast_strength_start_value);
		}

		// stop if time exceed or indoor
		if (bIndoor || Device.dwTimeGlobal>=ambient_effect_stop_time){
			if (ambient_particles)					ambient_particles->Stop();
			
			Environment().wind_gust_factor		= 0.f;
			
		}

		if (Device.fTimeGlobal>=ambient_effect_wind_end && ambient_effect_wind_on)
		{
			Environment().wind_blast_strength_start_value=Environment().wind_strength_factor;
			Environment().wind_blast_strength_stop_value	=0.f;

			ambient_effect_wind_on=false;
		}

		if (Device.fTimeGlobal>=ambient_effect_wind_end &&  Device.fTimeGlobal<=ambient_effect_wind_out_time)
		{
			float delta=ambient_effect_wind_out_time-ambient_effect_wind_end;
			float t;
			if (delta!=0.f)
			{
				float cur_in=Device.fTimeGlobal-ambient_effect_wind_end;
				t=cur_in/delta;
			}
			else
			{
				t=0.f;
			}
			Environment().wind_strength_factor=Environment().wind_blast_strength_start_value+t*(Environment().wind_blast_strength_stop_value-Environment().wind_blast_strength_start_value);
		}
		if (Device.fTimeGlobal>ambient_effect_wind_out_time && ambient_effect_wind_out_time!=0.f )
		{			
			Environment().wind_strength_factor=0.0;
		}

		// if particles not playing - destroy
		if (ambient_particles&&!ambient_particles->IsPlaying())
			CParticlesObject::Destroy(ambient_particles);
	}
}

#include "UI/UIGameTutorial.h"

void CGamePersistent::start_logo_intro		()
{
	if (strstr(Core.Params,"-nointro"))
	{
		m_intro_event			= 0;
		Console->Show			();
		Console->Execute		("main_menu on");
		return;
	}
	if (Device.dwPrecacheFrame==0)
	{
		m_intro_event.bind		(this,&CGamePersistent::update_logo_intro);
		if (!g_dedicated_server && 0==xr_strlen(m_game_params.m_game_or_spawn) && NULL==g_pGameLevel)
		{
			VERIFY				(NULL==m_intro);
			m_intro				= xr_new<CUISequencer>();
			m_intro->Start		("intro_logo");
			Console->Hide		();
		}
	}
}
void CGamePersistent::update_logo_intro			()
{
	if(m_intro && (false==m_intro->IsActive())){
		m_intro_event			= 0;
		xr_delete				(m_intro);
		Console->Execute		("main_menu on");
	}
}

void CGamePersistent::start_game_intro		()
{
	if (g_pGameLevel && g_pGameLevel->bReady && Device.dwPrecacheFrame<=2){
		m_intro_event.bind		(this,&CGamePersistent::update_game_intro);
		if (0==_stricmp(m_game_params.m_new_or_load,"new")){
			VERIFY				(NULL==m_intro);
			m_intro				= xr_new<CUISequencer>();
			m_intro->Start		("intro_game");
#ifdef DEBUG
			Log("Intro start",Device.dwFrame);
#endif // #ifdef DEBUG
		}
	}
}
void CGamePersistent::update_game_intro			()
{
	if(m_intro && (false==m_intro->IsActive())){
		xr_delete				(m_intro);
		m_intro_event			= 0;
	}
}
#include "holder_custom.h"
extern CUISequencer * g_tutorial;
extern CUISequencer * g_tutorial2;

void CGamePersistent::OnFrame	()
{
	if(g_tutorial2){ 
		g_tutorial2->Destroy	();
		xr_delete				(g_tutorial2);
	}

	if(g_tutorial && !g_tutorial->IsActive()){
		xr_delete(g_tutorial);
	}

#ifdef DEBUG
	++m_frame_counter;
#endif
	if (!g_dedicated_server && !m_intro_event.empty())	m_intro_event();

	if( !m_pMainMenu->IsActive() )
		m_pMainMenu->DestroyInternal(false);

	// Above the "no level" return on purpose: sitting in the main menu is a state worth showing.
	DiscordRPC().OnFrame		();

	if(!g_pGameLevel)			return;
	if(!g_pGameLevel->bReady)	return;

	if(Device.Paused())
	{
		if (Level().IsDemoPlay())
		{
			CSpectator* tmp_spectr = smart_cast<CSpectator*>(Level().CurrentControlEntity());
			if (tmp_spectr)
			{
				tmp_spectr->UpdateCL();	//updating spectator in pause (pause ability of demo play)
			}
		}
#ifndef MASTER_GOLD
		if (Level().CurrentViewEntity() && IsGameTypeSingle()) {
			if (!g_actor || (g_actor->ID() != Level().CurrentViewEntity()->ID())) {
				CCustomMonster	*custom_monster = smart_cast<CCustomMonster*>(Level().CurrentViewEntity());
				if (custom_monster) // can be spectator in multiplayer
					custom_monster->UpdateCamera();
			}
			else 
			{
				CCameraBase* C = NULL;
				if (g_actor)
				{
					if(!Actor()->Holder())
						C = Actor()->cam_Active();
					else
						C = Actor()->Holder()->Camera();

				Actor()->Cameras().UpdateFromCamera		(C);
				Actor()->Cameras().ApplyDevice			(VIEWPORT_NEAR);
				}
			}
		}
#else // MASTER_GOLD
		if (g_actor && IsGameTypeSingle())
		{
			CCameraBase* C = NULL;
			if(!Actor()->Holder())
				C = Actor()->cam_Active();
			else
				C = Actor()->Holder()->Camera();

			Actor()->Cameras().UpdateFromCamera			(C);
			Actor()->Cameras().ApplyDevice				(VIEWPORT_NEAR);
		}
#endif // MASTER_GOLD
	}
	__super::OnFrame			();

	if(!Device.Paused())
		Engine.Sheduler.Update		();

	// update weathers ambient
	if(!Device.Paused())
		WeathersUpdate				();

	if	(0!=pDemoFile)
	{
		if	(Device.dwTimeGlobal>uTime2Change){
			// Change level + play demo
			if			(pDemoFile->elapsed()<3)	pDemoFile->seek(0);		// cycle

			// Read params
			string512			params;
			pDemoFile->r_string	(params,sizeof(params));
			string256			o_server, o_client, o_demo;	u32 o_time;
			sscanf				(params,"%[^,],%[^,],%[^,],%d",o_server,o_client,o_demo,&o_time);

			// Start _new level + demo
			Engine.Event.Defer	("KERNEL:disconnect");
			Engine.Event.Defer	("KERNEL:start",size_t(xr_strdup(_Trim(o_server))),size_t(xr_strdup(_Trim(o_client))));
			Engine.Event.Defer	("GAME:demo",	size_t(xr_strdup(_Trim(o_demo))), u64(o_time));
			uTime2Change		= 0xffffffff;	// Block changer until Event received
		}
	}

#ifdef DEBUG
	if ((m_last_stats_frame + 1) < m_frame_counter)
		profiler().clear		();
#endif
	UpdateDof();
}

#include "game_sv_single.h"
#include "xrServer.h"
#include "hudmanager.h"
#include "UIGameCustom.h"

void CGamePersistent::OnEvent(EVENT E, u64 P1, u64 P2)
{
	if(E==eQuickLoad)
	{
		if (Device.Paused())
			Device.Pause		(FALSE, TRUE, TRUE, "eQuickLoad");
		
		if(HUD().GetUI())
			HUD().GetUI()->UIGame()->HideShownDialogs();

		LPSTR		saved_name	= (LPSTR)(P1);

		Level().remove_objects	();
		game_sv_Single			*game = smart_cast<game_sv_Single*>(Level().Server->game);
		R_ASSERT				(game);
		game->restart_simulator	(saved_name);
		xr_free					(saved_name);
		return;
	}else
	if(E==eDemoStart)
	{
		string256			cmd;
		LPCSTR				demo	= LPCSTR(P1);
		xr_sprintf				(cmd,"demo_play %s",demo);
		Console->Execute	(cmd);
		xr_free				(demo);
		uTime2Change		= Device.TimerAsync() + u32(P2)*1000;
	}
}

void CGamePersistent::Statistics	(CGameFont* F)
{
#ifdef DEBUG
#	ifndef _EDITOR
		m_last_stats_frame		= m_frame_counter;
		profiler().show_stats	(F,!!psAI_Flags.test(aiStats));
#	endif
#endif
}

float CGamePersistent::MtlTransparent(u32 mtl_idx)
{
	return GMLib.GetMaterialByIdx((u16)mtl_idx)->fVisTransparencyFactor;
}
static BOOL bRestorePause	= FALSE;
static BOOL bEntryFlag		= TRUE;
// Did WE pause on the way out? Only then may the return lift it: with g_pause_on_minimize off
// nothing was paused, and calling Pause(FALSE) anyway would clear a pause the player set.
static BOOL bPausedByDeactivate = FALSE;

void CGamePersistent::OnAppActivate		()
{
	bool bIsMP = (g_pGameLevel && Level().game && GameID() != eGameIDSingle);
	bIsMP		&= !Device.Paused();

	if( !bPausedByDeactivate )
	{
		bEntryFlag = TRUE;
		return;
	}
	bPausedByDeactivate = FALSE;

	if( !bIsMP )
	{
		Device.Pause			(FALSE, !bRestorePause, TRUE, "CGP::OnAppActivate");
	}else
	{
		Device.Pause			(FALSE, TRUE, TRUE, "CGP::OnAppActivate MP");
	}

	bEntryFlag = TRUE;
}

void CGamePersistent::OnAppDeactivate	()
{
	if(!bEntryFlag) return;

	bool bIsMP = (g_pGameLevel && Level().game && GameID() != eGameIDSingle);

	bRestorePause = FALSE;

	// Options: "pause on minimise" (g_pause_on_minimize / AF_PAUSE_ON_MINIMIZE, on by default).
	// Single player only -- a multiplayer session cannot stop because one window lost focus, and
	// that branch keeps its own timer-less pause.
	if ( !bIsMP && !psDeviceFlags.test(rsPauseOnMinimize) )
	{
		bEntryFlag = FALSE;
		return;
	}

	if ( !bIsMP )
	{
		bRestorePause			= Device.Paused();
		Device.Pause			(TRUE, TRUE, TRUE, "CGP::OnAppDeactivate");
	}else
	{
		Device.Pause			(TRUE, FALSE, TRUE, "CGP::OnAppDeactivate MP");
	}
	bPausedByDeactivate = TRUE;
	bEntryFlag = FALSE;
}


bool CGamePersistent::OnRenderPPUI_query()
{
	return MainMenu()->OnRenderPPUI_query();
	// enable PP or not
}

extern void draw_wnds_rects();
void CGamePersistent::OnRenderPPUI_main()
{
	// always
	MainMenu()->OnRenderPPUI_main();
	draw_wnds_rects();
}

void CGamePersistent::OnRenderPPUI_PP()
{
	MainMenu()->OnRenderPPUI_PP();
}

// 3D PDA: draw the PDA window (+cursor) into the backbuffer; the renderer calls this just before
// the scene and then snapshots the result into "$user$ui", which the PDA hud model's screen
// material samples. Returning true is the renderer's cue that there IS something to snapshot.
// g_pda_rt_pass tells CUIPdaWnd::Draw that this is the RT pass, not the normal on-screen one
// (which is suppressed while the 3D PDA is in hand).
bool CGamePersistent::OnRenderPdaUI()
{
	if (!g_pGameLevel || !g_pGameLevel->bReady)		return false;
	if (!HUD().GetUI() || !HUD().GetUI()->UIGame())	return false;
	CUIPdaWnd& pda = HUD().GetUI()->UIGame()->PdaMenu();
	if (!pda.IsShown())								return false;
	// with the 3D PDA switched off (AF_PDA_3D) nothing samples $user$ui and the window is drawn
	// full-screen again, so capturing it here would just be a second, wasted draw of the same UI
	if (!gwr_pda_screen_active())					return false;

	g_pda_rt_pass	= true;
	pda.Draw						();
	// A running tutorial points at the PDA's own UI (highlights, arrows), so it belongs on the
	// model's screen with it -- between the window and the cursor, the order it draws in normally.
	if (g_tutorial && g_tutorial->IsActive())
		g_tutorial->OnRender		();
	// Button tooltips are deferred: CUIButton::DrawText only calls g_btnHint->Draw_(), which merely
	// raises a flag, and the real draw happens from Device.seqRender at the very end of the frame --
	// long after this capture, hence hints landing on top of the world instead of on the model. The
	// flag was just raised by pda.Draw() above, so render it here and clear it; the late global pass
	// then finds nothing to do. Outside the PDA the hint keeps working exactly as before.
	if (g_btnHint)
		g_btnHint->OnRender			();
	if (GetUICursor()->IsVisible())
		GetUICursor()->OnRender		();
	g_pda_rt_pass	= false;
	return true;
}

// 3D PiP scope: the renderer asks whether to snapshot the scene into $user$scope this frame. True only
// while the actor aims through an attached scope (the ak74 PSO etc.) -- so the lens shows the world and
// the cost (one surface copy / re-render) is paid only when scoped.
bool CGamePersistent::OnRenderScopeActive()
{
	if (!g_pGameLevel || !g_pGameLevel->bReady)		return false;
	CActor* a = Actor();
	if (!a)											return false;
	CWeapon* w = smart_cast<CWeapon*>(a->inventory().ActiveItem());
	if (!w)											return false;
	// Fill $user$scope only while aiming a lensed scope (matches the lens being drawn only then), and
	// only while the lens is actually visible -- GS LensConditions drops the lens frames in the alter
	// pose (the ELCAN's backup 1x sight), keeping them through the cross-fade.
	return w->IsLensedScope() && w->LensVisibility() > 0.001f
		&& (w->IsZoomed() || w->GetZoomRotationFactor() > 0.01f);
}

// 3D PiP double-render (Gunslinger LensDoubleRender). On a throttled "lens frame" while aiming a lensed
// scope, tell the engine to render the whole scene at the magnified scope FOV -> the world-only capture in
// $user$scope becomes a true optical zoom. Every other frame is a lens frame (rendered but not presented);
// the frames in between are the normal view. -> screen + lens each refresh at ~half rate while scoped.
// GS lens_render_factor (gunsl_config.pas:1177, NeedLensFrameNow = frame mod (GPUs*factor) == 0):
// one lens frame out of every N. Bigger N = the main view keeps more of its frames (smoother) and the
// lens image refreshes more rarely. GS allows 1; we cannot, because our lens frame IS the frame -- the
// screen re-presents the last normal one -- so N=1 would never produce a normal frame to present.
// Console-only (`lens_render_factor`): unlike GS, where a lens frame is an extra scene render, ours
// costs the same at any N, so this changes smoothness, never the framerate. See console_commands.cpp.
int g_lens_render_factor = 2;

bool CGamePersistent::ComputeLensFrame(float& out_fov)
{
	m_bLensFrameNow = false;
	m_bLensAimActive = false;
	out_fov = 0.f;
	if (!g_pGameLevel || !g_pGameLevel->bReady)		return false;
	CActor* a = Actor();
	if (!a)											return false;
	CWeapon* w = smart_cast<CWeapon*>(a->inventory().ActiveItem());
	if (!w)											return false;

	// LENS SWITCHED OFF (AF_LENS_3D). The scope does not stop magnifying -- GS gives the flat 2D scope
	// exactly the LENS magnification: CCameraManager__Update_Lens_FOV_manipulation
	// (LensDoubleRender.pas:418) overrides the camera FOV with GetLensFOV once the aim is fully in, and
	// the scope picture is drawn over that zoomed world. So the option changes HOW the magnified image
	// is produced (double-rendered lens vs. a plain world zoom), not how much it magnifies -- and
	// scope_zoom_factor stays out of it entirely, which is why GS can leave it at 1.02 everywhere.
	// GS's gates, verbatim: aim factor > 0.999, not grenade mode, not the alter (backup 1x) pose.
	if (!w->IsLensedScope())
	{
		// Gunslinger's gate, verbatim (LensDoubleRender.pas:427): aim factor > 0.999, not grenade mode,
		// not the alter pose -- and OTHERWISE THE FOV IS LEFT ALONE, no easing of our own. GS can be that
		// blunt because for a lensed optic its vanilla zoom path is already a no-op (scope_zoom_factor
		// 1.02 through RecalcZoomFOV ~= the base fov), and ours is too: CActor::currentFOV returns g_fov
		// for IsLensedScopeCfg. So the un-overridden fov IS the base fov -- there is nothing to ramp
		// between, and an added lerp only fights the camera's own smoothing.
		if (!w->IsLensedScopeCfg())					return false;
		// IsZoomed() FIRST, and it is not redundant with the rotation factor below. CWeapon::OnZoomOut
		// clears m_bAlterZoom and m_bIsZoomModeNow together, in one call, while m_fZoomRotationFactor
		// only starts DECAYING (by dt/zoom_rotate_time). At a high framerate dt is tiny, so for the first
		// frame or two after release the factor is still above 0.999 while the alter flag has already
		// gone -- and this branch would fire GetLensFOV() for exactly one frame: the momentary fov click
		// on aim-out, most visible leaving the alter pose (which otherwise never changes the fov at all).
		// Keying on IsZoomed() closes that window because it flips in the very same call as the flag.
		if (!w->IsZoomed())							return false;
		if (w->IsAlterZoom())						return false;
		if (w->GetZoomRotationFactor() <= 0.999f)	return false;
		out_fov = w->GetLensFOV();
		return (out_fov > 0.f);
	}

	// The world FOV is OVERRIDDEN on EVERY frame while a lensed scope is in hand (this runs in ApplyDevice,
	// AFTER the camera zoom/dispersion effectors, so it wins). On a LENS frame (throttle + aiming) -> the
	// magnified GetLensFOV (captured to $user$scope, not presented); otherwise -> the base g_fov, so the
	// presented main view is always wide, steady, un-zoomed. Overriding on the non-aiming frames too is
	// belt-and-braces now that CActor::currentFOV already returns g_fov for this optic and ApplyDevice no
	// longer writes the override back into the camera's fov filter.
	extern float g_fov;
	const bool aiming = (w->IsZoomed() || w->GetZoomRotationFactor() > 0.01f);
	m_bLensAimActive = aiming;						// drives the present bridge (save/restore) while aiming
	if (!aiming)
		m_bLensSaveValid = false;					// aim released -> invalidate the saved frame for next session
	// A lens frame is only allowed once a valid normal frame has been saved this session (m_bLensSaveValid, set
	// by PresentBridgeLens after a save). So the FIRST aim frame(s) render normally and populate rt_scope_save
	// first -> the present bridge never restores a STALE save from a previous aim (the 1-frame camera pop).
	// GS LensConditions: with the alter pose engaged the lens is off, so stop paying for (and stop
	// showing) the magnified double-render -- the main view stays on the base FOV like any 1x sight.
	const bool lens_on = (w->LensVisibility() > 0.001f);
	const u32  lens_mod = (u32)_max(2, g_lens_render_factor);
	if (aiming && lens_on && m_bLensSaveValid && (Device.dwFrame % lens_mod) == 0)	// 1 frame in N + aiming + valid save = lens frame
	{
		out_fov = w->GetLensFOV();
		if (out_fov <= 0.f)							{ out_fov = g_fov; return true; }
		m_bLensFrameNow = true;
	}
	else											// presented normal frame (also the first aim frame)
	{
		// Same alter_scope_zoom_factor rule with the lens ON: the alter pose fades the lens out
		// (lens_on false), and the backup sight's own magnification -- 1.0/none by default -- is what
		// the world FOV should follow, not the base FOV by accident.
		out_fov = (aiming && !lens_on && w->IsAlterZoom() && w->GetZoomRotationFactor() > 0.999f)
					? w->AlterZoomFOV() : g_fov;
	}
	return true;
}

// The renderer asks once per NORMAL phase; the actor decides whether there is anything to draw.
void CGamePersistent::RenderFirstPersonLegs()
{
	if (g_pGameLevel && g_pGameLevel->bReady && Actor())
		Actor()->RenderLegs();
}

void CGamePersistent::OnRenderForward()
{
	// draw world tracers with the scene depth still bound (MSAA path) so the HUD occludes them
	if (g_pGameLevel && g_pGameLevel->bReady)
		Level().BulletManager().Render();
}
#include "string_table.h"
#include "../xrEngine/x_ray.h"
void CGamePersistent::LoadTitle(LPCSTR str)
{
	string512			buff;
	xr_sprintf			(buff, "%s...", CStringTable().translate(str).c_str());
	pApp->LoadTitleInt	(buff);
}

bool CGamePersistent::CanBePaused()
{
	return IsGameTypeSingle	() || (g_pGameLevel && Level().IsDemoPlay());
}
void CGamePersistent::SetPickableEffectorDOF(bool bSet)
{
	m_bPickableDOF = bSet;
	if(!bSet)
	{
		// A zone with pick_dof_effector (zone_base, so every radiation zone too) drives the DOF
		// from the crosshair range in UpdateDof for as long as the actor is inside, and swallows
		// every SetEffectorDOF meanwhile -- so m_dof_changed is usually still FALSE on the way out,
		// and GS's "nothing to undo" early-out in RestoreEffectorDOF returned without restoring:
		// the pick blur stayed on until something else happened to move the DOF. The zone did
		// change it, so mark it and let the restore run.
		m_dof_changed = true;
		RestoreEffectorDOF();
	}
}

void CGamePersistent::GetCurrentDof(Fvector3& dof)
{
	// A LENS frame IS the picture the scope shows, and the aiming DOF belongs to the MAIN view -- it is
	// there to blur the world AROUND the lens while the lens image stays sharp (RefreshZoomDOF -> LensDof).
	// Rendering the lens frame with it would bake that blur INTO the scope image. The lens capture used to
	// be taken before the combine pass, which hid this; now that $user$scopeui is grabbed from the finished
	// backbuffer (CRender::CaptureLensUIToRT) the DOF is in it.
	// GS does exactly this and with these numbers: dof_lens_on (LensDoubleRender.pas:285) swaps the whole
	// DOF context for LENS_DOF_NEAR/FOCUS/FAR = -9151 / 0 / 9151 for the duration of the lens frame and
	// restores it in dof_lens_off -- a range that wide simply leaves everything in focus. Taken verbatim
	// rather than reusing our base dof, which is itself whatever r2_dof happens to be.
	if (m_bLensFrameNow)	dof.set(-9151.f, 0.f, 9151.f);
	else					dof = m_dof[1];
}

void CGamePersistent::SetBaseDof(const Fvector3& dof)
{
	m_dof[0]=m_dof[1]=m_dof[2]=m_dof[3]	= dof;
}

// GS gunsl_config.pas:1206..1217 -- same key names, same fallbacks.
const CGamePersistent::SDofDefaults& CGamePersistent::DofDefaults()
{
	static SDofDefaults	D;
	static bool			loaded = false;
	if (!loaded)
	{
		loaded = true;
		LPCSTR S = "gunslinger_base";
		D.zoom.set	(READ_IF_EXISTS(pSettings, r_float, S, "default_zoom_dof_near",   0.5f),
					 READ_IF_EXISTS(pSettings, r_float, S, "default_zoom_dof_focus",  0.8f),
					 READ_IF_EXISTS(pSettings, r_float, S, "default_zoom_dof_far",    10000.f));
		D.action.set(READ_IF_EXISTS(pSettings, r_float, S, "default_action_dof_near", 0.f),
					 READ_IF_EXISTS(pSettings, r_float, S, "default_action_dof_focus",0.5f),
					 READ_IF_EXISTS(pSettings, r_float, S, "default_action_dof_far",  5.f));
		D.speed		  = READ_IF_EXISTS(pSettings, r_float, S, "default_dof_speed",       5.f);
		D.speed_in	  = READ_IF_EXISTS(pSettings, r_float, S, "default_dof_speed_in",    3.f);
		D.speed_out	  = READ_IF_EXISTS(pSettings, r_float, S, "default_dof_speed_out",   1.f);
		D.time_offset = READ_IF_EXISTS(pSettings, r_float, S, "default_dof_time_offset",-0.5f);
	}
	return D;
}

void CGamePersistent::SetEffectorDOF(const Fvector& needed_dof, float speed)
{
	if(m_bPickableDOF)	return;
	m_dof_speed	= speed;
	m_dof[0]	= needed_dof;
	m_dof[2]	= m_dof[1]; //current
	m_dof_changed = true;
}

void CGamePersistent::RestoreEffectorDOF(float speed)
{
	// GS ResetDOF opens with `cmp _dof_changed, 0 / je @finish` -- restoring when nothing was ever
	// applied would re-arm the interpolation (and its speed) for no reason.
	if(!m_dof_changed)	return;
	SetEffectorDOF	(m_dof[3], speed);
	m_dof_changed	= false;
}

void CGamePersistent::SetEffectorDOF(const Fvector& needed_dof)
{
	SetEffectorDOF	(needed_dof, DofDefaults().speed);
}

void CGamePersistent::RestoreEffectorDOF()
{
	RestoreEffectorDOF	(DofDefaults().speed_out);
}
#include "hudmanager.h"

//	m_dof		[4];	// 0-dest 1-current 2-from 3-original
void CGamePersistent::UpdateDof()
{
	static float diff_far	= pSettings->r_float("zone_pick_dof","far");//70.0f;
	static float diff_near	= pSettings->r_float("zone_pick_dof","near");//-70.0f;

	if(m_bPickableDOF)
	{
		Fvector pick_dof;
		pick_dof.y	= HUD().GetCurrentRayQuery().range;
		pick_dof.x	= pick_dof.y+diff_near;
		pick_dof.z	= pick_dof.y+diff_far;
		m_dof[0]	= pick_dof;
		m_dof[2]	= m_dof[1]; //current
	}
	if(m_dof[1].similar(m_dof[0]))
						return;

	float td			= Device.fTimeDelta;
	// GS DOFLoadSpeed_Patch: vanilla's fixed `td/0.2f` becomes `td * <speed of THIS transition>`,
	// and GS RecalcDofSpeed snaps the DOF outright while there is no actor (menu / level load).
	const float speed	= (g_pGameLevel && Level().CurrentEntity()) ? m_dof_speed : 1000.f;
	Fvector				diff;
	diff.sub			(m_dof[0], m_dof[2]);
	diff.mul			(td*speed);
	m_dof[1].add		(diff);
	(m_dof[0].x<m_dof[2].x)?clamp(m_dof[1].x,m_dof[0].x,m_dof[2].x):clamp(m_dof[1].x,m_dof[2].x,m_dof[0].x);
	(m_dof[0].y<m_dof[2].y)?clamp(m_dof[1].y,m_dof[0].y,m_dof[2].y):clamp(m_dof[1].y,m_dof[2].y,m_dof[0].y);
	(m_dof[0].z<m_dof[2].z)?clamp(m_dof[1].z,m_dof[0].z,m_dof[2].z):clamp(m_dof[1].z,m_dof[2].z,m_dof[0].z);
}

#include "ui\uimainingamewnd.h"
void CGamePersistent::OnSectorChanged(int sector)
{
	if(HUD().GetUI())
		HUD().GetUI()->UIMainIngameWnd->OnSectorChanged(sector);
}

void CGamePersistent::OnAssetsChanged()
{
	IGame_Persistent::OnAssetsChanged	();
	CStringTable().rescan				();
}
