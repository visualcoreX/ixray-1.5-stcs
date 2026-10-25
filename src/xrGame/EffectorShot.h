// EffectorShot.h: interface for the CCameraShotEffector class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "CameraEffector.h"
#include "../xrEngine/cameramanager.h"
#include "Actor.h"
#include "CameraRecoil.h"

class CWeapon;

class CWeaponShotEffector
{
protected:
	CameraRecoil	m_cam_recoil;

	float			m_angle_vert;
	float			m_angle_horz;

	float			m_prev_angle_vert;
	float			m_prev_angle_horz;

	float			m_delta_vert;
	float			m_delta_horz;

	int				m_shot_numer;
	bool			m_shot_end;
	bool			m_first_shot;
//	float			m_first_shot_pos;

	bool			m_actived;
	bool			m_single_shot;

	// Recoil is driven as two eased animation phases layered over m_angle_vert/m_angle_horz above
	// (see EffectorShot.cpp for the curves): a per-shot "rise" toward the angle the shot kicked to,
	// then -- if the weapon returns at all -- a "relax" that only gives back cam_relax_amount of
	// THAT shot's own kick, never the full accumulated recoil. Horizontal only ever rises; it has no
	// relax phase (see the header comment on m_angle_horz_target).
	enum EPhase
	{
		ePhaseIdle = 0,
		ePhaseRising,
		ePhaseRelaxing,
	};

	EPhase			m_phase;

	// Logical (pre-interpolation) recoil level: what Shot2() would have set m_angle_vert/horz to
	// instantly under the old model. Next shot's kick is added on top of this, NOT on top of the
	// currently visible mid-animation value, so the burst-climb math stays independent of frame rate.
	float			m_angle_vert_target;
	float			m_angle_horz_target;

	// Where the visible angle was when the current rise phase started, and how far into it we are.
	float			m_angle_vert_from;
	float			m_angle_horz_from;
	float			m_rise_elapsed;

	// The relax phase eases from m_relax_from down to m_relax_floor (never all the way to 0).
	float			m_relax_from;
	float			m_relax_floor;
	float			m_relax_elapsed;
	float			m_relax_duration;

	// Vertical delta this shot's rise actually contributed (post max-angle clamp) -- cam_relax_amount
	// is applied against this, not the total accumulated angle.
	float			m_last_kick_vert;

	// What the last shot kicked by BEFORE the max-angle clamps (rad): vertical up, horizontal as
	// m_angle_horz counts it. The hud's own recoil leads the camera by a part of this (player_hud) and
	// keeps getting it once the camera itself has reached cam_max_angle.
	float			m_raw_kick_vert;
	float			m_raw_kick_horz;

	// The camera's own share of each kick (1 = all of it). With hud_recoil_camera_split the weapon on the
	// hud takes the rest (CActor::on_weapon_shot_start sets it before every shot).
	float			m_kick_scale_vert;
	float			m_kick_scale_horz;

	// Roll (Z-axis) shake: one full damped sine cycle per shot, amplitude cam_roll_amount (or
	// cam_dispersion*0.5 as a fallback), duration cam_rise_time*1.25 -- see GetRoll(). Each shot
	// starts its OWN independent wave instead of restarting a shared timer, so overlapping shots
	// (fast RPM) sum together instead of snapping the camera when the previous wave hadn't finished.
	enum { MAX_ROLL_INSTANCES = 16 };
	float			m_roll_elapsed[MAX_ROLL_INSTANCES];
	int				m_roll_count;

private:
	CRandom			m_Random;
	s32				m_LastSeed;

public:
				CWeaponShotEffector	();
	virtual		~CWeaponShotEffector(){};

		void	Initialize			(const CameraRecoil& cam_recoil);
		void	Reset				();

	// Also true while a roll wave from an already-finished burst is still decaying, so the camera
	// effector isn't torn down (and the roll cut off mid-wave) before it reaches 0 on its own.
	IC	bool	IsActive			(){return m_actived || m_roll_count > 0;}
//		void	SetActive			(bool Active)		{			m_actived = Active;		}
	IC	void	StopShoting			()	{ m_shot_end = true; }

		void	Update				();
	
		void	SetRndSeed			(s32 Seed);

		void	Shot				(CWeapon* weapon);
		void	Shot2				(float angle);

		void	GetDeltaAngle		(Fvector& angle);
		void	GetLastDelta		(Fvector& delta_angle);
		void	ChangeHP			(float* pitch, float* yaw);
		float	GetRoll				() const;
		void	GetLastKick			(float& vert, float& horz) const	{ vert = m_raw_kick_vert; horz = m_raw_kick_horz; }
		void	SetKickScale		(float vert, float horz)			{ m_kick_scale_vert = vert; m_kick_scale_horz = horz; }
		// a kick on top of the last shot's, in the same rise (what the hud weapon had no room left for)
		void	AddKick				(float vert, float horz);

protected:
		void	BeginRelax			();
};

class CCameraShotEffector : public CWeaponShotEffector, public CEffectorCam
{
protected:
	CActor*			m_pActor;
public:
//-					CCameraShotEffector	(float max_angle, float relax_speed, float max_angle_horz, float step_angle_horz, float angle_frac);
					CCameraShotEffector	(const CameraRecoil& cam_recoil);
	virtual			~CCameraShotEffector();
	
	virtual BOOL	ProcessCam			(SCamEffectorInfo& info);
	virtual void	SetActor			(CActor* pActor) {m_pActor = pActor;};
	
	virtual CCameraShotEffector*		cast_effector_shot				()	{return this;}
	u16				m_WeaponID;
};