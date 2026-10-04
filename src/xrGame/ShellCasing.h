//////////////////////////////////////////////////////////////////////
// ShellCasing.h:	a spent cartridge case thrown out of a weapon on a shot --
//					a short-lived physics object with the model of its calibre
//////////////////////////////////////////////////////////////////////
#pragma once

#include "PhysicsShellHolder.h"

// The server half is CSE_Temporary, the same one the rockets use: it is not an ALife object, so a case
// never reaches a save and never outlives the level. Everything a case needs to start flying travels in
// the spawn packet (position and rotation in the usual fields, the two velocities in client_data), so
// there is nothing to match up between the weapon that asked for it and the object that arrives a
// frame or two later.
class CShellCasing : public CPhysicsShellHolder
{
	typedef CPhysicsShellHolder inherited;

public:
							CShellCasing		();
	virtual					~CShellCasing		();

	virtual void			Load				(LPCSTR section);
	virtual BOOL			net_Spawn			(CSE_Abstract* DC);
	virtual void			net_Destroy			();
	// not a save: the launch velocities written by Spawn() below
	virtual void			net_Load			(IReader& ireader);
	virtual BOOL			net_SaveRelevant	()				{ return FALSE; }
	virtual BOOL			net_Relevant		()				{ return FALSE; }
	virtual void			UpdateCL			();
	virtual void			renderable_Render	();

	// a case is not something the AI walks around, stands on a level vertex or gets snapped to the ground
	virtual BOOL			UsedAI_Locations	()				{ return FALSE; }
	virtual bool			can_validate_position_on_spawn	()	{ return false; }
	virtual bool			is_ai_obstacle		() const		{ return false; }

	// Ask the server for a case. xform = where it appears and how it is turned (the case lies along Z,
	// mouth at +Z), velocities in world space.
	// hud_scale > 0: thrown from the first-person view. The case is then drawn the way the gun in the
	// hands is drawn for its first moments, hud_scale being how much larger that looks -- see RenderHud.
	// launch_time != 0: the Device.dwTimeGlobal xform and the velocities are true for -- the case then
	// appears where it has got to by the time the server hands it over (a frame or two later), not back there.
	static void				Spawn				(LPCSTR section, const Fmatrix& xform, const Fvector& linear_vel, const Fvector& angular_vel, float hud_scale = 0.f, u32 launch_time = 0);
	// how many are alive right now
	static u32				Count				();
	// how many have appeared so far: a change = the one just asked for is there now (or another)
	static u32				Spawned				();
	// The hud pass (CHUDManager::Render_Last): draws the cases that are still in their first moments
	// after leaving a first-person gun, with the hud projection and on top of the world like the gun.
	static void				RenderHud			();

private:
			bool			in_hud_phase		() const;

	u32						m_life_time;		// ms, from the section
	u32						m_destroy_time;		// Device.dwTimeGlobal when it goes
	float					m_mass;				// <= 0: what the model says
	float					m_linear_scale;
	float					m_angular_scale;
	float					m_angular_limit;
	bool					m_hud;				// thrown from the first-person view
	float					m_scale_from;		// drawn size at the spawn, as a multiple of the real one
	float					m_draw_scale;		// ...and right now
	u32						m_scale_time;		// ms it takes to come down to 1
	float					m_model_scale;		// the model's size as a share of the hud model's part it replaces
	shared_str				m_count_group;		// the cap counts only the ones of the same group...
	u32						m_max_count;		// ...up to this many (0 = [shell_casings] max_count)
	u32						m_hud_time;			// ms it is drawn in the hud pass
	u32						m_spawn_time;
	Fmatrix					m_phys_xform;		// where the physics body is, before any drawing scale
	Fvector					m_spawn_pos;
	bool					m_reported;			// the debug line about where it ended up has been written
	Fvector					m_launch_linear;
	Fvector					m_launch_angular;
	u32						m_launch_time;		// see Spawn; 0 = none
	bool					m_processing;
};
