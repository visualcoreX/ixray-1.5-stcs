#pragma once
#include "ui\uistatic.h"
class CObject;


enum{
	flVisObjNotValid		=(1<<0),
	flTargetLocked			=(1<<1),
	// 2D eyepiece clipping (CWeapon's scope detector): which corners sit inside the glass, and whether
	// the found sound is still owed (it waits for the frame to first appear inside the glass)
	flCornerLT				=(1<<2),
	flCornerLB				=(1<<3),
	flCornerRT				=(1<<4),
	flCornerRB				=(1<<5),
	flFoundSndPending		=(1<<6),
};
struct SBinocVisibleObj{
							SBinocVisibleObj		()					{};
	CObject*				m_object;
	CUIStatic				m_lt;
	CUIStatic				m_lb;
	CUIStatic				m_rt;
	CUIStatic				m_rb;
	Frect					cur_rect;

	float					m_upd_speed;
	Flags8					m_flags;
	void					create_default			(u32 color);
	void					Draw					();
	void					Update					(bool eyepiece);
	bool					operator <				(const SBinocVisibleObj& other) const{ return  m_flags.test(flVisObjNotValid) < other.m_flags.test(flVisObjNotValid);} //move non-actual to tail
};

class CBinocularsVision
{
	typedef xr_vector<SBinocVisibleObj*>	VIS_OBJECTS;
	typedef VIS_OBJECTS::iterator			VIS_OBJECTS_IT;
	VIS_OBJECTS								m_active_objects;
public:
	CBinocularsVision			(const shared_str& sect);
	~CBinocularsVision			();
	void	Update				();
	void	Draw				();
	void	remove_links		(CObject *object);
	// true = the frames are seen through a 2D scope's eyepiece (see SBinocVisibleObj::Update); the
	// binoculars never set it
	void	SetEyepiece			(bool on)	{ m_bEyepiece = on; }

protected :
	Fcolor						m_frame_color;
	float						m_rotating_speed;
	void	Load				(const shared_str& section);
	ref_sound					m_snd_found;
	// GS/CoP parity: a SECOND sound, played the moment a frame stops crawling and LOCKS onto its
	// target (found_snd = "something is there", catch_snd = "locked"). Optional -- silent if the
	// params section has no catch_snd.
	ref_sound					m_snd_catch;
	bool						m_bEyepiece = false;
};