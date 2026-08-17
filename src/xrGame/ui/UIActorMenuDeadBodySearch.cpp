#include "stdafx.h"
#include "UIActorMenu.h"
#include "UIDragDropListEx.h"
#include "UIDragDropReferenceList.h"	// UIActorMenu.h only forward-declares it, m_pQuickSlot is used below
#include "UICharacterInfo.h"
#include "UIInventoryUtilities.h"
#include "UI3tButton.h"
#include "UICellItem.h"
#include "UICellItemFactory.h"
#include "UIFrameLineWnd.h"

#include "xrMessages.h"
#include "../alife_registry_wrappers.h"
#include "../GameObject.h"
#include "../WeaponAmmo.h"		// DeadBodyStateStamp: rounds inside a box change without the list doing so
#include "../InventoryOwner.h"
#include "../Inventory.h"
#include "../Inventory_item.h"
#include "../InventoryBox.h"
#include "../string_table.h"
#include "../ai/monsters/BaseMonster/base_monster.h"

void move_item_from_to (u16 from_id, u16 to_id, u16 what_id)
{
	NET_Packet P;
	CGameObject::u_EventGen					(P, GE_TRADE_SELL, from_id);
	P.w_u16									(what_id);
	CGameObject::u_EventSend				(P);

	//другому инвентарю - взять вещь 
	CGameObject::u_EventGen					(P, GE_TRADE_BUY, to_id);
	P.w_u16									(what_id);
	CGameObject::u_EventSend				(P);
}

bool move_item_check( PIItem itm, CInventoryOwner* from, CInventoryOwner* to, bool weight_check )
{
	if ( weight_check )
	{
		float invWeight		= to->inventory().CalcTotalWeight();
		float maxWeight		= to->MaxCarryWeight();
		float itmWeight		= itm->Weight();
		if ( invWeight + itmWeight >= maxWeight )
		{
			return false;
		}
	}
	move_item_from_to( from->object_id(), to->object_id(), itm->object_id() );
	return true;
}

// -------------------------------------------------------------------------------------------------

// What the corpse (or the box) is holding right now, as one number to compare against.
u32 CUIActorMenu::DeadBodyStateStamp() const
{
	if ( m_pPartnerInvOwner )	return m_pPartnerInvOwner->inventory().ModifyFrame();
	if ( !m_pInvBox )			return 0;

	// A box has no CInventory and so no modify counter, and its item list alone is not enough:
	// unloading a magazine into a box that already holds that calibre only raises the round count
	// INSIDE an existing ammo box, leaving the list exactly as long as it was. Fold the counts in.
	u32 stamp = u32(m_pInvBox->m_items.size());
	xr_vector<u16>::const_iterator it = m_pInvBox->m_items.begin();
	for ( ; it != m_pInvBox->m_items.end(); ++it )
	{
		CWeaponAmmo* ammo = smart_cast<CWeaponAmmo*>(Level().Objects.net_Find(*it));
		if ( ammo )		stamp = stamp * 131 + ammo->m_boxCurr;
	}
	return stamp;
}

// (Re)build the corpse/box list from whatever is in there now.
void CUIActorMenu::FillDeadBodyBag()
{
	m_dead_body_state = DeadBodyStateStamp();

	// The player is usually looking at this list when it gets refilled (a move they just made, an
	// ammo box whose count changed under them), and ClearAll puts the container back at the top.
	// Keep the view where it was -- otherwise every transfer scrolls a long corpse back to the start.
	const int scroll = m_pDeadBodyBagList->ScrollPos();

	m_pDeadBodyBagList->ClearAll	(true);

	TIItemContainer					items_list;
	if ( m_pPartnerInvOwner )
	{
		m_pPartnerInvOwner->inventory().AddAvailableItems( items_list, false ); //true
		UpdatePartnerBag();
	}
	else
	{
		VERIFY( m_pInvBox );
		m_pInvBox->AddAvailableItems( items_list );
	}

	std::sort( items_list.begin(), items_list.end(), InventoryUtilities::GreaterRoomInRuck );

	TIItemContainer::iterator it	= items_list.begin();
	TIItemContainer::iterator it_e	= items_list.end();
	for(; it != it_e; ++it)
	{
		CUICellItem* itm			= create_cell_item	(*it);
		m_pDeadBodyBagList->SetItem	(itm);
	}
	m_pDeadBodyBagList->SetScrollPos(scroll);
	UpdateDeadBodyBag				();
}

