#include "stdafx.h"
#include "player_hud.h"
#include "HudItem.h"
#include "Weapon.h"		// GetCurrentFireMode() for the per-motion camera anm rule (single vs auto fire)
#include "ui_base.h"
#include "actor.h"
#include "physic_item.h"
#include "static_cast_checked.hpp"
#include "actoreffector.h"
#include "../xrEngine/IGame_Persistent.h"
#include "InertionData.h"

player_hud* g_player_hud = NULL;
Fvector _ancor_pos;
Fvector _wpn_root_pos;

extern int g_block_wpn_switch;	// != 0 while an animated item-use / device gesture is playing (ActorInput.cpp)

float CalcMotionSpeed(const shared_str& anim_name)
{

	if(!IsGameTypeSingle() && (anim_name=="anm_show" || anim_name=="anm_hide") )
		return 2.0f;
	// Gunslinger-style: while an item is being used, the weapon holsters to free the hands for the
	// eat/heal gesture -> speed up that holster so the item comes up quickly. The item-use raises
	// g_block_wpn_switch before triggering the holster; only the hide is affected (the redraw runs
	// after the block is cleared, so it stays normal speed).
	// Match the whole anm_hide FAMILY, not the bare alias: PlayHUDMotion rewrites it to
	// anm_hide_jammed / anm_hide_empty (and the _g / _w_gl / BM16 _0.._2 twins) before playing, so
	// an exact compare silently missed every one of those -- a jammed or empty weapon holstered at
	// normal speed. anm_hide_fast is deliberately NOT in: that's the detector's own quick holster,
	// already fast, and gwr_eatable times its sound against that length (snd_delay_detector).
	if(g_block_wpn_switch != 0 && anim_name.size() >= 8 &&
		0 == strncmp(anim_name.c_str(), "anm_hide", 8) &&
		NULL == strstr(anim_name.c_str(), "_fast"))
		return 2.0f;
	return 1.0f;
}

player_hud_motion* player_hud_motion_container::find_motion(const shared_str& name)
{
	xr_vector<player_hud_motion>::iterator it	= m_anims.begin();
	xr_vector<player_hud_motion>::iterator it_e = m_anims.end();
	for(;it!=it_e;++it)
	{
		const shared_str& s = (true)?(*it).m_alias_name:(*it).m_base_name;
		if( s == name)
			return &(*it);
	}
	return NULL;
}

void player_hud_motion_container::load(IKinematicsAnimated* model, const shared_str& sect)
{
	CInifile::Sect& _sect		= pSettings->r_section(sect);
	CInifile::SectCIt _b		= _sect.Data.begin();
	CInifile::SectCIt _e		= _sect.Data.end();
	player_hud_motion* pm		= NULL;
	
	string512					buff;
	MotionID					motion_ID;

	for(;_b!=_e;++_b)
	{
		if(strstr(_b->first.c_str(), "anm_")==_b->first.c_str())
		{
			const shared_str& anm	= _b->second;
			m_anims.resize			(m_anims.size()+1);
			pm						= &m_anims.back();
			//base and alias name
			pm->m_alias_name		= _b->first;
			
			if(_GetItemCount(anm.c_str())==1)
			{
				pm->m_base_name			= anm;
				pm->m_additional_name	= anm;
			}else
			{
				R_ASSERT2(_GetItemCount(anm.c_str())==2, anm.c_str());
				string512				str_item;
				_GetItem(anm.c_str(),0,str_item);
				pm->m_base_name			= str_item;

				_GetItem(anm.c_str(),1,str_item);
				pm->m_additional_name	= str_item;
			}

			//and load all motions for it

			for(u32 i=0; i<=8; ++i)
			{
				if(i==0)
					xr_strcpy				(buff,pm->m_base_name.c_str());		
				else
					sprintf				(buff,"%s%d",pm->m_base_name.c_str(),i);		

				motion_ID				= model->ID_Cycle_Safe(buff);
				if(motion_ID.valid())
				{
					pm->m_animations.resize			(pm->m_animations.size()+1);
					pm->m_animations.back().mid		= motion_ID;
					pm->m_animations.back().name	= buff;
#ifdef DEBUG
					Msg(" alias=[%s] base=[%s] name=[%s]",pm->m_alias_name.c_str(), pm->m_base_name.c_str(), buff);
#endif // #ifdef DEBUG
				}
			}
			R_ASSERT2(pm->m_animations.size(),make_string("motion not found [%s]", pm->m_base_name.c_str()).c_str());
		}
	}
}

Fvector& attachable_hud_item::hands_attach_pos()
{
	return m_measures.m_hands_attach[0];
}

Fvector& attachable_hud_item::hands_attach_rot()
{
	return m_measures.m_hands_attach[1];
}

Fvector& attachable_hud_item::hands_offset_pos()
{
	u8 idx	= m_parent_hud_item->GetCurrentHudOffsetIdx();
	return m_measures.m_hands_offset[0][idx];
}

Fvector& attachable_hud_item::hands_offset_rot()
{
	u8 idx	= m_parent_hud_item->GetCurrentHudOffsetIdx();
	return m_measures.m_hands_offset[1][idx];
}

void attachable_hud_item::set_bone_visible(const shared_str& bone_name, BOOL bVisibility, BOOL bSilent)
{
	u16  bone_id;
	BOOL bVisibleNow;
	bone_id			= m_model->LL_BoneID			(bone_name);
	if(bone_id==BI_NONE)
	{
		if(bSilent)	return;
		R_ASSERT2	(0,			make_string("model [%s] has no bone [%s]",pSettings->r_string(m_sect_name, "item_visual"), bone_name.c_str()).c_str());
	}
	bVisibleNow		= m_model->LL_GetBoneVisible	(bone_id);
	if(bVisibleNow!=bVisibility)
		m_model->LL_SetBoneVisible	(bone_id,bVisibility, TRUE);
}

void attachable_hud_item::update(bool bForce)
{
	if(!bForce && m_upd_firedeps_frame==Device.dwFrame)	return;
	bool is_16x9 = UI()->is_widescreen();
	
	if(!!m_measures.m_prop_flags.test(hud_item_measures::e_16x9_mode_now)!=is_16x9)
		m_measures.load(m_sect_name, m_model);

	Fvector ypr						= m_measures.m_item_attach[1];
	ypr.mul							(PI/180.f);
	m_attach_offset.setHPB			(ypr.x,ypr.y,ypr.z);
	m_attach_offset.translate_over	(m_measures.m_item_attach[0]);

	m_parent->calc_transform		(m_attach_place_idx, m_attach_offset, m_item_transform);
	m_upd_firedeps_frame			= Device.dwFrame;

	IKinematicsAnimated* ka			=	m_model->dcast_PKinematicsAnimated();
	if(ka)
	{
		ka->UpdateTracks									();
		ka->dcast_PKinematics()->CalculateBones_Invalidate	();
		ka->dcast_PKinematics()->CalculateBones				(TRUE);
	}
}

void attachable_hud_item::update_hud_additional(Fmatrix& trans)
{
	if(m_parent_hud_item)
	{
		m_parent_hud_item->UpdateHudAdditonal(trans);
	}
}

