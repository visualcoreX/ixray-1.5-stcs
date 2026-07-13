#include "stdafx.h"
#include "WpnActionAnimator.h"
#include "player_hud.h"
#include "actor.h"
#include "level.h"

void CWpnActionAnimator::Load(LPCSTR section)
{
	inherited::Load	(section);
}

BOOL CWpnActionAnimator::net_Spawn(CSE_Abstract* DC)
{
	inherited::net_Spawn	(DC);
	SwitchState				(eHidden);
	m_show_pending			= true;
	processing_activate		();		// ensure per-frame UpdateCL so we can auto-show even from the ruck
	return					TRUE;
}

void CWpnActionAnimator::net_Destroy()
{
	// make sure we release the left-hand HUD slot if we were showing
	if (g_player_hud && HudItemData() &&
		g_player_hud->attached_item(HudItemData()->m_attach_place_idx) == HudItemData())
	{
		g_player_hud->detach_item	(this);
	}
	inherited::net_Destroy	();
}

void CWpnActionAnimator::UpdateXForm()
{}

void CWpnActionAnimator::UpdateCL()
{
	inherited::UpdateCL	();

	if (!m_show_pending)	return;
	m_show_pending = false;

	// only for the locally-viewed actor, and only if the left hand is free
	// (e.g. no detector currently out -- v1 simply skips the gesture in that case)
	CActor* actor = smart_cast<CActor*>(H_Parent());
	if (!actor || Level().CurrentViewEntity() != actor || !g_player_hud)
		return;

	u16 idx = pSettings->r_u16(HudSection().c_str(), "attach_place_idx");
	if (g_player_hud->attached_item(idx) == NULL)
		SwitchState	(eShowing);
}

void CWpnActionAnimator::OnStateSwitch(u32 S)
{
	inherited::OnStateSwitch(S);

	switch (S)
	{
	case eShowing:
		{
			g_player_hud->attach_item	(this);
			PlayHUDMotion				("anm_show", TRUE, this, GetState());
			SetPending					(TRUE);
		}break;
	case eHiding:
		{
			PlayHUDMotion				("anm_hide", TRUE, this, GetState());
			SetPending					(TRUE);
		}break;
	case eIdle:
		{
			SetPending					(FALSE);
		}break;
	case eHidden:
		{
			SetPending					(FALSE);
		}break;
	};
}

void CWpnActionAnimator::OnAnimationEnd(u32 state)
{
	inherited::OnAnimationEnd(state);

	switch (state)
	{
	case eShowing:
		{
			// hold a neutral idle until the script releases the item
			SwitchState		(eIdle);
			PlayAnimIdle	();
		}break;
	case eHiding:
		{
			SwitchState		(eHidden);
		}break;
	};
}
