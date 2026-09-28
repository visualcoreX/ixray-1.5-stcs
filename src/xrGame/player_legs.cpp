#include "stdafx.h"
#include "player_legs.h"

#include "Actor.h"
#include "../xrEngine/CameraBase.h"	// cam_Active()->GetWorldYaw() for the body offset heading
#include "Inventory.h"
#include "inventory_item.h"
#include "HudItem.h"
#include "CustomDetector.h"
#include "../xrServerEntities/inventory_space.h"
#include "../Include/xrRender/Kinematics.h"
#include "../xrEngine/IGame_Persistent.h"

// How the body is parked behind the camera. g_legs_body_offset moves the WHOLE model (feet and
// all) along the model heading; g_legs_fwd_offset leans the TORSO alone in model space and never
// touches the legs, so it costs nothing in movement but comes off the hips if pushed far.
// The direction the first one uses is the interesting part -- see update().
float	g_legs_body_offset		= -0.5f;
float	g_legs_fwd_offset		= 0.f;
// How the first-person SHADOW follows the body: 0 off, 1 the whole legs transform, 2 position only.
// See CActor::renderable_Render for what the difference costs.
int		g_legs_shadow			= 2;	// 2 = position only: mode 1 dragged the planted-feet yaw lag into
										// the shadow and it jerked when the player went diagonal
float	g_legs_spine_offset_y	= 0.1f;		// lifts the spine so the waist does not poke into the lens
BOOL	g_legs_attach_to_camera	= FALSE;	// see update(): the camera leans on Q/E and the body must not
BOOL	g_legs_in_low_crouch	= TRUE;		// low crouch folds the body into the camera; still better
											// than the legs blinking out of existence there
BOOL	g_legs_arms				= TRUE;		// draw the body's arms whenever the hud is not using them
float	g_legs_align_speed		= 3.f;		// how fast "back" re-aims at the view when it is free to
// Feet planted while the player turns on the spot. The actor object's yaw follows the camera
// every frame, so a model placed by actor->XFORM() simply spins under the mouse, and the feet --
// a fifth of a metre off the axis -- sweep an arc across the floor. No amount of tuning the
// OFFSET touches that: the offset is not what is rotating, the model is.
// So the legs model gets a yaw of its own. Standing, it holds still and lets the view twist away
// from it up to g_legs_yaw_deadzone (that is what a person does -- head and torso turn, feet stay);
// past that it turns after the camera at g_legs_yaw_speed until it catches up, which reads as
// re-planting the feet. Walking, it just follows the view, because then the feet are moving anyway.
BOOL	g_legs_yaw_hold			= TRUE;
float	g_legs_yaw_deadzone		= 30.f;		// degrees the view may twist away before the feet follow,
											// and the most the body may lag the view (see update())
float	g_legs_yaw_speed		= 240.f;	// degrees a second the model turns while catching up
float	g_legs_yaw_max_lag		= 60.f;		// hard limit: the feet never trail the VIEW by more than this
float	g_legs_torso_max_lag	= 15.f;		// the same for the chest and the arms on it (see clamp_torso_yaw())
float	g_legs_sprint_offset	= -0.2f;	// extra distance the body drops back while sprinting
float	g_legs_sprint_speed		= 4.f;		// how quickly it goes there and comes back
BOOL	g_legs_anchor_pelvis	= FALSE;	// park the HIPS over the actor (see update())
// Torso bent back off the camera (see lean_torso_off_camera()). Only the stand-in body bends: the
// shadow is cast by the actor's own skeleton, so none of this reaches it.
BOOL	g_legs_lean				= TRUE;
float	g_legs_lean_margin		= 0.5f;	// how far behind the eyes the chest bones have to stay, metres
float	g_legs_lean_max			= 60.f;		// the most the torso may bend back, degrees
float	g_legs_lean_speed_in	= 15.f;		// how fast it bends away (fast: a late lean is a chest in the lens)
float	g_legs_lean_speed_out	= 5.f;		// how fast it straightens once the chest is clear again

namespace
{
// Bone layout of the stalker skeleton (47 bones, dumped from stalker_hero_*.ogf):
//   bip01_pelvis -> {l/r_thigh, bip01_spine -> spine1 -> spine2 -> bip01_neck}
//   bip01_neck   -> bip01_head (+eyes/jaw) AND BOTH CLAVICLES -> upperarm -> ...
// That last line is the catch: the arms hang off the NECK, so collapsing the neck branch takes
// them with it -- which is why hiding "the head" also silently killed the arms.
LPCSTR	HEAD_BONE		= "bip01_head";		// the head belongs to the camera, never to this model
LPCSTR	ARM_BONE_L		= "bip01_l_upperarm";
LPCSTR	ARM_BONE_R		= "bip01_r_upperarm";
LPCSTR	SPINE_BONE		= "bip01_spine";
LPCSTR	PELVIS_BONE		= "bip01_pelvis";
LPCSTR	CLAVICLE_L		= "bip01_l_clavicle";
LPCSTR	CLAVICLE_R		= "bip01_r_clavicle";
// What the torso lean keeps behind the eyes: the upper chest and the shoulders. The chest SURFACE
// sits a hand in front of these, which is what g_legs_lean_margin pays for.
LPCSTR	LEAN_PROBES[]	= { "bip01_spine2", "bip01_neck", "bip01_l_clavicle", "bip01_r_clavicle" };
} // namespace

