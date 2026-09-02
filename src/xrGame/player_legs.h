#pragma once

// First-person legs.
//
// Ported from themrdemonized/xray-monolith (src\xrGame\player_hud_legs.cpp, the Anomaly branch).
// The idea is not to make the actor's own model visible in first person -- that would fight the
// self-shadow machinery, which relies on the actor being setVisible(FALSE) -- but to keep a SECOND
// copy of his visual, drive its bones from the real skeleton, hide everything above the waist and
// draw it hanging off the camera.
//
// Where the pieces live:
//   * the model comes from the worn outfit (legs_visual -> actor_visual), or [actor] visual;
//   * CActor::UpdateCL calls update(), which recreates the model on an outfit change and copies
//     the bone transforms over;
//   * the renderer asks the game once per NORMAL phase (IGame_Persistent::RenderFirstPersonLegs)
//     because the invisible actor is never reached by the visibility loop.
//
// Console: g_legs (off by default), g_legs_fwd_offset, g_legs_spine_offset_y, g_legs_attach_to_camera,
// g_legs_in_low_crouch.

class CActor;
class IKinematics;

class player_legs_controller
{
public:
					player_legs_controller	();

	void			update					(CActor* actor);
	void			render					();
	void			destroy					();

	bool			is_active				() const { return m_model != NULL; }
	// Where the legs model actually stands this frame, and whether it is being drawn at all. The
	// SHADOW needs both: it comes from the real actor visual, which stays at the actor origin, while
	// what the player SEES is this model parked behind the camera -- so the two have to be told to
	// agree. Valid only right after update(); m_draw is per-frame.
	bool			is_drawn				() const { return m_model && m_draw; }
	const Fmatrix&	transform				() const { return m_legs_transform; }

private:
	bool			resolve_config			(CActor* actor, shared_str& sect, shared_str& model);
	bool			ensure_model			(const shared_str& sect, const shared_str& model);
	void			copy_bones_from_actor	(CActor* actor);
	void			collapse_bone_branch	(u16 branch_root);
	void			shift_bone_branch		(u16 branch_root, const Fvector& delta);
	void			warn_once				(LPCSTR fmt, ...);

	IKinematics*	m_model;
	bool			m_draw;					// this frame only: built but not drawn (ladder, crouch, 3rd person)

	// the model's own heading, held while the player looks around (see update())
	float			m_yaw;
	bool			m_yaw_valid;
	bool			m_yaw_turning;

	// ...and, separately, which way "back" points: a strafe turns the MODEL up to 45 degrees away,
	// while the body still has to sit behind the EYES
	Fvector			m_offset_dir;
	bool			m_offset_dir_valid;
	float			m_sprint_blend;			// 0..1, eases the extra sprint offset in and out
	shared_str		m_visual_name;
	shared_str		m_last_outfit_sect;
	shared_str		m_last_model;

	bool			m_has_fwd_offset;		// the outfit section carried its own legs_fwd_offset
	float			m_fwd_offset;
	Fmatrix			m_legs_transform;

	xr_vector<shared_str>	m_warnings;		// warn_once bookkeeping -- a config typo must not spam the log
};

