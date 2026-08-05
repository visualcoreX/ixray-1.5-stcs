#pragma once
#include "UIDragDropListEx.h"

class CInventoryOwner;

// CoP quick-use slots (ported from the OpenXRay/CoP CUIDragDropReferenceList). Unlike an ordinary
// drag-drop list this one does not OWN what it shows: each cell remembers a SECTION NAME in
// ACTOR_DEFS::g_quick_use_slots and merely draws a reference to it. The item itself stays in the
// bag, so the same slot keeps working as the stack is used up and refilled -- and when the last one
// is gone the reference dims instead of disappearing.
class CUIDragDropReferenceList : public CUIDragDropListEx
{
private:
	typedef CUIDragDropListEx		inherited;
	typedef xr_vector<CUIStatic*>	ITEMS_REFERENCES_VEC;

	ITEMS_REFERENCES_VEC			m_references;
	ITEMS_REFERENCES_VEC			m_labels;		// the bound key, bottom-right of each slot

public:
									CUIDragDropReferenceList	();
	virtual							~CUIDragDropReferenceList	();

	virtual void					SetItem						(CUICellItem* itm);
	virtual void					SetItem						(CUICellItem* itm, Fvector2 abs_pos);
	virtual void					SetItem						(CUICellItem* itm, Ivector2 cell_pos);
	virtual CUICellItem*			RemoveItem					(CUICellItem* itm, bool force_root);

			// label_ofs, if given, is FOUR offsets that nudge each key caption off its cell's
			// bottom-right corner. Per-slot rather than one shared value because the frames drawn in
			// the texture are not evenly spaced, so a uniform grid cannot line every plate up.
			void					Initialize					(const Fvector2* label_ofs = NULL);
			void					LoadItemTexture				(LPCSTR section, Ivector2 cell_pos);
			void					ReloadReferences			(CInventoryOwner* pActor);
			// the grid lives in the container, which is not public -- expose what the menu needs
			// A quick slot holds only a REFERENCE. Dragging its content out therefore never moves the
			// real item: it just unbinds the slot, and dragging onto another slot swaps the two.
			void					UnbindCell					(CUICellItem* itm);
			void					SwapCellWithCursor			(CUICellItem* itm);
			Ivector2				PickCell					(const Fvector2& abs_pos);
			const Ivector2&			SlotsCapacity				();

	virtual void __stdcall			OnItemDBClick				(CUIWindow* w, void* pData);
			void					UpdateLabels				();	// re-read the bindings (they can change in the options)
};
