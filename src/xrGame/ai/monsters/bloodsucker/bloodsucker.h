#pragma once

#include "../BaseMonster/base_monster.h"
#include "../ai_monster_bones.h"
#include "../controlled_entity.h"
#include "../controlled_actor.h"
#include "../anim_triple.h"
#include "../../../../xrServerEntities/script_export_space.h"
#include "bloodsucker_alien.h"


class CAI_Bloodsucker : public CBaseMonster, 
						public CControlledActor {

	typedef		CBaseMonster	inherited;
	
public:
							CAI_Bloodsucker	();
	virtual					~CAI_Bloodsucker();	

	virtual void			reinit					();
	virtual	void			reload					(LPCSTR section);

	virtual void			UpdateCL				();
	virtual void			shedule_Update			(u32 dt);
	virtual void			Die						(CObject* who);
	virtual BOOL			net_Spawn				(CSE_Abstract* DC);
	virtual	void			Load					(LPCSTR section);
	virtual	void			Hit						(SHit* pHDS);

	virtual	void			CheckSpecParams			(u32 spec_params);
	virtual bool			ability_invisibility	() {return true;}
	virtual bool			ability_pitch_correction() {return false;}
	virtual bool			ability_can_drag		() {return true;}

	virtual	void			post_fsm_update			();

	
	virtual bool			use_center_to_aim		() const {return true;}
	virtual bool			check_start_conditions	(ControlCom::EControlType);
	virtual void			HitEntity				(const CEntity *pEntity, float fDamage, float impulse, Fvector &dir);

	virtual void			renderable_Render		();
	virtual BOOL			AlwaysTheCrow			();
	
	//--------------------------------------------------------------------
	// Utils
	//--------------------------------------------------------------------
			void			move_actor_cam			(float angle);
			bool			is_collision_off		();
			void			set_collision_off		(bool b_collision);

	//--------------------------------------------------------------------
	// Bones
	//--------------------------------------------------------------------
private:
	static	void			BoneCallback			(CBoneInstance *B);
			void			vfAssignBones			();
			void			LookDirection			(Fvector to_dir, float bone_turn_speed);


	bonesManipulation		Bones;


	CBoneInstance			*bone_spine;
	CBoneInstance			*bone_head;
	bool					collision_hit_off;
	// No damage while a script holds it invisible (set_invisible / mob state "invis"): the scenes
	// built around an unkillable bloodsucker. Config key invisible_invulnerable, default on.
	bool					m_invisible_invulnerable;
	// No damage while its own invisibility is on (partial or no visibility). Stock CS had this for
	// every bloodsucker; CoP dropped it. Config key invulnerable_while_invisible, default off.
	bool					m_invulnerable_while_invisible;
	// What part of a hit's damage reaches it while invisible, script or its own (and not made
	// untouchable by the two keys above). Config key invisible_hit_factor, default 1.
	float					m_invisible_hit_factor;
	bool					collision_off;

	//--------------------------------------------------------------------
	// Invisibility
	//--------------------------------------------------------------------
private:
	SMotionVel				invisible_vel;
	LPCSTR					invisible_particle_name;

public:
			void			start_invisible_predator ();
			void			stop_invisible_predator	 ();

			virtual bool    in_solid_state           ();

	//--------------------------------------------------------------------
	// Vampire
	//--------------------------------------------------------------------
public:

	u32						m_vampire_min_delay;
	SAnimationTripleData	anim_triple_vampire;

	SPPInfo					pp_vampire_effector;

	// When the last grab by ANY bloodsucker ended: Vampire_Delay counts from it, so a pack does not
	// pass the actor from one to the next.
	static TTime			m_time_last_vampire;


			void			ActivateVampireEffector	();
	IC		bool			WantVampire				() {return (fsimilar(m_vampire_want_value,1.f) == TRUE);}
			void			SatisfyVampire			();

			float			get_vampire_distance	() const { return m_vampire_distance; }
			float			get_vampire_hold_distance () const { return m_vampire_hold_distance; }
			u32				get_vampire_approach_time () const { return m_vampire_approach_time; }

	// The grab is earned either way:
	// - by blows: melee hits landed on the actor since the last grab reach the number for the
	//   current difficulty (Vampire_Sufficient_Hits_<difficulty> = min, max; where in that range
	//   is drawn anew after each grab);
	// - by wounds: its health is under Vampire_Wounded_Health and its own last grab ended at
	//   least Vampire_Wounded_Delay ms ago.
			void			on_actor_hit_landed		() { ++m_hits_before_vampire; }
			bool			done_enough_hits_before_vampire ();
			bool			wounded_enough_for_vampire ();
			void			reset_hits_before_vampire ();
			// a grab is over: the blows start again and its own wounded cooldown runs from now
			void			on_vampire_finished		();

			u32             get_last_critical_hit_tick () { return m_last_critical_hit_tick; }
			void            clear_last_critical_hit_tick () { m_last_critical_hit_tick = 0; }
private:
	TTime                   m_last_critical_hit_tick;
	float                   m_critical_hit_chance; //0..1
	float                   m_critical_hit_camera_effector_angle;

	float					m_vampire_want_value;
	float					m_vampire_want_speed;		// load from ltx
	float					m_vampire_wound;
	float					m_vampire_gain_health;
	float					m_vampire_distance;
	float					m_vampire_hold_distance;
	u32						m_vampire_approach_time;
	u32						m_hits_before_vampire;
	float					m_sufficient_hits_roll;			// 0..1: where in the range this grab waits
	enum { eVampireDifficultyCount = 4 };					// ESingleGameDifficulty: novice..master
	u32						m_sufficient_hits_min[eVampireDifficultyCount];
	u32						m_sufficient_hits_max[eVampireDifficultyCount];
	float					m_vampire_wounded_health;
	u32						m_vampire_wounded_delay;
	u32						m_vampire_wounded_first_delay;
	TTime					m_time_wounded;					// since when it is under Vampire_Wounded_Health
	TTime					m_time_own_last_vampire;
	float					m_vis_state;
	bool					m_drag_anim_jump;
	bool					m_animated;
	static void				animation_end_jump		(CBlend* B);


			void			LoadVampirePPEffector	(LPCSTR section);	

	//--------------------------------------------------------------------
	// Alien
	//--------------------------------------------------------------------
public:
	CBloodsuckerAlien		m_alien_control;
	u32						m_time_lunge;

			void			set_alien_control		(bool val);


	//--------------------------------------------------------------------
	// Predator
	//--------------------------------------------------------------------
public:
	shared_str				m_visual_default;
	LPCSTR					m_visual_predator;
	bool					m_predator;
	CEntityAlive			*m_cob;
	LPCSTR					m_str_cel;
	Fvector					j_position;
	float					j_factor;
			
			void			predator_start			();
			void			predator_stop			();
			void			predator_freeze			();
			void			predator_unfreeze		();
			void			set_vis					();
			void			set_invis				();
			void			jump					(const Fvector &position, float factor);
			void			start_drag				();
			void			set_drag_jump			(CEntityAlive* e, LPCSTR s, const Fvector &position, float factor);
			bool			is_drag_anim_jump		();
			bool			is_animated				();
	//--------------------------------------------------------------------
	// Sounds
	//--------------------------------------------------------------------
public:

	enum EBloodsuckerSounds {
		eAdditionalSounds		= MonsterSound::eMonsterSoundCustom,

		eVampireGrasp			= eAdditionalSounds | 0,
		eVampireSucking			= eAdditionalSounds | 1,
		eVampireHit				= eAdditionalSounds | 2,
		eVampireStartHunt		= eAdditionalSounds | 3,

		eGrowl					= eAdditionalSounds | 5,

		eChangeVisibility		= eAdditionalSounds | 6,
		
		eAlien					= eAdditionalSounds | 7,
	};

	//--------------------------------------------------------------------

public:
			void	set_manual_control	(bool value) {}
			void	manual_activate		();
			void	manual_deactivate	();

	//--------------------------------------------------------------------
	// Visibility stages (Call of Pripyat): by the distance to the enemy
	//--------------------------------------------------------------------
public:
	enum			visibility_t { unset = -1, no_visibility = 0, partial_visibility = 1, full_visibility = 2 };

			visibility_t	get_visibility_state	() const { return m_visibility_state; }
			// Switches at once, bypassing the minimum delay between changes -- for the states that
			// must show the bloodsucker now (the grab) and for death.
			void			set_visibility_state	(visibility_t new_state, bool immediate = false);

private:
			void			update_invisibility		();
			bool			invisibility_scripted	();

	visibility_t	m_visibility_state;
	TTime			m_visibility_state_last_changed_time;
	u32				m_visibility_state_change_min_delay;
	float			m_full_visibility_radius;
	float			m_partial_visibility_radius;
	// A script hid it (manual_activate): the stages leave it alone, and invisible_invulnerable applies.
	bool			m_manual_invisible;


#ifdef DEBUG
	virtual CBaseMonster::SDebugInfo show_debug_info();

	// Lain: added 
	virtual void add_debug_info  (debug::text_tree& root_s);

#ifdef _DEBUG
			void debug_on_key						(int key);
#endif

#endif

	void    apply_actor_camera_effector ();

	float   m_camera_effector_move_angular_speed;
	TTime   m_camera_effector_start_move_tick;
	TTime   m_camera_effector_last_move_tick;
	TTime   m_camera_effector_move_time_milis;
	float   m_camera_effector_hor_angle;
	float   m_camera_effector_ver_angle;

	DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CAI_Bloodsucker)
#undef script_type_list
#define script_type_list save_type_list(CAI_Bloodsucker)