player_legs_controller::player_legs_controller()
{
	m_model				= NULL;
	m_draw				= false;
	m_yaw				= 0.f;
	m_yaw_valid			= false;
	m_yaw_turning		= false;
	m_offset_dir.set	(0.f, 0.f, 1.f);
	m_offset_dir_valid	= false;
	m_sprint_blend		= 0.f;
	m_lean				= 0.f;
	m_chest_sign		= 1.f;
	m_has_fwd_offset	= false;
	m_fwd_offset		= 0.f;
	m_legs_transform.identity();
}

void player_legs_controller::destroy()
{
	if (!m_model)
		return;

	IRenderVisual* v	= m_model->dcast_RenderVisual();
	if (v)
		::Render->model_Delete(v);

	m_model				= NULL;
	m_visual_name		= NULL;
	// forget the resolved config too: the next update has to go through resolve_config again,
	// otherwise a model destroyed by an outfit change would never be rebuilt
	m_last_outfit_sect	= NULL;
	m_last_model		= NULL;
}

void player_legs_controller::warn_once(LPCSTR fmt, ...)
{
	string1024		buf;
	va_list			args;
	va_start		(args, fmt);
	vsprintf_s		(buf, sizeof(buf), fmt, args);
	va_end			(args);

	shared_str		msg(buf);
	for (u32 i=0; i<m_warnings.size(); ++i)
		if (m_warnings[i] == msg)
			return;

	m_warnings.push_back(msg);
	Msg				("! [legs] %s", buf);
}

bool player_legs_controller::resolve_config(CActor* actor, shared_str& sect, shared_str& model)
{
	PIItem outfit			= actor->inventory().m_slots[OUTFIT_SLOT].m_pIItem;
	// no outfit: the same [without_outfit] actor_visual the third-person model uses (CActor::net_Spawn)
	shared_str current		= outfit ? outfit->object().cNameSect()
							: (pSettings->line_exist("without_outfit", "legs_visual") || pSettings->line_exist("without_outfit", "actor_visual"))
								? shared_str("without_outfit") : shared_str("actor");

	if (m_last_outfit_sect == current && m_last_model.size())
	{
		sect	= m_last_outfit_sect;
		model	= m_last_model;
		return	true;
	}

	m_last_outfit_sect		= current;

	// A worn outfit already names the model the actor is seen in from the outside (actor_visual);
	// legs_visual is the override for a model cut down to the lower body.
	LPCSTR keys[]			= { "legs_visual", "actor_visual", "visual" };
	for (u32 i=0; i<sizeof(keys)/sizeof(keys[0]); ++i)
	{
		if (pSettings->line_exist(current, keys[i]))
		{
			m_last_model	= pSettings->r_string(current, keys[i]);
			sect			= current;
			model			= m_last_model;
			return			true;
		}
	}

	warn_once				("section [%s] has no legs_visual / actor_visual / visual", current.c_str());
	return					false;
}

bool player_legs_controller::ensure_model(const shared_str& sect, const shared_str& model)
{
	if (m_model && (m_visual_name == model))
		return				true;

	destroy					();
	// destroy() cleared the cache resolve_config just filled -- put it back, the caller's values
	// are the ones we are building from
	m_last_outfit_sect		= sect;
	m_last_model			= model;

	IRenderVisual* raw		= ::Render->model_Create(model.c_str());
	if (!raw)
	{
		warn_once			("cannot create model [%s]", model.c_str());
		return				false;
	}

	IKinematics* K			= raw->dcast_PKinematics();
	if (!K)
	{
		::Render->model_Delete(raw);
		warn_once			("model [%s] is not a skeleton", model.c_str());
		return				false;
	}

	m_model					= K;
	m_visual_name			= model;

	// Which way the chest faces is read off the shoulder line (chest_heading()), and a line has two
	// normals. Pick the one that points forward in the BIND pose, where the model stands facing +z
	// -- the heading convention everything here uses. m2b_transform is the inverse of the bind
	// pose in model space, so inverting it gives the bone's bind position.
	m_chest_sign			= 1.f;
	{
		const u16 l			= K->LL_BoneID(CLAVICLE_L);
		const u16 r			= K->LL_BoneID(CLAVICLE_R);
		if ((BI_NONE != l) && (BI_NONE != r))
		{
			Fmatrix bl;		bl.invert(K->LL_GetData(l).m2b_transform);
			Fmatrix br;		br.invert(K->LL_GetData(r).m2b_transform);
			Fvector side;	side.sub(br.c, bl.c);
			Fvector up;		up.set(0.f, 1.f, 0.f);
			Fvector fwd;	fwd.crossproduct(side, up);
			if (fwd.z < 0.f)
				m_chest_sign = -1.f;
		}
	}

	m_has_fwd_offset		= !!pSettings->line_exist(sect, "legs_fwd_offset");
	m_fwd_offset			= m_has_fwd_offset ? pSettings->r_float(sect, "legs_fwd_offset") : 0.f;

	return					true;
}

