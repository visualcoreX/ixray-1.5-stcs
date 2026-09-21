// EffectorShot.cpp: implementation of the CCameraShotEffector class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "EffectorShot.h"
#include "Weapon.h"

// Diagnostic for "which way does the camera actually kick" reports (console: g_recoil_dbg 1).
// Prints the recoil block in force and every angle change, in DEGREES, so a log can settle
// whether a complaint is the camera or the hud animation. Off by default, costs nothing then.
int g_recoil_dbg = 0;
#define RDBG(...)	do { if (g_recoil_dbg) Msg(__VA_ARGS__); } while (0)

namespace
{
	// Rise: fast start, gentle stop -- the kick shoots up then settles into the peak.
	IC float ease_out_cubic( float x )
	{
		float inv = 1.0f - x;
		return 1.0f - inv * inv * inv;
	}

	// Relax: overshoots slightly past the resting level before springing back to settle exactly on
	// it -- the camera dips a touch below the floor, then bounces up onto it.
	IC float ease_out_back( float x )
	{
		const float c1 = 1.70158f;
		const float c3 = c1 + 1.0f;
		float inv = x - 1.0f;
		return 1.0f + c3 * inv * inv * inv + c1 * inv * inv;
	}
}

//-----------------------------------------------------------------------------
// Weapon shot effector
//-----------------------------------------------------------------------------
CWeaponShotEffector::CWeaponShotEffector()
{
	Reset();
//	m_first_shot_pos = 0.0f;
}

void CWeaponShotEffector::Initialize( const CameraRecoil& cam_recoil )
{
	m_cam_recoil.Clone( cam_recoil );
	Reset();
}

void CWeaponShotEffector::Reset()
{
	m_angle_vert	= 0.0f;
	m_angle_horz	= 0.0f;

	m_prev_angle_vert = 0.0f;
	m_prev_angle_horz = 0.0f;

	m_delta_vert	= 0.0f;
	m_delta_horz	= 0.0f;

	m_LastSeed		= 0;
	m_single_shot	= false;
	m_first_shot	= false;
	m_actived		= false;
	m_shot_end		= true;

	m_phase				= ePhaseIdle;
	m_angle_vert_target	= 0.0f;
	m_angle_horz_target	= 0.0f;
	m_angle_vert_from	= 0.0f;
	m_angle_horz_from	= 0.0f;
	m_rise_elapsed		= 0.0f;
	m_relax_from		= 0.0f;
	m_relax_floor		= 0.0f;
	m_relax_elapsed		= 0.0f;
	m_relax_duration	= 0.0f;
	m_last_kick_vert	= 0.0f;

	m_roll_count		= 0;
}

void CWeaponShotEffector::Shot( CWeapon* weapon )
{
	R_ASSERT( weapon );
	m_shot_numer = weapon->ShotsFired() - 1;
	if ( m_shot_numer <= 0 )
	{
		m_shot_numer = 0;
		Reset();
	}
	m_single_shot = (weapon->GetCurrentFireMode() == 1);

	float angle	= m_cam_recoil.Dispersion    * weapon->cur_silencer_koef.cam_dispersion;
	angle      += m_cam_recoil.DispersionInc * weapon->cur_silencer_koef.cam_disper_inc * (float)m_shot_numer;
	RDBG("~recoil SHOT n=%d single=%d mode=%d | disp=%.3f inc=%.3f frac=%.2f maxV=%.1f relax=%.1f ret=%d | add=%.3f deg",
		 m_shot_numer, m_single_shot ? 1 : 0, weapon->GetCurrentFireMode(),
		 rad2deg(m_cam_recoil.Dispersion), rad2deg(m_cam_recoil.DispersionInc), m_cam_recoil.DispersionFrac,
		 rad2deg(m_cam_recoil.MaxAngleVert), rad2deg(m_cam_recoil.RelaxSpeed), m_cam_recoil.ReturnMode ? 1 : 0,
		 rad2deg(angle));
	Shot2( angle );
	RDBG("~recoil  after shot: vert=%.3f deg", rad2deg(m_angle_vert));
}

