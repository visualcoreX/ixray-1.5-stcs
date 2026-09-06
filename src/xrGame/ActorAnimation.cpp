#include "stdafx.h"
#include "Actor.h"
#include "ActorAnimation.h"
#include "actor_anim_defs.h"

#include "hudmanager.h"
#include "UI.h"
#include "weapon.h"
#include "inventory.h"
#include "missile.h"
#include "level.h"
#ifdef DEBUG
#include "PHDebug.h"
#endif
#include "hit.h"
#include "PHDestroyable.h"
#include "Car.h"
#include "../Include/xrRender/Kinematics.h"
#include "ai_object_location.h"
#include "game_cl_base.h"
#include "../xrEngine/motion.h"
#include "artefact.h"
#include "IKLimbsController.h"
#include "player_hud.h"
#include "CustomDetector.h"
#include "Bolt.h"
#include "PDA.h"

static const float y_spin0_factor		= 0.0f;
static const float y_spin1_factor		= 0.4f;
static const float y_shoulder_factor	= 0.4f;
static const float y_head_factor		= 0.2f;
static const float p_spin0_factor		= 0.0f;
static const float p_spin1_factor		= 0.2f;
static const float p_shoulder_factor	= 0.7f;
static const float p_head_factor		= 0.1f;
static const float r_spin0_factor		= 0.3f;
static const float r_spin1_factor		= 0.3f;
static const float r_shoulder_factor	= 0.2f;
static const float r_head_factor		= 0.2f;

// ---- torso-set yaw correction ------------------------------------------------------------------
// The xrMPE animation pack bakes a bladed stance into its torso motions. Composed through the bone
// chain exactly as the engine does it (mTransform = local*parent), the chest bone bip01_spine2 ends
// up 20..42 deg further round than in the stock sets -- e.g. norm_torso_3_aim_1 (BM-16, slot 3) sits
// at +27.8 deg against the stock +7.6, and every rifle set at +45.0. The actor's own yaw callbacks
// add nothing while standing still (r_torso.yaw - r_model_yaw is ~0 inside the 45 deg dead zone in
// g_cl_Orientate), so that baked twist is what you see: the character stands turned off centre.
// [actor_torso_yaw] in creatures\actor.ltx carries the correction in DEGREES per torso set -- key =
// the weapon's `actor_anim_group`, or its numeric `animation_slot` when it names no group, or "0"
// with nothing in hand. Absent key = 0 = stock behaviour. `actor_torso_yaw` is a global trim on top,
// for dialling this in without a rebuild.
// The value adds to bone_yaw in Spin1Callback (bip01_spine1) and therefore rotates the WHOLE upper
// body -- chest, neck, head and both arms hang off that bone.
#define ACTOR_TORSO_YAW_SECT "actor_torso_yaw"
// Same idea, same keys, but for bip01_neck -- and it is NOT a duplicate of the torso one. Measured
// on the pack's detector gestures: drawdevice_0 sits 0.0 deg off the idle at bip01_spine, 7.7 at
// spine2 and 29.8 at the NECK. A spine1 correction rotates the upper body rigidly and so cannot say
// "chest where it was, neck round" -- that is why four passes at the torso value changed nothing
// visible. The head is deliberately left alone: it hangs off the neck and follows it.
// Stock CS puts no callback on bip01_neck at all (spine / spine1 / spine2 / head only), so one is
// installed in CActor::SetCallbacks. Values here are what is LEFT after the torso key has already
// carried the neck along: neck_key = (baked_neck - torso_key) - idle_neck.
#define ACTOR_NECK_YAW_SECT "actor_neck_yaw"
float g_actor_torso_yaw = 0.f;

// Which of the two stances the actor holds when the pack ships both (see actor_anim_defs.h). It
// switches the LEGS ("_0" braced / "_1" relaxed, + <base>_turn_safe) and the TORSO (aim_1/2/3 /
// idle_1/walk_1/run_1) TOGETHER -- they are two halves of one pose and mixing them reads as a
// twisted upper body, because the relaxed leg cycles carry a much less bladed pelvis and spine.
//   0 = stock braced only (feet planted apart, the vanilla stance)
//   1 = relaxed with nothing in hand (default; a weapon in hand IS the reason to stand braced)
//   2 = relaxed until the actor aims
//   3 = relaxed always
// Anything but 0 degrades to the stock set by itself when the relaxed one is incomplete, so this is
// safe with the vanilla animations (they carry a lone norm_walk_fwd_1 and no back/strafe twin).
int g_actor_legs_relaxed = 1;

// How hard the spine/head chase the camera with NOTHING in hand. 0 = they do not (xrMPE behaviour,
// the pack's empty-hands set animates the whole body itself), 1 = same as with a weapon.
float g_actor_torso_follow_empty = 1.f;	// was 0: the working setup wants the horizontal chase on
// ...and the same for the VERTICAL half of that chase alone, so the body can turn with the camera
// without tipping forward/back with it. -1 = follow actor_torso_follow_empty (the old, coupled
// behaviour and the default, so nothing changes unless this is set); 0 = horizontal only.
// Only the empty-hands case is split: with a weapon in hand both halves stay at 1, as before.
// bone_roll is deliberately untouched -- it never rode this factor in the first place.
float g_actor_torso_follow_empty_pitch = 0.f;	// was -1 (= tied to the yaw knob): horizontal only
// Seconds over which m_fTorsoFollowCam / m_fTorsoYawFix ease to their new value. 0 = snap (stock).
float g_actor_torso_blend = 0.25f;
// ...and the yaw correction gets its own, so the two can be tuned apart -- but it keeps the SAME
// soft 0.25 s by default (user's call: the gentle roll-in is wanted). I briefly cut it to 0.04
// thinking the roll was the bug behind "the body turns during the gesture and swings back"; it was
// not -- that was the neck, see ACTOR_NECK_YAW_SECT. Note the ease is an exponential approach:
// 63% of the way in one blend time, 95% in three. 0 = snap.
float g_actor_torso_yaw_blend = 0.25f;
// Diagonal leg cycles (<base>_walk_fwd_ls_0 and friends) when moving forward/back AND strafing.
// 0 = off (stock: the straight cycle wins), 1 = only in the relaxed stance, 2 = always. The braced
// diagonals read as a forward walk rotated sideways, the relaxed ones hold their chest heading.
// OFF: measured, the pack's diagonal is the straight cycle with the HIPS turned up to 53 deg, so
// even with the chest holding its heading the body still reads as swung into the movement direction.
int g_actor_legs_diagonal = 0;
// Playback speed of the WALK cycles (legs + torso together), 1.0 = as authored. The pack's walk is
// slower than the distance the actor actually covers, so the feet skate.
float g_actor_walk_anim_speed = 1.1f;
// Use the pack's "<x>+detector" torso SETS while the detector is out (1 = on, 0 = the weapon keeps
// its own set as if no detector existed). The chest and the gun arm turning left with the device is
// CORRECT -- xrMPE itself does exactly that (user, 2026-08-09) -- so no yaw correction goes with it.
// Two earlier attempts are dead ends, kept in the comments so they are not retried: straightening
// the chest with [actor_torso_yaw] rotates the gun arm too, and giving the left arm its own
// partition does not work because X-Ray partitions must be DISJOINT and bip01_l_clavicle..l_hand
// already belong to "torso" -- both blends then write the same bones and the arm averages out.
int g_actor_detector_torso = 1;
// Play the pack's drawdevice_0 / holsterdevice_0 while the device goes in and out of the off hand.
// 0 = keep the idle for the whole trip. Worth trying if the gesture reads as a lurch: those motions
// are posed differently from the set's idle in more than just chest yaw, and a single spine1
// correction (actor_torso_yaw) can only cancel a rigid rotation, not a different pose.
int g_actor_detector_gesture = 1;
// Blend speed for the leg cycles when one replaces another: 0 = use the speed baked into the
// motion (stock, a very short fade -- straight <-> diagonal and walk <-> run pop), otherwise this
// accrue/falloff. LOWER = longer cross-fade; the pack's own values are 2..6.
float g_actor_legs_blend = 2.5f;
// Live trim, DEGREES, applied on top of [actor_torso_yaw] but ONLY while a "<x>+detector" set is
// playing -- the global actor_torso_yaw would drag every other weapon with it. Dial it in game, then
// bake the result into the config as `pistol+detector = ...` and friends.
float g_actor_detector_yaw = 0.f;
// Stretch/squeeze the third-person torso motion so it ends together with the FIRST-person one the
// player is watching (CHudItem::MotionEndTm). The two sets are authored independently, so a reload
// that takes 2.5 s in the hands can be 1.8 s on the body. 0 = off, play both at their own pace.
int g_actor_torso_sync_hud = 1;	// was 0
// esmSyncPart scrubs the torso blend to the LEGS' phase every frame (bottom of g_SetAnimation). That
// is only meaningful for a matched PAIR -- the pack authors them the same length, so the ratio is
// exactly 1.0 (walk legs 33 frames <-> torso aim_2 33, run 24 <-> aim_3 24, idle 401 <-> aim_1 401).
// The AIM stance breaks the pairing: the actor holds aim_0 while the legs walk, and where the pack
// left the sync bit on a 401-frame aim_0 (slots 0/1/5/6/11/13 and pda; the named rifle groups and
// slot 2 have it cleared) 13 s of animation get replayed inside every 1.1 s step and snap back --
// on screen the upper body vibrates. Reported for pistols (slot 1) 2026-08-10.
// This is the largest torso:legs length ratio still considered a pair. 0 = no guard = stock.
float g_actor_torso_sync_part = 1.5f;