void player_legs_controller::copy_bones_from_actor(CActor* actor)
{
	IRenderVisual* actor_visual	= actor->Visual();
	if (!actor_visual)
		return;

	IKinematics* actor_K		= actor_visual->dcast_PKinematics();
	if (!actor_K)
		return;

	actor_K->CalculateBones		(TRUE);
	m_model->CalculateBones_Invalidate();
	m_model->CalculateBones		(TRUE);

	// The root goes to identity: the whole model is placed by m_legs_transform below, not by
	// whatever world position the actor's own root carries.
	u16 root					= m_model->LL_GetBoneRoot();
	CBoneInstance& root_bi		= m_model->LL_GetBoneInstance(root);
	root_bi.mTransform.identity();
	root_bi.mRenderTransform.mul_43(root_bi.mTransform, m_model->LL_GetData(root).m2b_transform);

	const u16 bone_count		= m_model->LL_BoneCount();
	if (bone_count == actor_K->LL_BoneCount())
	{
		// same skeleton -- indices line up, skip the name lookups
		for (u16 i=0; i<bone_count; ++i)
		{
			m_model->LL_GetTransform(i).set		(actor_K->LL_GetTransform(i));
			m_model->LL_GetTransform_R(i).set	(actor_K->LL_GetTransform_R(i));
		}
	}
	else
	{
		IKinematics::accel* bones = m_model->LL_Bones();
		for (IKinematics::accel::iterator it=bones->begin(); it!=bones->end(); ++it)
		{
			const u16 src		= actor_K->LL_BoneID(it->first);
			if (BI_NONE == src)
				continue;

			m_model->LL_GetTransform(it->second).set	(actor_K->LL_GetTransform(src));
			m_model->LL_GetTransform_R(it->second).set	(actor_K->LL_GetTransform_R(src));
		}
	}

	// Everything from the waist up is moved as a block, in MODEL space: up a little so the belt
	// does not poke into the lens, and back so the camera sits behind the chest instead of inside
	// it. Doing it here rather than by shifting the whole model in the world is what keeps the FEET
	// planted -- a world-space offset rides on the body (or camera) heading, so it swings the model
	// sideways the moment either turns, which is the "legs sliding across the floor" effect.
	//
	// NOT Bone_Calculate: that one is BuildBoneMatrix -> mul_43(parent, bd->bind_transform) plus a
	// recursion over every child (SkeletonRigid.cpp:120). On a model nobody animates it means the
	// BIND POSE, so calling it on the spine wiped the pose just copied from the actor for the whole
	// upper body -- torso, neck, and both arms with it. That is why the arms were "not working".
	shift_bone_branch			(m_model->LL_BoneID(SPINE_BONE),
								 Fvector().set(0.f, g_legs_spine_offset_y, m_has_fwd_offset ? m_fwd_offset : g_legs_fwd_offset));

	// The head is never ours. The arms are, but only while the hud is not drawing them: anything
	// in hand -- a weapon, or the detector on its own -- takes both, and only with empty hands
	// does the player see his own arms hanging there.
	collapse_bone_branch		(m_model->LL_BoneID(HEAD_BONE));

	bool hide_left = true, hide_right = true;
	if (g_legs_arms)
	{
		// NOT IsWorking(): a detector that is being drawn or holstered still occupies the hand,
		// and its state covers the whole out-and-back trip (same test ActorAnimation.cpp uses).
		CCustomDetector* det	= smart_cast<CCustomDetector*>(actor->inventory().ItemFromSlot(DETECTOR_SLOT));
		const bool det_in_hand	= det && (det->GetState() != CHUDState::eHidden);
		const bool item_in_hand	= !!smart_cast<CHudItem*>(actor->inventory().ActiveItem());

		// Either hand busy hides BOTH. The detector is held in the left, so the right one is
		// technically free -- but the hud draws it in a pose of its own, and the body's idle arm
		// next to it read wrong, so it goes too.
		hide_left				= item_in_hand || det_in_hand;
		hide_right				= hide_left;
	}

	if (hide_left)
		collapse_bone_branch	(m_model->LL_BoneID(ARM_BONE_L));
	if (hide_right)
		collapse_bone_branch	(m_model->LL_BoneID(ARM_BONE_R));
}

