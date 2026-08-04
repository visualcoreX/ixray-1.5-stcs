#include "stdafx.h"
#include "controller_psy_hit.h"
#include "../BaseMonster/base_monster.h"
#include "controller.h"
#include "../control_animation_base.h"
#include "../control_direction_base.h"
#include "../control_movement_base.h"
#include "../../../level.h"
#include "../../../actor.h"
#include "../../../ActorEffector.h"
#include "../../../../xrEngine/CameraBase.h"
#include "../../../CharacterPhysicsSupport.h"
#include "../../../level_debug.h"
#include "../../../HUDManager.h"
#include "../../../../xrEngine/ObjectAnimator.h"
#include "../../../UIGameCustom.h"	// HideShownDialogs (GS PsiStart)


// The .anm files are read and parsed by CObjectAnimator::LoadMotions on every Start(), and its first
// lookup goes through $level$ before falling back to $game_anims$. Cold, that lands as a hitch right at
// the start of the attack -- which is exactly where it is most visible. Touch all three once when the
// controller spawns, so the file system and the OS cache are warm before the psi hit ever runs.
static void warm_controller_cam_anims()
{
	static bool s_done = false;
	if (s_done)		return;
	s_done = true;

	static LPCSTR s_files[] = {
		"camera_effects\\controller_attack_prepare.anm",
		"camera_effects\\controller_attack_suicide.anm",
		"camera_effects\\controller_attack_std.anm",
	};
	for (int i = 0; i < 3; ++i)
	{
		CObjectAnimator* a = xr_new<CObjectAnimator>();
		a->Load(s_files[i]);
		xr_delete(a);
	}
}

void CControllerPsyHit::load(LPCSTR section)
{
	m_min_tube_dist = pSettings->r_float(section,"tube_condition_min_distance");
}

void CControllerPsyHit::reinit()
{
	inherited::reinit();

	IKinematicsAnimated	*skel = smart_cast<IKinematicsAnimated *>(m_object->Visual());
	m_stage[0] = skel->ID_Cycle_Safe("psy_attack_0"); VERIFY(m_stage[0]);
	m_stage[1] = skel->ID_Cycle_Safe("psy_attack_1"); VERIFY(m_stage[1]);
	m_stage[2] = skel->ID_Cycle_Safe("psy_attack_2"); VERIFY(m_stage[2]);
	m_stage[3] = skel->ID_Cycle_Safe("psy_attack_3"); VERIFY(m_stage[3]);
	m_current_index		= 0;

	warm_controller_cam_anims();

	m_sound_state		= eNone;
	m_suicide_started	= false;
	m_switch_blocked	= false;
}

bool CControllerPsyHit::check_start_conditions()
{
	if (is_active())				return false;	
	if (m_man->is_captured_pure())	return false;
	
	if (Actor()->Cameras().GetCamEffector(eCEControllerPsyHit))	
									return false;

// 	if (m_object->Position().distance_to(Actor()->Position()) < m_min_tube_dist) 
// 									return false;

	return true;
}

void CControllerPsyHit::activate()
{
	// GS OnPsyHitActivate is hooked exactly here (ControllerMonster.pas:877): the attack's WINDUP
	// already counts -- for `controller_prepare_time + 1s` the victim cannot aim, sprint, jump or use
	// a quick item, and his hands are shaking, before the grab itself lands.
	if (Actor())	Actor()->StartControllerPrepare(m_object->Position().distance_to(Actor()->Position()));

	m_man->capture_pure				(this);
	m_man->subscribe				(this, ControlCom::eventAnimationEnd);

	m_man->path_stop				(this);
	m_man->move_stop				(this);

	//////////////////////////////////////////////////////////////////////////
	// set direction
	SControlDirectionData			*ctrl_dir = (SControlDirectionData*)m_man->data(this, ControlCom::eControlDir); 
	VERIFY							(ctrl_dir);
	ctrl_dir->heading.target_speed	= 3.f;
	ctrl_dir->heading.target_angle	= m_man->direction().angle_to_target(Actor()->Position());

	//////////////////////////////////////////////////////////////////////////
	m_current_index					= 0;
	play_anim						();

	m_blocked						= false;
	m_cam_anim_mode					= 0;
	PlayCamAnim						(1);	// GS on_psi_attack_prepare -> controller_attack_prepare.anm

	// The grab itself does NOT land here. GS hooks PsiStart on death_glide_start (Init:934), i.e. after
	// the controller's first attack animation has played -- it uses its tube first, and only then does
	// the victim lose control. Starting it in activate() made the grab land the instant the controller
	// began the attack. Only the windup window opens here (OnPsyHitActivate, hooked on activate).
	m_suicide_started				= false;

	set_sound_state					(ePrepare);
}

