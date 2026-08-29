#pragma once

#include "UICursor.h"

#include "UIDialogHolder.h"


// refs
class CHUDManager;
class CUIGameCustom;
class CUIMainIngameWnd;
class CUIMessagesWindow;
struct SDrawStaticStruct;

class CUI			: public CDialogHolder
{
	CUIGameCustom*			pUIGame;
	bool					m_bShowGameIndicators;

public:
	CHUDManager*			m_Parent;
	CUIMainIngameWnd*		UIMainIngameWnd;
	CUIMessagesWindow*		m_pMessagesWnd;
public:
							CUI						(CHUDManager* p);
	virtual					~CUI					();

	bool					Render					();
	void					UIOnFrame				();

	void					Load					(CUIGameCustom* pGameUI);
	void					UnLoad					();

	bool					IR_OnKeyboardHold		(int dik);
	bool					IR_OnKeyboardPress		(int dik);
	bool					IR_OnKeyboardRelease	(int dik);
	bool					IR_OnMouseMove			(int,int);
	bool					IR_OnMouseWheel			(int direction);

	CUIGameCustom*			UIGame					()					{return pUIGame;}

	void					ShowGameIndicators		(bool b);
	bool					GameIndicatorsShown		()					{return m_bShowGameIndicators;};

	void					ShowCrosshair			(bool b);
	bool					CrosshairShown			();

	// GS Messenger.SendMessage(msg, max_difficulty) (Messenger.pas:11): every on-screen line carries a
	// difficulty ceiling and is simply not shown above it -- the harder the game, the less it tells you.
	// Default = no ceiling, so a call that does not care is unchanged.
	SDrawStaticStruct*		AddInfoMessage			(LPCSTR message, u32 max_difficulty = u32(-1));
	void					OnConnected				();

	void					UpdatePda				();
};

