#pragma once
#include "UICellItem.h"
#include "../Weapon.h"


class CUIInventoryCellItem :public CUICellItem
{
	typedef  CUICellItem	inherited;
public:
								CUIInventoryCellItem		(CInventoryItem* itm);
	virtual		bool			EqualTo						(CUICellItem* itm);
	virtual		void			UpdateItemText				();
				CUIDragItem*	CreateDragItem				();
	virtual		bool			IsHelper					();
	virtual		void			SetIsHelper					(bool is_helper);
				bool			IsHelperOrHasHelperChild	();
				void			Update						();
				CInventoryItem* object						() {return (CInventoryItem*)m_pData;}
};

class CUIAmmoCellItem :public CUIInventoryCellItem
{
	typedef  CUIInventoryCellItem	inherited;
protected:
	virtual		void			 UpdateItemText				();
public:
								 CUIAmmoCellItem			(CWeaponAmmo* itm);

				u32				 CalculateAmmoCount			();
	virtual		bool			 EqualTo						(CUICellItem* itm);
	virtual		CUIDragItem*	 CreateDragItem				();
				CWeaponAmmo*	 object						() {return (CWeaponAmmo*)m_pData;}
};

// ---- Gunslinger layered weapon icon, shared config logic ---------------------------------------
// The weapon's own inv_grid slot is deliberately EMPTY in GS: the whole picture is composed from
// sprite layers (the body, the magazine, one per installed upgrade, a unique-weapon overlay). So any
// UI that wants to show a weapon icon has to draw these, not just the raw inv_grid rect -- otherwise
// it renders nothing at all. This collector holds the rules in ONE place; each UI only has to turn
// the result into its own widgets.
class CWeapon;
struct GWR_IconLayer
{
	shared_str	section;		// icon section carrying the inv_grid_* atlas rect
	Fvector2	offset;			// in ATLAS PIXELS, relative to the icon's top-left
};
// Visible layers, already filtered by banned_addons_mask and the hide lists, in draw order
// (always_front entries last). Empty for a weapon that doesn't use the layered icon at all.
void GWR_CollectIconLayers(CWeapon* wpn, xr_vector<GWR_IconLayer>& out);

// Realise those layers as child statics of `parent`, for the UIs that draw a weapon icon as one plain
// CUIStatic (pickup indicator, item info, HUD weapon icon) rather than as a cell item. sx/sy convert
// ATLAS PIXELS to that widget's screen units -- the same factor the caller used for the base rect,
// including any widescreen/kx correction on x. Detaches whatever was in `out` first, so it can just be
// called again on refresh. No-op for a weapon that doesn't use the layered icon.
class CUIStatic;
void GWR_AttachIconLayers(CUIStatic* parent, CWeapon* wpn, float sx, float sy,
						  xr_vector<CUIStatic*>& out, u32 tex_color);

// Dynamic cell resize (GS UIUtils.pas _grid_size_dt/_grid_lt_dt): sum of the installed upgrades'
// inv_grid_width/height deltas (-> size_dt) and their inv_grid_x/y shifts (-> lt_dt), so the composed
// icon GROWS to fit a layer that spills past the base cell (e.g. an ak74 bayonet blade, whose upgrade
// sets inv_grid_width = +1). Both zero for a weapon with no such upgrades installed.
void GWR_CalcGridResize(CWeapon* wpn, Ivector2& size_dt, Ivector2& lt_dt);

class CUIWeaponCellItem :public CUIInventoryCellItem
{
	typedef  CUIInventoryCellItem	inherited;
public:
	enum eAddonType{	eSilencer=0, eScope, eLauncher, eMaxAddon};
protected:
	CUIStatic*					m_addons					[eMaxAddon];
	Fvector2					m_addon_offset				[eMaxAddon];
	void						CreateIcon					(eAddonType);
	void						DestroyIcon					(eAddonType);
	void RefreshOffset();
	CUIStatic*					GetIcon						(eAddonType);
	void						InitAddon					(CUIStatic* s, LPCSTR section, Fvector2 offset, bool use_heading);
	bool						is_scope					();
	bool						is_silencer					();
	bool						is_launcher					();
public:
								CUIWeaponCellItem			(CWeapon* itm);
				virtual			~CUIWeaponCellItem			();
	virtual		void			Update						();
	virtual void Draw();
	virtual		void			SetColor					(u32 color);

				CWeapon*		object						() {return (CWeapon*)m_pData;}
	virtual		void			OnAfterChild				(CUIDragDropListEx* parent_list);
	virtual		CUIDragItem*	CreateDragItem				();
	virtual		bool			EqualTo						(CUICellItem* itm);
	CUIStatic*					get_addon_static			(u32 idx)				{return m_addons[idx];}

	// ---- Gunslinger layered inventory icon (UIUtils.pas, CellItemBuffer) -------------------------
	// The cell icon is COMPOSED from sprites instead of being one static picture: the weapon's own
	// parts (element_icon_<N>), one sprite per installed upgrade (upgrade_addon_icon), and an
	// optional unique-weapon overlay (uniq_icon). Each layer names an icon section whose
	// inv_grid_x/y/width/height point into the icon atlas, so InitAddon() places it exactly like a
	// stock addon icon. A layer can hide others (so the 45-round mag upgrade replaces the stock mag
	// sprite) and can be suppressed while certain addons are attached (banned_addons_mask).
protected:
	xr_vector<GWR_IconLayer>	m_gwr_shown;	// layer set currently realised as statics
	xr_vector<CUIStatic*>		m_gwr_icons;	// one static per entry of m_gwr_shown
public:
	// Rebuild the layer statics when the visible set changes (an upgrade installed, an addon
	// attached/removed, the optic swapped for another one, or the cell rotated).
	void	gwr_UpdateLayers	();
};

class CBuyItemCustomDrawCell :public ICustomDrawCell
{
	CGameFont*			m_pFont;
	string16			m_string;
public:
						CBuyItemCustomDrawCell	(LPCSTR str, CGameFont* pFont);
	virtual void		OnDraw					(CUICellItem* cell);

};