void CControllerPsyHit::deactivate()
{
	m_man->release_pure				(this);
	m_man->unsubscribe				(this, ControlCom::eventAnimationEnd);

	// grab broken (controller lost the actor / died / attack ended): lower the weapon again. Past the
	// shot GS calls it irreversible, and CActor::StopControllerSuicide ignores the call.
	if (m_suicide_started)
	{
		Actor()->StopControllerSuicide();
		m_suicide_started = false;
	}
	if (m_switch_blocked)
	{
		extern int g_block_wpn_switch;
		g_block_wpn_switch	= 0;
		m_switch_blocked	= false;
	}

	if (m_blocked) {
		NET_Packet			P;

		Actor()->u_EventGen	(P, GEG_PLAYER_WEAPON_HIDE_STATE, Actor()->ID());
		P.w_u32				(INV_STATE_BLOCK_ALL);
		P.w_u8				(u8(false));
		Actor()->u_EventSend(P);
	}

	PlayCamAnim(0);			// GS on_suicide_scheme_finish / on_stop_suicide -> TryStartControllerCamAnim(0)
	set_sound_state(eNone);
}

void CControllerPsyHit::on_event(ControlCom::EEventType type, ControlCom::IEventData *data)
{
	if (type == ControlCom::eventAnimationEnd) {
		if (m_current_index < 3) {
			m_current_index++;
			play_anim			();

			switch (m_current_index) {
				case 1: death_glide_start();	break;
				case 2: hit();					break;
				case 3: death_glide_end();		break;
			}
		} else if (m_suicide_started && Actor()->IsSuicideInProgress() && check_conditions_final()) {
			// GS holds the victim for `controller_time` -- until the scene resolves. Our controller's
			// animation cycle is far shorter than a 7-second gesture, so loop it instead of letting the
			// grab expire. The abort decision is NOT taken here: GS takes it when the gesture ends
			// (OnSuicideAnimEnd), from the visibility we keep reporting in update_frame().
			// check_conditions_final() MUST be re-tested on every lap: it normally runs in
			// death_glide_start (index 1), which this loop never reaches again by pinning the index at
			// 3. Without it the attack had no end condition at all -- a dead controller or a victim out
			// of sight kept looping, and with the control timer re-armed every frame the actor was
			// never released. GS re-tests its own attack conditions the same way.
			// GS PsiEffects re-arms `_controlled_time_remains := GetControllerTime()` (:591) once per
			// ATTACK PULSE -- here, at the start of a lap -- not once per frame. Cadence matters at the
			// END: refreshing every frame left the timer standing at a full controller_time on the very
			// frame the controller died, so the victim was held five more seconds after the attack was
			// already over. Per lap, what remains is whatever the lap has not used up.
			if (Actor())	Actor()->RefreshControlTime();
			m_current_index		= 3;
			play_anim			();
			return;
		} else {
			m_man->deactivate	(this);
			return;
		}
	}
}

void CControllerPsyHit::play_anim()
{
	SControlAnimationData		*ctrl_anim = (SControlAnimationData*)m_man->data(this, ControlCom::eControlAnimation); 
	VERIFY						(ctrl_anim);

	ctrl_anim->global.motion	= m_stage[m_current_index];
	ctrl_anim->global.actual	= false;
}

namespace detail
{

bool check_actor_visibility (const Fvector trace_from, 
							 const Fvector trace_to,
							 CObject* object)
{
	const float dist = trace_from.distance_to(trace_to);
	Fvector trace_dir;
	trace_dir.sub(trace_to, trace_from);

	//DBG().level_info(this).add_item(trace_from, trace_to, color_xrgb(0, 150, 150));


	collide::rq_result l_rq;
	l_rq.O = NULL;
	Level().ObjectSpace.RayPick(trace_from,
								trace_dir, 
								dist, 
								collide::rqtBoth, 
								l_rq, 
								object);
	return l_rq.O == Actor();
}

} // namespace detail

