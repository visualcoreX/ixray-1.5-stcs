#pragma once

#include "ui3tbutton.h"
#include "UIOptionsItem.h"

class UIHint;

class CUICheckButton : public CUI3tButton, public CUIOptionsItem
{
	typedef CUI3tButton			inherited;

public:
					CUICheckButton			();
	virtual			~CUICheckButton			();

	virtual void	Update					();

	// CUIOptionsItem
	virtual void	SetCurrentValue			();
	virtual void	SaveValue				();
	virtual bool	IsChanged				();
	virtual void 	SeveBackUpValue			();
	virtual void 	Undo					();

	virtual void 	OnFocusReceive		();
	virtual void	OnFocusLost			();
	virtual void	Show				( bool status );
	virtual bool	OnMouseDown			( int mouse_btn );

			void InitCheckButton		(Fvector2 pos, Fvector2 size, LPCSTR texture_name);
			void init_hint_wnd_xml		( CUIXml& xml, LPCSTR path );

//	virtual void SetTextX(float x) {/*do nothing*/}

			void	set_hint_wnd			(UIHint* hint_wnd);

	//��������� ������
	IC	bool	GetCheck()
	{
		return m_eButtonState == BUTTON_PUSHED;
	}
	IC	void	SetCheck(bool ch)
	{
		m_eButtonState = ch ? BUTTON_PUSHED : BUTTON_NORMAL;
		SeveBackUpValue();
	}
	// Same, but WITHOUT re-taking the undo baseline. SetCheck moves it, which would make the option
	// look unchanged -- and CUIOptionsManager::SaveValues only saves what IsChanged() reports, so a
	// tick cleared from code would silently never be applied.
	IC	void	SetCheckKeepBackup(bool ch)
	{
		m_eButtonState = ch ? BUTTON_PUSHED : BUTTON_NORMAL;
	}

	// Controls that follow this checkbox: they are Enable()d while it is checked and greyed out
	// while it isn't. Set replaces the list, Add appends -- a master option can own several
	// sub-options (pda_3d owns pda_autozoom + pda_savezoomstate).
	void SetDependControl(CUIWindow* pWnd);
	void AddDependControl(CUIWindow* pWnd);

	// Options that cannot both be on (fullscreen / borderless window): ticking this one clears the
	// others. Wire it BOTH ways -- each side names the other -- and whichever the player clicks last
	// is the one that stays on.
	void AddExclusiveControl(CUICheckButton* pBtn);

private:
	bool			b_backup_val;
	void InitTexture2				(LPCSTR texture_name);
	xr_vector<CUIWindow*>	m_depend_controls;
	xr_vector<CUICheckButton*>	m_exclusive_controls;

protected:
	UIHintWindow*	m_hint_owner;
};