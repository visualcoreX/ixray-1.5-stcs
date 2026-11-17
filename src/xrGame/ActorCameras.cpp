#include "stdafx.h"
#include "Actor.h"
#include "../xrEngine/CameraBase.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/UIRender.h"
#include "../Include/xrRender/UIShader.h"
#ifdef DEBUG
#include "PHDebug.h"
#endif
#include "hit.h"
#include "PHDestroyable.h"
#include "Car.h"

#include "Weapon.h"
#include "Inventory.h"

#include "SleepEffector.h"
#include "ActorEffector.h"
#include "level.h"
#include "../xrEngine/cl_intersect.h"

#include "elevatorstate.h"
#include "CharacterPhysicsSupport.h"
#include "EffectorShot.h"

#include "PHMovementControl.h"

extern BOOL dbg_draw_camera_collision;
void	collide_camera( CCameraBase & camera, float _viewport_near  );

// ---- GS camera-height inertia (WeaponInertion.pas CorrectActorCameraHeight port) ----
// Smooths CameraHeight() changes (crouch/stand no longer snap) and dips the camera on landing
// (mcLanding = soft, mcLanding2 = hard) with a timed recovery phase for the soft landing.
// Params: [gunslinger_base] actor_camera_* keys (values ported verbatim from GS gunslinger_params.ltx);
// code defaults below equal the GS config so a missing section still behaves.
struct SGwrCamSmooth
{
	bool	loaded;
	float	speed_def, speed_pow;
	float	land_off, land2_off;
	u32		land_time, land2_time, finish_time;
	float	land_speed_k, land2_speed_k, finish_speed_k;
	float	land_pow_k, land2_pow_k, finish_pow_k;

	float	last_h;
	u32		last_tm;
	u32		land_t, land2_t, finish_t;
};
static SGwrCamSmooth s_gwr_cam = {};

void gwr_ResetCamHeight()
{
	s_gwr_cam.last_h	= 0.f;
	s_gwr_cam.last_tm	= 0;
	s_gwr_cam.land_t	= s_gwr_cam.land2_t = s_gwr_cam.finish_t = 0;
}