bool CControllerPsyHit::check_conditions_final()
{
	if (!m_object->g_Alive())						return false;
	if (!Actor())									return false;
	if (m_object->EnemyMan.get_enemy() != Actor())	return false;
	if (!Actor()->g_Alive())						return false;
	
	if ( !m_blocked && !m_object->EnemyMan.see_enemy_now() ) 
	{
		using namespace detail;
		const Fvector self_head = get_head_position(m_object);
		Fvector actor_center;
		Actor()->Center(actor_center);

		if ( !check_actor_visibility(self_head, get_head_position(Actor()), m_object) 
									&&
			 !check_actor_visibility(self_head, actor_center, m_object) )
		{
			return false;
		}
	}

	return true;
}


void CControllerPsyHit::death_glide_start()
{
	if (!check_conditions_final()) {
		m_man->deactivate	(this);
		return;
	}

	// GS PsiEffects:583 -- telepathic protection (ours: the vodka still working) stops the grab dead
	// before any branch is picked. The victim keeps his own hands and his own weapon; all he gets is
	// the shakes for `controller_psyblocked_time`, and the ordinary psi attack lands as usual. The roll
	// that decides whether the protection holds was made at activate, by distance.
	if (Actor()->ControllerPsiBlocked())
	{
		Actor()->SetHandsJitterTime(u32(1000.f * READ_IF_EXISTS(pSettings, r_float, "gunslinger_base",
																"controller_psyblocked_time", 5.f)));
		m_suicide_started			= false;
	}
	else
	{
		// GS PsiStart is hooked exactly here (Init:934): the attack has been delivered, and NOW the
		// victim is taken. PsiEffects picks the branch, and whatever the player had open is closed.
		if (HUD().GetUI() && HUD().GetUI()->UIGame())	HUD().GetUI()->UIGame()->HideShownDialogs();
		Actor()->StartControllerGrab(m_object->Position().distance_to(Actor()->Position()));
		m_suicide_started			= Actor()->StartControllerSuicide();
	}

	// GS has no "tube" AT ALL -- not just when a suicide takes over. Its Init() cuts the vanilla one
	// out of death_glide_start (a jump over the setup block plus five nop_code patches), and the
	// comment there says why: that effector fed NaNs into CRenderDevice and tripped the CLensFlare
	// assert. So no camera flight at the controller, no FOV sweep, no hidden HUD, no impulse and no
	// slot block -- GS never takes the weapon out of the victim's hands either. What is left is the
	// attack itself: particles, sounds, the psi damage, and a CAMERA ANIMATION per phase.
	smart_cast<CController *>(m_object)->draw_fire_particles();

	if (m_suicide_started)
	{
		PlayCamAnim(2);			// GS on_suicide_attack  -> controller_attack_suicide.anm
		extern int g_block_wpn_switch;
		g_block_wpn_switch			= 1;	// keep the weapon in hand, block only slot switching
		m_switch_blocked			= true;
	}
	else
		PlayCamAnim(3);			// GS on_std_attack      -> controller_attack_std.anm

	// GS PsiStart: a handful of phantoms flicker around the victim while the controller holds him.
	// Counts/radii from [gunslinger_base] (GS's own controller_phantoms_* keys).
	{
		extern void spawn_phantom(const Fvector& position);
		const int  mn = (int)READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", "controller_phantoms_min", 0.f);
		const int  mx = (int)READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", "controller_phantoms_max", 0.f);
		const float r0 = READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", "controller_phantoms_min_radius", 0.f);
		const float r1 = READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", "controller_phantoms_max_radius", 10.f);
		const int  cnt = (mx > mn) ? (mn + ::Random.randI(mx - mn)) : mn;
		for (int i = 0; i < cnt; ++i)
		{
			Fvector v;
			v.set(::Random.randF(-500.f, 500.f), ::Random.randF(0.f, 200.f), ::Random.randF(-500.f, 500.f));
			v.set_length(::Random.randF(r0, r1));
			v.add(Actor()->cam_Active()->vPosition);
			spawn_phantom(v);
		}
	}

	set_sound_state					(eStart);

	// GS: the psi grab does not just hurt -- it makes the victim turn his own weapon on himself. The
	// actor drives the gesture/shot/kill from here (CActor::StartControllerSuicide); if the weapon in
	// hand cannot be used for it (no ammo, jammed, `prohibit_suicide`, no anm_suicide) we fall back to
	// the vanilla tube damage at death_glide_end.
	// INV_STATE_BLOCK_ALL blocks the slots, which HOLSTERS whatever is in hand -- that cut the suicide
	// gesture and re-drew the weapon when the block lifted (first in-game report). During the scene the
	// weapon must stay out; activate() already blocked slot SWITCHING, which is all GS needs.
	// ...and no slot block either, for the same reason: GS never takes the weapon out of the victim's
	// hands. INV_STATE_BLOCK_ALL holsters whatever is held, which is the vanilla tube's doing, not the
	// attack's. The weapon stays where it is whether the psi hit lands, is blocked, or turns into a
	// suicide; only slot SWITCHING is held, and that is set up in activate().

	//////////////////////////////////////////////////////////////////////////
	// set direction
	SControlDirectionData			*ctrl_dir = (SControlDirectionData*)m_man->data(this, ControlCom::eControlDir); 
	VERIFY							(ctrl_dir);
	ctrl_dir->heading.target_speed	= 3.f;
	ctrl_dir->heading.target_angle	= m_man->direction().angle_to_target(Actor()->Position());

	//////////////////////////////////////////////////////////////////////////
}