void attachable_hud_item::setup_firedeps(firedeps& fd)
{
	update							(false);
	// fire point&direction
	if(m_measures.m_prop_flags.test(hud_item_measures::e_fire_point))
	{
		Fmatrix& fire_mat								= m_model->LL_GetTransform(m_measures.m_fire_bone);
		fire_mat.transform_tiny							(fd.vLastFP, m_measures.m_fire_point_offset);
		m_item_transform.transform_tiny					(fd.vLastFP);

		fd.vLastFD.set									(0.f,0.f,1.f);
		m_item_transform.transform_dir					(fd.vLastFD);
		VERIFY(_valid(fd.vLastFD));
		VERIFY(_valid(fd.vLastFD));

		fd.m_FireParticlesXForm.identity				();
		fd.m_FireParticlesXForm.k.set					(fd.vLastFD);
		Fvector::generate_orthonormal_basis_normalized	(	fd.m_FireParticlesXForm.k,
															fd.m_FireParticlesXForm.j, 
															fd.m_FireParticlesXForm.i);
		VERIFY(_valid(fd.m_FireParticlesXForm));
	}

	if(m_measures.m_prop_flags.test(hud_item_measures::e_fire_point2))
	{
		Fmatrix& fire_mat			= m_model->LL_GetTransform(m_measures.m_fire_bone2);
		fire_mat.transform_tiny		(fd.vLastFP2,m_measures.m_fire_point2_offset);
		m_item_transform.transform_tiny	(fd.vLastFP2);
		VERIFY(_valid(fd.vLastFP2));
		VERIFY(_valid(fd.vLastFP2));
	}

	if(m_measures.m_prop_flags.test(hud_item_measures::e_shell_point))
	{
		Fmatrix& fire_mat			= m_model->LL_GetTransform(m_measures.m_shell_bone);
		fire_mat.transform_tiny		(fd.vLastSP,m_measures.m_shell_point_offset);
		m_item_transform.transform_tiny	(fd.vLastSP);
		VERIFY(_valid(fd.vLastSP));
		VERIFY(_valid(fd.vLastSP));
	}
}

bool  attachable_hud_item::need_renderable()
{
	return m_parent_hud_item->need_renderable();
}

void attachable_hud_item::render()
{
	::Render->set_Transform		(&m_item_transform);
	::Render->add_Visual		(m_model->dcast_RenderVisual());
	debug_draw_firedeps			();
	m_parent_hud_item->render_hud_mode();
}

bool attachable_hud_item::render_item_ui_query()
{
	return m_parent_hud_item->render_item_3d_ui_query();
}

void attachable_hud_item::render_item_ui()
{
	m_parent_hud_item->render_item_3d_ui();
}

void hud_item_measures::load(const shared_str& sect_name, IKinematics* K)
{
	bool is_16x9 = UI()->is_widescreen();
	string64	_prefix;
	xr_sprintf	(_prefix,"%s",is_16x9?"_16x9":"");
	string128	val_name;

	strconcat					(sizeof(val_name),val_name,"hands_position",_prefix);
	m_hands_attach[0]			= pSettings->r_fvector3(sect_name, val_name);
	strconcat					(sizeof(val_name),val_name,"hands_orientation",_prefix);
	m_hands_attach[1]			= pSettings->r_fvector3(sect_name, val_name);

	m_item_attach[0]			= pSettings->r_fvector3(sect_name, "item_position");
	m_item_attach[1]			= pSettings->r_fvector3(sect_name, "item_orientation");

	shared_str					 bone_name;
	m_prop_flags.set			 (e_fire_point,pSettings->line_exist(sect_name,"fire_bone"));
	if(m_prop_flags.test(e_fire_point))
	{
		bone_name				= pSettings->r_string(sect_name, "fire_bone");
		m_fire_bone				= K->LL_BoneID(bone_name);
		m_fire_point_offset		= pSettings->r_fvector3(sect_name, "fire_point");
	}else
		m_fire_point_offset.set(0,0,0);

	m_prop_flags.set			 (e_fire_point2,pSettings->line_exist(sect_name,"fire_bone2"));
	if(m_prop_flags.test(e_fire_point2))
	{
		bone_name				= pSettings->r_string(sect_name, "fire_bone2");
		m_fire_bone2			= K->LL_BoneID(bone_name);
		m_fire_point2_offset	= pSettings->r_fvector3(sect_name, "fire_point2");
	}else
		m_fire_point2_offset.set(0,0,0);

	m_prop_flags.set			 (e_shell_point,pSettings->line_exist(sect_name,"shell_bone"));
	if(m_prop_flags.test(e_shell_point))
	{
		bone_name				= pSettings->r_string(sect_name, "shell_bone");
		m_shell_bone			= K->LL_BoneID(bone_name);
		m_shell_point_offset	= pSettings->r_fvector3(sect_name, "shell_point");
	}else
		m_shell_point_offset.set(0,0,0);

	m_hands_offset[0][0].set	(0,0,0);
	m_hands_offset[1][0].set	(0,0,0);

	strconcat					(sizeof(val_name),val_name,"aim_hud_offset_pos",_prefix);
	m_hands_offset[0][1]		= pSettings->r_fvector3(sect_name, val_name);
	strconcat					(sizeof(val_name),val_name,"aim_hud_offset_rot",_prefix);
	m_hands_offset[1][1]		= pSettings->r_fvector3(sect_name, val_name);

	strconcat					(sizeof(val_name),val_name,"gl_hud_offset_pos",_prefix);
	m_hands_offset[0][2]		= pSettings->r_fvector3(sect_name, val_name);
	strconcat					(sizeof(val_name),val_name,"gl_hud_offset_rot",_prefix);
	m_hands_offset[1][2]		= pSettings->r_fvector3(sect_name, val_name);


	R_ASSERT2(pSettings->line_exist(sect_name,"fire_point")==pSettings->line_exist(sect_name,"fire_bone"),		sect_name.c_str());
	R_ASSERT2(pSettings->line_exist(sect_name,"fire_point2")==pSettings->line_exist(sect_name,"fire_bone2"),	sect_name.c_str());
	R_ASSERT2(pSettings->line_exist(sect_name,"shell_point")==pSettings->line_exist(sect_name,"shell_bone"),	sect_name.c_str());

	m_prop_flags.set(e_16x9_mode_now,is_16x9);
}

attachable_hud_item::~attachable_hud_item()
{
	IRenderVisual* v			= m_model->dcast_RenderVisual();
	::Render->model_Delete		(v);
	m_model						= NULL;
}

void attachable_hud_item::load(const shared_str& sect_name)
{
	m_sect_name					= sect_name;

	// Visual
	const shared_str& visual_name = pSettings->r_string(sect_name, "item_visual");
	m_model						 = smart_cast<IKinematics*>(::Render->model_Create(visual_name.c_str()));

	m_attach_place_idx			= pSettings->r_u16(sect_name, "attach_place_idx");
	m_measures.load				(sect_name, m_model);
}

