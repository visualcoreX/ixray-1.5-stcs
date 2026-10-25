#pragma once

#include "HUDCrosshair.h"
#include "../xrCDB/xr_collide_defs.h"


class CHUDManager;
class CLAItem;

struct SPickParam
{
	collide::rq_result RQ;
	float				power;
	u32					pass;
};

class CHUDTarget 
{
private:
	ui_shader				hShader;
	float					fuzzyShowInfo;
	SPickParam				PP;			// along the view: what is looked at (GetRQ -- use, pick up, dof)
	// along the shot's ray (CActor::HudFireRay) while a gun is in first-person hands: where the crosshair
	// is drawn, and whom it is coloured for
	SPickParam				PPF;
	bool					m_fire_ray;
	Fvector					m_fire_pos, m_fire_dir;

	bool					m_bShowCrosshair;
	CHUDCrosshair			HUDCrosshair;

private:
	collide::rq_results		RQR;

public:
							CHUDTarget	();
							~CHUDTarget	();
	void					CursorOnFrame ();
	void					Render		();
	void					Load		();
	collide::rq_result&		GetRQ		() {return PP.RQ;};
	float					GetRQVis	() {return PP.power;};
	CHUDCrosshair&			GetHUDCrosshair	() {return HUDCrosshair;}
	void					ShowCrosshair	(bool b);
	void					net_Relcase		(CObject* O);
};