// LL_SetBoneVisible(FALSE) is NOT the way to do this. All it does is mTransform.scale(0,0,0), and
// Fmatrix::scale starts from identity -- so the branch collapses to the MODEL ORIGIN, i.e. the point
// between the feet. Vertices weighted ONLY to the head then vanish (zero-area triangles, fine), but
// every vertex at the collar is weighted PARTLY to the neck and partly to the chest, so it gets
// dragged a metre and a half downwards: the head does not disappear, it stretches into a spike
// pointing at the actor's root.
//
// Collapsing to the point where the branch JOINS the visible body fixes exactly that: the fully
// weighted vertices still make zero-area triangles, and the blended ones move a couple of
// centimetres at most, inside the collar or the shoulder where nothing can be seen.
// Walks the branch and calls back with every bone that belongs to it, the caller's transform in
// hand. Both users below need the same walk: up the parent chain to the branch root, because the
// interface hands out parents (GetParentID) and not children.
void player_legs_controller::shift_bone_branch(u16 branch_root, const Fvector& delta)
{
	Fmatrix xf;					xf.translate(delta);
	transform_bone_branch		(branch_root, xf);
}

// The same walk, applying a model-space transform to every bone of the branch: m = xform * m.
void player_legs_controller::transform_bone_branch(u16 branch_root, const Fmatrix& xform)
{
	if (BI_NONE == branch_root)
		return;

	const u16 skeleton_root		= m_model->LL_GetBoneRoot();
	const u16 count				= m_model->LL_BoneCount();

	for (u16 i=0; i<count; ++i)
	{
		u16 b					= i;
		while ((b != branch_root) && (b != skeleton_root))
			b					= m_model->LL_GetData(b).GetParentID();

		if (b != branch_root)
			continue;

		Fmatrix& m				= m_model->LL_GetTransform(i);
		Fmatrix src;			src.set(m);
		m.mul_43				(xform, src);	// src first, then xform
		m_model->LL_GetBoneInstance(i).mRenderTransform.mul_43(m, m_model->LL_GetData(i).m2b_transform);
	}
}

void player_legs_controller::collapse_bone_branch(u16 branch_root)
{
	if (BI_NONE == branch_root)
		return;

	const Fvector at			= m_model->LL_GetTransform(branch_root).c;
	const u16 skeleton_root		= m_model->LL_GetBoneRoot();
	const u16 count				= m_model->LL_BoneCount();

	for (u16 i=0; i<count; ++i)
	{
		// walk up to see whether this bone belongs to the branch
		u16 b					= i;
		while ((b != branch_root) && (b != skeleton_root))
			b					= m_model->LL_GetData(b).GetParentID();

		if (b != branch_root)
			continue;

		Fmatrix& m				= m_model->LL_GetTransform(i);
		m.scale					(0.f, 0.f, 0.f);
		m.c.set					(at);
		m_model->LL_GetBoneInstance(i).mRenderTransform.mul_43(m, m_model->LL_GetData(i).m2b_transform);
	}
}