// `action` is an optional suffix tried before the plain key. Some sets in the pack are not
// internally consistent -- 8_mini (MP5 / AKS-74u / the slot-8 family) has its reload baked 17 deg
// round from its own idle, which on screen is the torso swinging left the moment a reload starts --
// so a `<key>_reload` line corrects just that action. No such line = the set's own value = old
// behaviour.
// ---- left-arm partition -------------------------------------------------------------------------
// The pack's "<x>+detector" sets differ from the weapon's own mainly in the OFF hand, but they are
// whole-torso motions: played on the torso partition they dragged the chest, the head and the gun
// arm round with them (measured: pistol+detector chest sits 32 deg off the pistol set). So they go
// on the left arm alone and the torso keeps whatever the weapon asked for.
// MAX_PARTS is 4 and the pack defines three (legs / torso / head), so index 3 is free. An undefined
// partition simply never plays (SkeletonAnimated.cpp:311 tests part(i).Name), so filling it in is
// additive -- nothing else in the game addresses partition 3.
#define ACTOR_LARM_PART 3
static void ensure_larm_partition(IKinematicsAnimated* KA, IKinematics* K)
{
	if (!KA || !K)											return;
	CPartition& parts = const_cast<CPartition&>(KA->partitions());
	if (parts.part(ACTOR_LARM_PART).Name.size())			return;		// already built

	static LPCSTR s_bones[] = {
		"bip01_l_clavicle", "bip01_l_upperarm", "bip01_l_forearm", "bip01_l_hand",
		"bip01_l_finger0", "bip01_l_finger01", "bip01_l_finger02",
		"bip01_l_finger1", "bip01_l_finger11", "bip01_l_finger12",
		"bip01_l_finger2", "bip01_l_finger21", "bip01_l_finger22",
	};
	CPartDef& P = parts[ACTOR_LARM_PART];
	P.bones.clear();
	for (int i=0; i<int(sizeof(s_bones)/sizeof(s_bones[0])); ++i)
	{
		const u16 id = K->LL_BoneID(s_bones[i]);
		if (BI_NONE != id)	P.bones.push_back(id);
	}
	// name last: it is the "is this partition defined" flag, so a half-filled one is never playable
	if (P.bones.size())		P.Name = "l_arm";
	else					Msg("! actor l_arm partition: no left-arm bones on this visual");
}

static float yaw_fix_from(LPCSTR sect, LPCSTR key, LPCSTR action)
{
	if (!key || !key[0])						return 0.f;
	if (!pSettings->section_exist(sect))		return 0.f;
	if (action && action[0])
	{
		string128	k;
		strconcat	(sizeof(k), k, key, action);
		if (pSettings->line_exist(sect, k))		return pSettings->r_float(sect, k);
	}
	if (!pSettings->line_exist(sect, key))		return 0.f;
	return pSettings->r_float(sect, key);
}
static float neck_yaw_fix(LPCSTR key, LPCSTR action = NULL)	{ return yaw_fix_from(ACTOR_NECK_YAW_SECT, key, action); }

static float torso_yaw_fix(LPCSTR key, LPCSTR action = NULL)
{
	if (!key || !key[0])									return 0.f;
	if (!pSettings->section_exist(ACTOR_TORSO_YAW_SECT))	return 0.f;
	if (action && action[0])
	{
		string128	k;
		strconcat	(sizeof(k), k, key, action);
		if (pSettings->line_exist(ACTOR_TORSO_YAW_SECT, k))	return pSettings->r_float(ACTOR_TORSO_YAW_SECT, k);
	}
	if (!pSettings->line_exist(ACTOR_TORSO_YAW_SECT, key))	return 0.f;
	return pSettings->r_float(ACTOR_TORSO_YAW_SECT, key);
}

void  CActor::Spin0Callback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);

	Fmatrix				spin;
	float				bone_yaw	= angle_normalize_signed(A->r_torso.yaw - A->m_fModelYawVis)*y_spin0_factor*A->m_fTorsoFollowCam;
	float				bone_pitch	= angle_normalize_signed(A->r_torso.pitch)*p_spin0_factor*A->m_fTorsoFollowCamPitch;
	float				bone_roll	= angle_normalize_signed(A->r_torso.roll)*r_spin0_factor;
	Fvector c			= B->mTransform.c;
	spin.setXYZ			(-bone_pitch,bone_yaw,bone_roll);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}
void  CActor::Spin1Callback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);

	Fmatrix				spin;
	// mulA_43 below composes world = world*spin, so `spin` acts in MODEL space, not in the bone's
	// own frame: bone_yaw turns the bone, and everything under it, about the model's Y.
	// SIGN, established in game and not by derivation: a chest that sits N degrees too far round
	// is straightened with bone_yaw -= N, so the [actor_torso_yaw] values are the measured offsets
	// negated. Deriving it from setHPB/mul_43 gave the opposite answer and was wrong.
	float				bone_yaw	= angle_normalize_signed(A->r_torso.yaw - A->m_fModelYawVis)*y_spin1_factor*A->m_fTorsoFollowCam + A->m_fTorsoYawFix;
	float				bone_pitch	= angle_normalize_signed(A->r_torso.pitch)*p_spin1_factor*A->m_fTorsoFollowCamPitch;
	float				bone_roll	= angle_normalize_signed(A->r_torso.roll)*r_spin1_factor;
	Fvector c			= B->mTransform.c;
	spin.setXYZ			(-bone_pitch,bone_yaw,bone_roll);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}
void  CActor::ShoulderCallback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);
	Fmatrix				spin;
	float				bone_yaw	= angle_normalize_signed(A->r_torso.yaw - A->m_fModelYawVis)*y_shoulder_factor*A->m_fTorsoFollowCam;
	float				bone_pitch	= angle_normalize_signed(A->r_torso.pitch)*p_shoulder_factor*A->m_fTorsoFollowCamPitch;
	float				bone_roll	= angle_normalize_signed(A->r_torso.roll)*r_shoulder_factor;
	Fvector c			= B->mTransform.c;
	spin.setXYZ			(-bone_pitch,bone_yaw,bone_roll);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}
void  CActor::HeadCallback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);
	Fmatrix				spin;
	float				bone_yaw	= angle_normalize_signed(A->r_torso.yaw - A->m_fModelYawVis)*y_head_factor*A->m_fTorsoFollowCam;
	float				bone_pitch	= angle_normalize_signed(A->r_torso.pitch)*p_head_factor*A->m_fTorsoFollowCamPitch;
	float				bone_roll	= angle_normalize_signed(A->r_torso.roll)*r_head_factor;
	Fvector c			= B->mTransform.c;
	spin.setXYZ			(-bone_pitch,bone_yaw,bone_roll);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}

// bip01_neck. Stock CS animates this bone from the motion alone; the correction is added the same
// way Spin1Callback does it, in MODEL space, so it turns the neck and everything under it (the head)
// without touching the chest.
void  CActor::NeckCallback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);
	if (fis_zero(A->m_fNeckYawFix))	return;
	Fmatrix				spin;
	Fvector c			= B->mTransform.c;
	spin.setXYZ			(0.f, A->m_fNeckYawFix, 0.f);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}

void  CActor::VehicleHeadCallback(CBoneInstance* B)
{
	CActor*	A			= static_cast<CActor*>(B->callback_param());	VERIFY	(A);
	Fmatrix				spin;
	float				bone_yaw	= angle_normalize_signed(A->r_torso.yaw)*0.75f;
	float				bone_pitch	= angle_normalize_signed(A->r_torso.pitch)*0.75f;
	float				bone_roll	= angle_normalize_signed(A->r_torso.roll)*r_head_factor;
	Fvector c			= B->mTransform.c;
	spin.setHPB			(bone_yaw,bone_pitch,-bone_roll);
	B->mTransform.mulA_43(spin);
	B->mTransform.c		= c;
}

