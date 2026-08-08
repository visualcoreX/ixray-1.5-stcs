#include "StdAfx.h"
#include "UIOptionsManager.h"
#include "UIOptionsItem.h"
#include "../../xrEngine/xr_ioconsole.h"
#include "../MainMenu.h"

CUIOptionsManager::CUIOptionsManager()
{
	m_b_vid_restart = false;
	m_b_vid_restart = false;
	m_b_system_restart = false;
}

void CUIOptionsManager::RegisterItem(CUIOptionsItem* item, const shared_str& group)
{
	groups_it it = m_groups.find(group);

	if (m_groups.end() != it)
	{
		(*it).second.push_back(item);
	}
	else
	{
		group_name gr_name = group;
		items_list list;

		list.push_back(item);
		m_groups.insert(std::make_pair(gr_name, list));
	}
}

void CUIOptionsManager::UnRegisterGroup(const shared_str& group)
{
	groups_it it = m_groups.find(group);

	if (it != m_groups.end())
		m_groups.erase(it);
}
void CUIOptionsManager::UnRegisterItem(CUIOptionsItem* item)
{
	groups_it it;
	for (it = m_groups.begin(); it!= m_groups.end(); it++){
		for (u32 i = 0; i < (*it).second.size(); i++)
			if ((*it).second[i] == item){
				(*it).second.erase((*it).second.begin() + i); return;}
	}
}

void CUIOptionsManager::SendMessage2Group(const shared_str& group, const char* message)
{
	groups_it it = m_groups.find(group);

	R_ASSERT2(m_groups.end() != it, "invalid group name");

	for (u32 i = 0; i < (*it).second.size(); i++)
		(*it).second[i]->OnMessage(message);
}

void CUIOptionsManager::SeveBackupValues(const shared_str& group)
{
	groups_it it = m_groups.find(group);

	R_ASSERT3(m_groups.end() != it, "invalid group name",group.c_str());

	for (u32 i = 0; i < (*it).second.size(); i++){
		(*it).second[i]->SeveBackUpValue();
	}
}

void CUIOptionsManager::SetCurrentValues(const shared_str& group)
{
	groups_it it = m_groups.find(group);

	R_ASSERT3(m_groups.end() != it, "invalid group name",group.c_str());

	for (u32 i = 0; i < (*it).second.size(); i++)
		(*it).second[i]->SetCurrentValue();
}

void CUIOptionsManager::SaveValues(const shared_str& group)
{
	groups_it it = m_groups.find(group);

	R_ASSERT3(m_groups.end() != it, "invalid group name",group.c_str());

	Msg("~ options: SaveValues(%s)", group.c_str());	// DIAGNOSTIC: which path the dialog really takes

	// THE ONLY PLACE ALLOWED TO ORDER A vid/snd RESTART -- see CUIOptionsItem::BeginSave.
	CUIOptionsItem::BeginSave	();
	for (u32 i = 0; i < (*it).second.size(); i++)
	{
		if ((*it).second[i]->IsChanged())
            (*it).second[i]->SaveValue();
	}
	CUIOptionsItem::EndSave		();
}

bool CUIOptionsManager::IsGroupChanged(const shared_str& group)
{
	groups_it it = m_groups.find(group);	
	R_ASSERT2(m_groups.end() != it, "invalid group name");

	for (u32 i = 0; i < (*it).second.size(); i++)
	{
		if ((*it).second[i]->IsChanged())
			return true;
	}

	return false;
}

void CUIOptionsManager::UndoGroup(const shared_str& group)
{
	groups_it it = m_groups.find(group);
	R_ASSERT2(m_groups.end() != it, "invalid group name");

	// Undo restores values through SaveValue() too, but that no longer requests anything: the
	// restart flag is only open inside SaveValues above. Nothing was applied here, so nothing
	// needs restarting.
	Msg("~ options: UndoGroup(%s)", group.c_str());	// DIAGNOSTIC
	for (u32 i = 0; i < (*it).second.size(); i++)
	{
		if ((*it).second[i]->IsChanged())
            (*it).second[i]->Undo();
	}
}

void CUIOptionsManager::OptionsPostAccept()
{

	if (m_b_vid_restart)
	{
		Msg					("~ options: vid_restart requested by the options screen");
		Console->Execute	("vid_restart");

		// THE RESTART WE JUST RAN MUST NOT BOOK ANOTHER ONE. Device.Reset fans out to every
		// pureDeviceReset listener, and CMainMenu::OnDeviceReset arms flNeedVidRestart whenever a
		// reset lands while the menu is up -- so closing the menu afterwards ran vid_restart a
		// SECOND time (the log showed pairs, one of them 4-5 s). The flag exists for resets nobody
		// asked for, e.g. an alt-tab; this one we asked for ourselves and it is already done.
		if (MainMenu())	MainMenu()->ClearNeedVidRestart();
	}

	if (m_b_snd_restart)
		Console->Execute("snd_restart");

	m_b_vid_restart = false;
	m_b_snd_restart = false;
}

void CUIOptionsManager::DoVidRestart()
{
	m_b_vid_restart = true;
}

void CUIOptionsManager::DoSndRestart()
{
    m_b_snd_restart = true;
}

void CUIOptionsManager::DoSystemRestart()
{
    m_b_system_restart = true;
}