void CWeaponShotEffector::Shot2( float angle )
{
	float old_target_vert = m_angle_vert_target;

	float new_target_vert = old_target_vert +
		angle * ( m_cam_recoil.DispersionFrac + m_Random.randF(-1.0f, 1.0f) * (1.0f - m_cam_recoil.DispersionFrac) );

	clamp( new_target_vert, -m_cam_recoil.MaxAngleVert, m_cam_recoil.MaxAngleVert );
	if ( fis_zero(new_target_vert - m_cam_recoil.MaxAngleVert) )
	{
		new_target_vert *= m_Random.randF( 0.96f, 1.04f );
	}

	// What THIS shot actually added, after the max-angle clamp -- cam_relax_amount later gives back
	// only a fraction of this, not of the whole accumulated angle.
	m_last_kick_vert = new_target_vert - old_target_vert;

	float rdm = m_Random.randF( -1.0f, 1.0f );
	float new_target_horz = m_angle_horz_target + (new_target_vert / m_cam_recoil.MaxAngleVert) * rdm * m_cam_recoil.StepAngleHorz;

	clamp( new_target_horz, -m_cam_recoil.MaxAngleHorz, m_cam_recoil.MaxAngleHorz );

	// Kick off a fresh rise from wherever the camera is actually sitting right now -- mid-rise,
	// mid-relax or resting -- toward the new targets. Rapid fire keeps re-targeting this way instead
	// of ever snapping.
	m_angle_vert_from	= m_angle_vert;
	m_angle_horz_from	= m_angle_horz;
	m_angle_vert_target	= new_target_vert;
	m_angle_horz_target	= new_target_horz;
	m_rise_elapsed		= 0.0f;
	m_phase				= ePhaseRising;

	// Start a fresh, independent roll wave for this shot (see the member comment on
	// m_roll_elapsed) -- never touches any wave already in flight from an earlier shot.
	if ( m_roll_count < MAX_ROLL_INSTANCES )
	{
		m_roll_elapsed[m_roll_count] = 0.0f;
		++m_roll_count;
	}
	else
	{
		// Pathologically fast fire (or an absurd modded cam_rise_time) -- drop the oldest wave to
		// make room rather than grow unbounded.
		for ( int i = 1; i < MAX_ROLL_INSTANCES; ++i )
			m_roll_elapsed[i - 1] = m_roll_elapsed[i];
		m_roll_elapsed[MAX_ROLL_INSTANCES - 1] = 0.0f;
	}

	m_first_shot	= true;
	m_actived		= true;
	m_shot_end		= false;
}

void CWeaponShotEffector::BeginRelax()
{
	m_relax_from     = m_angle_vert_target;
	float give_back  = m_cam_recoil.RelaxAmount * m_last_kick_vert;
	m_relax_floor    = m_angle_vert_target - give_back;
	m_relax_elapsed  = 0.0f;
	m_relax_duration = _abs(give_back) / m_cam_recoil.RelaxSpeed;
	m_phase          = ePhaseRelaxing;
}

void CWeaponShotEffector::Update()
{
	float dt = Device.fTimeDelta;

	if ( m_roll_count > 0 )
	{
		float duration = m_cam_recoil.RiseTime * 1.25f;
		int write = 0;
		for ( int i = 0; i < m_roll_count; ++i )
		{
			m_roll_elapsed[i] += dt;
			if ( m_roll_elapsed[i] < duration )
			{
				if ( write != i )
					m_roll_elapsed[write] = m_roll_elapsed[i];
				++write;
			}
		}
		m_roll_count = write;
	}

	if ( m_phase == ePhaseRising )
	{
		m_rise_elapsed += dt;
		float t = ( m_cam_recoil.RiseTime > EPS_L ) ? _min(m_rise_elapsed / m_cam_recoil.RiseTime, 1.0f) : 1.0f;

		m_angle_vert = m_angle_vert_from + (m_angle_vert_target - m_angle_vert_from) * ease_out_cubic(t);
		// Horizontal has no relax phase -- just a linear rise synced to the same duration as the
		// vertical kick, and it stays wherever it lands.
		m_angle_horz = m_angle_horz_from + (m_angle_horz_target - m_angle_horz_from) * t;

		if ( t >= 1.0f )
		{
			m_angle_vert = m_angle_vert_target;
			m_angle_horz = m_angle_horz_target;

			if ( m_cam_recoil.ReturnMode || m_single_shot )
				BeginRelax();
			else
				m_phase = ePhaseIdle;
		}
	}
	else if ( m_phase == ePhaseRelaxing )
	{
		m_relax_elapsed += dt;
		float t = ( m_relax_duration > EPS_L ) ? _min(m_relax_elapsed / m_relax_duration, 1.0f) : 1.0f;

		m_angle_vert = m_relax_from + (m_relax_floor - m_relax_from) * ease_out_back(t);

		if ( t >= 1.0f )
		{
			m_angle_vert        = m_relax_floor;
			m_angle_vert_target = m_relax_floor;	// the next shot's kick builds on the settled level
			m_phase             = ePhaseIdle;
			m_actived           = false;			// nothing left to animate until the next shot
		}
	}

	if ( !m_cam_recoil.ReturnMode && m_shot_end && !m_single_shot )
	{
		m_phase   = ePhaseIdle;
		m_actived = false;
	}

	m_delta_vert = m_angle_vert - m_prev_angle_vert;
	m_delta_horz = m_angle_horz - m_prev_angle_horz;

	m_prev_angle_vert = m_angle_vert;
	m_prev_angle_horz = m_angle_horz;

	// negative delta = the camera is being pulled back DOWN this frame
	if (!fis_zero(m_delta_vert))
		RDBG("~recoil  frame: d_vert=%+.4f deg vert=%.3f | phase=%d actived=%d shot_end=%d single=%d",
			 rad2deg(m_delta_vert), rad2deg(m_angle_vert), (int)m_phase, m_actived ? 1 : 0, m_shot_end ? 1 : 0,
			 m_single_shot ? 1 : 0);

//	Msg( " <<[%d]  v=%.4f  dv=%.4f   a=%d s=%d  fr=%d", m_shot_numer, m_angle_vert, m_delta_vert, m_actived, m_first_shot, Device.dwFrame );
}

