#pragma once

#include "../../../../Include/xrRender/KinematicsAnimated.h"
#include "../../../actor.h"
#include "../../../../xrEngine/CameraBase.h"
#include "../../../HUDManager.h"
#include "../../../Inventory.h"
#include "../../../../xrCore/_vector3d_ext.h"
#include "../control_animation_base.h"
#include "../control_direction_base.h"
#include "../control_path_builder_base.h"
#include "../../../level_graph.h"
#include "../../../ai_space.h"
#include "../../../ai_object_location.h"

#define TEMPLATE_SPECIALIZATION template <\
	typename _Object\
>

#define CStateBloodsuckerVampireExecuteAbstract CStateBloodsuckerVampireExecute<_Object>

#define VAMPIRE_TIME_HOLD		4000
#define VAMPIRE_HIT_IMPULSE		400.f
#define VAMPIRE_MIN_DIST		0.5f
#define VAMPIRE_MAX_DIST		1.5f

TEMPLATE_SPECIALIZATION
void CStateBloodsuckerVampireExecuteAbstract::initialize()
{
	inherited::initialize					();

	object->CControlledActor::install		();

	look_head				();

	m_action				= eActionApproach;
	time_vampire_started	= 0;
	m_time_approach_started	= Device.dwTimeGlobal;
	m_approach_best_dist	= flt_max;
	m_approach_best_time	= Device.dwTimeGlobal;

	// In the open for the grab, whatever the distance stages say (Call of Pripyat).
	object->set_visibility_state			(CAI_Bloodsucker::full_visibility, true);

	// The actor is held: no hud, the weapon put away and the slots locked, as for the
	// controller's psy attack.
	HUD().SetRenderable						(false);
	Actor()->SetWeaponHideState				(INV_STATE_BLOCK_ALL, true);
	m_hud_hidden							= true;

	m_effector_activated			= false;
}

TEMPLATE_SPECIALIZATION
void CStateBloodsuckerVampireExecuteAbstract::execute()
{
	if (!object->CControlledActor::is_turning() && !m_effector_activated) {
		object->ActivateVampireEffector	();
		m_effector_activated			= true;
	}
	
	look_head							();

	Fvector const enemy_to_self	= Fvector().sub(object->EnemyMan.get_enemy()->Position(), object->Position());
	float const dist_to_enemy	= enemy_to_self.magnitude();
	float const vampire_dist	= object->get_vampire_distance();

	switch (m_action) {
		case eActionApproach:
			// It seizes him from melee reach (MinAttackDist, over 2 m) but drinks only from close
			// up: the vampire animation pins it to the spot (the triple captures its movement), so
			// it closes in first, while the actor is already held. It starts within Vampire_Distance,
			// or as soon as it can get no closer -- its body against his stops it some 1.3-1.5 m off,
			// and waiting there for the full Vampire_Approach_Time left the actor frozen for nothing.
			// Still out of Vampire_Hold_Distance when the time is up: the grab fails.
			if (dist_to_enemy < m_approach_best_dist - 0.05f) {
				m_approach_best_dist	= dist_to_enemy;
				m_approach_best_time	= Device.dwTimeGlobal;
			}

			if (dist_to_enemy <= vampire_dist + 0.2f) {
				m_action = eActionPrepare;
			} else if (dist_to_enemy <= object->get_vampire_hold_distance() &&
					   Device.dwTimeGlobal > m_approach_best_time + 250) {
				m_action = eActionPrepare;
			} else if (Device.dwTimeGlobal > m_time_approach_started + object->get_vampire_approach_time()) {
				m_action = eActionCompleted;
			}
			break;

		case eActionPrepare:
			execute_vampire_prepare();
			m_action = eActionContinue;
			break;

		case eActionContinue:
			execute_vampire_continue();
			break;

		case eActionFire:
			execute_vampire_hit();
			m_action = eActionWaitTripleEnd;
			break;

		case eActionWaitTripleEnd:
			if (!object->com_man().ta_is_active()) {
				m_action = eActionCompleted; 
			}

		case eActionCompleted:
			break;
	}

	object->dir().face_target	(object->EnemyMan.get_enemy());

	// Close in to Vampire_Distance (Call of Pripyat), while there is still somewhere to go.
	if ( m_action == eActionApproach &&
		 angle_between_vectors(object->Direction(), enemy_to_self) < deg2rad(20.f) && 
		 dist_to_enemy > vampire_dist )
	{
		object->set_action					(ACT_RUN);
		object->anim().accel_activate		(eAT_Aggressive);
		object->anim().accel_set_braking	(false);

		u32 const target_vertex		= object->EnemyMan.get_enemy()->ai_location().level_vertex_id();
		Fvector const target_pos	= ai().level_graph().vertex_position(target_vertex);

		object->path().set_target_point		(target_pos, target_vertex);
		object->path().set_rebuild_time		(100);
		object->path().set_use_covers		(false);
		object->path().set_distance_to_end	(vampire_dist);
	}
	else
	{
		object->set_action					(ACT_STAND_IDLE);
	}
}