void STorsoWpn::Create(IKinematicsAnimated* K, LPCSTR base0, LPCSTR base1)
{
	char			buf[128];
	moving[eIdle]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_aim_1"));
	moving[eWalk]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_aim_2"));
	moving[eRun]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_aim_3"));
	moving[eSprint]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_escape_0"));
	// ...and the relaxed twin (see actor_anim_defs.h). The sprint has none, so it stays shared.
	relaxed_moving[eIdle]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_idle_1"));
	relaxed_moving[eWalk]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_walk_1"));
	relaxed_moving[eRun]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_run_1"));
	relaxed_moving[eSprint]	= moving[eSprint];
	// An authoring slip in the pack: norm_torso_0+detector_idle_1 carries esmStopAtEnd, so this
	// looping stance would play once and freeze on its last frame. Clearing the bit is safe here --
	// these motions come from the actor-only extra slot (see extra_motions / CActor::OnChangeVisual),
	// nothing else plays them -- and it is the flag, not the animation, that is wrong.
	for (int i=eIdle; i<=eRun; ++i)
	{
		if (!relaxed_moving[i])		continue;
		CMotionDef* d = K->LL_GetMotionDef(relaxed_moving[i]);
		if (d && (d->flags & esmStopAtEnd))	d->flags = u16(d->flags & ~esmStopAtEnd);
	}
	has_relaxed		= relaxed_moving[eIdle] && relaxed_moving[eWalk] && relaxed_moving[eRun];
	zoom			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_aim_0"));
	holster			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_holster_0"));
	draw			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_draw_0"));
	draw_device		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_drawdevice_0"));
	holster_device	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_holsterdevice_0"));
	draw_all		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_drawall_0"));
	holster_all		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_holsterall_0"));
	reload			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_reload_0"));
	reload_1		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_reload_1"));
	reload_2		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_reload_2"));
	reload_half		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_reload_half_0"));
	reload_half_1	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_reload_half_1"));
	reload_half_2	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_reload_half_2"));
	drop			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_drop_0"));
	attack			= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_attack_1"));
	attack_zoom		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_attack_0"));
	fire_idle		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_attack_1"));
	fire_end		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_torso",base1,"_attack_2"));
	// A second slip of the same kind, and the one behind "the head and arms run out of step with the
	// body while sprinting with the detector out": norm_torso_0+detector_escape_0 is the ONLY motion
	// of the pack's 961 _torso_ cycles whose def says bone_or_part = BI_NONE instead of the torso
	// partition. CKinematicsAnimated::LL_PlayCycle treats BI_NONE as "every partition"
	// (SkeletonAnimated.cpp:305) and unrolls the cycle onto legs, torso AND head, so the sprint
	// cycle drove the legs over their own cycle and the head over head_idle_0 -- which reads exactly
	// as the head swinging and the arms losing the body's rhythm. Only that one combination showed it:
	// the set is the empty-hands one, so it needs the detector out and no weapon in hand, and escape_0
	// is the sprint.
	// Put the whole torso set back on the torso partition, taking the index from a member of this very
	// set rather than hardcoding it. The _all_*_attack_* members below are NOT touched: they are the
	// full-body throws, and BI_NONE is correct for them.
	{
		u16 torso_part = u16(1);					// the pack's torso partition; 961 motions agree
		if (moving[eIdle])
		{
			CMotionDef* d0 = K->LL_GetMotionDef(moving[eIdle]);
			if (d0 && BI_NONE != d0->bone_or_part)	torso_part = d0->bone_or_part;
		}
		MotionID* const set[] = {
			&moving[eIdle], &moving[eWalk], &moving[eRun], &moving[eSprint],
			&relaxed_moving[eIdle], &relaxed_moving[eWalk], &relaxed_moving[eRun], &relaxed_moving[eSprint],
			&zoom, &holster, &draw, &draw_device, &holster_device, &draw_all, &holster_all,
			&reload, &reload_1, &reload_2, &reload_half, &reload_half_1, &reload_half_2,
			&drop, &attack, &attack_zoom, &fire_idle, &fire_end };
		for (int i = 0; i < int(sizeof(set)/sizeof(set[0])); ++i)
		{
			if (!*set[i])	continue;
			CMotionDef* d = K->LL_GetMotionDef(*set[i]);
			if (d && d->bone_or_part != torso_part)		d->bone_or_part = torso_part;
		}
		// ...and the SAME motion is missing esmSyncPart, which is the other half of the report ("the
		// head and arms run out of step with the rest"). That flag scrubs the torso blend to the LEGS'
		// phase every frame; without it the upper body cycles at its own rate against the leg cycle and
		// the two drift in and out of step -- a slow beat, which is why it looked fine some of the time.
		// 175 of the pack's 176 locomotion torso cycles carry it, and 39 of its 40 *_escape_0; only the
		// locomotion members are touched here, because that is where riding the legs' phase is the point.
		MotionID* const loco[] = {
			&moving[eIdle], &moving[eWalk], &moving[eRun], &moving[eSprint],
			&relaxed_moving[eIdle], &relaxed_moving[eWalk], &relaxed_moving[eRun], &relaxed_moving[eSprint] };
		for (int i = 0; i < int(sizeof(loco)/sizeof(loco[0])); ++i)
		{
			if (!*loco[i])	continue;
			CMotionDef* d = K->LL_GetMotionDef(*loco[i]);
			if (d && !(d->flags & esmSyncPart))		d->flags = u16(d->flags | esmSyncPart);
		}
	}
	all_attack_0	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_all",base1,"_attack_0"));
	all_attack_1	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_all",base1,"_attack_1"));
	all_attack_2	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,"_all",base1,"_attack_2"));
}
void STorsoWpn::FillGapsFrom(const STorsoWpn& src)
{
	for (int i=0; i<eTotal; ++i)
	{
		if (!moving[i])			moving[i]			= src.moving[i];
		if (!relaxed_moving[i])	relaxed_moving[i]	= src.relaxed_moving[i];
	}
	has_relaxed		= relaxed_moving[eIdle] && relaxed_moving[eWalk] && relaxed_moving[eRun];
	if (!zoom)			zoom			= src.zoom;
	if (!holster)		holster			= src.holster;
	if (!draw)			draw			= src.draw;
	if (!draw_device)	draw_device		= src.draw_device;
	if (!holster_device)holster_device	= src.holster_device;
	if (!draw_all)		draw_all		= src.draw_all;
	if (!holster_all)	holster_all		= src.holster_all;
	if (!drop)			drop			= src.drop;
	if (!reload)		reload			= src.reload;
	if (!reload_1)		reload_1		= src.reload_1;
	if (!reload_2)		reload_2		= src.reload_2;
	if (!reload_half)	reload_half		= src.reload_half;
	if (!reload_half_1)	reload_half_1	= src.reload_half_1;
	if (!reload_half_2)	reload_half_2	= src.reload_half_2;
	if (!attack)		attack			= src.attack;
	if (!attack_zoom)	attack_zoom		= src.attack_zoom;
	if (!fire_idle)		fire_idle		= src.fire_idle;
	if (!fire_end)		fire_end		= src.fire_end;
	if (!all_attack_0)	all_attack_0	= src.all_attack_0;
	if (!all_attack_1)	all_attack_1	= src.all_attack_1;
	if (!all_attack_2)	all_attack_2	= src.all_attack_2;
}

void SAnimState::Create(IKinematicsAnimated* K, LPCSTR base0, LPCSTR base1)
{
	char			buf[128];
	legs_fwd		= K->ID_Cycle(strconcat(sizeof(buf),buf,base0,base1,"_fwd_0"));
	legs_back		= K->ID_Cycle(strconcat(sizeof(buf),buf,base0,base1,"_back_0"));
	legs_ls			= K->ID_Cycle(strconcat(sizeof(buf),buf,base0,base1,"_ls_0"));
	legs_rs			= K->ID_Cycle(strconcat(sizeof(buf),buf,base0,base1,"_rs_0"));
	legs_fwd_ls		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_fwd_ls_0"));
	legs_fwd_rs		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_fwd_rs_0"));
	legs_back_ls	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_back_ls_0"));
	legs_back_rs	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_back_rs_0"));
	// ...and the relaxed twins, Safe because only the xrMPE pack ships them (see actor_anim_defs.h)
	relaxed_fwd		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_fwd_1"));
	relaxed_back	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_back_1"));
	relaxed_ls		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_ls_1"));
	relaxed_rs		= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_rs_1"));
	relaxed_fwd_ls	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_fwd_ls_1"));
	relaxed_fwd_rs	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_fwd_rs_1"));
	relaxed_back_ls	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_back_ls_1"));
	relaxed_back_rs	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base0,base1,"_back_rs_1"));
	has_relaxed		= relaxed_fwd && relaxed_back && relaxed_ls && relaxed_rs;
}


void SActorState::CreateClimb(IKinematicsAnimated* K)
{
	string128		buf,buf1;
	string16		base;

	m_torso_named.clear();
	//climb anims
	xr_strcpy(base,"cl");
	legs_idle		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_idle_1"));
	// no relaxed leg set while climbing -- the pack ships nothing under the "cl" base but cl_idle_1,
	// which is already the idle used here
	legs_idle_relaxed.invalidate();
	legs_turn_relaxed.invalidate();
	m_torso_idle	= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_torso_0_aim_0"));
	// the empty-hands set (see actor_anim_defs.h) -- Safe: absent in the stock animations
	m_torso_none[STorsoWpn::eIdle]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base,"_torso_0_idle_1"));
	m_torso_none[STorsoWpn::eWalk]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base,"_torso_0_walk_1"));
	m_torso_none[STorsoWpn::eRun]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base,"_torso_0_run_1"));
	m_torso_none[STorsoWpn::eSprint]= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base,"_torso_0_escape_0"));
	m_walk.Create	(K,base,"_run");
	m_run.Create	(K,base,"_run");

	//norm anims
	xr_strcpy(base,"norm");
	// the climb state's TORSO sets are built on "norm" (below), not on "cl" -- named lookups must match
	xr_strcpy(m_anim_base, sizeof(m_anim_base), base);
	legs_turn		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_turn"));
	death			= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_death_0"));
	m_torso[0].Create(K,base,"_1");
	m_torso[1].Create(K,base,"_2");
	m_torso[2].Create(K,base,"_3");
	m_torso[3].Create(K,base,"_4");
	m_torso[4].Create(K,base,"_5");
	m_torso[5].Create(K,base,"_6");
	m_torso[6].Create(K,base,"_7");
	m_torso[7].Create(K,base,"_8");
	m_torso[8].Create(K,base,"_9");
	m_torso[9].Create(K,base,"_10");
	m_torso[10].Create(K,base,"_11");
	m_torso[11].Create(K,base,"_12");
	m_torso[12].Create(K,base,"_13");


	m_head_idle.invalidate();///K->ID_Cycle("head_idle_0");
	jump_begin		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_begin"));
	jump_idle		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_idle"));
	landing[0]		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_end"));
	landing[1]		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_end_1"));

	for (int k=0; k<12; ++k)
		m_damage[k]	= K->ID_FX(strconcat(sizeof(buf),buf,base,"_damage_",_itoa(k,buf1,10)));
}