u32 attachable_hud_item::anim_play(const shared_str& anm_name_b, BOOL bMixIn, const CMotionDef*& md, u8& rnd_idx)
{
	// Guard a NULL/empty animation name. An empty string becomes a NULL shared_str (the string container
	// docks "" -> null), and the R_ASSERT's strstr(anm_name_b.c_str(),...) below then dereferences null ->
	// silent access violation (seen "иногда при доставании ПДА": the 3D-PDA phantom occasionally plays an
	// empty alias). Bail gracefully + log the section so the offending item is visible instead of a no-log crash.
	if (!anm_name_b.c_str() || !anm_name_b.c_str()[0])
	{
		Msg("! [attachable_hud_item::anim_play] empty/NULL animation name on [%s] -- skipped", m_sect_name.c_str());
		md = nullptr;
		rnd_idx = 0;
		return 0;
	}

	float speed				= CalcMotionSpeed(anm_name_b);

	R_ASSERT				(strstr(anm_name_b.c_str(),"anm_")==anm_name_b.c_str());
	string256				anim_name_r;
	bool is_16x9			= UI()->is_widescreen();
	xr_sprintf				(anim_name_r,"%s%s",anm_name_b.c_str(),((m_attach_place_idx==1)&&is_16x9)?"_16x9":"");

	player_hud_motion* anm	= m_hand_motions.find_motion(anim_name_r);
	R_ASSERT2				(anm, make_string("model [%s] has no motion alias defined [%s]", m_sect_name.c_str(), anim_name_r).c_str());
	R_ASSERT2				(anm->m_animations.size(), make_string("model [%s] has no motion defined in motion_alias [%s]", pSettings->r_string(m_sect_name, "item_visual"), anim_name_r).c_str());
	
	rnd_idx					= (u8)Random.randI(anm->m_animations.size()) ;
	const motion_descr& M	= anm->m_animations[ rnd_idx ];

	// one-shot blend-in override requested by the parent CHudItem (e.g. a soft sprint enter). 0 = none.
	float blend_accrue		= m_parent_hud_item ? m_parent_hud_item->ConsumeNextBlendAccrue() : 0.f;

	u32 ret					= g_player_hud->anim_play(m_attach_place_idx, M.mid, bMixIn, md, speed, blend_accrue);
	
	if(m_model->dcast_PKinematicsAnimated())
	{
		IKinematicsAnimated* ka			= m_model->dcast_PKinematicsAnimated();

		shared_str item_anm_name;
		if(anm->m_base_name!=anm->m_additional_name)
			item_anm_name = anm->m_additional_name;
		else
			item_anm_name = M.name;

		MotionID M2						= ka->ID_Cycle_Safe(item_anm_name);
		if(!M2.valid())
			M2							= ka->ID_Cycle_Safe("idle");
		else
			if(bDebug)
				Msg						("playing item animation [%s]",item_anm_name.c_str());
		
		R_ASSERT3(M2.valid(),"model has no motion [idle] ", pSettings->r_string(m_sect_name, "item_visual"));

		u16 root_id						= m_model->LL_GetBoneRoot();
		CBoneInstance& root_binst		= m_model->LL_GetBoneInstance(root_id);
		root_binst.set_callback_overwrite(TRUE);
		root_binst.mTransform.identity	();

		u16 pc							= ka->partitions().count();
		for(u16 pid=0; pid<pc; ++pid)
		{
			CBlend* B					= ka->PlayCycle(pid, M2, bMixIn);
			R_ASSERT					(B);
			B->speed					*= speed;
			if(blend_accrue > 0.f)		B->blendAccrue = blend_accrue;	// softer mix-in (e.g. sprint enter)
		}

		m_model->CalculateBones_Invalidate	();
	}

	R_ASSERT2		(m_parent_hud_item, "parent hud item is NULL");
	CPhysicItem&	parent_object = m_parent_hud_item->object();
	//R_ASSERT2		(parent_object, "object has no parent actor");
	//CObject*		parent_object = static_cast_checked<CObject*>(&m_parent_hud_item->object());

	if (IsGameTypeSingle() && parent_object.H_Parent() == Level().CurrentControlEntity())
	{
		CActor* current_actor	= static_cast_checked<CActor*>(Level().CurrentControlEntity());
		VERIFY					(current_actor);
		CEffectorCam* ec		= current_actor->Cameras().GetCamEffector(eCEWeaponAction);

		string_path			ce_path;
		string_path			anm_name;
		strconcat			(sizeof(anm_name),anm_name,"camera_effects\\weapon\\", M.name.c_str(),".anm");

		// BASE motion name = M.name minus any trailing digits. A looping idle picks RANDOM numbered
		// variants each cycle (Gunslinger's burn idle alternates "fire_on_the_hand" / "fire_on_the_hand1",
		// each with its own camera anm), so keying suppression on the exact name re-fired the camera on
		// every switch. Comparing bases makes all variants of one gesture count as the same.
		string_path			cam_base;
		xr_strcpy			(cam_base, M.name.c_str());
		{ int n = (int)xr_strlen(cam_base); while (n > 0 && cam_base[n-1] >= '0' && cam_base[n-1] <= '9') cam_base[--n] = 0; }

		// Fire the weapon-action camera ONCE per gesture. A motion reused for the action AND the idle/hide
		// of one gesture (GS burn "fire_on_the_hand" is anm_show/idle/hide, and the idle loops through
		// RANDOM numbered variants fire_on_the_hand / fire_on_the_hand1, each with its own camera anm)
		// must not re-lurch the camera every idle/hide cycle. Rule: a DRAW (eShowing state) ALWAYS plays
		// and (re)seeds the tracker -- so every fresh gesture, including a REPEAT burn (a new phantom's
		// draw), starts clean with NO reliance on object identity (freed phantom pointers get reused, so
		// keying on the item pointer wrongly suppressed the 3rd+ burn). Any NON-draw motion whose BASE
		// (name minus trailing digits, collapsing the variants) matches the last one we started is
		// suppressed. A motion with no camera anm (idle/hide/settle) clears the tracker, so genuine
		// re-triggers (reload-then-reload) still play. Shoots are exempt (recoil re-fires per shot).
		static shared_str	s_last_action_cam;
		bool is_show = (m_parent_hud_item->GetState() == CHUDState::eShowing);

		if (FS.exist( ce_path, "$game_anims$", anm_name))
		{
			// if a different action's camera effector is still running (e.g. a shot's, when a
			// reload/draw starts right after), replace it so this motion's camera anim plays.
			// Skip when the same anim is already active, or when both are shots (so rapid auto
			// fire doesn't restart the recoil effector every shot -> stutter).
			CAnimatorCamEffector* cur = smart_cast<CAnimatorCamEffector*>(ec);
			bool same_anim	= cur && cur->AnimName()==anm_name;
			bool is_shoot	= (0 != strstr(M.name.c_str(), "shoot"));				// shoots re-fire recoil every shot

			// The anti-stutter rule for shots must apply to CONTINUOUS AUTO fire only. A single-shot
			// weapon (pump/semi -- the protecta at 155 rpm) fires slower than its own camera anm, so
			// suppressing while the previous one still runs made the camera visibly play on every OTHER
			// shot. Each trigger pull of a single-fire weapon is its own gesture -> always restart.
			bool auto_fire	= false;
			if (CWeapon* pw = smart_cast<CWeapon*>(m_parent_hud_item))
				auto_fire = (pw->GetCurrentFireMode() != 1);
			bool both_shoot	= cur && auto_fire && strstr(M.name.c_str(),"shoot") && strstr(cur->AnimName().c_str(),"shoot");

			// Suppressing a repeat of the same gesture is meant for a PASSIVE state that re-plays one
			// motion forever (the GS burn idle/hide reuse the draw's motion). An ACTION that legitimately
			// LOOPS -- the shotgun tri-state reload replays its add-cartridge motion once per shell --
			// must re-fire the camera anm every iteration, so only idle/hide keep the suppression.
			const u32 hud_state = m_parent_hud_item->GetState();
			const bool passive_state = (hud_state == CHUDState::eIdle) || (hud_state == CHUDState::eHiding);
			bool replay_same= (!is_shoot) && (!is_show) && passive_state					// a draw always plays; an idle/hide
							&& (s_last_action_cam == cam_base);							// repeat of the same base does not

			if(!same_anim && !both_shoot && !replay_same)
			{
				// grab the outgoing effector's current offset so the new one can ease in from it
				Fmatrix	from_offset;
				bool	has_from = false;
				if(cur)	{ from_offset = cur->OffsetXForm(); has_from = true; }

				if(ec)
					current_actor->Cameras().RemoveCamEffector(eCEWeaponAction);

				CAnimatorCamEffector* e		= xr_new<CAnimatorCamEffector>();
				e->SetType					(eCEWeaponAction);
				e->SetHudAffect				(false);
				e->SetCyclic				(false);
				e->Start					(anm_name);
				if(has_from)
					e->SetBlendFrom			(from_offset, 0.15f);	// smooth take-over, no snap
				current_actor->Cameras().AddCamEffector(e);
				s_last_action_cam		= cam_base;
			}
		}
		else
		{
			s_last_action_cam = "";			// a motion with no camera anm (idle/hide/settle) ends the current gesture's tracking
		}
	}
	return ret;
}

player_hud::player_hud()
{
	m_model					= NULL;
	m_attached_items[0]		= NULL;
	m_attached_items[1]		= NULL;
	m_transform.identity	();
}


player_hud::~player_hud()
{
	IRenderVisual* v			= m_model->dcast_RenderVisual();
	::Render->model_Delete		(v);
	m_model						= NULL;

	xr_vector<attachable_hud_item*>::iterator it	= m_pool.begin();
	xr_vector<attachable_hud_item*>::iterator it_e	= m_pool.end();
	for(;it!=it_e;++it)
	{
		attachable_hud_item* a	= *it;
		xr_delete				(a);
	}
	m_pool.clear				();
}