static void gwr_LoadCamSmooth()
{
	if (s_gwr_cam.loaded)	return;
	s_gwr_cam.loaded = true;
	LPCSTR S = "gunslinger_base";
	bool ex = !!pSettings->section_exist(S);
	s_gwr_cam.speed_def			= ex ? READ_IF_EXISTS(pSettings, r_float, S, "default_actor_camera_speed", 18.f)					: 18.f;
	s_gwr_cam.speed_pow			= ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_speed_pow", 1.2f)					: 1.2f;
	s_gwr_cam.land_off			= ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_landing_offset", -0.2f)				: -0.2f;
	s_gwr_cam.land2_off			= ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_landing2_offset", -0.8f)				: -0.8f;
	s_gwr_cam.land_time			= u32(1000.f * (ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_landing_time", 0.125f)	: 0.125f));
	s_gwr_cam.land2_time		= u32(1000.f * (ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_landing2_time", 0.125f)	: 0.125f));
	s_gwr_cam.finish_time		= u32(1000.f * (ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_finish_landing_time", 0.3f)	: 0.3f));
	s_gwr_cam.land_speed_k		= ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_landing_speed_factor", 0.5f)			: 0.5f;
	s_gwr_cam.land2_speed_k		= ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_landing2_speed_factor", 0.5f)		: 0.5f;
	s_gwr_cam.finish_speed_k	= ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_finish_landing_speed_factor", 0.5f)	: 0.5f;
	s_gwr_cam.land_pow_k		= ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_landing_speed_pow_factor", 0.7f)		: 0.7f;
	s_gwr_cam.land2_pow_k		= ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_landing2_speed_pow_factor", 0.7f)	: 0.7f;
	s_gwr_cam.finish_pow_k		= ex ? READ_IF_EXISTS(pSettings, r_float, S, "actor_camera_finish_landing_speed_pow_factor", 0.9f)	: 0.9f;
}

static float gwr_SmoothCameraHeight(u32 mstate, float h)
{
	gwr_LoadCamSmooth();
	u32 now = Device.dwTimeGlobal;
	if (s_gwr_cam.last_h == 0.f || !s_gwr_cam.last_tm)
	{
		s_gwr_cam.last_h	= h;
		s_gwr_cam.last_tm	= now;
		return h;
	}
	u32 dt = (now >= s_gwr_cam.last_tm) ? (now - s_gwr_cam.last_tm) : 0;
	if (dt > 100)	dt = 100;	// pause/load safety
	s_gwr_cam.last_tm = now;

	// (re)arm the landing windows while the state bits are set (GS does the same each frame)
	if (mstate & mcLanding2)		{ s_gwr_cam.land2_t = s_gwr_cam.land2_time; s_gwr_cam.land_t = 0; s_gwr_cam.finish_t = 0; }
	else if (mstate & mcLanding)	{ s_gwr_cam.land_t = s_gwr_cam.land_time; s_gwr_cam.land2_t = 0; s_gwr_cam.finish_t = 0; }

	float max_off = 0.f, speed = s_gwr_cam.speed_def, powk = s_gwr_cam.speed_pow;
	if (s_gwr_cam.land_t)			{ max_off = s_gwr_cam.land_off;  speed *= s_gwr_cam.land_speed_k;  powk *= s_gwr_cam.land_pow_k; }
	else if (s_gwr_cam.land2_t)		{ max_off = s_gwr_cam.land2_off; speed *= s_gwr_cam.land2_speed_k; powk *= s_gwr_cam.land2_pow_k; }
	else if (s_gwr_cam.finish_t)	{ speed *= s_gwr_cam.finish_speed_k; powk *= s_gwr_cam.finish_pow_k; }

	float target	= h + max_off;
	float dh		= target - s_gwr_cam.last_h;
	float delta		= _abs(powf(_abs(dh), powk) * float(dt) * speed / 1000.f);
	if (dh < 0.f)				delta = -delta;
	if (_abs(delta) > _abs(dh))	{ delta = dh; s_gwr_cam.finish_t = 0; }
	float out = s_gwr_cam.last_h + delta;
	s_gwr_cam.last_h = out;

	// countdown; the soft landing transitions into the recovery (finish) phase, the hard one just expires
	if (s_gwr_cam.land_t > dt)			s_gwr_cam.land_t -= dt;
	else if (s_gwr_cam.land_t)			{ s_gwr_cam.finish_t = s_gwr_cam.finish_time; s_gwr_cam.land_t = 0; }
	if (s_gwr_cam.finish_t > dt)		s_gwr_cam.finish_t -= dt;	else s_gwr_cam.finish_t = 0;
	if (s_gwr_cam.land2_t > dt)			s_gwr_cam.land2_t -= dt;	else s_gwr_cam.land2_t = 0;
	return out;
}

ENGINE_API extern float psHUD_FOV;
ENGINE_API extern float psHUD_FOV_def;

void CActor::cam_Set	(EActorCameras style)
{
	CCameraBase* old_cam = cam_Active();
	cam_active = style;
	old_cam->OnDeactivate();
	cam_Active()->OnActivate(old_cam);
}
float CActor::f_Ladder_cam_limit=1.f;
void CActor::cam_SetLadder()
{
	CCameraBase* C			= cameras[eacFirstEye];
	g_LadderOrient			();
	float yaw				= (-XFORM().k.getH());
	float &cam_yaw			= C->yaw;
	float delta_yaw			= angle_difference_signed(yaw,cam_yaw);

	if(-f_Ladder_cam_limit<delta_yaw&&f_Ladder_cam_limit>delta_yaw)
	{
		yaw					= cam_yaw+delta_yaw;
		float lo			= (yaw-f_Ladder_cam_limit);
		float hi			= (yaw+f_Ladder_cam_limit);
		C->lim_yaw[0]		= lo;
		C->lim_yaw[1]		= hi;
		C->bClampYaw		= true;
	}
}
void CActor::camUpdateLadder(float dt)
{
	if(!character_physics_support()->movement()->ElevatorState())
															return;
	if(cameras[eacFirstEye]->bClampYaw) return;
	float yaw				= (-XFORM().k.getH());

	float & cam_yaw			= cameras[eacFirstEye]->yaw;
	float delta				= angle_difference_signed(yaw,cam_yaw);

	if(-0.05f<delta&&0.05f>delta)
	{
		yaw									= cam_yaw+delta;
		float lo							= (yaw-f_Ladder_cam_limit);
		float hi							= (yaw+f_Ladder_cam_limit);
		cameras[eacFirstEye]->lim_yaw[0]	= lo;
		cameras[eacFirstEye]->lim_yaw[1]	= hi;
		cameras[eacFirstEye]->bClampYaw		= true;
	}else{
		cam_yaw								+= delta * _min(dt*10.f,1.f) ;
	}

	CElevatorState* es = character_physics_support()->movement()->ElevatorState();
	if(es && es->State()==CElevatorState::clbClimbingDown)
	{
		float &cam_pitch					= cameras[eacFirstEye]->pitch;
		const float ldown_pitch				= cameras[eacFirstEye]->lim_pitch.y;
		float delta_							= angle_difference_signed(ldown_pitch,cam_pitch);
		if(delta_>0.f)
			cam_pitch						+= delta_* _min(dt*10.f,1.f) ;
	}
}

void CActor::cam_UnsetLadder()
{
	CCameraBase* C			= cameras[eacFirstEye];
	C->lim_yaw[0]			= 0;
	C->lim_yaw[1]			= 0;
	C->bClampYaw			= false;
}
// ---- First-person death camera ----
// Stock CS throws the view out to cam_3 (eacFreeLook) when the actor dies. The style still switches --
// that is what shows the body (setVisible(!HUDview())) and drops the hands -- but the freelook camera's
// pose is overwritten every frame with one riding on the corpse's bip01_head: looking along the head
// bone's face axis, pushed death_cam_forward along that look and death_cam_up along the head's up so
// the near plane clears the face.
//
// Which of the bone's axes is the face is not hard-coded -- the head's local frame is whatever the
// skeleton's exporter left it as -- but found at the moment of death (death_cam_Capture).

static IC Fvector dc_to_local(const Fmatrix& M, const Fvector& v)
{
	Fvector i = M.i, j = M.j, k = M.k;
	i.normalize_safe(); j.normalize_safe(); k.normalize_safe();
	return Fvector().set(v.dotproduct(i), v.dotproduct(j), v.dotproduct(k));
}
static IC Fvector dc_to_world(const Fmatrix& M, const Fvector& l)
{
	Fvector i = M.i, j = M.j, k = M.k;
	i.normalize_safe(); j.normalize_safe(); k.normalize_safe();
	Fvector r;	r.mul(i, l.x).mad(j, l.y).mad(k, l.z);
	return r;
}
// v (in a bone's frame) snapped to the bone axis it lies closest to, sign kept; `skip` is an axis
// already taken. axis = -1 when v is degenerate.
static Fvector dc_snap_axis(const Fvector& v, int skip, int& axis)
{
	axis			= -1;
	float best		= EPS_L;
	for (int i = 0; i < 3; ++i)
		if (i != skip && _abs(v[i]) > best)	{ best = _abs(v[i]); axis = i; }
	Fvector r;		r.set(0.f, 0.f, 0.f);
	if (axis >= 0)	r[axis] = (v[axis] < 0.f) ? -1.f : 1.f;
	return			r;
}
// D normalized, N made perpendicular to it; false when the pair is degenerate
static bool dc_orthonormalize(Fvector& D, Fvector& N)
{
	if (D.square_magnitude() < EPS_S)	return false;
	D.normalize		();
	Fvector R;		R.crossproduct(N, D);
	if (R.square_magnitude() < EPS_S)	return false;
	R.normalize		();
	N.crossproduct	(D, R);
	return			true;
}

bool CActor::death_cam_HeadMatrix(Fmatrix& head)
{
	IKinematics* K			= smart_cast<IKinematics*>(Visual());
	if (!K || m_head < 0 || u16(m_head) == BI_NONE)	return false;
	K->CalculateBones		();
	head.mul_43				(XFORM(), K->LL_GetTransform(u16(m_head)));
	return					!!_valid(head);
}

void CActor::death_cam_Capture()
{
	m_death_cam_valid		= false;
	if (!m_death_cam_enabled || !psActorFlags.test(AF_FIRST_PERSON_DEATH))	return;

	Fmatrix					H;
	if (!death_cam_HeadMatrix(H))	return;

	Fvector D				= cameras[eacFirstEye]->Direction();
	Fvector N				= cameras[eacFirstEye]->Up();
	if (!dc_orthonormalize(D, N))	return;

	// The look is the head bone's OWN face axis, not the camera's: the torso follows the camera's pitch
	// only partly (m_fTorsoFollowCamPitch eases to less than 1 with empty hands), so a look down at the
	// moment of death pointed the eye below the head's line and into the chest. The axis is found on the
	// head as the ANIMATION alone poses it -- no camera twist from the spine/head callbacks -- where the
	// face looks along the model's +Z and the crown along +Y; each is snapped to the nearest of the bone's
	// own axes, so whatever the clip did with the head at that instant does not leak in.
	IKinematics* K			= smart_cast<IKinematics*>(Visual());
	Fmatrix A;				K->Bone_GetAnimPos(A, u16(m_head), u8(-1), true);
	int fa = -1, ua = -1;
	Fvector f_loc			= dc_snap_axis(dc_to_local(A, Fvector().set(0.f,0.f,1.f)), -1, fa);
	Fvector u_loc			= dc_snap_axis(dc_to_local(A, Fvector().set(0.f,1.f,0.f)), fa, ua);
	if (fa < 0 || ua < 0)	return;
	Msg						("* death cam: bip01_head face = %c%c, up = %c%c", (f_loc[fa] < 0.f) ? '-' : '+', "xyz"[fa], (u_loc[ua] < 0.f) ? '-' : '+', "xyz"[ua]);

	// [actor] death_cam_pitch, degrees, + = up: trims a rig whose head bone is not quite level
	const float p			= deg2rad(m_death_cam_pitch);
	m_death_cam_dir_local.set(f_loc).mul(_cos(p)).mad(u_loc,  _sin(p));
	m_death_cam_up_local.set (u_loc).mul(_cos(p)).mad(f_loc, -_sin(p));

	m_death_cam_dir			= D;	// the smoothing eases from the view the actor died with
	m_death_cam_nrm			= N;
	m_death_cam_valid		= true;
	m_death_time			= Device.dwTimeGlobal;

	// Hide the head from the eye that now sits in front of it. Face down, the ragdoll's head can roll
	// into the camera past any offset, and collide_camera does not push away from the actor's own body.
	if (m_death_cam_hide_head && !m_death_cam_head_hidden && Level().CurrentEntity() == this)
	{
		const u16 head				= u16(m_head);
		const u16 root				= K->LL_GetBoneRoot();
		m_death_cam_head_bones.clear();
		for (u16 i = 0, n = K->LL_BoneCount(); i < n; ++i)
		{
			u16 b					= i;
			while (b != head && b != root && b != BI_NONE)
				b					= K->LL_GetData(b).GetParentID();
			if (b == head)
				m_death_cam_head_bones.push_back(i);
		}
		if (!m_death_cam_head_bones.empty())
		{
			add_visual_callback		(&death_cam_HideHead);
			m_death_cam_head_hidden	= true;
			K->CalculateBones_Invalidate();		// collapse it from the very next draw
		}
	}
}

// Runs at the end of every full CalculateBones. Only the RENDER transforms of the head branch are
// collapsed -- mTransform is left alone, so the ragdoll, the hit tests and death_cam_HeadMatrix still
// see the real head. LL_SetBoneVisible(FALSE) would not do: it stops the bone being calculated at all
// (the camera would lose it), takes it out of the physics shell's mask, and collapses the branch to the
// model's origin, which stretches the collar vertices half weighted to the neck into a spike (see
// player_legs_controller::collapse_bone_branch). Collapsing onto the head bone's own origin -- the
// neck joint -- moves those blended vertices a few centimetres at most.
void __stdcall CActor::death_cam_HideHead(IKinematics* K)
{
	CActor* A			= smart_cast<CActor*>(static_cast<CObject*>(K->GetUpdateCallbackParam()));
	if (!A || A->g_Alive() || !A->m_death_cam_valid || !psActorFlags.test(AF_FIRST_PERSON_DEATH))	return;

	const Fvector at	= K->LL_GetTransform(u16(A->m_head)).c;
	Fmatrix collapsed;	collapsed.scale(0.f, 0.f, 0.f);
	collapsed.c.set		(at);
	for (xr_vector<u16>::const_iterator it = A->m_death_cam_head_bones.begin(); it != A->m_death_cam_head_bones.end(); ++it)
		K->LL_GetBoneInstance(*it).mRenderTransform.mul_43(collapsed, K->LL_GetData(*it).m2b_transform);
}

void CActor::death_cam_Update(float dt)
{
	Fmatrix					H;
	if (!death_cam_HeadMatrix(H))	return;		// keeps last frame's pose

	Fvector D				= dc_to_world(H, m_death_cam_dir_local);
	Fvector N				= dc_to_world(H, m_death_cam_up_local);
	if (!dc_orthonormalize(D, N))	return;

	// the eye is bolted to the head; only the look is eased, which takes the ragdoll's jitter out of it
	Fvector P				= H.c;
	P.mad					(D, m_death_cam_forward);
	P.mad					(N, m_death_cam_up);

	if (m_death_cam_smooth > 0.f)
	{
		const float k		= 1.f - expf(-m_death_cam_smooth * _max(dt, 0.f));
		m_death_cam_dir.add	(Fvector().sub(D, m_death_cam_dir).mul(k));
		m_death_cam_nrm.add	(Fvector().sub(N, m_death_cam_nrm).mul(k));
		if (!dc_orthonormalize(m_death_cam_dir, m_death_cam_nrm))
		{
			m_death_cam_dir	= D;
			m_death_cam_nrm	= N;
		}
	}
	else
	{
		m_death_cam_dir		= D;
		m_death_cam_nrm		= N;
	}

	cameras[eacFirstEye]->Set	(P, m_death_cam_dir, m_death_cam_nrm);
}

// ---- The eyes close ----
// death_eyes_delay after death two black lids come in from the top and the bottom of the screen and
// meet in the middle over death_eyes_time, eased in and out. The picture between them is an ellipse
// drawn out to the sides (death_eyes_width) that flattens into a line, with a soft band of
// death_eyes_feather inside its edge. Drawn in the UI pass under the
// "game over" tutorial (that one is its own seqRender entry, after the level), with the crosshair's
// flat-colour shader: the lids are just vertex colours.

// a trapezoid between two edges running from x0 to x1 (t = upper, b = lower, in screen y)
static IC void dc_lid_quad(float x0, float x1, float t0, float t1, float b0, float b1, u32 ct, u32 cb)
{
	UIRender->PushPoint(x0, b0, 0.f, cb, 0.f, 0.f);
	UIRender->PushPoint(x0, t0, 0.f, ct, 0.f, 0.f);
	UIRender->PushPoint(x1, b1, 0.f, cb, 0.f, 0.f);
	UIRender->PushPoint(x1, b1, 0.f, cb, 0.f, 0.f);
	UIRender->PushPoint(x0, t0, 0.f, ct, 0.f, 0.f);
	UIRender->PushPoint(x1, t1, 0.f, ct, 0.f, 0.f);
}

// The "game over" tutorial held back by Die() starts once the lids have fully met and the screen has
// then stayed black for death_game_over_delay. It is started here in the update, not from the
// eyelids' draw, so it does not open a window in the middle of the UI pass.
void CActor::death_cam_UpdateGameOver()
{
	if (!m_death_game_over_pending)	return;
	const float closed		= _max(m_death_eyes_delay, 0.f) + _max(m_death_eyes_time, 0.f);
	if (float(Device.dwTimeGlobal - m_death_time) < 1000.f * (closed + _max(m_death_game_over_hold, 0.f)))	return;

	m_death_game_over_pending = false;
	void start_tutorial		(LPCSTR name);
	start_tutorial			("game_over");
}

void CActor::death_cam_RenderEyelids()
{
	if (!m_death_eyes_enabled || g_Alive() || !m_death_cam_valid)	return;
	if (!psActorFlags.test(AF_FIRST_PERSON_DEATH) || Level().CurrentEntity() != this)	return;

	const float t			= float(Device.dwTimeGlobal - m_death_time) / 1000.f - m_death_eyes_delay;
	if (t <= 0.f)			return;
	float p					= (m_death_eyes_time > EPS) ? (t / m_death_eyes_time) : 1.f;
	clamp					(p, 0.f, 1.f);
	p						= p * p * (3.f - 2.f * p);		// ease in and out

	const float W			= float(Device.dwWidth);
	const float H			= float(Device.dwHeight);
	const float half_w		= 0.5f * W;
	const float half_h		= 0.5f * H;
	const float f			= _max(m_death_eyes_feather * H, 1.f);
	// What is left of the picture is an ellipse drawn out to the sides: its half width `a` stays put,
	// its half height `h` shrinks. A lid's edge is the ellipse's upper (lower) arc, so the lids are
	// hollowed out and close at the corners first; past |x| = a they have already met.
	const float a			= _max(m_death_eyes_width, 0.1f) * half_w;
	// open: the arc clears the top corners, soft band included (an `a` inside the screen never opens
	// them -- those corners stay black from the start)
	const float corner		= (a > half_w) ? _sqrt(1.f - _sqr(half_w / a)) : 1.f;
	const float h_open		= (half_h + f) / corner;
	// closed at h = 0: both lids' solid parts then meet on the centre line, the soft bands lie over black
	const float h			= h_open * (1.f - p);

	if (!m_death_eyes_shader->inited())
		m_death_eyes_shader->create("hud\\crosshair");

	const int N				= 48;
	const u32 solid			= color_rgba(0, 0, 0, 255);
	const u32 clear			= color_rgba(0, 0, 0, 0);
	UIRender->StartPrimitive(N * 4 * 6, IUIRender::ptTriList, IUIRender::pttTL);
	for (int i = 0; i < N; ++i)
	{
		const float x0		= W * float(i) / float(N);
		const float x1		= W * float(i + 1) / float(N);
		const float e0		= half_h - h * _sqrt(_max(0.f, 1.f - _sqr((x0 - half_w) / a)));
		const float e1		= half_h - h * _sqrt(_max(0.f, 1.f - _sqr((x1 - half_w) / a)));
		// upper lid: solid down to the edge, then the soft band below it
		dc_lid_quad			(x0, x1, -1.f, -1.f, e0, e1, solid, solid);
		dc_lid_quad			(x0, x1, e0, e1, e0 + f, e1 + f, solid, clear);
		// lower lid, the mirror image: the soft band above its edge, then solid to the bottom
		dc_lid_quad			(x0, x1, H - e0 - f, H - e1 - f, H - e0, H - e1, clear, solid);
		dc_lid_quad			(x0, x1, H - e0, H - e1, H + 1.f, H + 1.f, solid, solid);
	}
	UIRender->SetShader		(*m_death_eyes_shader);
	UIRender->FlushPrimitive();
}

float CActor::CameraHeight()
{
	Fvector						R;
	character_physics_support()->movement()->Box().getsize		(R);
	return						m_fCamHeightFactor*R.y;
}

IC float viewport_near(float& w, float& h)
{
	w = 2.f*VIEWPORT_NEAR*tan(deg2rad(Device.fFOV)/2.f);
	h = w*Device.fASPECT;
	float	c	= _sqrt					(w*w + h*h);
	return	_max(_max(VIEWPORT_NEAR,_max(w,h)),c);
}

ICF void calc_point(Fvector& pt, float radius, float depth, float alpha)
{
	pt.x	= radius*_sin(alpha);
	pt.y	= radius+radius*_cos(alpha);
	pt.z	= depth;
}
ICF void calc_gl_point(Fvector& pt, const Fmatrix& xform, float radius, float angle )
{
	calc_point			(pt,radius,VIEWPORT_NEAR/2,angle);
	xform.transform_tiny(pt);
}
ICF BOOL test_point( const Fvector	&pt, xrXRC& xrc,  const Fmatrix33& mat, const Fvector& ext )
{

	CDB::RESULT* it	=xrc.r_begin();
	CDB::RESULT* end=xrc.r_end	();
	for (; it!=end; it++)	{
		CDB::RESULT&	O	= *it;
		if ( GMLib.GetMaterialByIdx(O.material)->Flags.is(SGameMtl::flPassable) )
			continue;
		if ( CDB::TestBBoxTri(mat,pt,ext,O.verts,FALSE) )
			return		TRUE;
	}
	return FALSE;
}

bool test_camera_box( const Fvector &box_size, const Fmatrix &xform );
IC bool test_point( const Fvector	&pt, const Fmatrix33& mat, const Fvector& ext  )
{
	Fmatrix fmat = Fidentity;
	fmat.i.set( mat.i );
	fmat.j.set( mat.j );
	fmat.k.set( mat.k );
	fmat.c.set( pt ); 
	return test_camera_box( ext, fmat );
}

#ifdef	DEBUG
template<typename T>
void	dbg_draw_viewport( const T &cam_info, float _viewport_near )
{
	
	VERIFY( _viewport_near > 0.f );
	const Fvector near_plane_center = Fvector().mad( cam_info.Position(), cam_info.Direction(), _viewport_near );
	float h_w, h_h;
	viewport_size ( _viewport_near, cam_info, h_w, h_h );
	const Fvector right	= Fvector().mul( cam_info.Right(), h_w );
	const Fvector up	= Fvector().mul( cam_info.Up(), h_h );

	
	const Fvector	top_left = Fvector().sub( near_plane_center,  right ).add( up );
	const Fvector	top_right = Fvector().add( near_plane_center,  right ).add( up );
	const Fvector	bottom_left = Fvector().sub( near_plane_center,  right ).sub( up );
	const Fvector	bottom_right = Fvector().add( near_plane_center,  right ).sub( up );
	
	DBG_DrawLine(cam_info.Position(), top_left, color_xrgb(255, 0, 0));
	DBG_DrawLine(cam_info.Position(), top_right, color_xrgb(255, 0, 0));
	DBG_DrawLine(cam_info.Position(), bottom_left, color_xrgb(255, 0, 0));
	DBG_DrawLine(cam_info.Position(), bottom_right, color_xrgb(255, 0, 0));

	DBG_DrawLine(top_right, top_left, color_xrgb(255, 0, 0));
	DBG_DrawLine(bottom_right, top_right, color_xrgb(255, 0, 0));
	DBG_DrawLine(top_left, bottom_left, color_xrgb(255, 0, 0));
	DBG_DrawLine(bottom_left, bottom_right, color_xrgb(255, 0, 0));

}
#endif
IC void get_box_mat( Fmatrix33	&mat, float alpha, const SRotation	&r_torso  )
{
	float dZ			= ((PI_DIV_2-((PI+alpha)/2)));
	Fmatrix				xformR;
	xformR.setXYZ		(-r_torso.pitch,r_torso.yaw,-dZ);
	mat.i				= xformR.i;
	mat.j				= xformR.j;
	mat.k				= xformR.k;
}
IC void get_q_box( Fbox &xf,  float c, float alpha, float radius )
{
	Fvector src_pt,		tgt_pt;
	calc_point			(tgt_pt,radius,0,alpha);
	src_pt.set			(0,tgt_pt.y,0);
	xf.invalidate		();
	xf.modify			(src_pt);
	xf.modify			(tgt_pt);
	xf.grow				(c);

}

IC void get_cam_oob( Fvector	&bc, Fvector &bd, Fmatrix33	&mat, const Fmatrix &xform, const SRotation &r_torso, float alpha, float radius, float c )
{
	get_box_mat ( mat, alpha, r_torso );
	Fbox				xf;
	get_q_box( xf, c, alpha, radius );
	xf.xform			( Fbox().set(xf), xform )	;
	// query
	xf.get_CD			(bc,bd)		;
}
IC void get_cam_oob(  Fvector &bd, Fmatrix	&mat, const Fmatrix &xform, const SRotation &r_torso, float alpha, float radius, float c )
{
	
	Fmatrix33	mat3;
	Fvector		bc;
	get_cam_oob( bc, bd, mat3, xform, r_torso, alpha, radius, c );
	mat.set( Fidentity );
	mat.i.set( mat3.i );
	mat.j.set( mat3.j );
	mat.k.set( mat3.k );
	mat.c.set( bc );

}
void	CActor::cam_Lookout	( const Fmatrix &xform, float camera_height )
{
		if (!fis_zero(r_torso_tgt_roll))
		{
		
			float w,h;
			float c				= viewport_near(w,h); w/=2.f;h/=2.f;
			float alpha			= r_torso_tgt_roll/2.f;
			float radius		= camera_height*0.5f;
			// init valid angle
			float valid_angle	= alpha;
			Fvector				bc,bd;
			Fmatrix33			mat;
			get_cam_oob( bc, bd, mat, xform, r_torso, alpha, radius, c );

			/*
			xrXRC				xrc			;
			xrc.box_options		(0)			;
			xrc.box_query		(Level().ObjectSpace.GetStaticModel(), bc, bd)		;
			u32 tri_count		= xrc.r_count();

			*/
			//if (tri_count)		
			{
				float da		= 0.f;
				BOOL bIntersect	= FALSE;
				Fvector	ext		= {w,h,VIEWPORT_NEAR/2};
				Fvector				pt;
				calc_gl_point	( pt, xform, radius, alpha );
				if ( test_point( pt, mat, ext  ) )
				{
					da			= PI/1000.f;
					if (!fis_zero(r_torso.roll))
						da		*= r_torso.roll/_abs(r_torso.roll);
					float angle = 0.f;
					for (; _abs(angle)<_abs(alpha); angle+=da)
					{
						Fvector				pt_;
						calc_gl_point( pt_, xform, radius, angle );
						if (test_point( pt_, mat,ext )) 
							{ bIntersect=TRUE; break; } 
					}
					valid_angle	= bIntersect?angle:alpha;
				} 
			}
			r_torso.roll		= valid_angle*2.f;
			r_torso_tgt_roll	= r_torso.roll;
		}
		else
		{	
			r_torso_tgt_roll = 0.f;
			r_torso.roll = 0.f;
		}
}
void CActor::cam_Update(float dt, float fFOV)
{
	if (m_holder)
		return;

	// HUD FOV Update
	if (this == Level().CurrentControlEntity())
	{
		if (eacFirstEye == cam_active)
		{
			CHudItem* pItem = smart_cast<CHudItem*>(inventory().ActiveItem());
			CHudItem* pDevice = smart_cast<CHudItem*>(inventory().ItemFromSlot(DETECTOR_SLOT));

			if (pItem && pItem->HudItemData() && pDevice && pDevice->HudItemData())
				psHUD_FOV = fminf(pItem->GetHudFov(), pDevice->GetHudFov());
			else if (pItem && pItem->HudItemData())
				psHUD_FOV = pItem->GetHudFov();
			else if (pDevice && pDevice->HudItemData())
				psHUD_FOV = pDevice->GetHudFov();
			else
				psHUD_FOV = psHUD_FOV_def;
		}
		else
			psHUD_FOV = psHUD_FOV_def;
	}

	if( (mstate_real & mcClimb) && (cam_active!=eacFreeLook) )
		camUpdateLadder(dt);
	on_weapon_shot_update();
	Fvector point		= {0,gwr_SmoothCameraHeight(mstate_real, CameraHeight()),0};
	Fvector dangle		= {0,0,0};
	Fmatrix				xform;
	xform.setXYZ		(0,r_torso.yaw,0);
	xform.translate_over(XFORM().c);

	// lookout
	if (this == Level().CurrentControlEntity())
		cam_Lookout( xform, point.y  );


	if (!fis_zero(r_torso.roll))
	{
		float radius		= point.y*0.5f;
		float valid_angle	= r_torso.roll/2.f;
		calc_point			(point,radius,0,valid_angle);
		dangle.z			= (PI_DIV_2-((PI+valid_angle)/2));
	}

	float flCurrentPlayerY	= xform.c.y;

	// Smooth out stair step ups
	if ((character_physics_support()->movement()->Environment()==peOnGround) && (flCurrentPlayerY-fPrevCamPos>0)){
		fPrevCamPos			+= dt*1.5f;
		if (fPrevCamPos > flCurrentPlayerY)
			fPrevCamPos		= flCurrentPlayerY;
		if (flCurrentPlayerY-fPrevCamPos>0.2f)
			fPrevCamPos		= flCurrentPlayerY-0.2f;
		point.y				+= fPrevCamPos-flCurrentPlayerY;
	}else{
		fPrevCamPos			= flCurrentPlayerY;
	}

	float _viewport_near			= VIEWPORT_NEAR;
	// calc point
	xform.transform_tiny			(point);

	CCameraBase* C					= cam_Active();

	C->Update						(point,dangle);
	C->f_fov						= fFOV;

	if(eacFirstEye != cam_active)
	{
		cameras[eacFirstEye]->Update	(point,dangle);
		cameras[eacFirstEye]->f_fov		= fFOV;
	}

	// dead: the first-eye camera is put on the corpse's head, pushed out of the walls like a living
	// one, and then handed to the freelook camera that is still the active style (see death_cam_Update)
	// (the option is read every frame too, so switching it off in the menu over the corpse gives cam_3 back)
	const bool death_cam			= !g_Alive() && m_death_cam_valid && psActorFlags.test(AF_FIRST_PERSON_DEATH) && eacFirstEye != cam_active && Level().CurrentEntity() == this;
	if (death_cam)
		death_cam_Update			(dt);

	if (Level().CurrentEntity() == this)
		collide_camera( *cameras[eacFirstEye], _viewport_near );

	if (death_cam)
		C->Set						(cameras[eacFirstEye]->Position(), cameras[eacFirstEye]->Direction(), cameras[eacFirstEye]->Up());

	if( psActorFlags.test(AF_PSP) )
	{
		Cameras().UpdateFromCamera			(C);
	}else
	{
		Cameras().UpdateFromCamera			(cameras[eacFirstEye]);
	}

	fCurAVelocity			= vPrevCamDir.sub(cameras[eacFirstEye]->vDirection).magnitude()/Device.fTimeDelta;
	vPrevCamDir				= cameras[eacFirstEye]->vDirection;

#ifdef DEBUG
	if( dbg_draw_camera_collision )
	{
		dbg_draw_viewport( *cameras[eacFirstEye], _viewport_near );
		dbg_draw_viewport( Cameras(), _viewport_near );
	}
#endif

	if (Level().CurrentEntity() == this)
	{
		Level().Cameras().UpdateFromCamera	(C);
		if(eacFirstEye == cam_active && !Level().Cameras().GetCamEffector(cefDemo)){
			Cameras().ApplyDevice	(_viewport_near);
		}
	}
}

// shot effector stuff
void CActor::update_camera (CCameraShotEffector* effector)
{
	if (!effector) return;
	//	if (Level().CurrentViewEntity() != this) return;

	CCameraBase* pACam = cam_FirstEye();
	if (!pACam) return;

	if (pACam->bClampPitch)
	{
		while (pACam->pitch < pACam->lim_pitch[0])
			pACam->pitch += PI_MUL_2;
		while (pACam->pitch > pACam->lim_pitch[1])
			pACam->pitch -= PI_MUL_2;
	}

	effector->ChangeHP( &(pACam->pitch), &(pACam->yaw) );

	if (pACam->bClampYaw)	clamp(pACam->yaw,pACam->lim_yaw[0],pACam->lim_yaw[1]);
	if (pACam->bClampPitch)	clamp(pACam->pitch,pACam->lim_pitch[0],pACam->lim_pitch[1]);

	// Finished animating, but the queue may not be over: this effector holds the climb cam_max_angle is
	// measured against, and a new one would start it from zero with the camera still up. It waits for the
	// next round for as long as its weapon keeps firing.
	if (effector && !effector->IsActive())
	{
		CWeapon* weapon = smart_cast<CWeapon*>(inventory().ActiveItem());
		if (!weapon || weapon->ID() != effector->m_WeaponID || !weapon->IsWorking())
			Cameras().RemoveCamEffector(eCEShot);
	}
}


#ifdef DEBUG
void dbg_draw_frustum (float FOV, float _FAR, float A, Fvector &P, Fvector &D, Fvector &U);
extern	Flags32	dbg_net_Draw_Flags;

void CActor::OnRender	()
{
	if (!bDebug)				return;

	if ((dbg_net_Draw_Flags.is_any(dbg_draw_actor_phys)))
		character_physics_support()->movement()->dbg_Draw	();

	OnRender_Network();

	inherited::OnRender();
}
#endif
