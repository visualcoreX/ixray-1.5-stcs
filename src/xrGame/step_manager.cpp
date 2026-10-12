#include "stdafx.h"
#include "step_manager.h"
#include "entity_alive.h"
#include "../Include/xrRender/Kinematics.h"
#include "level.h"
#include "gamepersistent.h"
#include "material_manager.h"
#include "profiler.h"
#include "IKLimbsController.h"
#include "Actor.h"
#include "CustomOutfit.h"
#include "../xrSound/Sound.h"
#ifdef	DEBUG
BOOL debug_step_info = FALSE;
BOOL debug_step_info_load = FALSE;
#endif
CStepManager::CStepManager() : m_time_anim_started(0), m_anim_phase(0.f), m_phase_time(0), m_phase_blend(0), m_true_phase(false)
{
}

CStepManager::~CStepManager()
{
}

DLL_Pure *CStepManager::_construct	()
{
	m_object			= smart_cast<CEntityAlive*>(this);
	VERIFY				(m_object);
	return				(m_object);
}

void CStepManager::reload(LPCSTR section)
{
	// NOTE: the map is deliberately NOT cleared here. reload() runs on every visual change and the
	// same animation resolves to a DIFFERENT (slot, idx) depending on what is loaded at that
	// moment, so the entries of previously seen visuals are what keep the lookup hitting.
	// Clearing it makes the footsteps go silent. See also CActor::OnChangeVisual: this reload
	// must stay AFTER LL_AddMotions / m_anims->Create, or it resolves against the wrong slot.
	m_legs_count		= pSettings->r_u8		(section, "LegsCount");
	m_true_phase		= !!READ_IF_EXISTS(pSettings, r_bool, section, "step_phase_from_anim", FALSE);
	m_phase_blend		= 0;
	LPCSTR anim_section = pSettings->r_string	(section, "step_params");

	if (!pSettings->section_exist(anim_section))
	{
#ifdef	DEBUG
		Msg( "! no step_params section for :%s section :s", m_object->cName().c_str(), section );
#endif
		return;
	}
	VERIFY((m_legs_count>=MIN_LEGS_COUNT) && (m_legs_count<=MAX_LEGS_COUNT));

	SStepParam			param; 
	param.step[0].time = 0.1f;	// avoid warning

	LPCSTR				anim_name, val;
	string16			cur_elem;

	IKinematicsAnimated	*skeleton_animated = smart_cast<IKinematicsAnimated*>(m_object->Visual());

#ifdef	DEBUG
		if( debug_step_info_load )
			Msg( "loading step_params for object :%s, visual: %s, section: %s, step_params section: %s  ", m_object->cName().c_str(), m_object->cNameVisual().c_str(), section, anim_section );
#endif

	for (u32 i=0; pSettings->r_line(anim_section,i,&anim_name,&val); ++i) {
		_GetItem (val,0,cur_elem);

		param.cycles = u8(atoi(cur_elem));
		R_ASSERT(param.cycles >= 1);

		for (u32 j=0;j<m_legs_count;j++) {
			_GetItem	(val,1+j*2,		cur_elem);		param.step[j].time	= float(atof(cur_elem));
			_GetItem	(val,1+j*2+1,	cur_elem);		param.step[j].power	= float(atof(cur_elem));
			VERIFY		(_valid(param.step[j].power));			
		}
		
		MotionID motion_id = skeleton_animated->ID_Cycle_Safe(anim_name);
		if (!motion_id) 
		{
#ifdef	DEBUG

			IKinematicsAnimated *KA = smart_cast<IKinematicsAnimated*>(m_object->Visual());
			VERIFY( KA );
			
			Msg( "! (CStepManager::reload) no anim :%s object:%s, visual: %s, step_params section: %s ", anim_name, m_object->cName().c_str(), m_object->cNameVisual().c_str(), anim_section );

#endif		
			continue;
		}
#ifdef	DEBUG
		if( debug_step_info_load )
		{
			IKinematicsAnimated *KA = smart_cast<IKinematicsAnimated*>(m_object->Visual());
			VERIFY( KA );
			std::pair<LPCSTR,LPCSTR> anim_name_ = KA->LL_MotionDefName_dbg( motion_id );
			Msg( "step_params loaded for object :%s, visual: %s, motion: %s, anim set: %s  ", m_object->cName().c_str(), m_object->cNameVisual().c_str(), anim_name_.first, anim_name_.second );
		}
#endif
		m_steps_map.insert(std::make_pair(motion_id, param));
	}

#ifdef	DEBUG
	if( m_steps_map.empty() )
		Msg( "! no steps info loaded for :%s, section :s, step_params section: %s ", m_object->cName().c_str(), section, anim_section );
#endif
	// reload foot bones
	for (u32 i = 0; i < MAX_LEGS_COUNT; i++) m_foot_bones[i] = BI_NONE;
	reload_foot_bones	();

	
	m_time_anim_started	= 0;
	m_anim_phase		= 0.f;
	m_phase_time		= 0;
	m_blend				= 0;
}

