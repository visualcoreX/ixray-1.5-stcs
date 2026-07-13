#pragma once

#include "hud_item_object.h"

// One-shot LEFT-hand HUD animation (Gunslinger-style headlamp / night-vision toggle gesture).
// Spawned by the torch/NV toggle hook (ActorInput.cpp -> gwr_eatable.on_*); auto-shows its HUD on
// the left hand (attach_place_idx = 1) so the weapon in the right hand stays visible. The spawning
// script (gwr_action_binder) plays the sound and releases the item after its `timing`.
class CWpnActionAnimator : public CHudItemObject
{
	typedef			CHudItemObject	inherited;
	enum EAnimState { eHidden = 0, eShowing, eIdle, eHiding };

	bool			m_show_pending;

public:
	virtual void	Load			(LPCSTR section);
	virtual BOOL	net_Spawn		(CSE_Abstract* DC);
	virtual void	net_Destroy		();
	virtual void	UpdateCL		();
	virtual void	UpdateXForm		();
	virtual void	OnStateSwitch	(u32 S);
	virtual void	OnAnimationEnd	(u32 state);
};