void player_hud::load(const shared_str& player_hud_sect)
{
	if(player_hud_sect ==m_sect_name)	return;
	bool b_reload = (m_model!=NULL);
	if(m_model)
	{
		IRenderVisual* v			= m_model->dcast_RenderVisual();
		::Render->model_Delete		(v);
	}

	m_sect_name					= player_hud_sect;
	const shared_str& model_name= pSettings->r_string(player_hud_sect, "visual");
	m_model						= smart_cast<IKinematicsAnimated*>(::Render->model_Create(model_name.c_str()));

	CInifile::Sect& _sect		= pSettings->r_section(player_hud_sect);
	CInifile::SectCIt _b		= _sect.Data.begin();
	CInifile::SectCIt _e		= _sect.Data.end();
	for(;_b!=_e;++_b)
	{
		if(strstr(_b->first.c_str(), "ancor_")==_b->first.c_str())
		{
			const shared_str& _bone	= _b->second;
			m_ancors.push_back		(m_model->dcast_PKinematics()->LL_BoneID(_bone));
		}
	}
	if(!b_reload)
	{
		m_model->PlayCycle("hand_idle_doun");
	}else
	{
		if(m_attached_items[1])
			m_attached_items[1]->m_parent_hud_item->on_a_hud_attach();

		if(m_attached_items[0])
			m_attached_items[0]->m_parent_hud_item->on_a_hud_attach();
	}
	m_model->dcast_PKinematics()->CalculateBones_Invalidate	();
	m_model->dcast_PKinematics()->CalculateBones(TRUE);

	// Warm the hud visuals of the item-use PHANTOMS. They are created the first time the gesture runs
	// (attachable_hud_item::load -> model_Create), which for the 3D PDA means loading its model and
	// motions at the exact moment the player opens it -- a visible hitch on the first open, and the
	// same cause as the freeze at the start of the controller's psi attack. Creating and dropping the
	// model here leaves it in the render model pool, so the real load is instant. Data-driven: any
	// section listed in [gunslinger_base] preload_hud_visuals is warmed.
	{
		LPCSTR list = READ_IF_EXISTS(pSettings, r_string, "gunslinger_base", "preload_hud_visuals", (LPCSTR)0);
		string256 sect;
		// count FIRST: _GetItem hands back its buffer whatever the index, so using it as the loop
		// condition never terminates -- that hung the game solid on level load.
		const u32 cnt = list ? _GetItemCount(list) : 0;
		for (u32 i = 0; i < cnt; ++i)
		{
			_GetItem(list, i, sect);
			if (!pSettings->section_exist(sect) || !pSettings->line_exist(sect, "item_visual"))	continue;
			LPCSTR vis = pSettings->r_string(sect, "item_visual");
			IRenderVisual* v = ::Render->model_Create(vis);
			if (v)	::Render->model_Delete(v);
		}
	}
}

bool player_hud::render_item_ui_query()
{
	bool res = false;
	if(m_attached_items[0])
		res |= m_attached_items[0]->render_item_ui_query();

	if(m_attached_items[1])
		res |= m_attached_items[1]->render_item_ui_query();

	return res;
}

void player_hud::render_item_ui()
{
	if(m_attached_items[0])
		m_attached_items[0]->render_item_ui();

	if(m_attached_items[1])
		m_attached_items[1]->render_item_ui();
}

void player_hud::render_hud()
{
	if(!m_attached_items[0] && !m_attached_items[1])	return;

	bool b_r0 = (m_attached_items[0] && m_attached_items[0]->need_renderable());
	bool b_r1 = (m_attached_items[1] && m_attached_items[1]->need_renderable());

	if(!b_r0 && !b_r1)									return;

	::Render->set_Transform		(&m_transform);
	::Render->add_Visual		(m_model->dcast_RenderVisual());
	
	if(m_attached_items[0])
		m_attached_items[0]->render();
	
	if(m_attached_items[1])
		m_attached_items[1]->render();
}


#include "../xrEngine/motion.h"

u32 player_hud::motion_length(const shared_str& anim_name, const shared_str& hud_name, const CMotionDef*& md)
{
	float speed						= CalcMotionSpeed(anim_name);
	attachable_hud_item* pi			= create_hud_item(hud_name);
	player_hud_motion*	pm			= pi->m_hand_motions.find_motion(anim_name);
	if(!pm)
		return						100; // ms TEMPORARY
	R_ASSERT2						(pm, 
		make_string	("hudItem model [%s] has no motion with alias [%s]", hud_name.c_str(), anim_name.c_str() ).c_str() 
		);
	return motion_length			(pm->m_animations[0].mid, md, speed);
}

u32 player_hud::motion_length(const MotionID& M, const CMotionDef*& md, float speed)
{
	md					= m_model->LL_GetMotionDef(M);
	VERIFY				(md);
	if (md->flags & esmStopAtEnd) 
	{
		CMotion*			motion		= m_model->LL_GetRootMotion(M);
		return				iFloor( 0.5f + 1000.f*motion->GetLength() / (md->Dequantize(md->speed) * speed) );
	}
	return					0;
}


const Fvector& player_hud::attach_rot() const {
	if (m_attached_items[0]) {
		return m_attached_items[0]->hands_attach_rot();
	} else {
		if (m_attached_items[1]) {
			return m_attached_items[1]->hands_attach_rot();
		} else {
			static Fvector default_attach_rot {};
			default_attach_rot.set(0, 0, 0);
			return default_attach_rot;
		}
	}
}

const Fvector& player_hud::attach_pos() const {
	if (m_attached_items[0]) {
		return m_attached_items[0]->hands_attach_pos();
	} else {
		if (m_attached_items[1]) {
			return m_attached_items[1]->hands_attach_pos();
		} else {
			static Fvector default_attach_pos {};
			default_attach_pos.set(0, 0, 0);
			return default_attach_pos;
		}
	}
}

// The dominant looping blend on a bone partition: playing, not stop-at-end (a loop, not a one-shot),
// not fading out, biggest blend amount. During sprint that's the sprint loop for that hand.
static CBlend* hud_dominant_loop_blend(IKinematicsAnimated* ka, u16 part_id)
{
	if (part_id == u16(-1))	return NULL;
	CBlend* best = NULL;
	u32 cnt = ka->LL_PartBlendsCount(part_id);
	for (u32 i = 0; i < cnt; ++i)
	{
		CBlend* B = ka->LL_PartBlend(part_id, i);
		if (!B || !B->playing || B->stop_at_end)	continue;	// loops only
		if (B->blend == CBlend::eFalloff)			continue;	// fading out
		if (!best || B->blendAmount > best->blendAmount)	best = B;
	}
	return best;
}

// True for a sprint LOOP motion (not the one-shot _start/_end transitions).
static bool hud_is_sprint_loop(const shared_str& m)
{
	LPCSTR s = m.c_str();
	return s && strstr(s, "sprint") && !strstr(s, "_start") && !strstr(s, "_end");
}