void CStepManager::on_animation_start(MotionID motion_id, CBlend *blend)
{
	m_blend	= blend;
	if (!m_blend) return;

	if(m_object->character_ik_controller	())
		m_object->character_ik_controller	()->PlayLegs(blend);

	m_time_anim_started = Device.dwTimeGlobal; 
	m_anim_phase		= 0.f;
	m_phase_time		= Device.dwTimeGlobal;
	
	// ������ ������� �������� � STEPS_MAP
	STEPS_MAP_IT it = m_steps_map.find(motion_id);
	if (it == m_steps_map.end()) {
#ifdef	DEBUG
		if( debug_step_info )
		{
			IKinematicsAnimated *KA = smart_cast<IKinematicsAnimated*>(m_object->Visual());
			VERIFY( KA );
			std::pair<LPCSTR,LPCSTR> anim_name = KA->LL_MotionDefName_dbg( motion_id );
			Msg( "! no step_params found for object :%s, visual: %s, motion: %s, anim set: %s  ", m_object->cName().c_str(), m_object->cNameVisual().c_str(), anim_name.first, anim_name.second );
		}
#endif
		m_step_info.disable = true;
		return;
	}

	m_step_info.disable		= false;
	m_step_info.params		= it->second;
	m_step_info.cur_cycle	= 1;					// all cycles are 1-based

	for (u32 i=0; i<m_legs_count; i++) {
		m_step_info.activity[i].handled	= false;
		m_step_info.activity[i].cycle	= m_step_info.cur_cycle;
	}


	VERIFY					(m_blend);
}


// One step of one foot: the material sound, dust off the ground and the camera's step (CActor::event_on_step).
void CStepManager::do_step(u32 i, float power, SGameMtlPair* mtl_pair)
{
	if (is_on_ground())
		m_step_sound.play_next( mtl_pair, m_object, power );
	// ������ ��������
	if (!mtl_pair->CollideParticles.empty())	{
		LPCSTR ps_name = *mtl_pair->CollideParticles[::Random.randI(0,mtl_pair->CollideParticles.size())];

		//�������� �������� ������������ ����������
		CParticlesObject* ps = CParticlesObject::Create(ps_name,TRUE);

		// ��������� ������� � �������������� ��������
		Fmatrix pos; 

		// ���������� �����������
		pos.k.set(Fvector().set(0.0f,1.0f,0.0f));
		Fvector::generate_orthonormal_basis(pos.k, pos.j, pos.i);

		// ���������� �������
		pos.c.set(get_foot_position(ELegType(i)));

		ps->UpdateParent(pos,Fvector().set(0.f,0.f,0.f));
		GamePersistent().ps_needtoplay.push_back(ps);
	}

	// Play Camera FXs
	event_on_step();
}