TEMPLATE_SPECIALIZATION
void CStateBloodsuckerVampireExecuteAbstract::cleanup()
{
	if (object->com_man().ta_is_active())
		object->com_man().ta_deactivate();

	if (object->CControlledActor::is_controlling())
		object->CControlledActor::release		();

	if (m_hud_hidden)
	{
		HUD().SetRenderable						(true);
		Actor()->SetWeaponHideState				(INV_STATE_BLOCK_ALL, false);
		m_hud_hidden							= false;
	}

	// The next grab waits for a fresh run of hits (or its own wounded cooldown), and every
	// bloodsucker for Vampire_Delay.
	object->on_vampire_finished					();
	CAI_Bloodsucker::m_time_last_vampire		= Device.dwTimeGlobal;
}

TEMPLATE_SPECIALIZATION
void CStateBloodsuckerVampireExecuteAbstract::finalize()
{
	inherited::finalize();
	cleanup();
}

TEMPLATE_SPECIALIZATION
void CStateBloodsuckerVampireExecuteAbstract::critical_finalize()
{
	inherited::critical_finalize();
	cleanup();
}

// The grab, as Call of Pripyat starts it: once enough blows have landed on the actor
// (Vampire_Sufficient_Hits..Vampire_Sufficient_Hits_Max), from melee reach, facing him, with
// the thirst up (Vampire_Want_Speed) and no other grab in the last Vampire_Delay ms.
TEMPLATE_SPECIALIZATION
bool CStateBloodsuckerVampireExecuteAbstract::check_start_conditions()
{
	const CEntityAlive	*enemy = object->EnemyMan.get_enemy();
	if (!enemy)											return false;

	const CActor *actor = smart_cast<const CActor*>(enemy);
	if (!actor)											return false;
	if (!actor->g_Alive())								return false;

	if (!object->done_enough_hits_before_vampire() &&
		!object->wounded_enough_for_vampire())			return false;
	if (!object->WantVampire())							return false;
	if (object->berserk_always)							return false;

	if (CAI_Bloodsucker::m_time_last_vampire + object->m_vampire_min_delay > Device.dwTimeGlobal)
		return false;

	u32 const vertex_id = ai().level_graph().check_position_in_direction(object->ai_location().level_vertex_id(),
																		 object->Position(), enemy->Position());
	if (!ai().level_graph().valid_vertex_id(vertex_id))	return false;

	if (!object->MeleeChecker.can_start_melee(enemy))	return false;
	if (!object->control().direction().is_face_target(enemy, PI_DIV_2)) return false;

	if (object->CControlledActor::is_controlling())		return false;
	if (actor->input_external_handler_installed())		return false;

	return true;
}

TEMPLATE_SPECIALIZATION
bool CStateBloodsuckerVampireExecuteAbstract::check_completion()
{
	return (m_action == eActionCompleted);
}

//////////////////////////////////////////////////////////////////////////

TEMPLATE_SPECIALIZATION
void CStateBloodsuckerVampireExecuteAbstract::execute_vampire_prepare()
{
	object->com_man().ta_activate		(object->anim_triple_vampire);
	time_vampire_started				= Device.dwTimeGlobal;
	
	object->sound().play(CAI_Bloodsucker::eVampireGrasp);
}

TEMPLATE_SPECIALIZATION
void CStateBloodsuckerVampireExecuteAbstract::execute_vampire_continue()
{
	// He was knocked or pulled out of its hands: let go. (Melee reach would keep a grab going
	// at well over 2 m.)
	const CEntityAlive* enemy = object->EnemyMan.get_enemy();
	if (!enemy || enemy->Position().distance_to(object->Position()) > object->get_vampire_hold_distance()) {
		object->com_man().ta_deactivate();
		m_action = eActionCompleted;
		return;
	}
	
	object->sound().play(CAI_Bloodsucker::eVampireSucking);

	// проверить на грави удар
	if (time_vampire_started + VAMPIRE_TIME_HOLD < Device.dwTimeGlobal) {
		m_action = eActionFire;
	}
}

TEMPLATE_SPECIALIZATION
void CStateBloodsuckerVampireExecuteAbstract::execute_vampire_hit()
{
	object->com_man().ta_pointbreak				();
	object->sound().play						(CAI_Bloodsucker::eVampireHit);
	object->SatisfyVampire						();
}

//////////////////////////////////////////////////////////////////////////

TEMPLATE_SPECIALIZATION
void CStateBloodsuckerVampireExecuteAbstract::look_head()
{
	IKinematics *pK = smart_cast<IKinematics*>(object->Visual());
	Fmatrix bone_transform;
	bone_transform = pK->LL_GetTransform(pK->LL_BoneID("bip01_head"));	

	Fmatrix global_transform;
	global_transform.mul_43(object->XFORM(),bone_transform);

	object->CControlledActor::look_point	(global_transform.c);
}

#undef TEMPLATE_SPECIALIZATION
#undef CStateBloodsuckerVampireExecuteAbstract