// GS's camera reaction is a CAMERA ANIMATION, not a wobble and not a shove: gunsl_controller.script
// (the module behind the script_call hooks in ControllerMonster.pas) runs TryStartControllerCamAnim,
// which feeds level.add_cam_effector one of three .anm files. Ported here so no Lua module is needed;
// the mode rules are GS's, including the priorities -- the windup anim only starts from a clear state,
// and the attack anims only over a clear state or over the windup.
//   1 = controller_attack_prepare   (OnPsyHitActivate)
//   2 = controller_attack_suicide   (the grab turned into a suicide scene)
//   3 = controller_attack_std       (the ordinary psi attack -- what the tube used to be)
void CControllerPsyHit::PlayCamAnim(int mode)
{
	if (!Actor())		return;

	if (mode == 0)		{ m_cam_anim_mode = 0; return; }

	LPCSTR fn = 0;
	switch (mode)
	{
	case 1:	fn = "camera_effects\\controller_attack_prepare.anm";	break;
	case 2:	fn = "camera_effects\\controller_attack_suicide.anm";	break;
	case 3:	fn = "camera_effects\\controller_attack_std.anm";		break;
	default: return;
	}

	// GS: prepare only from idle; suicide/std from idle or over prepare (never over each other)
	if (mode == 1 && m_cam_anim_mode != 0)								return;
	if ((mode == 2 || mode == 3) && m_cam_anim_mode != 0 && m_cam_anim_mode != 1)	return;

	CAnimatorCamEffector* e = xr_new<CAnimatorCamEffector>();
	e->SetType		((ECamEffectorType)eCEControllerPsyHit);
	e->SetCyclic	(false);
	e->Start		(fn);
	Actor()->Cameras().AddCamEffector(e);

	m_cam_anim_mode = mode;
}

void CControllerPsyHit::death_glide_end()
{
	// Stop camera effector

	CController *monster = smart_cast<CController *>(m_object);
	// no effector is ever added now (see death_glide_start), so there is nothing to remove
	monster->draw_fire_particles();


	monster->m_sound_tube_hit_left.play_at_pos(Actor(), Fvector().set(-1.f, 0.f, 1.f), sm_2D);
	monster->m_sound_tube_hit_right.play_at_pos(Actor(), Fvector().set(1.f, 0.f, 1.f), sm_2D);

	//m_object->Hit_Psy		(Actor(), monster->m_tube_damage);
	// the suicide IS the damage when it took over -- the tube hit on top would kill before the shot
	if (!m_suicide_started)
		m_object->Hit_Wound	(Actor(), monster->m_tube_damage,Fvector().set(0.0f,1.0f,0.0f),0.0f);
	HUD().SetRenderable(true);

}

