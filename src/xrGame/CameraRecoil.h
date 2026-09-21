////////////////////////////////////////////////////////////////////////////
//	Module 		: CameraRecoil.h
//	Created 	: 26.05.2008
//	Author		: Evgeniy Sokolov
//	Description : Camera Recoil struct
////////////////////////////////////////////////////////////////////////////

#ifndef CAMERA_RECOIL_H_INCLUDED
#define CAMERA_RECOIL_H_INCLUDED

//������ ��� �������� 
struct CameraRecoil
{
	float		RelaxSpeed;
	float		RelaxSpeed_AI;
	float		Dispersion;
	float		DispersionInc;
	float		DispersionFrac;
	float		MaxAngleVert;
	float		MaxAngleVert_AI;	// ceiling used when an NPC holds the weapon -- see object_handler.cpp
	float		MaxAngleHorz;
	float		StepAngleHorz;
	bool		ReturnMode;
	bool		StopReturn;

	// Duration (seconds) of the eased rise from the current camera offset up to the angle a shot
	// just kicked it to -- see cam_rise_time. 0 reproduces the old instant "snap".
	float		RiseTime;
	// Fraction of the LAST shot's own kick that the relax phase is allowed to undo -- see
	// cam_relax_amount. 1.0 fully undoes that shot's kick (old-style return-to-base); smaller values
	// leave more of it standing so a burst climbs instead of fighting the relax every frame.
	float		RelaxAmount;

	// Peak angle (radians) of the per-shot roll shake -- see cam_roll_amount and
	// CWeaponShotEffector::GetRoll(). Negative means "not set in the ltx": the effector then falls
	// back to Dispersion*0.75.
	float		RollAmount;

	CameraRecoil():
		MaxAngleVert	( EPS   ),
		MaxAngleVert_AI	( EPS   ),
		RelaxSpeed		( EPS_L ),
		RelaxSpeed_AI	( EPS_L ),
		Dispersion		( EPS   ),
		DispersionInc	( 0.0f  ),
		DispersionFrac	( 1.0f  ),
		MaxAngleHorz	( EPS   ),
		StepAngleHorz	( 0.0f  ),
		ReturnMode		( false ),
		StopReturn		( false ),
		RiseTime		( 0.075f ),
		RelaxAmount		( 0.5f  ),
		RollAmount		( -1.0f )
	{};

	CameraRecoil( const CameraRecoil& clone )		{	Clone( clone );	}

	IC void Clone( const CameraRecoil& clone )
	{
		// *this = clone;
		RelaxSpeed		= clone.RelaxSpeed;
		RelaxSpeed_AI	= clone.RelaxSpeed_AI;
		Dispersion		= clone.Dispersion;
		DispersionInc	= clone.DispersionInc;
		DispersionFrac	= clone.DispersionFrac;
		MaxAngleVert	= clone.MaxAngleVert;
		MaxAngleVert_AI	= clone.MaxAngleVert_AI;
		MaxAngleHorz	= clone.MaxAngleHorz;
		StepAngleHorz	= clone.StepAngleHorz;

		ReturnMode		= clone.ReturnMode;
		StopReturn		= clone.StopReturn;

		RiseTime		= clone.RiseTime;
		RelaxAmount		= clone.RelaxAmount;
		RollAmount		= clone.RollAmount;

		VERIFY( !fis_zero(RelaxSpeed)    );
		VERIFY( !fis_zero(RelaxSpeed_AI) );
		VERIFY( !fis_zero(MaxAngleVert)  );
		VERIFY( !fis_zero(MaxAngleHorz)  );
	}
}; //struct CameraRecoil

#endif // CAMERA_RECOIL_H_INCLUDED