// Build (once) and hand back the named torso set `group`, i.e. norm_torso_<group>_aim_1 and friends.
// Same shape as the numbered m_torso[] entries -- ID_Cycle_Safe leaves the actions this pack does not
// ship (reload_1/_2, drop_0, the _all_ trio) invalid, exactly as it already does for the stock slots.
STorsoWpn* SActorState::TorsoNamed(IKinematicsAnimated* K, const shared_str& group)
{
	if (!K || !group.size())			return NULL;
	TorsoNamedMap::iterator it = m_torso_named.find(group);
	if (it != m_torso_named.end())		return &it->second;

	string64	base1;
	strconcat	(sizeof(base1), base1, "_", group.c_str());
	STorsoWpn&	t = m_torso_named[group];
	t.Create	(K, m_anim_base, base1);
	// A set that resolves nothing at all means the pack has no such group: keep it cached (so we do not
	// retry every frame) but say so once, and the caller falls back to the numbered slot.
	if (!t.moving[STorsoWpn::eIdle] && !t.zoom && !t.draw)
		Msg("! actor_anim_group: no motions for '%s_torso%s_*'", m_anim_base, base1);
	return &t;
}

void SActorState::Create(IKinematicsAnimated* K, LPCSTR base)
{
	string128		buf,buf1;
	m_torso_named.clear();							// the visual changed -> the cached sets are stale
	xr_strcpy		(m_anim_base, sizeof(m_anim_base), base);
	legs_turn		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_turn"));
	legs_idle		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_idle_0"));
	legs_turn_relaxed	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base,"_turn_safe"));
	legs_idle_relaxed	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base,"_idle_1"));
	death			= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_death_0"));

	m_walk.Create	(K,base,"_walk");
	m_run.Create	(K,base,"_run");

	m_torso[0].Create(K,base,"_1");
	m_torso[1].Create(K,base,"_2");
	m_torso[2].Create(K,base,"_3");
	m_torso[3].Create(K,base,"_4");
	m_torso[4].Create(K,base,"_5");
	m_torso[5].Create(K,base,"_6");
	m_torso[6].Create(K,base,"_7");
	m_torso[7].Create(K,base,"_8");
	m_torso[8].Create(K,base,"_9");
	m_torso[9].Create(K,base,"_10");
	m_torso[10].Create(K,base,"_11");
	m_torso[11].Create(K,base,"_12");
	m_torso[12].Create(K,base,"_13");
	
	m_torso_idle	= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_torso_0_aim_0"));
	// the empty-hands set (see actor_anim_defs.h) -- Safe: absent in the stock animations
	m_torso_none[STorsoWpn::eIdle]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base,"_torso_0_idle_1"));
	m_torso_none[STorsoWpn::eWalk]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base,"_torso_0_walk_1"));
	m_torso_none[STorsoWpn::eRun]	= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base,"_torso_0_run_1"));
	m_torso_none[STorsoWpn::eSprint]= K->ID_Cycle_Safe(strconcat(sizeof(buf),buf,base,"_torso_0_escape_0"));
	m_head_idle		= K->ID_Cycle("head_idle_0");
	jump_begin		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_begin"));
	jump_idle		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_idle"));
	landing[0]		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_end"));
	landing[1]		= K->ID_Cycle(strconcat(sizeof(buf),buf,base,"_jump_end_1"));

	for (int k=0; k<12; ++k)
		m_damage[k]	= K->ID_FX(strconcat(sizeof(buf),buf,base,"_damage_",_itoa(k,buf1,10)));
}

void SActorSprintState::Create(IKinematicsAnimated* K)
{
	//leg anims
	legs_fwd=K->ID_Cycle("norm_escape_00");
	legs_ls=K->ID_Cycle("norm_escape_ls_00");
	legs_rs=K->ID_Cycle("norm_escape_rs_00");

	legs_jump_fwd	=K->ID_Cycle("norm_escape_jump_00");
	legs_jump_ls	=K->ID_Cycle("norm_escape_ls_jump_00");
	legs_jump_rs	=K->ID_Cycle("norm_escape_rs_jump_00");
}

void SActorMotions::Create(IKinematicsAnimated* V)
{
	m_dead_stop				= V->ID_Cycle("norm_dead_stop_0");

	m_normal.Create	(V,"norm");
	m_crouch.Create	(V,"cr");
	//m_climb.Create	(V,"cr");
	m_climb.CreateClimb(V);
	m_sprint.Create(V);
}

SActorVehicleAnims::SActorVehicleAnims()
{
	
}
void SActorVehicleAnims::Create(IKinematicsAnimated* V)
{
	for(u16 i=0;TYPES_NUMBER>i;++i) m_vehicles_type_collections[i].Create(V,i);
}

SVehicleAnimCollection::SVehicleAnimCollection()
{
	for(u16 i=0;MAX_IDLES>i;++i) idles[i].invalidate();
	idles_num = 0;
	steer_left.invalidate();
	steer_right.invalidate();
}

void SVehicleAnimCollection::Create(IKinematicsAnimated* V,u16 num)
{
	string128 buf,buff1,buff2;
	strconcat(sizeof(buff1),buff1,_itoa(num,buf,10),"_");
	steer_left=	V->ID_Cycle(strconcat(sizeof(buf),buf,"steering_idle_",buff1,"ls"));
	steer_right=V->ID_Cycle(strconcat(sizeof(buf),buf,"steering_idle_",buff1,"rs"));

	for(int i=0;MAX_IDLES>i;++i){
		idles[i]=V->ID_Cycle_Safe(strconcat(sizeof(buf),buf,"steering_idle_",buff1,_itoa(i,buff2,10)));
		if(idles[i]) idles_num++;
		else break;
	}
}

void CActor::steer_Vehicle(float angle)	
{
	if(!m_holder)		return;
	CCar*	car			= smart_cast<CCar*>(m_holder);
	u16 anim_type       = car->DriverAnimationType();
	SVehicleAnimCollection& anims=m_vehicle_anims->m_vehicles_type_collections[anim_type];
	if(angle==0.f) 		smart_cast<IKinematicsAnimated*>	(Visual())->PlayCycle(anims.idles[0]);
	else if(angle>0.f)	smart_cast<IKinematicsAnimated*>	(Visual())->PlayCycle(anims.steer_right);
	else				smart_cast<IKinematicsAnimated*>	(Visual())->PlayCycle(anims.steer_left);
}

void legs_play_callback		(CBlend *blend)
{
	CActor					*object = (CActor*)blend->CallbackParam;
	VERIFY					(object);
	object->m_current_legs.invalidate();
}

void CActor::g_SetSprintAnimation( u32 mstate_rl,MotionID &head,MotionID &torso,MotionID &legs)
{
	SActorSprintState& sprint			= m_anims->m_sprint;
	
	bool jump = (mstate_rl&mcFall)		||
				(mstate_rl&mcLanding)	||
				(mstate_rl&mcLanding)	||
				(mstate_rl&mcLanding2)	||
				(mstate_rl&mcJump)		;
	
	if		(mstate_rl & mcFwd)		legs = (!jump) ? sprint.legs_fwd	: sprint.legs_jump_fwd;
	else if (mstate_rl & mcLStrafe) legs = (!jump) ? sprint.legs_ls		: sprint.legs_jump_ls;
	else if (mstate_rl & mcRStrafe)	legs = (!jump) ? sprint.legs_rs		: sprint.legs_jump_rs;	
}

CMotion*        FindMotionKeys(MotionID motion_ID,IRenderVisual* V)
{
	IKinematicsAnimated* VA = smart_cast<IKinematicsAnimated*>(V);
	return (VA && motion_ID.valid())?VA->LL_GetRootMotion(motion_ID):0;
}