void player_hud::update(const Fmatrix& cam_trans)
{
	Fmatrix	trans					= cam_trans;
	update_inertion					(trans);
	update_additional				(trans);

	// SNAP FIX (#6): attach_pos()/attach_rot() return the WEAPON[0]'s hands_attach, or the DETECTOR[1]'s when
	// no weapon is up. The instant a weapon holsters/draws (attach source flips [0]<->[1]) the base hud jumps,
	// which snaps the left-hand detector (its hands_position differs from the weapon's). While a detector
	// companion is out, ease the base attach toward the target instead of snapping. No detector -> instant
	// (the switch is masked by the show/hide anim anyway, so normal weapon feel is unchanged).
	Fvector tgt_pos					= attach_pos();
	Fvector tgt_ypr					= attach_rot();
	static Fvector s_pos{}, s_ypr{};
	static bool s_have = false;
	// Only the SWITCH needs easing. Running it for as long as a detector is out left the whole hud base
	// permanently lagging its target, which reads as sluggish hands on jumps/leans (user 2026-08-03).
	// Arm a short window when the attach source actually changes, and go instant outside it.
	static const void* s_src = nullptr;
	static u32 s_ease_until = 0;
	const void* src = m_attached_items[0] ? (const void*)m_attached_items[0] : (const void*)m_attached_items[1];
	if (src != s_src)
	{
		if (s_src && m_attached_items[1])	s_ease_until = Device.dwTimeGlobal + 200;	// ~0.2s, detector out only
		s_src = src;
	}
	if (s_have && s_ease_until && Device.dwTimeGlobal < s_ease_until)
	{
		float k = Device.fTimeDelta / 0.2f;	clamp(k, 0.f, 1.f);	// ~0.2s ease
		s_pos.lerp(s_pos, tgt_pos, k);
		s_ypr.lerp(s_ypr, tgt_ypr, k);
	}
	else { s_pos = tgt_pos; s_ypr = tgt_ypr; s_ease_until = 0; }
	s_have = true;

	Fvector ypr						= s_ypr;
	ypr.mul							(PI/180.f);
	m_attach_offset.setHPB			(ypr.x,ypr.y,ypr.z);
	m_attach_offset.translate_over	(s_pos);
	m_transform.mul					(trans, m_attach_offset);
	// insert inertion here

	m_model->UpdateTracks				();

	// SPRINT PHASE-LOCK: hard-sync the left-hand detector's sprint loop to the weapon's (right hand),
	// so a staggered entry (drawing/holstering or reloading while running -> the weapon reaches the
	// sprint loop before the detector) doesn't leave the two hands bobbing out of phase. Gunslinger
	// keeps them together by entering sprint on the same movement event; we additionally clamp the
	// loop phase every frame so a late-joining detector snaps to the weapon's cadence and stays locked.
	// Only touches the pure sprint loop of BOTH hands (the _start/_end transitions run free).
	if (m_attached_items[0] && m_attached_items[1])
	{
		CHudItem* wi = m_attached_items[0]->m_parent_hud_item;
		CHudItem* di = m_attached_items[1]->m_parent_hud_item;
		if (wi && di && hud_is_sprint_loop(wi->CurrentMotion()) && hud_is_sprint_loop(di->CurrentMotion()))
		{
			u16 rp = m_model->partitions().part_id("right_hand");
			u16 lp = m_model->partitions().part_id("left_hand");
			CBlend* wB = hud_dominant_loop_blend(m_model, rp);
			CBlend* dB = hud_dominant_loop_blend(m_model, lp);
			if (wB && dB && wB->timeTotal > 0.001f && dB->timeTotal > 0.001f)
				dB->timeCurrent = (wB->timeCurrent / wB->timeTotal) * dB->timeTotal;	// normalized phase lock
		}
	}

	m_model->dcast_PKinematics()->CalculateBones_Invalidate	();
	m_model->dcast_PKinematics()->CalculateBones				(TRUE);

	if(m_attached_items[0])
		m_attached_items[0]->update(true);

	if(m_attached_items[1])
		m_attached_items[1]->update(true);
}

u32 player_hud::anim_play(u16 part, const MotionID& M, BOOL bMixIn, const CMotionDef*& md, float speed, float blend_accrue)
{

	u16 part_id							= u16(-1);
	if(attached_item(0) && attached_item(1))
		part_id = m_model->partitions().part_id((part==0)?"right_hand":"left_hand");

	u16 pc					= m_model->partitions().count();
	for(u16 pid=0; pid<pc; ++pid)
	{
		if(pid==0 || pid==part_id || part_id==u16(-1))
		{
			CBlend* B	= m_model->PlayCycle(pid, M, bMixIn);
			R_ASSERT	(B);
			B->speed	*= speed;
			if(blend_accrue > 0.f)	B->blendAccrue = blend_accrue;	// softer mix-in (e.g. sprint enter)
		}
	}
	m_model->dcast_PKinematics()->CalculateBones_Invalidate	();

	return				motion_length(M, md, speed);
}

void player_hud::update_additional	(Fmatrix& trans)
{
	if(m_attached_items[0])
		m_attached_items[0]->update_hud_additional(trans);

	if(m_attached_items[1])
		m_attached_items[1]->update_hud_additional(trans);
}

void player_hud::update_inertion(Fmatrix& trans)
{
	auto hi = m_attached_items[0] ? m_attached_items[0] : m_attached_items[1];

	if (hi)
	{
		auto& inertion = hi->m_parent_hud_item->CurrentInertionData();

		Fmatrix								xform;
		Fvector& origin						= trans.c;
		xform								= trans;

		static Fvector						st_last_dir={0,0,0};

		// calc difference
		Fvector								diff_dir;
		diff_dir.sub						(xform.k, st_last_dir);

		// clamp by PI_DIV_2
		Fvector last;						last.normalize_safe(st_last_dir);
		float dot							= last.dotproduct(xform.k);
		if (dot<EPS){
			Fvector v0;
			v0.crossproduct					(st_last_dir,xform.k);
			st_last_dir.crossproduct		(xform.k,v0);
			diff_dir.sub					(xform.k, st_last_dir);
		}

		// tend to forward
		st_last_dir.mad(diff_dir, inertion.TendtoSpeed * Device.fTimeDelta);
		origin.mad(diff_dir, inertion.OriginOffset);

		// pitch compensation
		float pitch							= angle_normalize_signed(xform.k.getP());
		origin.mad(xform.k, -pitch * inertion.PitchOffsetD);
		origin.mad(xform.i, -pitch * inertion.PitchOffsetR);
		origin.mad(xform.j, -pitch * inertion.PitchOffsetN);
	}
}


attachable_hud_item* player_hud::create_hud_item(const shared_str& sect)
{
	xr_vector<attachable_hud_item*>::iterator it = m_pool.begin();
	xr_vector<attachable_hud_item*>::iterator it_e = m_pool.end();
	for(;it!=it_e;++it)
	{
		attachable_hud_item* itm = *it;
		if(itm->m_sect_name==sect)
			return itm;
	}
	attachable_hud_item* res	= xr_new<attachable_hud_item>(this);
	res->load					(sect);
	res->m_hand_motions.load	(m_model, sect);
	m_pool.push_back			(res);

	return	res;
}

bool player_hud::allow_activation(CHudItem* item)
{
	if(m_attached_items[1])
		return m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);
	else
		return true;
}

void player_hud::attach_item(CHudItem* item)
{
	attachable_hud_item* pi			= create_hud_item(item->HudSection());
	int item_idx					= pi->m_attach_place_idx;
	
	if(m_attached_items[item_idx] != pi || pi->m_parent_hud_item != item) {
		if(m_attached_items[item_idx])
			m_attached_items[item_idx]->m_parent_hud_item->on_b_hud_detach();

		m_attached_items[item_idx]						= pi;
		pi->m_parent_hud_item							= item;

		if(item_idx==0 && m_attached_items[1])
			m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);

		item->on_a_hud_attach();
	}
	pi->m_parent_hud_item							= item;
}

void player_hud::detach_item_idx(u16 idx)
{
	if( NULL==attached_item(idx) )					return;

	m_attached_items[idx]->m_parent_hud_item->on_b_hud_detach();

	m_attached_items[idx]->m_parent_hud_item		= NULL;
	m_attached_items[idx]							= NULL;

	if(idx==1 && attached_item(0))
	{
		u16 part_idR			= m_model->partitions().part_id("right_hand");
		u32 bc					= m_model->LL_PartBlendsCount(part_idR);
		for(u32 bidx=0; bidx<bc; ++bidx) {
			CBlend* BR = m_model->LL_PartBlend(part_idR, bidx);
			if (!BR) {
				continue;
			}

			MotionID motionId = BR->motionID;
			u16 pc = m_model->partitions().count();
			for (u16 pid = 0; pid < pc; ++pid) {
				if (pid != part_idR) {
					CBlend* B = m_model->PlayCycle(pid, motionId, TRUE); //this can destroy BR calling UpdateTracks !
					if (BR->blend != CBlend::eFREE_SLOT) {
						u16 bop = B->bone_or_part;
						*B = *BR;
						B->bone_or_part = bop;
					}
				}
			}
		}
	} else {
		if (idx == 0 && attached_item(1)) {
			OnMovementChanged(mcAnyMove);
		}
	}
}