void CStepManager::update()
{
	START_PROFILE("Step Manager")

	if (m_step_info.disable)	return;
	if (!m_blend)				return;


	// �������� ��������� ����
	SStepParam	&step		= m_step_info.params;
	u32		cur_time		= Device.dwTimeGlobal;

	// ����� ������ ����� ��������
	float cycle_anim_time	= get_blend_time() / step.cycles;

	// The loop position is ACCUMULATED: each frame adds its own dt at the speed the blend has in that
	// frame. It used to be "time since the loop started / loop length at the CURRENT speed", which
	// re-read the whole elapsed time at whatever speed this frame had -- and the actor's leg speed is
	// set every frame from the real velocity (CActor::UpdateCL). Wherever that velocity jitters (water,
	// rough ground) every upward spike jumped the phase forward, closed the loop early and started the
	// next one: the footsteps came faster than the legs actually moved.
	if (m_true_phase && m_blend->timeTotal > EPS_S)
	{
		// The actor: where the leg motion itself stands -- exactly what the legs show. A new blend starts
		// with the steps whose moment it is already past counted as made (no burst on a change of gait);
		// going round the end of the loop starts the next one.
		const float phase	= _min(1.f, _max(0.f, m_blend->timeCurrent / m_blend->timeTotal));
		if (m_phase_blend != m_blend)
		{
			m_phase_blend			= m_blend;
			m_step_info.cur_cycle	= u8(_min(float(step.cycles), 1.f + phase * float(step.cycles)));
			for (u32 i = 0; i < m_legs_count; i++)
			{
				const float sp	= (float(m_step_info.cur_cycle - 1) + step.step[i].time) / float(step.cycles);
				m_step_info.activity[i].handled	= (sp <= phase);
				m_step_info.activity[i].cycle	= m_step_info.cur_cycle;
			}
		}
		else if (!m_blend->stop_at_end && phase < m_anim_phase - 0.5f)
		{
			m_step_info.cur_cycle	= 1;
			for (u32 i = 0; i < m_legs_count; i++)
			{
				m_step_info.activity[i].handled	= false;
				m_step_info.activity[i].cycle	= m_step_info.cur_cycle;
			}
		}
		m_anim_phase		= phase;
		m_phase_time		= cur_time;
	}
	else
	{
		const float blend_time	= get_blend_time();
		const u32 dt				= cur_time - m_phase_time;
		m_phase_time				= cur_time;
		if (blend_time > EPS_S)	m_anim_phase += (0.001f * float(dt)) / blend_time;
		if (m_blend->stop_at_end && m_anim_phase > 1.f)	m_anim_phase = 1.f;
	}

	// after the phase has moved: a frame with no material (airborne) must not freeze the loop
	SGameMtlPair* mtl_pair		= m_object->material().get_current_pair();
	if (!mtl_pair)				return;

	// ������ �� ���� ����� � ��������� �����
	for (u32 i=0; i<m_legs_count; i++) {

		// ���� ������� ��� ���������� ��� ���� ����, �� skip
		if (m_step_info.activity[i].handled && (m_step_info.activity[i].cycle == m_step_info.cur_cycle)) continue;

		// ��������� ��������� ����� ���� � ������������ � ����������� �������� ������
		const float step_phase = (float(m_step_info.cur_cycle-1) + step.step[i].time) / float(step.cycles);
		if (step_phase <= m_anim_phase){
			do_step(i, m_step_info.params.step[i].power, mtl_pair);

			// �������� ���� handle
			m_step_info.activity[i].handled	= true;
			m_step_info.activity[i].cycle	= m_step_info.cur_cycle;
		}
	}

	// ���������� ������� ����
	if (m_step_info.cur_cycle < step.cycles) m_step_info.cur_cycle = u8(_min(float(step.cycles), 1.f + _max(0.f, m_anim_phase) * float(step.cycles)));

	// ���� �������� �����������...
	(void)cycle_anim_time;		// ����� ���������� ������ ��������
	if (!m_blend->stop_at_end && (m_anim_phase >= 1.f)) {
		
		m_anim_phase			-= floorf(m_anim_phase);
		m_time_anim_started		= cur_time;
		m_step_info.cur_cycle	= 1;

		for (u32 i=0; i<m_legs_count; i++) {
			m_step_info.activity[i].handled	= false;
			m_step_info.activity[i].cycle	= m_step_info.cur_cycle;
		}
	}
	STOP_PROFILE
}

//////////////////////////////////////////////////////////////////////////
// Function for foot processing
//////////////////////////////////////////////////////////////////////////
Fvector	CStepManager::get_foot_position(ELegType leg_type)
{
	R_ASSERT2(m_foot_bones[leg_type] != BI_NONE, "foot bone had not been set");

	IKinematics *pK					= smart_cast<IKinematics*>(m_object->Visual());
	const Fmatrix& bone_transform = pK->LL_GetBoneInstance(m_foot_bones[leg_type]).mTransform;	

	Fmatrix					global_transform;
	global_transform.mul_43	(m_object->XFORM(),bone_transform);

	return global_transform.c;
}

void CStepManager::load_foot_bones	(CInifile::Sect &data)
{
	for (CInifile::SectCIt I=data.Data.begin(); I!=data.Data.end(); ++I){
		const CInifile::Item& item	= *I;

		u16 index = smart_cast<IKinematics*>(m_object->Visual())->LL_BoneID(*item.second);
		VERIFY3(index != BI_NONE, "foot bone not found", *item.second);

		if (xr_strcmp(*item.first, "front_left") == 0) 			m_foot_bones[eFrontLeft]	= index;
		else if (xr_strcmp(*item.first, "front_right")== 0)		m_foot_bones[eFrontRight]	= index;
		else if (xr_strcmp(*item.first, "back_right")== 0)		m_foot_bones[eBackRight]	= index;
		else if (xr_strcmp(*item.first, "back_left")== 0)		m_foot_bones[eBackLeft]		= index;
	}
}

