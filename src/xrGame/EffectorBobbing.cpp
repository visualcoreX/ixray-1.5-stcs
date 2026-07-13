#include "stdafx.h"
#include "EffectorBobbing.h"


#include "actor.h"
#include "actor_defs.h"


#define BOBBING_SECT "bobbing_effector"

#define CROUCH_FACTOR	0.75f
#define SPEED_REMINDER	5.f 



//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CEffectorBobbing::CEffectorBobbing() : CEffectorCam(eCEBobbing,10000.f)
{
	fTime			= 0;
	fReminderFactor	= 0;
	is_limping		= false;
	m_fPhase		= 0.f;
	m_fCurrAmp		= 0.f;

	m_fAmplitudeRun		= pSettings->r_float(BOBBING_SECT, "run_amplitude");
	m_fAmplitudeWalk	= pSettings->r_float(BOBBING_SECT, "walk_amplitude");
	m_fAmplitudeLimp	= pSettings->r_float(BOBBING_SECT, "limp_amplitude");

	m_fSpeedRun			= pSettings->r_float(BOBBING_SECT, "run_speed");
	m_fSpeedWalk		= pSettings->r_float(BOBBING_SECT, "walk_speed");
	m_fSpeedLimp		= pSettings->r_float(BOBBING_SECT, "limp_speed");
}

CEffectorBobbing::~CEffectorBobbing	()
{
}

void CEffectorBobbing::SetState(u32 mstate, bool limping, bool ZoomMode){
	dwMState		= mstate;
	is_limping		= limping;
	m_bZoomMode		= ZoomMode;
}


BOOL CEffectorBobbing::ProcessCam(SCamEffectorInfo& info)
{
	fTime			+= Device.fTimeDelta;
	if (dwMState&ACTOR_DEFS::mcAnyMove){
		if (fReminderFactor<1.f)	fReminderFactor += SPEED_REMINDER*Device.fTimeDelta;
		else						fReminderFactor = 1.f;
	}else{
		if (fReminderFactor>0.f)	fReminderFactor -= SPEED_REMINDER*Device.fTimeDelta;
		else						fReminderFactor = 0.f;
	}
	if (!fsimilar(fReminderFactor,0)){
		Fmatrix		M;
		M.identity	();
		M.j.set		(info.n);
		M.k.set		(info.d);
		M.i.crossproduct(info.n, info.d);
		M.c.set		(info.p);
		
		// apply footstep bobbing effect
		Fvector dangle;
		float k		= ((dwMState& ACTOR_DEFS::mcCrouch)?CROUCH_FACTOR:1.f);

		// target amplitude/speed for the current gait
		float A_t, spd;
		if(isActorAccelerated(dwMState, m_bZoomMode))
		{
			A_t	= m_fAmplitudeRun*k;	spd = m_fSpeedRun*k;
		}
		else if(is_limping)
		{
			A_t	= m_fAmplitudeLimp*k;	spd = m_fSpeedLimp*k;
		}
		else
		{
			A_t	= m_fAmplitudeWalk*k;	spd = m_fSpeedWalk*k;
		}

		// Accumulate the sine PHASE continuously (was ST=speed*fTime, which jumps when
		// speed changes - e.g. aiming forces run->walk - snapping the camera). Ease the
		// AMPLITUDE too so run<->walk doesn't step the bob magnitude.
		const float AMP_LERP = 6.f;
		m_fPhase	+= spd * Device.fTimeDelta;
		m_fPhase	= (float)fmod(m_fPhase, 2.0*PI);
		m_fCurrAmp	+= (A_t - m_fCurrAmp) * _min(1.f, AMP_LERP*Device.fTimeDelta);

		float ST	= m_fPhase;
		float A		= m_fCurrAmp;

		float _sinA	= _abs(_sin(ST)*A)*fReminderFactor;
		float _cosA	= _cos(ST)*A*fReminderFactor;

		info.p.y	+=	_sinA;
		dangle.x	=	_cosA;
		dangle.z	=	_cosA;
		dangle.y	=	_sinA;

		Fmatrix		R;
		R.setHPB	(dangle.x,dangle.y,dangle.z);

		Fmatrix		mR;
		mR.mul		(M,R);
		
		info.d.set	(mR.k);
		info.n.set	(mR.j);
	}
//	else{
//		fTime		= 0;
//	}
	return TRUE;
}
