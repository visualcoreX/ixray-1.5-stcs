#pragma once
#include "step_manager_defs.h"

class CEntityAlive;
class CBlend;
struct SGameMtlPair;
class CStepManager {
	u8				m_legs_count;

	STEPS_MAP		m_steps_map;
	SStepInfo		m_step_info;

	CEntityAlive	*m_object;

	u16				m_foot_bones[MAX_LEGS_COUNT];
	CBlend			*m_blend;
	struct material_sound
	{
		u8				m_last_step_sound_played;
		SGameMtlPair*	last_mtl_pair;
		material_sound(): m_last_step_sound_played(u8(-1)), last_mtl_pair(0){}
		void play_next( SGameMtlPair* mtl_pair, CEntityAlive	*object, float volume );
	}				m_step_sound;
	u32				m_time_anim_started;
	// Where in the leg loop we are (0..1 of the whole animation), accumulated frame by frame at the
	// speed the blend had in that frame -- see CStepManager::update.
	float			m_anim_phase;
	u32				m_phase_time;

public: 
						CStepManager			();
	virtual				~CStepManager			();

	// init on construction
	virtual DLL_Pure	*_construct				();
	virtual	void		reload					(LPCSTR section);
	
	// call on set animation
			void		on_animation_start		(MotionID motion_id, CBlend *blend);
	// call on updateCL
			void		update					();
	
	// process event
	virtual	void		event_on_step			() {}	

protected:
			Fvector		get_foot_position		(ELegType leg_type);
	virtual bool		is_on_ground			()						{return true;}
private:
			void		reload_foot_bones		();
			void		load_foot_bones			(CInifile::Sect &data);
			
			float		get_blend_time			();
};