// Ordinary transfers keep the list right by themselves -- ToBag / ToDeadBodyBag add and remove the
// one cell that moved -- so there is nothing to watch for most of the time. Unloading a weapon that
// is IN the corpse is the exception: the rounds either merge into an ammo box already lying there
// (its count changes in place, and cell text is only built when a cell is) or are SPAWNED as a new
// box a frame or two later, over the network. Arm a short watch after such an action instead of
// polling forever.
void CUIActorMenu::WatchDeadBodyBag()
{
	if ( m_currMenuMode != mmDeadBodySearch )						return;

	m_dead_body_state		= DeadBodyStateStamp();
	m_dead_body_watch_until	= Device.dwTimeGlobal + 2000;	// the spawn is a round trip, not instant
}

void CUIActorMenu::UpdateDeadBodySearch()
{
	if ( !m_dead_body_watch_until )									return;
	if ( Device.dwTimeGlobal > m_dead_body_watch_until )
	{
		m_dead_body_watch_until = 0;
		return;
	}

	if ( DeadBodyStateStamp() == m_dead_body_state )				return;
	if ( CUIDragDropListEx::m_drag_item )							return;	// mid-drag, rebuild later

	// stay armed to the end of the window: one unload can BOTH top up an existing box now and spawn
	// a second one a few frames later, and that second arrival has to be picked up as well
	SetCurrentItem					(NULL);		// it may be one of the cells about to be destroyed
	FillDeadBodyBag					();
}

void CUIActorMenu::InitDeadBodySearchMode()
{
	m_pDeadBodyBagList->Show		(true);
	m_LeftBackground->Show			(true);
	m_PartnerBottomInfo->Show		(true);
	m_PartnerWeight->Show			(true);
	m_takeall_button->Show			(true);
	// the quick slot frames are part of the background art, so leaving the list hidden here read as
	// "the quick slots lost their items" while searching a corpse or a box (belt/outfit/weapon slots
	// stay visible in every mode). InitInventoryContents below refills them.
	if ( m_pQuickSlot )
	{
		m_pQuickSlot->Show			(true);
	}

	if ( m_pPartnerInvOwner )
	{
		m_PartnerCharacterInfo->Show(true);
	}
	else
	{
		m_PartnerCharacterInfo->Show(false);
	}

	InitInventoryContents			(m_pInventoryBagList);

	if ( m_pInvBox )
	{
		m_pInvBox->m_in_use = true;
	}
	FillDeadBodyBag					();

	CBaseMonster* monster = smart_cast<CBaseMonster*>( m_pPartnerInvOwner );
	
	//only for partner, box = no, monster = no
	if ( m_pPartnerInvOwner && !monster )
	{
		CInfoPortionWrapper						known_info_registry;
		known_info_registry.registry().init		(m_pPartnerInvOwner->object_id());
		KNOWN_INFO_VECTOR& known_infos			= known_info_registry.registry().objects();

		KNOWN_INFO_VECTOR_IT it_					= known_infos.begin();
		for(int i=0;it_!=known_infos.end();++it_,++i)
		{
			NET_Packet					P;
			CGameObject::u_EventGen		(P,GE_INFO_TRANSFER, m_pActorInvOwner->object_id());
			P.w_u16						(0);
			P.w_stringZ					((*it_).info_id);
			P.w_u8						(1);
			CGameObject::u_EventSend	(P);
		}
		known_infos.clear	();
	}
	UpdateDeadBodyBag();
}

void CUIActorMenu::DeInitDeadBodySearchMode()
{
	m_dead_body_watch_until			= 0;
	m_pDeadBodyBagList->Show		(false);
	m_PartnerCharacterInfo->Show	(false);
	m_LeftBackground->Show			(false);
	m_PartnerBottomInfo->Show		(false);
	m_PartnerWeight->Show			(false);
	m_takeall_button->Show			(false);
	if ( m_pQuickSlot )
	{
		m_pQuickSlot->Show			(false);
	}

	if ( m_pInvBox )
	{
		m_pInvBox->m_in_use = false;
	}
}