void CStepManager::reload_foot_bones()
{
	CInifile* ini = smart_cast<IKinematics*>(m_object->Visual())->LL_UserData();
	if(ini&&ini->section_exist("foot_bones")){
		load_foot_bones(ini->r_section("foot_bones"));
	}
	else {
		if (!pSettings->line_exist(*m_object->cNameSect(),"foot_bones"))
			R_ASSERT2(false,"section [foot_bones] not found in monster user_data");
		load_foot_bones(pSettings->r_section(pSettings->r_string(*m_object->cNameSect(),"foot_bones")));
	}

	// �������� �� �����������
	int count = 0;
	for (u32 i = 0; i < MAX_LEGS_COUNT; i++) 
		if (m_foot_bones[i] != BI_NONE) count++;

	VERIFY(count == m_legs_count);
}

float CStepManager::get_blend_time()
{
	return 	(m_blend->timeTotal / m_blend->speed);
}


// GS gunsl_exo.on_step_sound: while the ACTOR walks in an exoskeleton, play a random servo step sound on top
// of the material footstep -- 2D, volume rising as the exo wears down (GS 1.0 at full cond -> 2.0 when nearly
// broken). The outfit lists its clips in `exo_step_sounds` (CSV); no key = not an exo, nothing plays.
static void gwr_play_exo_step( CEntityAlive* object )
{
	CActor* act = smart_cast<CActor*>( object );
	if (!act)		return;						// actor only (NPC exos not handled here)
	CCustomOutfit* o = act->GetOutfit();
	if (!o)			return;
	const shared_str sect = o->cNameSect();
	if (!pSettings->line_exist( sect, "exo_step_sounds" ))	return;

	// load the servo clips once, re-load if the worn outfit section changes
	static xr_vector<ref_sound>	s_snds;
	static shared_str			s_sect;
	if (s_sect != sect)
	{
		for (ref_sound& s : s_snds)	s.destroy();
		s_snds.clear();
		string256 name;	LPCSTR p = pSettings->r_string( sect, "exo_step_sounds" );
		while (*p)
		{
			while (*p == ' ' || *p == ',')	++p;
			LPCSTR b = p;	while (*p && *p != ',')	++p;
			u32 n = (u32)(p - b);	while (n && b[n-1] == ' ')	--n;
			if (n && n < sizeof(name))
			{
				strncpy_s( name, sizeof(name), b, n );	name[n] = 0;
				ref_sound s;	::Sound->create( s, name, st_Effect, SOUND_TYPE_ITEM_USING );
				s_snds.push_back( s );
			}
		}
		s_sect = sect;
	}
	if (s_snds.empty())	return;

	const float cond = o->GetCondition();
	const float minv = READ_IF_EXISTS( pSettings, r_float, sect, "exo_step_vol_min", 1.0f );
	const float maxv = READ_IF_EXISTS( pSettings, r_float, sect, "exo_step_vol_max", 2.0f );
	const float hi = 0.7f, lo = 0.1f;			// GS: full-cond .. nearly-broken
	float vol = (cond > hi) ? minv : (cond < lo) ? maxv : (minv + (maxv - minv) * (hi - cond) / (hi - lo));

	Fvector z = { 0.f, 0.f, 0.f };
	s_snds[ Random.randI( s_snds.size() ) ].play_no_feedback( object, sm_2D, 0.f, &z, &vol );
}

void CStepManager::material_sound::play_next( SGameMtlPair* mtl_pair, CEntityAlive	*object, float volume  )
{
	gwr_play_exo_step( object );				// exo servo step on top of the material footstep (fires every step)

	// hud_step_sound_vol_k (0.21): quiet the actor's own footsteps so the exo servo (above) is audible at a
	// fast pace -- but ONLY while an EXOSKELETON is worn (an outfit that lists exo_step_sounds). Without an
	// exo the footsteps keep full volume. NPC footsteps are unaffected.
	if (CActor* act = smart_cast<CActor*>( object ))
	{
		CCustomOutfit* o = act->GetOutfit();
		if (o && pSettings->line_exist( o->cNameSect(), "exo_step_sounds" ))
			volume *= READ_IF_EXISTS( pSettings, r_float, object->cNameSect(), "hud_step_sound_vol_k", 0.21f );
	}

	if (mtl_pair->StepSounds.empty() )
		return;
	Fvector sound_pos = object->Position();
	sound_pos.y += 0.5;

	if( last_mtl_pair!= mtl_pair || m_last_step_sound_played == u8(-1) )
	{
		m_last_step_sound_played = u8( Random.randI(mtl_pair->StepSounds.size()) );
		last_mtl_pair = mtl_pair; 
	} else {
		
		u8 new_played = u8 ( ( m_last_step_sound_played + 1 +  Random.randI(mtl_pair->StepSounds.size()-1) ) % mtl_pair->StepSounds.size() );
	
		m_last_step_sound_played = new_played;
	}

	mtl_pair->StepSounds[m_last_step_sound_played].play_no_feedback(object,0,0,&sound_pos, &volume );
}