#ifdef DEBUG
BOOL	g_ShowAnimationInfo = TRUE;
#endif // DEBUG
char* mov_state[] ={
	"idle",
	"walk",
	"run",
	"sprint",
};
void CActor::g_SetAnimation( u32 mstate_rl )
{


	if (!g_Alive()) {
		if (m_current_legs||m_current_torso){
			SActorState*				ST = 0;
			if (mstate_rl&mcCrouch)		ST = &m_anims->m_crouch;
			else						ST = &m_anims->m_normal;
			mstate_real					= 0;
			m_current_legs.invalidate	();
			m_current_torso.invalidate	();

			//smart_cast<IKinematicsAnimated*>(Visual())->PlayCycle(m_anims->m_dead_stop);
		}

		return;
	}
	STorsoWpn::eMovingState	moving_idx 		= STorsoWpn::eIdle;
	SActorState*					ST 		= 0;
	SAnimState*						AS 		= 0;
	
	if		(mstate_rl&mcCrouch)	ST 		= &m_anims->m_crouch;
	else if	(mstate_rl&mcClimb)		ST 		= &m_anims->m_climb;
	else							ST 		= &m_anims->m_normal;

	bool bAccelerated = isActorAccelerated(mstate_rl, IsZoomAimingMode());
	if ( bAccelerated ){
		AS							= &ST->m_run;
	}else{
		AS							= &ST->m_walk;
	}
	if(mstate_rl&mcAnyMove){
		if( bAccelerated )
			moving_idx				= STorsoWpn::eRun;
		else
			moving_idx				= STorsoWpn::eWalk;
	}
	// ��������
	MotionID 						M_legs;
	MotionID 						M_torso;
	MotionID 						M_head;

	//���� �� ������ ����� �� �����
	bool is_standing = false;

	// Which of the two stances the pack ships (see actor_anim_defs.h) the actor holds. It drives the
	// TORSO as well as the legs, deliberately: the relaxed leg cycles carry a far less bladed pelvis
	// and spine, so under the combat chest motion the whole upper body ends up 18..32 deg round --
	// which is exactly what "the torso twists to the right while walking" looks like.
	// 0 = never; 1 = with nothing in hand (the empty-hands torso set we already play there IS the
	// relaxed one, so the two halves match); 2 = unless aiming; 3 = always.
	// Falls back to the stock set on its own whenever the relaxed one is not fully present.
	const bool bRelaxed	=	(g_actor_legs_relaxed == 1 && !inventory().ActiveItem()) ||
							(g_actor_legs_relaxed == 2 && !IsZoomAimingMode()) ||
							(g_actor_legs_relaxed == 3);
	const bool bDiagLegs =	(g_actor_legs_diagonal == 2) || (g_actor_legs_diagonal == 1 && bRelaxed);

	// Legs. The landing cycle is held by its OWN timer (m_fLegsLandingHold), not by mcLanding:
	// that state is over in 0.1s, and the movement cycle below would take the legs straight off the
	// landing animation -- which is why a jump taken while walking barely touched down and lost its
	// footstep sound (the mark for norm_jump_end sits at 0.2). Standing still masked it: with no
	// movement cycle to fall through to, the animation played on by itself.
	if		(m_fLegsLandingHold>0.f)M_legs	= ST->landing[m_uLegsLandingIdx ? 1 : 0];
	else if (mstate_rl&mcLanding)	M_legs	= ST->landing[0];
	else if (mstate_rl&mcLanding2)	M_legs	= ST->landing[1];
	else if ((mstate_rl&mcTurn)&&
			!(mstate_rl&mcClimb))	M_legs	= ST->TurnLegs(bRelaxed);
	else if (mstate_rl&mcFall)		M_legs	= ST->jump_idle;
	else if (mstate_rl&mcJump)		M_legs	= ST->jump_begin;
	// Diagonals: 1 = only in the relaxed stance, 2 = always. In the BRACED set a diagonal is the
	// straight cycle with the hips turned, which reads as a forward walk rotated sideways; in the
	// relaxed one the chest stays put across the whole family (11..16 deg) so the blend holds up.
	else if (mstate_rl&mcFwd)		M_legs	= (bDiagLegs && (mstate_rl&mcLStrafe)) ? AS->FwdLS (bRelaxed)
									:		  (bDiagLegs && (mstate_rl&mcRStrafe)) ? AS->FwdRS (bRelaxed)
									:													AS->Fwd   (bRelaxed);
	else if (mstate_rl&mcBack)		M_legs	= (bDiagLegs && (mstate_rl&mcLStrafe)) ? AS->BackLS(bRelaxed)
									:		  (bDiagLegs && (mstate_rl&mcRStrafe)) ? AS->BackRS(bRelaxed)
									:													AS->Back  (bRelaxed);
	else if (mstate_rl&mcLStrafe)	M_legs	= AS->LS  (bRelaxed);
	else if (mstate_rl&mcRStrafe)	M_legs	= AS->RS  (bRelaxed);
	else is_standing = true;

	if(mstate_rl&mcSprint){
		g_SetSprintAnimation			(mstate_rl,M_head,M_torso,M_legs);
		moving_idx						= STorsoWpn::eSprint;
	}

	if (this == Level().CurrentViewEntity())
	{	
		if ((mstate_rl&mcSprint) != (mstate_old&mcSprint))
		{
			g_player_hud->OnMovementChanged(mcSprint);
		}else
		if ((mstate_rl&mcAnyMove) != (mstate_old&mcAnyMove))
		{
			g_player_hud->OnMovementChanged(mcAnyMove);
		}else
		if ((mstate_rl&mcAnyMove) &&
			( (bAccelerated != isActorAccelerated(mstate_old, IsZoomAimingMode())) ||
			  (((mstate_rl^mstate_old)&mcCrouch)!=0) ||
			  (IsZoomAimingMode() && (((mstate_rl^mstate_old)&(mcFwd|mcBack|mcLStrafe|mcRStrafe))!=0)) ))
		{
			// while already moving, re-select the movement idle so it blends immediately:
			//  - run<->walk speed change -> anm_idle_moving <-> anm_idle_moving_slow
			//  - stand<->crouch change -> anm_idle_moving <-> anm_idle_moving_crouch[_slow]
			//  - (while aiming) strafe direction change -> directional anm_idle_aim_walk_*
			g_player_hud->OnMovementChanged(mcAccel);
		}
	};

	//-----------------------------------------------------------------------
	// Torso
	// Nothing in hand plays the slot-0 set; the branch below overrides this once it knows better.
	float	yaw_fix_target	= deg2rad(torso_yaw_fix("0") + g_actor_torso_yaw);
	float	neck_fix_target	= deg2rad(neck_yaw_fix("0"));
	// The set and key that won, so the correction can be keyed off the MOTION that ends up playing
	// rather than off the weapon's state. The two are not the same thing: our reload finishes on a
	// TIMER while the torso motion runs on, so a state-keyed correction switched off mid-animation
	// and the reload was left standing 17 deg from the idle. Keyed off the motion it cannot drift.
	STorsoWpn*	TW_used		= NULL;
	LPCSTR		yaw_key_used= NULL;
	// NOTE the buffer lives HERE, not in the branch below: yaw_key_used points into it for the
	// numeric slots, and it is read after that branch has exited. Declaring it inside left a
	// dangling pointer -- named groups (shared_str) were fine, every numeric slot read garbage.
	string16	slot_key;
	// ...and the SAME trap, one indirection further: with a detector out TW_used points at the merged
	// "<x>+detector" set, which used to be a local of the branch below. Reading a dead stack slot at
	// the bottom of this function made the action match ("_run" / "_walk" / nothing) flip about from
	// frame to frame, so the yaw correction eased toward a different target every frame and the whole
	// upper body -- head included -- rocked left and right while running with the device out. Without
	// a detector TW_used points into ST->m_torso[], which is a member and always valid, which is why
	// only the detector showed it.
	STorsoWpn	det_merged;
	// ...and with nothing in hand the spine/head stop chasing the camera (xrMPE behaviour): the
	// pack's empty-hands set animates the whole body, and the stock twist only fights it.
	const float	follow_target	= inventory().ActiveItem() ? 1.f : g_actor_torso_follow_empty;
	// Vertical half of the same chase. With a weapon it tracks the horizontal one (1); with empty
	// hands it takes its own knob, or falls back to the horizontal one when that knob is left at -1.
	const float	follow_target_p	= inventory().ActiveItem() ? 1.f
								: ((g_actor_torso_follow_empty_pitch < 0.f)
									? g_actor_torso_follow_empty : g_actor_torso_follow_empty_pitch);
	if(mstate_rl&mcClimb)
	{
		if		(mstate_rl&mcFwd)		M_torso	= AS->legs_fwd;
		else if (mstate_rl&mcBack)		M_torso	= AS->legs_back;
		else if (mstate_rl&mcLStrafe)	M_torso	= AS->legs_ls;
		else if (mstate_rl&mcRStrafe)	M_torso	= AS->legs_rs;
	}
	
	// xrMPE ships torso sets for the OTHER hand as well: with the detector out the pack has
	// norm_torso_0+detector_*, _pistol+detector_*, _knife+detector_* and _6+detector_* (the general
	// long-gun one). They are ordinary named sets -- TorsoNamed builds them with no special case --
	// but PARTIAL: only pistol+detector carries a reload, so whichever wins has its gaps filled from
	// the set the weapon would otherwise have used (FillGapsFrom).
	LPCSTR				det_group	= NULL;
	bool				det_showing	= false, det_hiding = false;
	if (g_actor_detector_torso)
	{
		CCustomDetector* det = smart_cast<CCustomDetector*>(inventory().ItemFromSlot(DETECTOR_SLOT));
		// NOT IsWorking(): that goes false the moment the device is stowed, and the holster motion
		// still has to play. The state covers the whole out-and-back trip.
		if (det && det->GetState() != CHUDState::eHidden)
		{
			det_showing	= (det->GetState() == CHUDState::eShowing);
			det_hiding	= (det->GetState() == CHUDState::eHiding);
			CHudItem* ah = smart_cast<CHudItem*>(inventory().ActiveItem());
			if		(!ah)											det_group = "0+detector";
			else if (inventory().GetActiveSlot() == KNIFE_SLOT)		det_group = "knife+detector";
			else if (ah->animation_slot() == 1)						det_group = "pistol+detector";
			else													det_group = "6+detector";
		}
	}

	// With NOTHING in hand the `if (H)` branch below never runs, so the detector keys were never even
	// looked up and 0+detector_drawdevice/_holsterdevice did nothing at all. Seed the correction here
	// from the detector set; a weapon, when there is one, overrides it further down with its own key.
	if (det_group)
	{
		LPCSTR det_action = det_showing ? "_drawdevice" : det_hiding ? "_holsterdevice" : NULL;
		yaw_fix_target = deg2rad(torso_yaw_fix(det_group, det_action) + g_actor_torso_yaw
								 + g_actor_detector_yaw);
		neck_fix_target = deg2rad(neck_yaw_fix(det_group, det_action));
	}

	if(!M_torso)
	{
		CInventoryItem* _i = inventory().ActiveItem();
		CHudItem		*H = smart_cast<CHudItem*>(_i);
		CWeapon			*W = smart_cast<CWeapon*>(_i);
		CMissile		*M = smart_cast<CMissile*>(_i);
		CArtefact		*A = smart_cast<CArtefact*>(_i);

		if (H) {
			VERIFY(H->animation_slot() <= _total_anim_slots_);
			STorsoWpn* TW			= &ST->m_torso[H->animation_slot() - 1];
			// xrMPE: a weapon may name its own torso set instead of taking one of the 13 numbered
			// slots. Fall back to the numbered one if the pack has no such set, so a typo or a
			// half-installed animation pack degrades to stock rather than freezing the actor.
			const shared_str& grp	= H->ActorAnimGroup();
			LPCSTR yaw_key			= NULL;
			if (grp.size())
			{
				STorsoWpn* named = ST->TorsoNamed(smart_cast<IKinematicsAnimated*>(Visual()), grp);
				if (named && (named->moving[STorsoWpn::eIdle] || named->zoom || named->draw))
				{
					TW		= named;
					yaw_key	= grp.c_str();
				}
			}
			// The detector, when it is out, wins over the weapon's own set -- it is the hand PAIR that
			// decides the pose, and the chest/gun arm turning left with it is correct (that is what
			// xrMPE itself does; the mistake was trying to straighten it with a yaw correction, which
			// rotates the arm too). Gaps come from the set chosen just above: the +detector sets are
			// partial, only pistol+detector carries a reload.
			if (det_group)
			{
				STorsoWpn* dset = ST->TorsoNamed(smart_cast<IKinematicsAnimated*>(Visual()), det_group);
				if (dset && (dset->moving[STorsoWpn::eIdle] || dset->zoom || dset->draw))
				{
					det_merged	= *dset;
					det_merged.FillGapsFrom(*TW);
					TW			= &det_merged;
					yaw_key		= det_group;
				}
			}
			// the yaw correction keys off whichever set actually won, named or numbered
			if (!yaw_key)			yaw_key = _itoa(H->animation_slot(), slot_key, 10);
			TW_used		= TW;
			yaw_key_used= yaw_key;
			// ...and an action may need its own line: see torso_yaw_fix. The pack is not internally
			// consistent action to action -- its +detector sets drift up to 12 deg between standing,
			// walking and running -- so walk/run get their own optional suffixes too.
			// The drop gesture may only latch m_bAnimTorsoPlayed if it can also CLEAR it again: the
			// flag is reset by AnimTorsoPlayCallBack when the blend is over, so it needs a ONE-SHOT
			// motion. The old code latched unconditionally and, when the set carried no drop_0, fell
			// back to ST->m_torso_idle -- a LOOPING stance (`<base>_torso_0_aim_0`) whose blend never
			// ends. The callback then never fired, m_bAnimTorsoPlayed stayed TRUE, and the block at
			// the bottom pinned M_torso to m_current_torso forever: the third-person pose froze in
			// idle after dropping a gun. It looked unfixable except by dropping the pistol because
			// the pistol's set is one of the few that does ship drop_0, so ITS callback finally
			// cleared the flag. The xrMPE named sets mostly have no drop_0 at all -- confirmed in
			// the log by "! drop animation for wpn_mp521034". No motion (or a looping one) now simply
			// means "no drop gesture": fall through to the normal state machine.
			MotionID drop_m;
			if (!b_DropActivated&&!fis_zero(f_DropPower))
			{
				drop_m = TW->drop;
				if (drop_m)
				{
					CMotionDef* dd = smart_cast<IKinematicsAnimated*>(Visual())->LL_GetMotionDef(drop_m);
					if (!dd || !(dd->flags & esmStopAtEnd))		drop_m.invalidate();
				}
				if (!drop_m)
					Msg("! no one-shot drop animation for %s -- keeping the normal torso pose", *(H->object().cName()));
			}
			if (drop_m){
				M_torso					= drop_m;
				m_bAnimTorsoPlayed		= TRUE;
			}else{
				if (!m_bAnimTorsoPlayed) {
					if (W) {
						bool K	=inventory().GetActiveSlot() == KNIFE_SLOT;
						bool R3 = W->IsTriStateReload();
						
						if(K)
						{
							switch (W->GetState()){
							case CWeapon::eIdle:		M_torso	= TW->Moving(moving_idx, bRelaxed);		break;
							
							case CWeapon::eFire:	
								if(is_standing)
														M_torso = M_legs = M_head = TW->all_attack_0;
								else
														M_torso	= TW->attack_zoom;
								break;

							case CWeapon::eFire2:
								if(is_standing)
														M_torso = M_legs = M_head = TW->all_attack_1;
								else
														M_torso	= TW->fire_idle;
								break;

							case CWeapon::eReload:		M_torso	= TW->reload;					break;
							case CWeapon::eShowing:		M_torso	= TW->draw;						break;
							case CWeapon::eHiding:		M_torso	= TW->holster;					break;
							default				 :  	M_torso	= TW->Moving(moving_idx, bRelaxed);		break;
							}
						}
						else
						{
							switch (W->GetState()){
							case CWeapon::eIdle:		M_torso	= W->IsZoomed()?TW->zoom:TW->Moving(moving_idx, bRelaxed);	break;
							case CWeapon::eFire:		M_torso	= W->IsZoomed()?TW->attack_zoom:TW->attack;				break;
							case CWeapon::eFire2:		M_torso	= W->IsZoomed()?TW->attack_zoom:TW->attack;				break;
							case CWeapon::eReload:
								{
								// Rounds still in the magazine -> the PARTIAL reload, when the loaded
								// animations offer one. Same test the weapon's own world animation uses
								// for its `_empty` suffix (CWeaponMagazined::gwr_UpdateWorldAnim), read
								// the other way round. Without the motion this is the old behaviour.
								const bool half = (W->GetAmmoElapsed() > 0);
								if(!R3)
									M_torso	= (half && TW->reload_half) ? TW->reload_half : TW->reload;
								else{
									CWeapon::EWeaponSubStates sub_st = W->GetReloadState();
									switch (sub_st){
										case CWeapon::eSubstateReloadBegin:			M_torso	= (half && TW->reload_half)   ? TW->reload_half   : TW->reload;		break;
										case CWeapon::eSubstateReloadInProcess:		M_torso	= (half && TW->reload_half_1) ? TW->reload_half_1 : TW->reload_1;	break;
										case CWeapon::eSubstateReloadEnd:			M_torso	= (half && TW->reload_half_2) ? TW->reload_half_2 : TW->reload_2;	break;
										default:									M_torso	= (half && TW->reload_half)   ? TW->reload_half   : TW->reload;		break;
									}
								}
								}break;

							case CWeapon::eShowing:	M_torso	= TW->draw;					break;
							case CWeapon::eHiding:	M_torso	= TW->holster;				break;
							default				 :  M_torso	= TW->Moving(moving_idx, bRelaxed);	break;
							}
						}
					}
					else if (M) {
						// One throw for every case: the torso-only one, standing or moving. The stock
						// code swapped to the full-body <base>_all_<slot>_attack_* when standing still,
						// and the pack's version of that reads wrong for both the bolt (user's call
						// earlier) and the grenades (this one). all_attack_* stays in use for the
						// knife's standing strike, which is a different set entirely.
						switch (M->GetState()){
						case CMissile::eShowing		:		M_torso	= TW->draw;						break;
						case CMissile::eHiding		:		M_torso	= TW->holster;					break;
						case CMissile::eIdle		:		M_torso	= TW->Moving(moving_idx, bRelaxed);	break;
						case CMissile::eThrowStart	:		M_torso	= TW->attack_zoom;				break;
						case CMissile::eReady		:		M_torso	= TW->fire_idle;				break;
						case CMissile::eThrow		:		M_torso	= TW->fire_end;					break;
						case CMissile::eThrowEnd	:		M_torso	= TW->fire_end;					break;
						default						:		M_torso	= TW->draw;						break;
						}
					}
					else if (A){
							switch(A->GetState()){
								case CArtefact::eIdle		: M_torso	= TW->Moving(moving_idx, bRelaxed);	break; 
								case CArtefact::eShowing	: M_torso	= TW->draw;					break; 
								case CArtefact::eHiding		: M_torso	= TW->holster;				break; 
								case CArtefact::eActivating : M_torso	= TW->zoom;					break; 
							default							: M_torso	= TW->Moving(moving_idx, bRelaxed);
							}
					
					}
				}
			}
		}
	}

	if (!M_legs)
	{
		if((mstate_rl&mcCrouch)&&!isActorAccelerated(mstate_rl, IsZoomAimingMode()))//!(mstate_rl&mcAccel))
		{
			M_legs=smart_cast<IKinematicsAnimated*>(Visual())->ID_Cycle("cr_idle_1");
		}
		else
			M_legs	= ST->IdleLegs(bRelaxed);
	}
	if (!M_head)					M_head	= ST->m_head_idle;
	if (!M_torso){
		if (m_bAnimTorsoPlayed)		M_torso	= m_current_torso;
		else
		{
			// The PDA is not a CHudItem (CPda : CInventoryItemObject), so it never reaches the branch
			// above and the actor used to hold nothing at all while reading it. The pack ships a whole
			// named set for it -- norm_torso_pda_* -- which xrMPE's engine addresses by literal name
			// (those strings are in its xrGame.dll); here it is just another named group.
			if (smart_cast<CPda*>(inventory().ActiveItem()))
			{
				STorsoWpn* pset = ST->TorsoNamed(smart_cast<IKinematicsAnimated*>(Visual()), "pda");
				if (pset)			M_torso = pset->Moving(moving_idx, bRelaxed);
			}
			// Nothing in hand. Follow the movement state if the animations offer an empty-hands set,
			// otherwise keep the stock single idle pose. With the detector out that set is the
			// dedicated 0+detector one (idle_1/walk_1/run_1, same actions as m_torso_none).
			if (det_group)
			{
				STorsoWpn* dset = ST->TorsoNamed(smart_cast<IKinematicsAnimated*>(Visual()), det_group);
				if (dset)			M_torso = dset->Moving(moving_idx, bRelaxed);
			}
			if (!M_torso)			M_torso = ST->m_torso_none[moving_idx];
			if (!M_torso)			M_torso = ST->m_torso_none[STorsoWpn::eIdle];
			if (!M_torso)			M_torso = ST->m_torso_idle;
		}
	}
	

	// ���� �������� ��� ����� - �������� / ����� �������� �������� �� ������
	// A whole-torso gesture named outright by the item (actor_torso_anim): the pack ships
	// norm_torso_item_medkit / _bandage / _antirad / _drink_staker as single motions, not families,
	// and xrMPE plays them by literal name. Ours takes the name from the config so the Lua item-use
	// PHANTOMS can carry it -- their section is the `hud` value of the real item.
	// MUST be here, past the weapon state switch: done inside the `if (H)` branch it was promptly
	// overwritten by the switch's own idle (which is the numeric slot 13 these phantoms inherit --
	// that is where the "item use plays the binocular animation" came from). Latching
	// m_bAnimTorsoPlayed to skip the switch is NOT the fix: that pins m_current_torso forever.
	CHudItem* HI = smart_cast<CHudItem*>(inventory().ActiveItem());
	if (!HI || !HI->ActorTorsoAnim().size())	{ m_torso_item_anim.invalidate(); m_torso_item_done = false; }
	if (HI)
	{
		const shared_str& tanm = HI->ActorTorsoAnim();
		if (tanm.size())
		{
			IKinematicsAnimated* KA_T	= smart_cast<IKinematicsAnimated*>(Visual());
			MotionID mi					= KA_T->ID_Cycle_Safe(tanm);
			if (mi)
			{
				// ONCE, not looped: these carry esmStopAtEnd, so the blend stops at timeTotal and we
				// let go of the torso there. Keeping the override on would freeze the last frame;
				// stripping the flag would loop it for as long as the phantom lives. Neither is right.
				// Re-arm on a new HUD motion as well as on a new motion id. Strike a knife twice and the
				// second gesture is the SAME mi, so keying on that alone left the latch set from the first
				// strike and the third-person animation played exactly once. MotionEndTm() is stamped
				// afresh by every hud motion; 0 means none is running, and re-arming on the way back to 0
				// would restart the gesture instead of letting it end.
				const u32 mend = HI->MotionEndTm();
				if (m_torso_item_anim != mi || (mend && mend != m_torso_item_end))
				{
					m_torso_item_anim = mi;
					m_torso_item_end  = mend;
					m_torso_item_done = false;
					m_torso_item_replay = true;
				}
				if (!m_torso_item_done)
				{
					if (m_current_torso == mi && m_current_torso_blend &&
						m_current_torso_blend->timeCurrent >= m_current_torso_blend->timeTotal - EPS_L)
						m_torso_item_done = true;
					else
						M_torso = mi;
				}
			}
		}
	}

	// The DEVICE going in or out of the off hand has its own torso motion in the "+detector" sets
	// (drawdevice_0 / holsterdevice_0). It has to win over the idle the weapon would otherwise hold,
	// which is what makes it play at the right moments by itself: the engine stows the detector
	// before a reload and brings it back after, so those transitions are already driven for us.
	if (g_actor_detector_gesture && det_group && (det_showing || det_hiding) && !m_bAnimTorsoPlayed)
	{
		STorsoWpn* dset = ST->TorsoNamed(smart_cast<IKinematicsAnimated*>(Visual()), det_group);
		if (dset)
		{
			const MotionID& dm = det_showing ? dset->draw_device : dset->holster_device;
			if (dm)		M_torso = dm;
		}
	}

	// Now that M_torso is final, name the action by looking at WHICH motion of the winning set it is
	// and pick <key>_<action> off that. This is what makes a corrected animation line up with the
	// idle instead of sitting a few degrees away from it: whatever is on screen is what gets keyed.
	if (TW_used && yaw_key_used)
	{
		LPCSTR act = NULL;
		if		(M_torso == TW_used->reload || M_torso == TW_used->reload_1 || M_torso == TW_used->reload_2
			 ||  M_torso == TW_used->reload_half || M_torso == TW_used->reload_half_1 || M_torso == TW_used->reload_half_2)
																	act = "_reload";
		else if (M_torso == TW_used->draw_device)					act = "_drawdevice";
		else if (M_torso == TW_used->holster_device)					act = "_holsterdevice";
		else if (M_torso == TW_used->draw_all)						act = "_drawall";
		else if (M_torso == TW_used->holster_all)					act = "_holsterall";
		else if (M_torso == TW_used->draw)							act = "_draw";
		else if (M_torso == TW_used->holster)						act = "_holster";
		else if (M_torso == TW_used->Moving(STorsoWpn::eWalk,   bRelaxed))	act = "_walk";
		else if (M_torso == TW_used->Moving(STorsoWpn::eRun,    bRelaxed))	act = "_run";
		else if (M_torso == TW_used->Moving(STorsoWpn::eSprint, bRelaxed))	act = "_sprint";

		yaw_fix_target	= deg2rad(torso_yaw_fix(yaw_key_used, act) + g_actor_torso_yaw
							+ (det_group ? g_actor_detector_yaw : 0.f));
		neck_fix_target	= deg2rad(neck_yaw_fix(yaw_key_used, act));

	}

	// The two upper-body constants the bone callbacks add ON TOP of whatever animation is playing.
	// The animations themselves already cross-fade (PlayCycle mixes in by default), but these do not:
	// holstering the last weapon flips follow-cam 1 -> 0 in a single frame, and p_shoulder_factor
	// alone is 0.7 of wherever the actor is looking, so the whole chest/head pops. Ease them over
	// actor_torso_blend seconds instead. Called every frame from UpdateCL, so a dt ramp is safe.
	{
		const float k = (g_actor_torso_blend > EPS) ? (Device.fTimeDelta / g_actor_torso_blend) : 1.f;
		const float w = (k < 1.f) ? k : 1.f;
		m_fTorsoFollowCam	+= (follow_target   - m_fTorsoFollowCam)      * w;
		m_fTorsoFollowCamPitch += (follow_target_p - m_fTorsoFollowCamPitch) * w;

		const float ky = (g_actor_torso_yaw_blend > EPS) ? (Device.fTimeDelta / g_actor_torso_yaw_blend) : 1.f;
		const float wy = (ky < 1.f) ? ky : 1.f;
		m_fTorsoYawFix		+= (yaw_fix_target  - m_fTorsoYawFix ) * wy;
		m_fNeckYawFix		+= (neck_fix_target - m_fNeckYawFix) * wy;
	}

	// A one-shot gesture stops on its last frame and stays m_current_torso, so striking again
	// selects the SAME motion and this comparison alone would never replay it. m_torso_item_replay
	// is raised when the gesture re-triggers and is consumed here.
	if (m_current_torso!=M_torso || m_torso_item_replay){
		m_torso_item_replay = false;
		if (m_bAnimTorsoPlayed)		m_current_torso_blend = smart_cast<IKinematicsAnimated*>	(Visual())->PlayCycle(M_torso,TRUE,AnimTorsoPlayCallBack,this);
		else						/**/m_current_torso_blend = /**/smart_cast<IKinematicsAnimated*>	(Visual())->PlayCycle(M_torso);

		m_current_torso=M_torso;

		// Length sync with the first-person motion, computed ONCE here. Doing it per frame from the
		// REMAINING times (as it was) makes the factor run away: the hud motion's remainder goes to
		// zero at its tail while the torso still has time left, so the body sprinted through the end
		// of every animation -- visible when leaving the PDA zoom, which cuts the hud motion short.
		// ...and ONLY for a one-shot ACTION. reload/draw/holster end; the idle, the walk/run cycles and
		// the AIM pose (TW->zoom) LOOP, and stretching a loop to the length of whatever hud motion
		// happened to start in the same frame is meaningless. Aiming starts `anm_idle_aim_start` -- a
		// few tenths of a second -- against a multi-second aim cycle, so the factor went straight into
		// the 4x clamp and stayed there for as long as the aim was held: the third-person aim animation
		// ran several times too fast. The identity test names exactly the actions this sync was asked
		// for (a reload that takes 2.5 s in the hands against 1.8 s on the body); esmStopAtEnd is the
		// second guard, so a set whose "action" is authored as a loop is left at its own pace too.
		const bool sync_action = TW_used && M_torso &&
			(   M_torso == TW_used->reload			|| M_torso == TW_used->reload_1			|| M_torso == TW_used->reload_2
			 || M_torso == TW_used->reload_half		|| M_torso == TW_used->reload_half_1	|| M_torso == TW_used->reload_half_2
			 || M_torso == TW_used->draw			|| M_torso == TW_used->holster
			 || M_torso == TW_used->draw_device		|| M_torso == TW_used->holster_device
			 || M_torso == TW_used->draw_all		|| M_torso == TW_used->holster_all);
		m_torso_sync_k = 0.f;
		CMotionDef* tdef = sync_action ? smart_cast<IKinematicsAnimated*>(Visual())->LL_GetMotionDef(M_torso) : NULL;
		if (g_actor_torso_sync_hud && tdef && (tdef->flags & esmStopAtEnd)
			&& m_current_torso_blend && m_current_torso_blend->timeTotal > EPS_L)
		{
			CHudItem*	HS	= smart_cast<CHudItem*>(inventory().ActiveItem());
			const u32	s0	= HS ? HS->MotionStartTm() : 0;
			const u32	e0	= HS ? HS->MotionEndTm()   : 0;
			// FULL length of the hud motion, not what is left of it -- and only when this torso
			// motion is starting alongside it, within SYNC_WINDOW of its start. Both guards exist
			// for the same failure: a torso motion that begins near the END of a hud one (leaving
			// the PDA zoom restarts the torso while the hud motion is all but over) got a huge
			// factor and the body raced through the animation.
			const u32	SYNC_WINDOW = 250;
			if (e0 > s0 && Device.dwTimeGlobal < s0 + SYNC_WINDOW)
			{
				const float hud_len = float(e0 - s0) / 1000.f;
				if (hud_len > 0.05f)
				{
					m_torso_sync_k = m_current_torso_blend->timeTotal / hud_len;
					clamp(m_torso_sync_k, 0.25f, 4.f);
				}
			}
		}
	}
	if(m_current_head!=M_head)
	{
		if(M_head)smart_cast<IKinematicsAnimated*>(Visual())->PlayCycle(M_head);
		m_current_head=M_head;
	}

	if (m_current_legs!=M_legs){
		float pos					= 0.f;
		VERIFY						(!m_current_legs_blend || !fis_zero(m_current_legs_blend->timeTotal));
		if ((mstate_real&mcAnyMove)&&(mstate_old&mcAnyMove)&&m_current_legs_blend)
			pos						= fmod(m_current_legs_blend->timeCurrent,m_current_legs_blend->timeTotal)/m_current_legs_blend->timeTotal;
		// Cross-fade instead of the motion's own accrue when asked. PlayCycle takes the blend speed
		// baked into the CMotionDef (2..6 for these cycles = a very short fade), which is why
		// straight <-> diagonal and walk <-> run pop. The long form of LL_PlayCycle lets us set it;
		// the partition is the motion's own (bone_or_part), not a guess -- the leg cycles live in
		// partition 0 but reading it keeps this correct for any pack.
		IKinematicsAnimated* KA_L	= smart_cast<IKinematicsAnimated*>(Visual());
		CMotionDef* mdef			= (g_actor_legs_blend > 0.f && M_legs) ? KA_L->LL_GetMotionDef(M_legs) : NULL;
		if (mdef)
			m_current_legs_blend	= KA_L->LL_PlayCycle(mdef->bone_or_part, M_legs, TRUE,
										g_actor_legs_blend, g_actor_legs_blend,
										1.f, FALSE, legs_play_callback, this);
		else
			m_current_legs_blend	= KA_L->PlayCycle(M_legs,TRUE,legs_play_callback,this);


		if ((!(mstate_old&mcAnyMove))&&(mstate_real&mcAnyMove))
		{
			pos						= 0.5f;//0.5f*Random.randI(2);
		}
		if (m_current_legs_blend)
			m_current_legs_blend->timeCurrent = m_current_legs_blend->timeTotal*pos;
		m_current_legs				= M_legs;

		CStepManager::on_animation_start(M_legs, m_current_legs_blend);
	}

	// Walk-cycle playback speed. Applied every frame, not just on the switch, because a blend that
	// was already running when the cvar changed would otherwise keep the old speed until the next
	// state change. Legs and torso together -- speeding up one alone desyncs the gait from the arms.
	if (!fsimilar(g_actor_walk_anim_speed, 1.f) && moving_idx == STorsoWpn::eWalk)
	{
		if (m_current_legs_blend)	m_current_legs_blend->speed  = g_actor_walk_anim_speed;
		if (m_current_torso_blend)	m_current_torso_blend->speed = g_actor_walk_anim_speed;
	}

	// re-assert the sync factor (the block above writes the same field), never recompute it
	if (m_torso_sync_k > 0.f && m_current_torso_blend)
		m_current_torso_blend->speed = m_torso_sync_k;





#ifdef _DEBUG
	if(bDebug){
		HUD().Font().pFontStat->OutSetI	(0,0);
		HUD().Font().pFontStat->OutNext("[%s]",mov_state[moving_idx]);
	}
#endif

#ifdef _DEBUG
	if ((Level().CurrentControlEntity() == this) && g_ShowAnimationInfo) {
		string128 buf;
		xr_strcpy(buf,"");
		if (isActorAccelerated(mstate_rl, IsZoomAimingMode()))		strcat(buf,"Accel ");
		if (mstate_rl&mcCrouch)		strcat(buf,"Crouch ");
		if (mstate_rl&mcFwd)		strcat(buf,"Fwd ");
		if (mstate_rl&mcBack)		strcat(buf,"Back ");
		if (mstate_rl&mcLStrafe)	strcat(buf,"LStrafe ");
		if (mstate_rl&mcRStrafe)	strcat(buf,"RStrafe ");
		if (mstate_rl&mcJump)		strcat(buf,"Jump ");
		if (mstate_rl&mcFall)		strcat(buf,"Fall ");
		if (mstate_rl&mcTurn)		strcat(buf,"Turn ");
		if (mstate_rl&mcLanding)	strcat(buf,"Landing ");
		if (mstate_rl&mcLLookout)	strcat(buf,"LLookout ");
		if (mstate_rl&mcRLookout)	strcat(buf,"RLookout ");
		if (m_bJumpKeyPressed)		strcat(buf,"+Jumping ");
		HUD().Font().pFontStat->OutNext	("MSTATE:     [%s]",buf);
/*
		switch (m_PhysicMovementControl->Environment())
		{
		case CPHMovementControl::peOnGround:	xr_strcpy(buf,"ground");			break;
		case CPHMovementControl::peInAir:		xr_strcpy(buf,"air");				break;
		case CPHMovementControl::peAtWall:		xr_strcpy(buf,"wall");				break;
		}
		HUD().Font().pFontStat->OutNext	(buf);
		HUD().Font().pFontStat->OutNext	("Accel     [%3.2f, %3.2f, %3.2f]",VPUSH(NET_SavedAccel));
		HUD().Font().pFontStat->OutNext	("V         [%3.2f, %3.2f, %3.2f]",VPUSH(m_PhysicMovementControl->GetVelocity()));
		HUD().Font().pFontStat->OutNext	("vertex ID   %d",ai_location().level_vertex_id());
		
		Game().m_WeaponUsageStatistic->Draw();
		*/
	};
#endif

	if (!m_current_torso_blend)
		return;

	IKinematicsAnimated		*skeleton_animated = smart_cast<IKinematicsAnimated*>(Visual());

	CMotionDef				*motion0 = skeleton_animated->LL_GetMotionDef(m_current_torso);
	VERIFY					(motion0);
	if (!(motion0->flags & esmSyncPart))
		return;

	if (!m_current_legs_blend)
		return;

	CMotionDef				*motion1 = skeleton_animated->LL_GetMotionDef(m_current_legs);
	VERIFY					(motion1);
	if (!(motion1->flags & esmSyncPart))
		return;

	// ...and only when the two are actually a pair -- see g_actor_torso_sync_part. Driving a long
	// motion off a short one's phase does not slow it down, it REPLAYS it: the aim stance held over
	// a walk cycle ran 12x and restarted every step, which is what "the upper body shakes while
	// walking aimed with a pistol" was.
	if (g_actor_torso_sync_part > 1.f
		&& m_current_legs_blend->timeTotal > EPS_L && m_current_torso_blend->timeTotal > EPS_L)
	{
		const float ratio = m_current_torso_blend->timeTotal / m_current_legs_blend->timeTotal;
		if (ratio > g_actor_torso_sync_part || ratio < 1.f / g_actor_torso_sync_part)
			return;
	}

	m_current_torso_blend->timeCurrent	= m_current_legs_blend->timeCurrent/m_current_legs_blend->timeTotal*m_current_torso_blend->timeTotal;
}