bool CUIActorMenu::ToDeadBodyBag(CUICellItem* itm, bool b_use_cursor_pos)
{
	CUIDragDropListEx*	old_owner		= itm->OwnerList();
	CUIDragDropListEx*	new_owner		= NULL;

	if(b_use_cursor_pos)
	{
		new_owner						= CUIDragDropListEx::m_drag_item->BackList();
		VERIFY							(new_owner==m_pDeadBodyBagList);
	}else
		new_owner						= m_pDeadBodyBagList;
	
	CUICellItem* i						= old_owner->RemoveItem(itm, (old_owner==new_owner) );

	if(b_use_cursor_pos)
		new_owner->SetItem				(i,old_owner->GetDragItemPosition());
	else
		new_owner->SetItem				(i);

	PIItem iitem						= (PIItem)i->m_pData;
	
	if ( m_pPartnerInvOwner )
	{
		move_item_from_to				(m_pActorInvOwner->object_id(), m_pPartnerInvOwner->object_id(), iitem->object_id());
	}
	else // box
	{
		move_item_from_to				(m_pActorInvOwner->object_id(), m_pInvBox->ID(), iitem->object_id());
	}
	
	UpdateDeadBodyBag();
	return true;
}

void CUIActorMenu::UpdateDeadBodyBag()
{
	string64 buf;

	LPCSTR kg_str = CStringTable().translate( "st_kg" ).c_str();
	float total	= CalcItemsWeight( m_pDeadBodyBagList );
	xr_sprintf( buf, "%.1f %s", total, kg_str );
	m_PartnerWeight->SetText( buf );
	m_PartnerWeight->AdjustWidthToText();

	Fvector2 pos = m_PartnerWeight->GetWndPos();
	pos.x = m_PartnerWeight_end_x - m_PartnerWeight->GetWndSize().x - 5.0f;
	m_PartnerWeight->SetWndPos( pos );
	pos.x = pos.x - m_PartnerBottomInfo->GetWndSize().x - 5.0f;
	m_PartnerBottomInfo->SetWndPos( pos );
}

void CUIActorMenu::TakeAllFromPartner(CUIWindow* w, void* d)
{
	VERIFY( m_pActorInvOwner );
	if ( !m_pPartnerInvOwner )
	{
		if ( m_pInvBox )
		{
			TakeAllFromInventoryBox();
		}
		return;
	}

	u32 const cnt = m_pDeadBodyBagList->ItemsCount();
	for ( u32 i = 0; i < cnt; ++i )
	{
		CUICellItem* ci = m_pDeadBodyBagList->GetItemIdx(i);
		for ( u32 j = 0; j < ci->ChildsCount(); ++j )
		{
			PIItem j_item = (PIItem)(ci->Child(j)->m_pData);
			move_item_check( j_item, m_pPartnerInvOwner, m_pActorInvOwner, false );
		}
		PIItem item = (PIItem)(ci->m_pData);
		move_item_check( item, m_pPartnerInvOwner, m_pActorInvOwner, false );
	}//for i
	m_pDeadBodyBagList->ClearAll( true ); // false
}

void CUIActorMenu::TakeAllFromInventoryBox()
{
	u16 actor_id = m_pActorInvOwner->object_id();

	u32 const cnt = m_pDeadBodyBagList->ItemsCount();
	for ( u32 i = 0; i < cnt; ++i )
	{
		CUICellItem* ci = m_pDeadBodyBagList->GetItemIdx(i);
		for ( u32 j = 0; j < ci->ChildsCount(); ++j )
		{
			PIItem j_item = (PIItem)(ci->Child(j)->m_pData);
			move_item_from_to( m_pInvBox->ID(), actor_id, j_item->object_id() );
		}

		PIItem item = (PIItem)(ci->m_pData);
		move_item_from_to( m_pInvBox->ID(), actor_id, item->object_id() );
	}//for i
	m_pDeadBodyBagList->ClearAll( true ); // false
}