void player_legs_controller::update(CActor* actor)
{
	const bool was_drawn		= m_draw;
	m_draw						= false;

	// Only the reasons that are not going to reverse a second later tear the model down. A ladder,
	// a crouch or a glance over the shoulder just stop the DRAWING: destroying here would mean a
	// model_Create every time the player ducks, and it is what made the legs blink out of existence
	// in a low crouch instead of merely being awkwardly close to the camera.
	if (!psActorFlags.test(AF_LEGS) || !actor || !actor->g_Alive())
	{
		destroy					();
		return;
	}

	shared_str sect, model;
	if (!resolve_config(actor, sect, model))
	{
		destroy					();
		return;
	}

	if (!ensure_model(sect, model))
		return;

	// Transient reasons not to draw. The model stays built and keeps following the skeleton, so
	// coming back out of a ladder or a vehicle costs nothing.
	//
	// Control taken away -- a scripted cutscene (level.disable_input) or the intro's "osoznanie"
	// talk: the actor is posed by script and the camera is no longer his eyes, so a body hanging in
	// front of it is nonsense. That is the same decision the self-shadow already makes, and it is
	// made in ONE place -- CActor::UpdateCL, a few lines above this call -- so read the result
	// instead of testing the game state a second time. It carries the 500 ms tail with it, which
	// covers the gap where the talk window closes just before the script disables input.
	if (g_pGamePersistent && g_pGamePersistent->m_bSuppressActorShadow)
		return;

	const u32 mstate			= actor->MovingState();
	const bool low_crouch		= (0 != (mstate & mcCrouch)) && (0 != (mstate & mcAccel));
	if ((actor->cam_ActiveStyle() != eacFirstEye) ||
		actor->Holder() ||
		(mstate & mcClimb) ||
		(low_crouch && !g_legs_in_low_crouch))
		return;

	m_draw						= true;

	copy_bones_from_actor		(actor);

	m_legs_transform.set		(actor->XFORM());

	// ---- which way the model faces ----------------------------------------------------------
	// Its own yaw, held. Two reasons, and neither of them is the offset:
	//   * the actor object's yaw tracks the mouse every frame, so a model placed straight from
	//     XFORM() spins under the player and its feet -- a fifth of a metre off the axis -- sweep
	//     the floor. Tuning the offset never touched this: the offset is not what rotates.
	//   * the SOURCE is the actor's heading, not Device.vCameraDirection, so that camera effectors
	//     (.anm animations, bobbing, the lean) move the view and leave the body where it stands.
	// Standing, the view may twist away up to g_legs_yaw_deadzone before the model follows -- which
	// is what a person does, head and torso first, feet later. Past that it turns after the view at
	// g_legs_yaw_speed until it catches up, and that reads as re-planting the feet. Walking, it just
	// follows, because the feet are moving anyway and there is nothing to hide.
	if (g_legs_yaw_hold)
	{
		const float target		= actor->XFORM().k.getH();
		if (!m_yaw_valid)
		{
			m_yaw				= target;
			m_yaw_valid			= true;
			m_yaw_turning		= false;
		}

		const float d			= angle_difference_signed(target, m_yaw);

		// Walking makes the model follow, but WITHOUT latching m_yaw_turning: latching it there was
		// a bug -- the flag only cleared when the angles met exactly, so after a walk, if the player
		// kept moving the mouse, they never met and the hold never re-engaged. The model then just
		// tracked the mouse again and the body floated while standing still.
		const bool moving		= 0 != (actor->MovingState() & mcAnyMove);
		if (!moving && !m_yaw_turning && (_abs(d) > deg2rad(g_legs_yaw_deadzone)))
			m_yaw_turning		= true;			// twisted far enough -- shuffle the feet round

		if (moving || m_yaw_turning)
		{
			const float step	= deg2rad(g_legs_yaw_speed) * Device.fTimeDelta;
			if (_abs(d) <= step)
			{
				m_yaw			= target;
				m_yaw_turning	= false;
			}
			else
				m_yaw			+= (d > 0.f ? step : -step);
		}

		// ...but g_legs_yaw_speed alone does not bound the LAG. A flick of the mouse turns the view
		// far faster than the feet may, so for as long as the flick lasts the gap just grows -- a
		// quick half turn left the body facing the player's back, legs pointing the other way.
		// So past g_legs_yaw_max_lag the model is carried along with the view, rigidly, whatever
		// the rate; once the view stops, m_yaw_turning lets it finish the turn at the normal speed.
		// Measured against the CAMERA, not the actor heading the turn above chases: the promise is
		// about what the player sees, and the diagonal strafe keeps the actor 45 degrees off the
		// view on purpose (actor.ltx *_strafe_yaw), which the default limit leaves room for.
		Fmatrix view_m;			view_m.setHPB(actor->cam_Active()->GetWorldYaw(), 0.f, 0.f);
		const float view_h		= view_m.k.getH();		// the getH convention m_yaw is kept in
		const float lag			= angle_difference_signed(m_yaw, view_h);
		const float max_lag		= deg2rad(g_legs_yaw_max_lag);
		if (_abs(lag) > max_lag)
		{
			m_yaw				= angle_normalize_signed(view_h + ((lag > 0.f) ? max_lag : -max_lag));
			m_yaw_turning		= true;
		}

		// setHPB, NOT identity+rotateY: getH() reads a heading as direction (-sin h, 0, cos h) and
		// setHPB builds exactly that, while rotateY builds (+sin a, 0, cos a) -- mirrored. Building
		// the matrix with rotateY turned the model the wrong way round, so a quarter turn of the
		// mouse left the player looking at his own body face to face.
		Fvector pos;			pos.set(m_legs_transform.c);
		m_legs_transform.setHPB	(m_yaw, 0.f, 0.f);
		m_legs_transform.c.set	(pos);
	}
	else
		m_yaw_valid				= false;

	// Legacy: park the model under the camera instead of over the actor. OFF -- the camera leans on
	// Q/E and swims under the step effectors, and the body used to ride along with all of it.
	if (g_legs_attach_to_camera)
		m_legs_transform.c.set	(Device.vCameraPosition.x, m_legs_transform.c.y, Device.vCameraPosition.z);

	// ---- which way "back" points -------------------------------------------------------------
	// NOT the model's own heading. Measured: strafing diagonally, the engine turns the ACTOR 45
	// degrees to face where he walks (yaw cam/actor read 190/235 on W+A and 190/145 on W+D), and
	// the model turns with him -- correctly, the legs should point where he is going. But laying
	// the offset along that heading swings the body a third of a metre out from under the camera,
	// which is the "diagonals are shifted" report: |d_actor| stayed 0.5 while its direction turned.
	// So "back" is kept aimed at the VIEW instead -- and, because aiming it at a live camera would
	// drag the model on every mouse movement, it only re-aims when that cannot be seen: while the
	// actor walks (the feet already slide) or while the camera is not pointed down at the body.
	if (!fis_zero(g_legs_body_offset))
	{
		// The VIEW heading, read as the camera's own yaw instead of Device.vCameraDirection.
		// That vector is the direction AFTER the effectors, so a camera .anm dragged the body
		// with it; and looking straight down it is nearly (0,-1,0), whose horizontal part is
		// almost nothing -- zeroing y and normalising there gives a direction that swings on any
		// tremor. Both halves of the report. The yaw is a separate scalar: exact at any pitch,
		// and no effector writes to it. setHPB to match the convention m_legs_transform uses.
		Fmatrix aim_m;			aim_m.setHPB(actor->cam_Active()->GetWorldYaw(), 0.f, 0.f);
		Fvector aim				= aim_m.k;

		if (!m_offset_dir_valid)
		{
			m_offset_dir		= aim;
			m_offset_dir_valid	= true;
		}
		else
		{
			// Re-aim exactly when the model is re-planting its feet anyway: walking, or turning
			// after a view that twisted past the deadzone. Standing and merely looking around,
			// both the model heading and this direction are frozen, so nothing moves at all.
			// (The previous gate asked whether the camera pointed down -- a guess about what the
			// player can see, and it opened while he was staring straight at the body.)
			const bool free		= (0 != (actor->MovingState() & mcAnyMove)) || m_yaw_turning;

			float k				= free ? (g_legs_align_speed * Device.fTimeDelta) : 0.f;
			clamp				(k, 0.f, 1.f);
			m_offset_dir.lerp	(m_offset_dir, aim, k);
			m_offset_dir.y		= 0.f;
			// normalize_safe() with NO argument. The overload that takes one does not mean "normalize
			// me, fall back to v" -- it normalizes V AND STORES IT IN ME (_vector3d.h:220). Written as
			// normalize_safe(aim) this line overwrote the frozen direction with the camera every frame,
			// whatever k said, which is the whole "the legs still float" saga.
			m_offset_dir.normalize_safe();

			// ...and however long it stays frozen, it may never lag the view by more than the same
			// deadzone the feet use. Without this clamp the freeze had no end: the actor's own
			// heading holds too (that is what actor_legs_relaxed does), so nothing ever asked the
			// direction to catch up, and a slow turn on the spot left the body standing in FRONT of
			// the player -- looking at his own back. Past the limit the direction is dragged along,
			// which is both the natural read and self-masking: it only moves while the view moves.
			const float dir_yaw	= m_offset_dir.getH();
			const float lag		= angle_difference_signed(dir_yaw, aim.getH());
			const float limit	= deg2rad(g_legs_yaw_deadzone);
			if (_abs(lag) > limit)
			{
				const float h	= aim.getH() + ((lag > 0.f) ? limit : -limit);
				m_offset_dir.set(-_sin(h), 0.f, _cos(h));	// the heading convention getH/setHPB use
			}
		}

		// Sprinting drops the body a little further back, and eases back when it ends. Blended,
		// not switched: sprint starts and stops abruptly and a step change would read as a jolt.
		const float sprint_tgt	= (actor->MovingState() & mcSprint) ? 1.f : 0.f;
		float sk				= g_legs_sprint_speed * Device.fTimeDelta;
		clamp					(sk, 0.f, 1.f);
		m_sprint_blend			+= (sprint_tgt - m_sprint_blend) * sk;

		m_legs_transform.c.mad	(m_offset_dir, g_legs_body_offset + g_legs_sprint_offset * m_sprint_blend);
	}

	// Optional: park the hips over the actor instead of the model origin, cancelling the body
	// displacement a locomotion clip carries of its own (measured at 2-7 cm). OFF by default --
	// it is a correction that slides the whole model whenever the clip changes, which is exactly
	// the kind of sliding this feature keeps being reported for. The vertical is left alone in any
	// case, since crouching is supposed to lower the body.
	const u16 pelvis			= m_model->LL_BoneID(PELVIS_BONE);
	if (g_legs_anchor_pelvis && (BI_NONE != pelvis))
	{
		Fvector local			= m_model->LL_GetTransform(pelvis).c;
		local.y					= 0.f;

		Fvector drift;
		m_legs_transform.transform_dir(drift, local);
		m_legs_transform.c.x	-= drift.x;
		m_legs_transform.c.z	-= drift.z;
	}

	// Last, because it needs the model's final place: the lean is measured against the eyes.
	// Coming back from a frame that was not drawn (ladder, vehicle, third person) it snaps straight
	// to where it should be -- easing in from a stale angle would show the chest for a moment.
	clamp_torso_yaw				(actor);
	lean_torso_off_camera		(actor, !was_drawn);
}

