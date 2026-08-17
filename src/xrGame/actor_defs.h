#include "PHSynchronize.h"
#include "xrserver_space.h"

#pragma once


#define ACTOR_HEIGHT			1.75f
#define ACTOR_LOOKOUT_SPEED		2.f

class CInventory;
class CInventoryItem;

namespace ACTOR_DEFS
{
// CoP quick-use slots: the SECTION assigned to each of the four slots, empty = unassigned.
// Global rather than per-actor, exactly as in CoP; saved with the actor.
extern string32	g_quick_use_slots[4];
// Short name of the key bound to quick slot `idx` ("F1"), for the labels on the slots and on
// the hud. Empty when the action is unbound.
LPCSTR			quick_use_key_name(int idx);
// What the slot bound to `section` actually points at right now -- see Actor.cpp. Used by the key
// that fires the slot, by the menu that draws it and by the hud icons, so they can never disagree.
CInventoryItem*	quick_use_resolve(CInventory& inv, LPCSTR section);
// ...and how many drinks the slot is worth in total: a sealed bottle and a half-drunk one are two
// different sections but still two uses.
u32				quick_use_count(CInventory& inv, LPCSTR section);


enum ESoundCcount {
//	SND_HIT_COUNT=8,
	SND_DIE_COUNT=4
};

enum EActorCameras {
	eacFirstEye		= 0,
	eacLookAt,
	eacFreeLook,
	eacMaxCam
};
enum EDamages {DAMAGE_FX_COUNT = 12};


enum EMoveCommand
{
	mcFwd		= (1ul<<0ul),
	mcBack		= (1ul<<1ul),
	mcLStrafe	= (1ul<<2ul),
	mcRStrafe	= (1ul<<3ul),
	mcCrouch	= (1ul<<4ul),
	mcAccel		= (1ul<<5ul),
	mcTurn		= (1ul<<6ul),
	mcJump		= (1ul<<7ul),
	mcFall		= (1ul<<8ul),
	mcLanding	= (1ul<<9ul),
	mcLanding2	= (1ul<<10ul),
	mcClimb		= (1ul<<11ul),
	mcSprint	= (1ul<<12ul),
	mcLLookout	= (1ul<<13ul),
	mcRLookout	= (1ul<<14ul),
	mcAnyMove	= (mcFwd|mcBack|mcLStrafe|mcRStrafe),
	mcAnyAction = (mcAnyMove|mcJump|mcFall|mcLanding|mcLanding2), //mcTurn|
	mcAnyState	= (mcCrouch|mcAccel|mcClimb|mcSprint),
	mcLookout	= (mcLLookout|mcRLookout),
};

// enum дл€ определени€ действи€ над вещью на которую наведен в текущее врем€ прицел.
// »спользуетс€ дл€ показа всплывающих динамических подсказок
enum EActorAction
{
	eaaNoAction			= 0,
	eaaPickup,
	eaaTalk,
	eaaOpenDoor,
	eaaSearchCorpse,
};

typedef const char*		EActorSleep;
extern EActorSleep		easCanSleepResult;
/*
//результат функции GoSleep у актера
enum EActorSleep
{
	easCanSleep			= 0,
	easNotSolidGround,
	easEnemies		
};
*/

//---------------------------------------------
// ввод с клавиатуры и мыши
struct					net_input
{
	u32					m_dwTimeStamp;

	u32					mstate_wishful;	

	u8					cam_mode;
	float				cam_yaw;
	float				cam_pitch;
	float				cam_roll;

	bool operator < (const u32 Time)
	{
		return m_dwTimeStamp < Time;
	};
};

//------------------------------
struct				net_update 		
{
	u32					dwTimeStamp;			// server(game) timestamp
	float				o_model;				// model yaw
	SRotation			o_torso;				// torso in world coords
	Fvector				p_pos;					// in world coords
	Fvector				p_accel;				// in world coords
	Fvector				p_velocity;				// in world coords
	u32					mstate;
	int					weapon;
	float				fHealth;
//	float				fArmor;

	net_update()	{
		dwTimeStamp		= 0;
		p_pos.set		(0,0,0);
		p_accel.set		(0,0,0);
		p_velocity.set	(0,0,0);
	}

	void	lerp		(net_update& A,net_update& B, float f);
};

///////////////////////////////////////////////////////
// апдайт с данными физики
struct					net_update_A
{
	u32					dwTimeStamp;
//	u32					dwTime0;
//	u32					dwTime1;
	SPHNetState			State;
};

///////////////////////////////////////////////////////
// данные дл€ интерпол€ции
struct					InterpData
{
	Fvector				Pos;
	Fvector				Vel;
	float				o_model;				// model yaw
	SRotation			o_torso;				// torso in world coords
};

}; // namespace ACTOR_DEFS 