void player_hud::detach_item(CHudItem* item)
{
	if( NULL==item->HudItemData() )		return;
	u16 item_idx						= item->HudItemData()->m_attach_place_idx;

	if( m_attached_items[item_idx]==item->HudItemData() )
	{
		detach_item_idx	(item_idx);
	}
}

void player_hud::calc_transform(u16 attach_slot_idx, const Fmatrix& offset, Fmatrix& result)
{
	Fmatrix ancor_m			= m_model->dcast_PKinematics()->LL_GetTransform(m_ancors[attach_slot_idx]);
	result.mul				(m_transform, ancor_m);
	result.mulB_43			(offset);
}

void player_hud::OnMovementChanged(ACTOR_DEFS::EMoveCommand cmd)
{
	if(cmd==0)
	{
		if(m_attached_items[0])
		{
			if(m_attached_items[0]->m_parent_hud_item->GetState()==CHUDState::eIdle)
				m_attached_items[0]->m_parent_hud_item->PlayAnimIdle();
		}
		if(m_attached_items[1])
		{
			if(m_attached_items[1]->m_parent_hud_item->GetState()==CHUDState::eIdle)
				m_attached_items[1]->m_parent_hud_item->PlayAnimIdle();
		}
	}else
	{
		if(m_attached_items[0])
			m_attached_items[0]->m_parent_hud_item->OnMovementChanged(cmd);

		if(m_attached_items[1])
			m_attached_items[1]->m_parent_hud_item->OnMovementChanged(cmd);
	}
}

// ======================================================================================
// GS hud_move hand-offset system (WeaponInertion.pas UpdateWeaponOffset port).
// Each frame the active hud item hands attach (m_measures.m_hands_attach, re-read by
// player_hud::update every frame) is dragged toward "base config attach + per-state offset":
// movement directions, jump/fall/landing/landing2, timed crouch/slow-crouch transitions,
// with a separate reduced hud_aim_move_* family while aiming. Exponential approach is
// integrated at a fixed 8ms step (120Hz) like GS. Lookout/suicide/jitter branches are not
// ported (CS has no lookouts; suicide/jitter are controller features).
// Keys live in the weapon HUD section; all offsets default to zero so weapons without
// them behave as before. NOTE like GS, in 16x9 mode ONLY the _16x9 key is read (no fallback).
#include "Weapon.h"

using namespace ACTOR_DEFS;

namespace
{
	struct SGwrHudMove
	{
		u32			acc;
		u32			to_crouch_t, from_crouch_t, to_slow_t, from_slow_t;
		u32			to_rlook_t, from_rlook_t, to_llook_t, from_llook_t;
		const void*	last_item;
	};
	SGwrHudMove s_hm = {};

	Fvector gwr_read_v3(LPCSTR sect, LPCSTR base, LPCSTR kind, bool w)
	{
		string256 k;
		xr_sprintf(k, "%s%s%s", base, kind, w ? "_16x9" : "");
		if (pSettings->line_exist(sect, k))	return pSettings->r_fvector3(sect, k);
		Fvector z;	z.set(0.f, 0.f, 0.f);	return z;
	}

	void gwr_add_offsets(LPCSTR sect, LPCSTR base, Fvector& pos, Fvector& rot, float koef, bool w)
	{
		Fvector t;
		t = gwr_read_v3(sect, base, "_pos", w);	t.mul(koef);	pos.add(t);
		t = gwr_read_v3(sect, base, "_rot", w);	t.mul(koef);	rot.add(t);
	}

	float gwr_posture_koef(LPCSTR sect, u32 mreal, LPCSTR pfx)	// pfx = "hud_move" / "hud_aim_move"
	{
		// CS movement semantics differ from GS/CoP: CS default WASD walk = !mcAccel, run = mcAccel.
		// GS's actSlow is a SEPARATE careful-walk mode (not normal walking), so mapping !mcAccel to it
		// halved every standing-walk offset (hud_move_slow_factor 0.5) -> "weaker in all directions".
		// Correct mapping: standing walk/run = GS normal (factor 1); the slow factor only applies to the
		// slow-crouch (creep = crouch + !accel). crouch-walk uses crouch_factor.
		bool cr = !!(mreal & mcCrouch);
		bool accel = !!(mreal & mcAccel);
		string128 k;
		if (cr && !accel)	{ xr_sprintf(k, "%s_slow_crouch_factor", pfx);	return READ_IF_EXISTS(pSettings, r_float, sect, k, 1.f); }
		if (cr)				{ xr_sprintf(k, "%s_crouch_factor", pfx);		return READ_IF_EXISTS(pSettings, r_float, sect, k, 1.f); }
		return 1.f;
	}

	// GS GetCurrentTargetOffset_aim: while aiming only the crouch-transition offsets apply (hud_aim_move_*)
	void gwr_target_aim(LPCSTR sect, u32 mreal, Fvector& pos, Fvector& rot, bool w)
	{
		float koef = gwr_posture_koef(sect, mreal, "hud_aim_move");
		if (s_hm.to_crouch_t)	gwr_add_offsets(sect, "hud_aim_move_to_crouch_offset", pos, rot, koef, w);
		if (s_hm.from_crouch_t)	gwr_add_offsets(sect, "hud_aim_move_from_crouch_offset", pos, rot, koef, w);
		if (s_hm.to_slow_t)		gwr_add_offsets(sect, "hud_aim_move_to_slow_crouch_offset", pos, rot, koef, w);
		if (s_hm.from_slow_t)	gwr_add_offsets(sect, "hud_aim_move_from_slow_crouch_offset", pos, rot, koef, w);
		if (s_hm.to_rlook_t)	gwr_add_offsets(sect, "hud_aim_move_to_rlookout_offset", pos, rot, koef, w);
		if (s_hm.from_rlook_t)	gwr_add_offsets(sect, "hud_aim_move_from_rlookout_offset", pos, rot, koef, w);
		if (s_hm.to_llook_t)	gwr_add_offsets(sect, "hud_aim_move_to_llookout_offset", pos, rot, koef, w);
		if (s_hm.from_llook_t)	gwr_add_offsets(sect, "hud_aim_move_from_llookout_offset", pos, rot, koef, w);
	}