// Heading of the chest in MODEL space, in the getH convention: the normal of the shoulder line,
// on the side the bind pose calls the front.
bool player_legs_controller::chest_heading(float& h) const
{
	const u16 l					= m_model->LL_BoneID(CLAVICLE_L);
	const u16 r					= m_model->LL_BoneID(CLAVICLE_R);
	if ((BI_NONE == l) || (BI_NONE == r))
		return					false;

	Fvector side;				side.sub(m_model->LL_GetTransform(r).c, m_model->LL_GetTransform(l).c);
	Fvector up;					up.set(0.f, 1.f, 0.f);
	Fvector fwd;				fwd.crossproduct(side, up);
	fwd.mul						(m_chest_sign);
	fwd.y						= 0.f;
	if (fwd.square_magnitude() < EPS)
		return					false;

	fwd.normalize				();
	h							= fwd.getH();
	return						true;
}

// THE ARMS TURNED ROUND.
// g_legs_yaw_max_lag keeps the FEET within reach of the view, but the chest is not placed by the
// model heading alone: the actor's torso callbacks (Spin0/Spin1/ShoulderCallback) twist his spine
// by the gap between the camera and HIS OWN heading, and that pose is copied here as it is. His
// heading lags on a flick with rules of its own, so the copied twist and our clamped m_yaw add up
// to whatever they add up to -- with empty hands the arms are drawn, and a fast turn could leave
// them pointing behind the player.
// So the chest is measured where it actually ended up, and past g_legs_torso_max_lag off the view
// the whole spine branch is turned about the vertical through bip01_spine until it is back at the
// limit. Before the lean: that one bends away from the view, and should bend what is facing it.
void player_legs_controller::clamp_torso_yaw(CActor* actor)
{
	const u16 spine				= m_model->LL_BoneID(SPINE_BONE);
	if (BI_NONE == spine)
		return;

	float chest_h;
	if (!chest_heading(chest_h))
		return;

	// Everything in the getH convention. The model only turns about y, so a heading in model space
	// plus the model's own is the heading in the world.
	Fmatrix view_m;				view_m.setHPB(actor->cam_Active()->GetWorldYaw(), 0.f, 0.f);
	const float view_h			= view_m.k.getH();
	const float world_h			= m_legs_transform.k.getH() + chest_h;
	const float lag				= angle_difference_signed(world_h, view_h);
	const float max_lag			= deg2rad(g_legs_torso_max_lag);
	if (_abs(lag) <= max_lag)
		return;

	// Turn the heading by dh = back towards the view. For a rotation about +y by t the heading goes
	// to -t (getH of (sin t, 0, cos t) is -t), hence the minus.
	const float dh				= ((lag > 0.f) ? max_lag : -max_lag) - lag;
	Fmatrix rot;				rot.identity();
	const float c				= _cos(-dh);
	const float sn				= _sin(-dh);
	rot.i.set					(c, 0.f, -sn);	// Rodrigues about +y: v cos + (y x v) sin
	rot.j.set					(0.f, 1.f, 0.f);
	rot.k.set					(sn, 0.f, c);

	const Fvector pivot			= m_model->LL_GetTransform(spine).c;
	Fvector rp;					rot.transform_dir(rp, pivot);
	rot.c.sub					(pivot, rp);

	transform_bone_branch		(spine, rot);
}

