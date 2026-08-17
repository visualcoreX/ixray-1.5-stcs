#include "stdafx.h"
#include "UIDragDropReferenceList.h"
#include "UICellItem.h"
#include "UICellItemFactory.h"
#include "UIStatic.h"
#include "UIInventoryUtilities.h"
#include "UICursor.h"
#include "../HUDManager.h"	// HUD().Font()
#include "../Inventory.h"
#include "../InventoryOwner.h"
#include "../actor.h"
#include "../actor_defs.h"
#include "../Level.h"

CUIDragDropReferenceList::CUIDragDropReferenceList()
{
	AddCallback("cell_item_reference", WINDOW_LBUTTON_DB_CLICK,
		CUIWndCallback::void_function(this, &CUIDragDropReferenceList::OnItemDBClick));
}

CUIDragDropReferenceList::~CUIDragDropReferenceList()
{
}

// One static per cell, laid out over the grid. They are what is actually drawn for an EMPTY slot
// that still remembers a section (dimmed icon); a cell holding a real item draws the item as usual.
void CUIDragDropReferenceList::Initialize(const Fvector2* label_ofs)
{
	const Ivector2& cellSize		= m_container->CellSize();
	const Ivector2& cellSpacing		= m_container->CellsSpacing();
	const Ivector2& cellsCapacity	= m_container->CellsCapacity();

	m_references.reserve(cellsCapacity.x * cellsCapacity.y);
	for (int i = 0; i < cellsCapacity.x; ++i)
	{
		for (int j = 0; j < cellsCapacity.y; ++j)
		{
			CUIStatic* reference = xr_new<CUIStatic>();
			m_references.push_back(reference);

			reference->SetWndPos	(Fvector2().set(float((cellSize.x + cellSpacing.x) * i),
													float((cellSize.y + cellSpacing.y) * j)));
			reference->SetWndSize	(Fvector2().set(float(cellSize.x), float(cellSize.y)));
			reference->SetWindowName("cell_item_reference");
			reference->SetAutoDelete(true);
			AttachChild				(reference);
			Register				(reference);

			// the key that fires this slot, tucked into the bottom-right corner of the cell
			CUIStatic* label = xr_new<CUIStatic>();
			m_labels.push_back		(label);
			label->SetAutoDelete	(true);
			label->SetFont			(HUD().Font().pFontLetterica16Russian);
			label->SetTextColor		(color_rgba(230, 230, 230, 255));
			label->SetTextAlignment	(CGameFont::alRight);
			label->SetWndSize		(Fvector2().set(float(cellSize.x), 12.f));
			const Fvector2 ofs = label_ofs ? label_ofs[m_labels.size() - 1] : Fvector2().set(0.f, 0.f);
			label->SetWndPos		(Fvector2().set(float((cellSize.x + cellSpacing.x) * i) - 2.f + ofs.x,
													float((cellSize.y + cellSpacing.y) * j + cellSize.y) - 13.f + ofs.y));
			AttachChild				(label);
		}
	}

	UpdateLabels();
}

void CUIDragDropReferenceList::UpdateLabels()
{
	for (u32 i = 0; i < m_labels.size(); ++i)
		m_labels[i]->SetText(ACTOR_DEFS::quick_use_key_name(int(i)));
}


void CUIDragDropReferenceList::SetItem(CUICellItem* itm)
{
	inherited::SetItem(itm);
}

void CUIDragDropReferenceList::SetItem(CUICellItem* itm, Fvector2 abs_pos)
{
	const Ivector2 dest_cell_pos = m_container->PickCell(abs_pos);
	if (m_container->ValidCell(dest_cell_pos) && m_container->IsRoomFree(dest_cell_pos, itm->GetGridSize()))
		SetItem(itm, dest_cell_pos);
	else if (dest_cell_pos.x != -1 && dest_cell_pos.y != -1)
	{
		CUICellItem* old_itm = m_container->GetCellAt(dest_cell_pos).m_item;
		if (old_itm)	RemoveItem(old_itm, false);
		SetItem(itm, dest_cell_pos);
	}
}

void CUIDragDropReferenceList::SetItem(CUICellItem* itm, Ivector2 cell_pos)
{
	CUIStatic* ref = m_references[m_container->CellsCapacity().x * cell_pos.y + cell_pos.x];
	ref->SetShader			(itm->GetShader());
	ref->SetOriginalRect	(itm->GetOriginalRect());
	ref->TextureOn			();
	ref->SetTextureColor	(color_rgba(255, 255, 255, 255));
	ref->SetStretchTexture	(true);

	CUICell& C = m_container->GetCellAt(cell_pos);
	if (C.m_item != itm)
	{
		m_container->PlaceItemAtPos	(itm, cell_pos);
		itm->SetWindowName			("cell_item");
		Register					(itm);
		itm->SetOwnerList			(this);
	}
}

CUICellItem* CUIDragDropReferenceList::RemoveItem(CUICellItem* itm, bool force_root)
{
	const Ivector2 pos = m_container->GetItemPos(itm);
	if (pos.x != -1 && pos.y != -1)
	{
		const int index = m_container->CellsCapacity().x * pos.y + pos.x;
		ACTOR_DEFS::g_quick_use_slots[index][0] = 0;
		m_references[index]->SetTextureColor(color_rgba(255, 255, 255, 0));
	}
	// CoP returns NULL here, but our menu moves items by handing this straight to the next list
	// (`CUICellItem* i = old_owner->RemoveItem(...); new_owner->SetItem(i);`) -- a NULL there is the
	// crash when dragging something back OUT of a quick slot.
	return inherited::RemoveItem(itm, force_root);
}