	// GS GetCurrentTargetOffset: the full per-state family; any active state resets factor to 1
	void gwr_target(LPCSTR sect, u32 mreal, Fvector& pos, Fvector& rot, float& factor, bool w)
	{
		factor = READ_IF_EXISTS(pSettings, r_float, sect, "hud_move_stabilize_factor", 2.f);
		float koef = gwr_posture_koef(sect, mreal, "hud_move");

		if (s_hm.to_crouch_t)	{ gwr_add_offsets(sect, "hud_move_to_crouch_offset", pos, rot, koef, w);		factor = 1.f; }
		if (s_hm.from_crouch_t)	{ gwr_add_offsets(sect, "hud_move_from_crouch_offset", pos, rot, koef, w);		factor = 1.f; }
		if (s_hm.to_slow_t)		{ gwr_add_offsets(sect, "hud_move_to_slow_crouch_offset", pos, rot, koef, w);	factor = 1.f; }
		if (s_hm.from_slow_t)	{ gwr_add_offsets(sect, "hud_move_from_slow_crouch_offset", pos, rot, koef, w);	factor = 1.f; }
		if (s_hm.to_rlook_t)	{ gwr_add_offsets(sect, "hud_move_to_rlookout_offset", pos, rot, koef, w);		factor = 1.f; }
		if (s_hm.from_rlook_t)	{ gwr_add_offsets(sect, "hud_move_from_rlookout_offset", pos, rot, koef, w);	factor = 1.f; }
		if (s_hm.to_llook_t)	{ gwr_add_offsets(sect, "hud_move_to_llookout_offset", pos, rot, koef, w);		factor = 1.f; }
		if (s_hm.from_llook_t)	{ gwr_add_offsets(sect, "hud_move_from_llookout_offset", pos, rot, koef, w);	factor = 1.f; }

		// held lean offsets carry their own approach-speed factor (GS *_offset_speed_factor)
		bool RL = !!(mreal & mcRLookout), LL = !!(mreal & mcLLookout);
		if (RL && !LL)
		{
			gwr_add_offsets(sect, "hud_move_rlookout_offset", pos, rot, koef, w);
			factor = READ_IF_EXISTS(pSettings, r_float, sect, "hud_move_rlookout_offset_speed_factor", 1.f);
		}
		if (LL && !RL)
		{
			gwr_add_offsets(sect, "hud_move_llookout_offset", pos, rot, koef, w);
			factor = READ_IF_EXISTS(pSettings, r_float, sect, "hud_move_llookout_offset_speed_factor", 1.f);
		}

		bool L = !!(mreal & mcLStrafe), R = !!(mreal & mcRStrafe);
		bool F = !!(mreal & mcFwd), B = !!(mreal & mcBack);
		if (L && !R)	{ gwr_add_offsets(sect, "hud_move_left_offset", pos, rot, koef, w);		factor = 1.f; }
		if (R && !L)	{ gwr_add_offsets(sect, "hud_move_right_offset", pos, rot, koef, w);	factor = 1.f; }
		if (F && !B)	{ gwr_add_offsets(sect, "hud_move_forward_offset", pos, rot, koef, w);	factor = 1.f; }
		if (B && !F)	{ gwr_add_offsets(sect, "hud_move_back_offset", pos, rot, koef, w);		factor = 1.f; }

		bool J = !!(mreal & mcJump), FL = !!(mreal & mcFall), L1 = !!(mreal & mcLanding), L2 = !!(mreal & mcLanding2);
		if (J && !FL && !L1 && !L2)		{ gwr_add_offsets(sect, "hud_move_jump_offset", pos, rot, koef, w);		factor = 1.f; }
		if (FL && !J && !L1 && !L2)		{ gwr_add_offsets(sect, "hud_move_fall_offset", pos, rot, koef, w);		factor = 1.f; }
		if (L1 && !J && !FL && !L2)		{ gwr_add_offsets(sect, "hud_move_landing_offset", pos, rot, koef, w);	factor = 1.f; }
		if (L2 && !J && !FL && !L1)		{ gwr_add_offsets(sect, "hud_move_landing2_offset", pos, rot, koef, w);	factor = 1.f; }
	}
}