void CWeaponShotEffector::GetDeltaAngle		(Fvector& angle)
{
	angle.x			= -m_angle_vert;
	angle.y			= -m_angle_horz;
	angle.z			= 0.0f;
}

void CWeaponShotEffector::GetLastDelta		(Fvector& delta_angle)
{
	delta_angle.x	= -m_delta_vert;
	delta_angle.y	= -m_delta_horz;
	delta_angle.z	= 0.0f;
}

void CWeaponShotEffector::SetRndSeed	(s32 Seed)
{
	if (m_LastSeed == 0)
	{
		m_LastSeed			= Seed;
//		m_Random.seed		(Seed);
		m_Random.seed		(Device.dwFrame);
	}
}

float CWeaponShotEffector::GetRoll() const
{
	if ( m_roll_count <= 0 )
		return 0.0f;

	float duration = m_cam_recoil.RiseTime * 1.25f;
	if ( duration <= EPS_L )
		return 0.0f;

	// cam_roll_amount overrides the peak angle; absent (negative sentinel) falls back to
	// Dispersion*0.75.
	float amplitude = ( m_cam_recoil.RollAmount >= 0.0f ) ? m_cam_recoil.RollAmount : m_cam_recoil.Dispersion * 0.75f;

	// Sum every still-running wave -- one full sine cycle each, its own peak tapering linearly to 0
	// across the span -- instead of one shared timer that would snap when a shot restarts it early.
	float roll = 0.0f;
	for ( int i = 0; i < m_roll_count; ++i )
	{
		float t = m_roll_elapsed[i] / duration;
		if ( t < 1.0f )
			roll += amplitude * _sin( PI_MUL_2 * t ) * (1.0f - t);
	}
	return roll;
}

void CWeaponShotEffector::ChangeHP( float* pitch, float* yaw )
{
	*pitch -= m_delta_vert; // y = pitch = p = vert
	*yaw   -= m_delta_horz; // x = yaw   = h = horz

//	if ( m_first_shot )
//	{
//		m_first_shot_pos = *pitch;
//		m_first_shot = false;
//	}

//	if ( m_cam_recoil.ReturnMode && m_cam_recoil.StopReturn && (*pitch > m_first_shot_pos + 0.1f) )
//	{
//		m_actived = false;
//	}
//	Msg( "[%d]  pitch = %.4f   yaw = %.4f    fs=%d    a=%d  fr=%d", m_shot_numer, *pitch, *yaw, m_first_shot, m_actived, Device.dwFrame );

}

//-----------------------------------------------------------------------------
// Camera shot effector
//-----------------------------------------------------------------------------

CCameraShotEffector::CCameraShotEffector(const CameraRecoil& cam_recoil)
 : CEffectorCam(eCEShot,100000.0f)
{
	CWeaponShotEffector::Initialize( cam_recoil );
	m_pActor		= NULL;
}

CCameraShotEffector::~CCameraShotEffector()
{
}

BOOL CCameraShotEffector::ProcessCam(SCamEffectorInfo& info)
{
	Update();

	float roll = GetRoll();
	if ( !fis_zero(roll) )
	{
		// Bank the camera's up/direction pair around the direction axis -- same composition
		// EffectorBobbing.cpp uses for its own per-frame tilt.
		Fmatrix M;
		M.identity();
		M.j.set( info.n );
		M.k.set( info.d );
		M.i.crossproduct( info.n, info.d );
		M.c.set( info.p );

		Fmatrix R;
		R.setHPB( 0.0f, 0.0f, roll );

		Fmatrix mR;
		mR.mul( M, R );

		info.d.set( mR.k );
		info.n.set( mR.j );
	}

	return TRUE;
}