// THE CHEST IN THE LENS.
// Some animations -- leaning over a weapon, the crouches, the sprint -- bring the upper chest forward
// far enough that the camera ends up in front of it, or inside it, and the player looks down at the
// inside of his own jacket. Fixing the clips is out of the question: they are shared with the third
// person view and with the SHADOW, and there are hundreds of them.
// But this model is ours alone. The shadow is cast by the actor's own skeleton (see
// CActor::renderable_Render), so whatever is done to these bones stays in first person.
//
// So: a vertical plane g_legs_lean_margin behind the eyes, facing the view, and the chest bones are
// kept behind it by bending the whole spine branch back around bip01_spine. A ROTATION about the
// waist, not a shift like g_legs_fwd_offset: a shift pulls the torso off the hips, a bend keeps the
// waist where the pelvis is and only changes the angle the skinning already blends across.
// Only as much as the pose needs: standing straight nothing moves at all.
//
// The plane is horizontal-only on purpose. The first-person camera stands right over the actor
// origin (CActor::cam_Update: XFORM().c plus the eye height), so the eyes are known from the actor
// alone, this frame -- the camera itself is updated AFTER this in UpdateCL, and a one-frame-old
// camera position is ten centimetres off at a sprint. The Q/E lookout moves the eyes sideways,
// along the plane, where it changes nothing.
void player_legs_controller::lean_torso_off_camera(CActor* actor, bool snap)
{
	const u16 spine				= m_model->LL_BoneID(SPINE_BONE);
	if (!g_legs_lean || (BI_NONE == spine))
	{
		m_lean					= 0.f;
		return;
	}

	Fmatrix inv;				inv.invert(m_legs_transform);

	// the view heading in model space, flattened
	Fmatrix aim_m;				aim_m.setHPB(actor->cam_Active()->GetWorldYaw(), 0.f, 0.f);
	Fvector aim;				inv.transform_dir(aim, aim_m.k);
	aim.y						= 0.f;
	if (aim.square_magnitude() < EPS)
		return;
	aim.normalize				();

	Fvector eye;				inv.transform_tiny(eye, actor->XFORM().c);
	const Fvector pivot			= m_model->LL_GetTransform(spine).c;

	// Every probe has to end up with its forward coordinate (along aim, measured from the pivot)
	// at most t. Bending back by a turns a point at (forward f, up u) = R*(sin p, cos p) into
	// R*sin(p - a), so the bend it asks for is p - asin(t/R). The most demanding probe wins.
	Fvector to_eye;				to_eye.sub(eye, pivot);
	const float t				= aim.dotproduct(to_eye) - g_legs_lean_margin;
	float target				= 0.f;
	for (u32 i=0; i<sizeof(LEAN_PROBES)/sizeof(LEAN_PROBES[0]); ++i)
	{
		const u16 id			= m_model->LL_BoneID(LEAN_PROBES[i]);
		if (BI_NONE == id)
			continue;

		Fvector r;				r.sub(m_model->LL_GetTransform(id).c, pivot);
		const float f			= aim.dotproduct(r);
		if (f <= t)
			continue;			// already behind the plane

		const float u			= r.y;
		const float R			= _sqrt(f*f + u*u);
		if (R < EPS_L)
			continue;

		float ratio				= t / R;
		clamp					(ratio, -1.f, 1.f);
		target					= _max(target, atan2f(f, u) - asinf(ratio));
	}
	clamp						(target, 0.f, deg2rad(g_legs_lean_max));

	if (snap)
		m_lean					= target;
	else
	{
		float k					= ((target > m_lean) ? g_legs_lean_speed_in : g_legs_lean_speed_out) * Device.fTimeDelta;
		clamp					(k, 0.f, 1.f);
		m_lean					+= (target - m_lean) * k;
	}

	if (m_lean < deg2rad(0.1f))
		return;

	// Rotation about n = aim x up: for a positive angle that carries "up" towards -aim, i.e. the
	// top of the spine goes BACK. Built by hand (Rodrigues) rather than Fmatrix::rotation so the
	// direction does not hang on that function's handedness. The rows of an Fmatrix are the images
	// of the basis vectors (transform_tiny is v.x*i + v.y*j + v.z*k + c).
	Fvector up;					up.set(0.f, 1.f, 0.f);
	Fvector n;					n.crossproduct(aim, up);
	n.normalize					();
	const float c				= _cos(m_lean);
	const float sn				= _sin(m_lean);

	Fmatrix xf;					xf.identity();
	Fvector* rows[3]			= { &xf.i, &xf.j, &xf.k };
	for (int a=0; a<3; ++a)
	{
		Fvector v;				v.set(0.f, 0.f, 0.f);
		v[a]					= 1.f;
		Fvector nxv;			nxv.crossproduct(n, v);
		rows[a]->set			(v);
		rows[a]->mul			(c);
		rows[a]->mad			(nxv, sn);
		rows[a]->mad			(n, n.dotproduct(v) * (1.f - c));
	}
	// about the pivot: c = pivot - R(pivot)
	Fvector rp;					xf.transform_dir(rp, pivot);
	xf.c.sub					(pivot, rp);

	transform_bone_branch		(spine, xf);
}

void player_legs_controller::render()
{
	if (!m_model || !m_draw)
		return;

	IRenderVisual* visual		= m_model->dcast_RenderVisual();
	if (!visual)
		return;

	::Render->set_Transform		(&m_legs_transform);
	::Render->add_Visual		(visual);
}