void gwr_UpdateHudMove(u32 mreal, u32 mwish, u32 dt)
{
	if (!g_player_hud || !dt)	return;
	attachable_hud_item* hi		= g_player_hud->attached_item(0);
	attachable_hud_item* det	= g_player_hud->attached_item(1);
	if (!hi)	{ hi = det; det = NULL; }
	if (!hi || !hi->m_parent_hud_item)	return;

	// reset the transition state when the item in hand changes
	if (s_hm.last_item != (const void*)hi)
	{
		s_hm.last_item	= hi;
		s_hm.acc		= 0;
		s_hm.to_crouch_t = s_hm.from_crouch_t = s_hm.to_slow_t = s_hm.from_slow_t = 0;
	}

	LPCSTR sect	= hi->m_sect_name.c_str();
	bool w		= UI()->is_widescreen();

	// crouch / slow-crouch transition triggers: WISHFUL vs REAL bit edges (GS mState_WISHFUL checks)
	bool cr_w = !!(mwish & mcCrouch), cr_r = !!(mreal & mcCrouch);
	bool sl_w = !(mwish & mcAccel),   sl_r = !(mreal & mcAccel);
	if (cr_w && !cr_r)
	{
		s_hm.to_crouch_t	= u32(READ_IF_EXISTS(pSettings, r_float, sect, "to_crouch_time", 0.f) * 1000.f);
		s_hm.from_crouch_t	= 0;
	}
	else if (!cr_w && cr_r)
	{
		s_hm.from_crouch_t	= u32(READ_IF_EXISTS(pSettings, r_float, sect, "from_crouch_time", 0.f) * 1000.f);
		s_hm.to_crouch_t	= 0;
	}
	if (cr_w && sl_w && !sl_r)
	{
		s_hm.to_slow_t		= u32(READ_IF_EXISTS(pSettings, r_float, sect, "to_slow_crouch_time", 0.f) * 1000.f);
		s_hm.from_slow_t	= 0;
	}
	else if (cr_w && !sl_w && sl_r)
	{
		s_hm.from_slow_t	= u32(READ_IF_EXISTS(pSettings, r_float, sect, "from_slow_crouch_time", 0.f) * 1000.f);
		s_hm.to_slow_t		= 0;
	}

	// lean transitions (GS skips them when both leans are somehow REAL at once)
	if (!((mreal & mcRLookout) && (mreal & mcLLookout)))
	{
		bool rl_w = !!(mwish & mcRLookout), rl_r = !!(mreal & mcRLookout);
		bool ll_w = !!(mwish & mcLLookout), ll_r = !!(mreal & mcLLookout);
		if (rl_w && !rl_r)		{ s_hm.to_rlook_t = u32(READ_IF_EXISTS(pSettings, r_float, sect, "to_rlookout_time", 0.f) * 1000.f);   s_hm.from_rlook_t = 0; }
		else if (!rl_w && rl_r)	{ s_hm.from_rlook_t = u32(READ_IF_EXISTS(pSettings, r_float, sect, "from_rlookout_time", 0.f) * 1000.f); s_hm.to_rlook_t = 0; }
		if (ll_w && !ll_r)		{ s_hm.to_llook_t = u32(READ_IF_EXISTS(pSettings, r_float, sect, "to_llookout_time", 0.f) * 1000.f);   s_hm.from_llook_t = 0; }
		else if (!ll_w && ll_r)	{ s_hm.from_llook_t = u32(READ_IF_EXISTS(pSettings, r_float, sect, "from_llookout_time", 0.f) * 1000.f); s_hm.to_llook_t = 0; }
	}

	// base attach = the CONFIG values (the live m_hands_attach is our animated state)
	LPCSTR pk = w ? "hands_position_16x9" : "hands_position";
	LPCSTR rk = w ? "hands_orientation_16x9" : "hands_orientation";
	if (!pSettings->line_exist(sect, pk) || !pSettings->line_exist(sect, rk))	return;
	Fvector base_pos = pSettings->r_fvector3(sect, pk);
	Fvector base_rot = pSettings->r_fvector3(sect, rk);

	Fvector tpos, trot;	tpos.set(0.f, 0.f, 0.f);	trot.set(0.f, 0.f, 0.f);
	float factor = 1.f;

	CHudItem* hitm	= hi->m_parent_hud_item;
	bool hiding		= (hitm->GetState() == CHUDState::eHiding) ||
					  (det && det->m_parent_hud_item && det->m_parent_hud_item->GetState() == CHUDState::eHiding);
	CWeapon* wpn	= smart_cast<CWeapon*>(hitm);
	// threshold, not >0: a zoom rotation factor stuck at a tiny residue after unzoom would otherwise
	// keep us in the aim branch forever -- where the walk/strafe offsets never apply
	bool aiming		= wpn && (wpn->IsZoomed() || wpn->GetZoomRotationFactor() > 0.01f);

	if (hiding)
		factor = READ_IF_EXISTS(pSettings, r_float, sect, "hud_move_weaponhide_factor", 1.f);
	else if (aiming)
	{
		gwr_target_aim(sect, mreal, tpos, trot, w);
		factor = READ_IF_EXISTS(pSettings, r_float, sect, "hud_move_unzoom_factor", 1.f);
	}
	else
		gwr_target(sect, mreal, tpos, trot, factor, w);

	// GS AddSuicideOffset (WeaponInertion.pas): a controller-suicide weapon with NO suicide animation
	// (RPG-7, RG-6, a launcher in GL mode) is aimed at the head by this offset alone. `no_other_hud_
	// moving_while_suicide` throws the walk/lean offsets away first, so only the suicide pose remains.
	CActor* c_act = smart_cast<CActor*>(Level().CurrentControlEntity());
	// scene running -> travel at the suicide pace (GS :635); seen by a controller -> actually aim at
	// the head (GS :623). Splitting the two is what makes "hide from it and the launcher comes back
	// down" work without a snap.
	const bool suicide_scene = c_act && c_act->SuicideHudOffsetActive() &&
							   !READ_IF_EXISTS(pSettings, r_bool, sect, "prohibit_suicide", FALSE);
	const bool suicide_hud   = suicide_scene && c_act->SuicideHudAimActive();
	if (suicide_hud)
	{
		if (READ_IF_EXISTS(pSettings, r_bool, sect, "no_other_hud_moving_while_suicide", FALSE))
			{ tpos.set(0.f, 0.f, 0.f);	trot.set(0.f, 0.f, 0.f); }
		LPCSTR spk = w ? "hud_move_suicide_offset_pos_16x9" : "hud_move_suicide_offset_pos";
		LPCSTR srk = w ? "hud_move_suicide_offset_rot_16x9" : "hud_move_suicide_offset_rot";
		// hands_attach_rot is kept in DEGREES here (attachable_hud_item::update multiplies by PI/180 at
		// use), so the offset goes in raw, exactly like every other hud_move offset -- converting it to
		// radians shrank GS's 130 degrees to 2 and left only the position part visible.
		if (pSettings->line_exist(sect, spk))	tpos.add(pSettings->r_fvector3(sect, spk));
		if (pSettings->line_exist(sect, srk))	trot.add(pSettings->r_fvector3(sect, srk));
	}

	tpos.add(base_pos);
	trot.add(base_rot);

	float sp_rot = READ_IF_EXISTS(pSettings, r_float, sect, "hud_move_speed_rot", 0.4f) * factor / 100.f;
	float sp_pos = READ_IF_EXISTS(pSettings, r_float, sect, "hud_move_speed_pos", 0.1f) * factor / 100.f;
	// GS uses its own speeds for the suicide move, and they are a CONSTANT step per tick (v_setlength),
	// not the usual proportional easing -- the weapon travels to the head at a steady pace.
	const float su_rot = READ_IF_EXISTS(pSettings, r_float, sect, "suicide_speed_rot", 0.0901f);
	const float su_pos = READ_IF_EXISTS(pSettings, r_float, sect, "suicide_speed_pos", 0.00205f);

	s_hm.acc += dt;
	if (s_hm.acc > 200)	s_hm.acc = 200;	// pause/load safety
	Fvector cur_pos = hi->hands_attach_pos();
	Fvector cur_rot = hi->hands_attach_rot();
	while (s_hm.acc > 8)
	{
		Fvector d;
		if (suicide_scene)			// pace follows the SCENE, not the line of sight
		{
			d.sub(tpos, cur_pos);	if (d.magnitude() > su_pos) d.set_length(su_pos);	cur_pos.add(d);
			d.sub(trot, cur_rot);	if (d.magnitude() > su_rot) d.set_length(su_rot);	cur_rot.add(d);
		}
		else
		{
			d.sub(tpos, cur_pos);	if (d.magnitude() > 0.0001f) d.mul(sp_pos);	cur_pos.add(d);
			d.sub(trot, cur_rot);	if (d.magnitude() > 0.0001f) d.mul(sp_rot);	cur_rot.add(d);
		}
		s_hm.acc -= 8;
	}
	// GS hands jitter (SetHandsJitterTime): while the shock timer runs, the hands shake -- a
	// per-frame random offset on top of everything else. Amplitudes come from the weapon hud
	// (jitter_pos_amplitude / jitter_rot_amplitude), falling back to [gunslinger_base]'s
	// base_jitter_*. Used after a controller lets go and while a psi block holds it off.
	{
		CActor* act = smart_cast<CActor*>(Level().CurrentControlEntity());
		if (act && act->HandsJitterActive())
		{
			// GS GetHandJitterScale: full while the controller holds you, then fading over jitter_stop_time
			const float stop_ms = READ_IF_EXISTS(pSettings, r_float, sect, "jitter_stop_time", 3.f) * 1000.f;
			const float k  = act->HandsJitterScale(stop_ms);
			const float ap = READ_IF_EXISTS(pSettings, r_float, sect, "jitter_pos_amplitude",
					READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", "base_jitter_pos_amplitude", 0.001f)) * k;
			const float ar = READ_IF_EXISTS(pSettings, r_float, sect, "jitter_rot_amplitude",
					READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", "base_jitter_rot_amplitude", 0.1f)) * k;
			// same unit rule as the offsets above: this vector is degrees, add the amplitude raw
			cur_pos.x += ::Random.randF(-ap, ap);	cur_pos.y += ::Random.randF(-ap, ap);	cur_pos.z += ::Random.randF(-ap, ap);
			cur_rot.x += ::Random.randF(-ar, ar);
			cur_rot.y += ::Random.randF(-ar, ar);
			cur_rot.z += ::Random.randF(-ar, ar);
		}
	}

	hi->hands_attach_pos().set(cur_pos);
	hi->hands_attach_rot().set(cur_rot);

	// GS: the shot goes off when the hands have ARRIVED at the suicide pose -- what is left of the
	// distance has to fit inside twice the jitter amplitude, i.e. the hands are only shaking now.
	if (suicide_hud)
	{
		const float ap = READ_IF_EXISTS(pSettings, r_float, sect, "jitter_pos_amplitude",
				READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", "base_jitter_pos_amplitude", 0.002f));
		const float ar = READ_IF_EXISTS(pSettings, r_float, sect, "jitter_rot_amplitude",
				READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", "base_jitter_rot_amplitude", 0.09f));
		Fvector dp, dr;
		dp.sub(cur_pos, tpos);
		dr.sub(cur_rot, trot);
		{
			extern int g_ctrl_dbg;
			static u32 s_last = 0;
			if (g_ctrl_dbg && Device.dwTimeGlobal - s_last > 500)
			{
				s_last = Device.dwTimeGlobal;
				Msg("~ctrl POSE: sect=%s cur=(%.2f %.2f %.2f) tgt=(%.2f %.2f %.2f) dr=%.3f dp=%.4f thr_r=%.3f",
					sect, cur_rot.x, cur_rot.y, cur_rot.z, trot.x, trot.y, trot.z,
					dr.magnitude(), dp.magnitude(), ar * 2.f);
			}
		}
		if (dp.magnitude() < ap * 2.f && dr.magnitude() < ar * 2.f)
			c_act->SuicideHudOffsetArrived();
	}

	if (s_hm.to_crouch_t > dt)		s_hm.to_crouch_t -= dt;		else s_hm.to_crouch_t = 0;
	if (s_hm.from_crouch_t > dt)	s_hm.from_crouch_t -= dt;	else s_hm.from_crouch_t = 0;
	if (s_hm.to_slow_t > dt)		s_hm.to_slow_t -= dt;		else s_hm.to_slow_t = 0;
	if (s_hm.from_slow_t > dt)		s_hm.from_slow_t -= dt;		else s_hm.from_slow_t = 0;
	if (s_hm.to_rlook_t > dt)		s_hm.to_rlook_t -= dt;		else s_hm.to_rlook_t = 0;
	if (s_hm.from_rlook_t > dt)		s_hm.from_rlook_t -= dt;	else s_hm.from_rlook_t = 0;
	if (s_hm.to_llook_t > dt)		s_hm.to_llook_t -= dt;		else s_hm.to_llook_t = 0;
	if (s_hm.from_llook_t > dt)		s_hm.from_llook_t -= dt;	else s_hm.from_llook_t = 0;
}