void CControllerPsyHit::update_frame()
{

	// GS: the controller KEEPS turning to face its victim for the whole attack. Ours took the heading
	// as a snapshot in activate()/death_glide_start(), so it stared wherever the actor had been when
	// the grab started -- walk around it and it never turned.
	if (is_active() && Actor() && Actor()->g_Alive())
	{
		SControlDirectionData* ctrl_dir = (SControlDirectionData*)m_man->data(this, ControlCom::eControlDir);
		if (ctrl_dir)
		{
			ctrl_dir->heading.target_speed = 3.f;
			ctrl_dir->heading.target_angle = m_man->direction().angle_to_target(Actor()->Position());
		}
	}

	// GS CheckActorVisibilityForController: the victim's decision at the end of the gesture needs to
	// know whether a controller can still see him. `mandatory_suicide_visibility_check` on the monster
	// section is GS's own knob -- without it, veteran+ ignores line of sight entirely.
	if (m_suicide_started && Actor() && Actor()->IsSuicideInProgress())
	{
		const bool mandatory = !!READ_IF_EXISTS(pSettings, r_bool, m_object->cNameSect().c_str(),
												"mandatory_suicide_visibility_check", FALSE);
		Actor()->NotifyControllerSees(!!m_object->EnemyMan.see_enemy_now(), mandatory);
		// GS recomputes the distance inside PsiEffects on every pulse (v_length of controller - actor),
		// so walking towards the controller mid-scene really does change the branch. Reading it once at
		// grab time meant an RPG or a launcher never got dropped no matter how close the victim came.
		const float cd = m_object->Position().distance_to(Actor()->Position());
		Actor()->SetControllerDist(cd);
		if (!m_object->g_Alive())
			Actor()->StopControllerSuicide();		// dead controller = broken grab
	}

	//if (m_sound_state == eStart) {
	//	CController *monster = smart_cast<CController *>(m_object);
	//	if (!monster->m_sound_tube_start._feedback()) {
	//		m_sound_state = ePull;
	//		monster->m_sound_tube_pull.play_at_pos(Actor(), Fvector().set(0.f, 0.f, 0.f), sm_2D);
	//	}
	//}
}

void CControllerPsyHit::set_sound_state(ESoundState state)
{
	CController *monster = smart_cast<CController *>(m_object);
	if (state == ePrepare) {
		monster->m_sound_tube_prepare.play_at_pos(Actor(), Fvector().set(0.f, 0.f, 0.f), sm_2D);
	} else 
	if (state == eStart) {
		if (monster->m_sound_tube_prepare._feedback())	monster->m_sound_tube_prepare.stop();

		monster->m_sound_tube_start.play_at_pos(Actor(), Fvector().set(0.f, 0.f, 0.f), sm_2D);
		monster->m_sound_tube_pull.play_at_pos(Actor(), Fvector().set(0.f, 0.f, 0.f), sm_2D);
	} else 
	if (state == eHit) {
		if (monster->m_sound_tube_start._feedback())	monster->m_sound_tube_start.stop();
		if (monster->m_sound_tube_pull._feedback())		monster->m_sound_tube_pull.stop();
		
		//monster->m_sound_tube_hit_left.play_at_pos(Actor(), Fvector().set(-1.f, 0.f, 1.f), sm_2D);
		//monster->m_sound_tube_hit_right.play_at_pos(Actor(), Fvector().set(1.f, 0.f, 1.f), sm_2D);
	} else 
	if (state == eNone) {
		if (monster->m_sound_tube_start._feedback())	monster->m_sound_tube_start.stop();
		if (monster->m_sound_tube_pull._feedback())		monster->m_sound_tube_pull.stop();
		if (monster->m_sound_tube_prepare._feedback())	monster->m_sound_tube_prepare.stop();
	}

	m_sound_state = state;
}

void CControllerPsyHit::hit()
{
	//CController *monster	= smart_cast<CController *>(m_object);
	
	set_sound_state			(eHit);
	//m_object->Hit_Psy		(Actor(), monster->m_tube_damage);
}

void CControllerPsyHit::on_death()
{
	if (!is_active()) return;
	HUD().SetRenderable(true);
	
	// Stop camera effector
	CEffectorCam* ce = Actor()->Cameras().GetCamEffector(eCEControllerPsyHit);
	if (ce) {
		Actor()->Cameras().RemoveCamEffector(eCEControllerPsyHit);
	}

	m_man->deactivate		(this);
}
