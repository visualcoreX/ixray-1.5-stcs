#pragma once
#include "../control_combase.h"
#include "../../../../Include/xrRender/KinematicsAnimated.h"

class CPsyHitEffectorCam;
class CPsyHitEffectorPP;	

class CControllerPsyHit : public CControl_ComCustom<> {
	typedef CControl_ComCustom<> inherited;

	MotionID			m_stage[4];
	u8					m_current_index;

	CPsyHitEffectorCam	*m_effector_cam;
	CPsyHitEffectorPP	*m_effector_pp;

	// GS's camera reaction: one of three .anm files, chosen by phase (gunsl_controller.script's
	// TryStartControllerCamAnim). 0 = nothing playing, 1 = prepare, 2 = suicide, 3 = std attack.
	int					m_cam_anim_mode;
	void				PlayCamAnim			(int mode);

	enum ESoundState{
		ePrepare,
		eStart,
		ePull,
		eHit,
		eNone
	} m_sound_state;


	float				m_min_tube_dist;

	// internal flag if weapon was hidden
	bool				m_blocked;
	bool				m_suicide_started;	// GS: the grab handed over to CActor's suicide sequence
	bool				m_switch_blocked;	// we raised g_block_wpn_switch for the suicide scene

public:
	virtual void	load					(LPCSTR section);
	virtual	void	reinit					();
	virtual	void	update_frame			();
	virtual bool	check_start_conditions	();
	virtual void	activate				();
	virtual void	deactivate				();
	
	virtual void	on_event				(ControlCom::EEventType, ControlCom::IEventData*);

			void	on_death				();
private:

			void	play_anim				();
			void	death_glide_start			();
			void	death_glide_end			();

			void	set_sound_state			(ESoundState state);
			void	hit						();
			bool	check_conditions_final	();
};

