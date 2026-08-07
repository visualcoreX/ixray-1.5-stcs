#pragma once

#include "../xrEngine/CameraManager.h"
#include "../xrEngine/effector.h"
#include "../xrEngine/effectorPP.h"

#define eStartEffectorID		50

#define effHit					(eStartEffectorID+1)
#define effAlcohol				(eStartEffectorID+2)
#define effFireHit				(eStartEffectorID+3)
#define effExplodeHit			(eStartEffectorID+4)
#define effNightvision			(eStartEffectorID+5)
#define effPsyHealth			(eStartEffectorID+6)
#define effControllerAura		(eStartEffectorID+7)
#define effControllerAura2		(eStartEffectorID+8)
#define effBigMonsterHit		(eStartEffectorID+9)
#define effActorDeath			(eStartEffectorID+10)
#define effActionAnimPPE		(eStartEffectorID+11)	// gwr: ppe played over a slice of a hud gesture
#define effScopeNightvision		(eStartEffectorID+12)	// GS scope_nightvision: the optic's own NV, separate from the goggles'

#define	eCEFall					((ECamEffectorType)(cefNext+1))
#define	eCENoise				((ECamEffectorType)(cefNext+2))
#define	eCEShot					((ECamEffectorType)(cefNext+3))
#define	eCEZoom					((ECamEffectorType)(cefNext+4))
#define	eCERecoil				((ECamEffectorType)(cefNext+5))
#define	eCEBobbing				((ECamEffectorType)(cefNext+6))
#define	eCEHit					((ECamEffectorType)(cefNext+7))
#define	eCEUser					((ECamEffectorType)(cefNext+11))
#define	eCEControllerPsyHit		((ECamEffectorType)(cefNext+12))
#define	eCEVampire				((ECamEffectorType)(cefNext+13))
#define	eCEPseudoGigantStep		((ECamEffectorType)(cefNext+14))
#define	eCEMonsterHit			((ECamEffectorType)(cefNext+15))
#define	eCEDOF					((ECamEffectorType)(cefNext+16))
#define	eCEWeaponAction			((ECamEffectorType)(cefNext+17))
#define	eCEActorMoving			((ECamEffectorType)(cefNext+18))
// GS per-category actor-move camera effector ids (GetActorCameraMovingAnim, ActorUtils.pas:3290):
// separate types let e.g. a landing shake start while a strafe effect is still running
#define	eCEActorMovingFwd		((ECamEffectorType)(cefNext+20))
#define	eCEActorMovingBack		((ECamEffectorType)(cefNext+21))
#define	eCEActorMovingLeft		((ECamEffectorType)(cefNext+22))
#define	eCEActorMovingRight		((ECamEffectorType)(cefNext+23))
#define	eCEActorMovingSprint	((ECamEffectorType)(cefNext+24))
#define	eCEActorCrouchDown		((ECamEffectorType)(cefNext+25))
#define	eCEActorCrouchUp		((ECamEffectorType)(cefNext+26))
#define	eCEActorJump			((ECamEffectorType)(cefNext+27))
#define	eCEActorFallCam			((ECamEffectorType)(cefNext+28))
#define	eCEActorLanding			((ECamEffectorType)(cefNext+29))
#define	eCEActorRLookoutStart	((ECamEffectorType)(cefNext+30))
#define	eCEActorLLookoutStart	((ECamEffectorType)(cefNext+31))
#define	eCEActorRLookoutEnd		((ECamEffectorType)(cefNext+32))
#define	eCEActorLLookoutEnd		((ECamEffectorType)(cefNext+33))
