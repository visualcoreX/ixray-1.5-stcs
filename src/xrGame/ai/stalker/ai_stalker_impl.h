////////////////////////////////////////////////////////////////////////////
//	Module 		: ai_stalker_impl.h
//	Created 	: 25.02.2003
//  Modified 	: 25.02.2003
//	Author		: Dmitriy Iassenev
//	Description : AI Behaviour for monster "Stalker" (inline functions implementation)
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "../../level.h"
#include "../../seniority_hierarchy_holder.h"
#include "../../team_hierarchy_holder.h"
#include "../../squad_hierarchy_holder.h"
#include "../../group_hierarchy_holder.h"
#include "../../effectorshot.h"
#include "stalker_movement_manager_smart_cover.h"
#include "smart_cover_animation_selector.h"
#include "smart_cover_animation_planner.h"

IC	CAgentManager &CAI_Stalker::agent_manager	() const
{
	return			(Level().seniority_holder().team(g_Team()).squad(g_Squad()).group(g_Group()).agent_manager());
}

IC	Fvector CAI_Stalker::weapon_shot_effector_direction	(const Fvector &current) const
{
	// Call of Pripyat -- and so Gunslinger, which does not patch this -- hands the aim back
	// UNCHANGED: GSC left the displacement below behind an `#if 1 return current`. The shot
	// effector still runs (it drives the shooting animation), it just no longer walks the NPC's
	// aim. That is why the GS recoil figures work there and sprayed here: with the displacement
	// live, an aks74u (1.2 deg + 0.6 per round) put everything after the first bullet of a burst
	// over the target. NPC spread stays what it always was -- fire_dispersion_base and AI skill.
	return			(current);

#if 0

	VERIFY			(weapon_shot_effector().IsActive());
	Fvector			result;
	weapon_shot_effector().GetDeltaAngle(result);

	float			y,p;
	current.getHP	(y,p);

	result.setHP	(-result.y + y, -result.x + p);

	return			(result);
#endif
}