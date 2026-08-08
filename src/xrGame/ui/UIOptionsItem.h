#pragma once
#include "UIOptionsManager.h"

class CUIOptionsItem
{
	friend class CUIOptionsManager;
public:
	enum ESystemDepends		{sdNothing, sdVidRestart, sdSndRestart, sdSystemRestart};

public:
							CUIOptionsItem		();
	virtual					~CUIOptionsItem		();
	virtual void			AssignProps			(const shared_str& entry, const shared_str& group);
	void					SetSystemDepends	(ESystemDepends val) {m_dep = val;}

	static CUIOptionsManager* GetOptionsManager	() {return &m_optionsManager;}

	// ONLY AN EXPLICIT SAVE MAY ORDER A RESTART. SaveValue() is called from all over the widget
	// code -- CUITrackBar::UpdatePos ends in it, so merely OPENING the options screen (which reads
	// every current value into its widget via SetCurrentValue -> UpdatePos) used to arm a
	// vid_restart for texture_lod and r__supersample, and then any button, even Cancel with nothing
	// touched, spent four seconds restarting the renderer. Undo() restoring a value went the same
	// way. Call of Pripyat splits these (SaveOptValue arms restarts, UndoOptValue does not); this
	// bracket is that split without touching every item class: the flag is off by default and
	// CUIOptionsManager::SaveValues turns it on around its own loop. Values are still written
	// everywhere they were before -- only the restart REQUEST is gated.
	static void				BeginSave			()	{ s_allow_restart_req = true;  }
	static void				EndSave				()	{ s_allow_restart_req = false; }

protected:
	virtual void			SetCurrentValue		()	=0;	
	virtual void			SaveValue			();

	virtual bool			IsChanged			()			=0;
	virtual void			SeveBackUpValue		()	{};
	virtual void			Undo				()				{SetCurrentValue();};
			
			void			SendMessage2Group	(LPCSTR group, LPCSTR message);
	virtual	void			OnMessage			(LPCSTR message);


			// string
			LPCSTR			GetOptStringValue	();
			void			SaveOptStringValue	(LPCSTR val);
			// integer
			void			GetOptIntegerValue	(int& val, int& min, int& max);
			void			SaveOptIntegerValue	(int val);
			// float
			void			GetOptFloatValue	(float& val, float& min, float& max);
			void			SaveOptFloatValue	(float val);
			// bool
			bool			GetOptBoolValue		();
			void			SaveOptBoolValue	(bool val);
			// token
			LPCSTR			GetOptTokenValue	();
			xr_token*		GetOptToken			();
			void			SaveOptTokenValue	(LPCSTR val);

	shared_str				m_entry;
	ESystemDepends			m_dep;

	static CUIOptionsManager m_optionsManager;
	static bool				s_allow_restart_req;
};
