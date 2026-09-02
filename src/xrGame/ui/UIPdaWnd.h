#pragma once

#include "UIDialogWnd.h"
#include "../encyclopedia_article_defs.h"

class CInventoryOwner;
class CUIFrameLineWnd;
class CUIButton;
class CUI3tButtonEx;
class CUITabControl;
class CUIStatic;
class CUIXml;
class CUIFrameWindow;
class UIHint;

class CUIActorInfoWnd;
class CUIPdaContactsWnd;
class CUITaskWnd;
class CUIFactionWarWnd;
class CUIRankingWnd;
class CUILogsWnd;
class CUIAnimatedStatic;
class UIHint;

// Play the PDA's click gesture (pda_click / pda_aim_click) once, for an action that reaches the PDA
// from outside its own mouse handling -- see UIPdaWnd.cpp. No-op when the 3D PDA is not in hand.
void pda_play_click();


class CUIPdaWnd: public CUIDialogWnd
{
	typedef CUIDialogWnd	inherited;
protected:
	CUITabControl*			UITabControl;
	CUI3tButtonEx*			m_btn_close;

	CUIStatic*				UIMainPdaFrame;
	CUIStatic*				UINoice;
	
	CUIStatic*				m_caption;
	shared_str				m_caption_const;
	shared_str				m_caption_time;		// game clock shown in the caption, last value drawn
	CUIAnimatedStatic*		m_anim_static;

	// Текущий активный диалог
	CUIWindow*				m_pActiveDialog;
	shared_str				m_sActiveSection;

	UIHint*					m_hint_wnd;

public:
	CUITaskWnd*				pUITaskWnd;
	CUIFactionWarWnd*		pUIFactionWarWnd;
	CUIRankingWnd*			pUIRankingWnd;
	CUILogsWnd*				pUILogsWnd;

	virtual void			Reset				();

public:
							CUIPdaWnd			();
	virtual					~CUIPdaWnd			();

	virtual void 			Init				();

	virtual void 			SendMessage			(CUIWindow* pWnd, s16 msg, void* pData = NULL);

	virtual void 			Draw				();
	virtual void 			Update				();
	virtual void 			Show				();
	virtual void 			Hide				();
	// 3D PDA. RMB holds the PDA to the face (the dialog eats the input, so the item can never see
	// kWPN_ZOOM by itself); MMB flips the mouse between the PDA cursor and looking around.
	virtual bool			IR_OnKeyboardPress	(int dik);
	virtual bool			IR_OnMouseMove		(int dx, int dy);
	virtual bool			OnMouseAction		(float x, float y, EUIMessages mouse_action) {CUIDialogWnd::OnMouseAction(x,y,mouse_action);return true;} //always true because StopAnyMove() == false
		
			UIHint*			get_hint_wnd		() const { return m_hint_wnd; }
			void			DrawHint			();

			void			SetActiveCaption	();
			void			SetCaption			(LPCSTR text);
			void			ResetMapView		();
			void			Show_SecondTaskWnd	(bool status);
			void			Show_MapLegendWnd	(bool status);

			void			SetActiveSubdialog	(const shared_str& section);
	virtual bool			StopAnyMove			(){return false;}

			void			UpdatePda			();

};