// Draw a section the actor does NOT currently carry: straight from its inv_grid_* keys.
void CUIDragDropReferenceList::LoadItemTexture(LPCSTR section, Ivector2 cell_pos)
{
	CUIStatic* ref = m_references[m_container->CellsCapacity().x * cell_pos.y + cell_pos.x];
	ref->SetShader(InventoryUtilities::GetEquipmentIconsShader());

	const float w = INV_GRID_WIDTH(false);
	const float h = INV_GRID_HEIGHT(false);

	Frect r;
	r.x1 = pSettings->r_float(section, "inv_grid_x")		* w;
	r.y1 = pSettings->r_float(section, "inv_grid_y")		* h;
	r.x2 = pSettings->r_float(section, "inv_grid_width")	* w;
	r.y2 = pSettings->r_float(section, "inv_grid_height")	* h;
	r.rb.add(r.lt);

	ref->SetOriginalRect	(r);
	ref->TextureOn			();
	ref->SetTextureColor	(color_rgba(255, 255, 255, 255));
	ref->SetStretchTexture	(true);
}

void CUIDragDropReferenceList::ReloadReferences(CInventoryOwner* pActor)
{
	if (!pActor)	return;

	if (m_drag_item)	DestroyDragItem();

	m_container->ClearAll	(true);
	m_selected_item			= NULL;

	const Ivector2& cap = m_container->CellsCapacity();
	for (int i = 0; i < cap.x; ++i)
	{
		for (int j = 0; j < cap.y; ++j)
		{
			const int	idx	= cap.x * j + i;
			CUIStatic*	ref	= m_references[idx];
			LPCSTR		name = ACTOR_DEFS::g_quick_use_slots[idx];

			if (name && name[0])
			{
				// not GetAny: the slot remembers the FULL bottle, and after a sip the bag holds the
				// next stage instead. Resolve the same way the key does, or the slot dims while the
				// item it fires is sitting in the inventory.
				PIItem itm = ACTOR_DEFS::quick_use_resolve(pActor->inventory(), name);
				if (itm)
					SetItem(create_cell_item(itm), Ivector2().set(i, j));
				else
				{
					// the section is remembered but nothing of it is left -- show it dimmed
					LoadItemTexture		(name, Ivector2().set(i, j));
					ref->SetTextureColor(color_rgba(255, 255, 255, 100));
				}
			}
			else
				ref->SetTextureColor(color_rgba(255, 255, 255, 0));
		}
	}
}

// Double-clicking a slot clears it.
void CUIDragDropReferenceList::OnItemDBClick(CUIWindow* w, void* pData)
{
	CUIStatic* ref = smart_cast<CUIStatic*>(w);
	ITEMS_REFERENCES_VEC::iterator it = std::find(m_references.begin(), m_references.end(), ref);
	if (it == m_references.end())	return;

	const int	idx	= int(it - m_references.begin());
	const Ivector2 cap = m_container->CellsCapacity();
	const Ivector2 pos = Ivector2().set(idx % cap.x, idx / cap.x);

	CUICellItem* ci = m_container->ValidCell(pos) ? m_container->GetCellAt(pos).m_item : NULL;
	if (ci)		inherited::RemoveItem(ci, false);

	ACTOR_DEFS::g_quick_use_slots[idx][0] = 0;
	(*it)->SetTextureColor(color_rgba(255, 255, 255, 0));
}

Ivector2 CUIDragDropReferenceList::PickCell(const Fvector2& abs_pos)
{
	return m_container->PickCell(abs_pos);
}

const Ivector2& CUIDragDropReferenceList::SlotsCapacity()
{
	return m_container->CellsCapacity();
}

void CUIDragDropReferenceList::UnbindCell(CUICellItem* itm)
{
	const Ivector2 pos = m_container->GetItemPos(itm);
	if (pos.x < 0 || pos.y < 0)		return;
	ACTOR_DEFS::g_quick_use_slots[m_container->CellsCapacity().x * pos.y + pos.x][0] = 0;
}

void CUIDragDropReferenceList::SwapCellWithCursor(CUICellItem* itm)
{
	const Ivector2 from = m_container->GetItemPos(itm);
	const Ivector2 to	= m_container->PickCell(GetUICursor()->GetCursorPosition());
	if (from.x < 0 || from.y < 0 || to.x < 0 || to.y < 0)	return;
	if (!m_container->ValidCell(to))						return;

	const int cap = m_container->CellsCapacity().x;
	const int a = cap * from.y + from.x;
	const int b = cap * to.y   + to.x;
	if (a == b)												return;

	string32 tmp;
	xr_strcpy(tmp, ACTOR_DEFS::g_quick_use_slots[a]);
	xr_strcpy(ACTOR_DEFS::g_quick_use_slots[a], ACTOR_DEFS::g_quick_use_slots[b]);
	xr_strcpy(ACTOR_DEFS::g_quick_use_slots[b], tmp);
}
