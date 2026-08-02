#include "stdafx.h"
#include "UICellCustomItems.h"
#include "UIProgressBar.h"		// CUIProgressBar full type for the z-order re-attach (m_pConditionState)
#include "UIInventoryUtilities.h"
#include "../Weapon.h"
#include "UIDragDropListEx.h"
#include "IXRayGameConstants.h"

#define INV_GRID_WIDTHF(HQ_ICONS) ((HQ_ICONS) ? (100.0f) : (50.0f))
#define INV_GRID_HEIGHTF(HQ_ICONS) ((HQ_ICONS) ? (100.0f) : (50.0f))

namespace detail 
{

struct is_helper_pred
{
	bool operator ()(CUICellItem* child)
	{
		return child->IsHelper();
	}

}; // struct is_helper_pred

} //namespace detail 


CUIInventoryCellItem::CUIInventoryCellItem(CInventoryItem* itm)
{
	m_pData											= (void*)itm;

	inherited::SetShader							(InventoryUtilities::GetEquipmentIconsShader());

	m_grid_size.set									(itm->GetInvGridRect().rb);
	Frect rect; 
	rect.lt.set										(INV_GRID_WIDTHF(GameConstants::GetUseHQ_Icons()) * itm->GetInvGridRect().x1,
														INV_GRID_HEIGHTF(GameConstants::GetUseHQ_Icons()) * itm->GetInvGridRect().y1 );

	rect.rb.set										(	rect.lt.x+INV_GRID_WIDTHF(GameConstants::GetUseHQ_Icons()) * m_grid_size.x,
														rect.lt.y+INV_GRID_HEIGHTF(GameConstants::GetUseHQ_Icons()) * m_grid_size.y);

	inherited::SetOriginalRect						(rect);
	inherited::SetStretchTexture					(true);
}

bool CUIInventoryCellItem::EqualTo(CUICellItem* itm)
{
	CUIInventoryCellItem* ci = smart_cast<CUIInventoryCellItem*>( itm );
	if ( !itm )
	{
		return false;
	}
	if ( object()->object().cNameSect() != ci->object()->object().cNameSect() )
	{
		return false;
	}
	if ( !fsimilar( object()->GetCondition(), ci->object()->GetCondition(), 0.01f ) )
	{
		return false;
	}
	if ( !object()->equal_upgrades( ci->object()->upgardes() ) )
	{
		return false;
	}
	return true;
}

bool CUIInventoryCellItem::IsHelperOrHasHelperChild()
{
	return std::count_if(m_childs.begin(), m_childs.end(), detail::is_helper_pred()) > 0 || IsHelper();
}

CUIDragItem* CUIInventoryCellItem::CreateDragItem()
{
	return IsHelperOrHasHelperChild() ? NULL : inherited::CreateDragItem();
}

bool CUIInventoryCellItem::IsHelper ()
{
	return object()->is_helper_item();
}

void CUIInventoryCellItem::SetIsHelper (bool is_helper)
{
	object()->set_is_helper(is_helper);
}

void CUIInventoryCellItem::Update()
{
	inherited::Update	();
	UpdateItemText();

	u32 color = GetColor();
	if ( IsHelper() && !ChildsCount() )
	{
		color = 0xbbbbbbbb;
	}
	else if ( IsHelperOrHasHelperChild() )
	{
		color = 0xffffffff;
	}

	SetColor(color);
}

void CUIInventoryCellItem::UpdateItemText()
{
	const u32	helper_count	=  	(u32)std::count_if(m_childs.begin(), m_childs.end(), detail::is_helper_pred()) 
									+ IsHelper() ? 1 : 0;

	const u32	count			=	ChildsCount() + 1 - helper_count;

	string32	str;

	if ( count > 1 || helper_count )
	{
		xr_sprintf						( str, "x%d", count );
		m_text->SetText					( str );
		m_text->Show					( true );
	}
	else
	{
		xr_sprintf						( str, "");
		m_text->SetText					( str );
		m_text->Show					( false );
	}
}

CUIAmmoCellItem::CUIAmmoCellItem(CWeaponAmmo* itm)
:inherited(itm)
{}

bool CUIAmmoCellItem::EqualTo(CUICellItem* itm)
{
	if(!inherited::EqualTo(itm))	return false;

	CUIAmmoCellItem* ci				= smart_cast<CUIAmmoCellItem*>(itm);
	if(!ci)							return false;

	return					( (object()->cNameSect() == ci->object()->cNameSect()) );
}

CUIDragItem* CUIAmmoCellItem::CreateDragItem()
{
	return IsHelper() ? NULL : inherited::CreateDragItem();
}

u32 CUIAmmoCellItem::CalculateAmmoCount()
{
	xr_vector<CUICellItem*>::iterator it   = m_childs.begin();
	xr_vector<CUICellItem*>::iterator it_e = m_childs.end();

	u32 total	= IsHelper() ? 0 : object()->m_boxCurr;
	for ( ; it != it_e; ++it )
	{
		CUICellItem* child = *it;

		if ( !child->IsHelper() )
		{
			total += ((CUIAmmoCellItem*)(*it))->object()->m_boxCurr;
		}
	}

	return total;
}

void CUIAmmoCellItem::UpdateItemText()
{
	m_text->Show( false );
	if ( !m_custom_draw )
	{
		const u32 total = CalculateAmmoCount();
		
		string32	str;
		xr_sprintf( str, "%d", total );
		m_text->SetText( str );
		m_text->Show( true );
	}
	else
	{
		SetText( "" );
	}
}

CUIWeaponCellItem::CUIWeaponCellItem(CWeapon* itm)
:inherited(itm)
{
	m_addons[eSilencer]		= NULL;
	m_addons[eScope]		= NULL;
	m_addons[eLauncher]		= NULL;

	// The composed-icon shift (inv_addons_correction_*, weapon + installed upgrades) applies to these stock
	// addon sprites as well -- otherwise an upgrade that moves the picture (winchester stock +50, saw-off +15)
	// leaves the scope/silencer icon sitting at its pre-upgrade spot.
	Fvector2 addon_corr;
	GWR_AddonsCorrection(itm, addon_corr);

	if(itm->SilencerAttachable())
		m_addon_offset[eSilencer].set(object()->GetSilencerX() + addon_corr.x, object()->GetSilencerY() + addon_corr.y);

	if(itm->ScopeAttachable())
		m_addon_offset[eScope].set(object()->GetScopeX() + addon_corr.x, object()->GetScopeY() + addon_corr.y);

	if(itm->GrenadeLauncherAttachable())
		m_addon_offset[eLauncher].set(object()->GetGrenadeLauncherX() + addon_corr.x, object()->GetGrenadeLauncherY() + addon_corr.y);

	// Dynamic cell grow (GS): if an installed upgrade spills a layer past the base cell (ak74 bayonet ->
	// inv_grid_width +1), grow the grid footprint + texture rect so the composed icon isn't clipped. The
	// cell is recreated whenever the upgrade set changes (EqualTo checks equal_upgrades), so doing it
	// here in the ctor is enough. No-op unless an upgrade carries an inv_grid_* delta.
	Ivector2 size_dt, lt_dt;
	GWR_CalcGridResize(itm, size_dt, lt_dt);
	if (size_dt.x || size_dt.y || lt_dt.x || lt_dt.y)
	{
		Irect gr = itm->GetInvGridRect();
		m_grid_size.x = gr.rb.x + size_dt.x;
		m_grid_size.y = gr.rb.y + size_dt.y;
		const float gw = INV_GRID_WIDTHF(GameConstants::GetUseHQ_Icons());
		const float gh = INV_GRID_HEIGHTF(GameConstants::GetUseHQ_Icons());
		Frect rect;
		rect.lt.set((gr.lt.x + lt_dt.x) * gw, (gr.lt.y + lt_dt.y) * gh);
		rect.rb.set(rect.lt.x + gw * m_grid_size.x, rect.lt.y + gh * m_grid_size.y);
		inherited::SetOriginalRect(rect);
	}
}

#include "../xrServerEntities/object_broker.h"
CUIWeaponCellItem::~CUIWeaponCellItem()
{
}

bool CUIWeaponCellItem::is_scope()
{
	return object()->ScopeAttachable()&&object()->IsScopeAttached();
}

bool CUIWeaponCellItem::is_silencer()
{
	return object()->SilencerAttachable()&&object()->IsSilencerAttached();
}

bool CUIWeaponCellItem::is_launcher()
{
	return object()->GrenadeLauncherAttachable()&&object()->IsGrenadeLauncherAttached();
}

void CUIWeaponCellItem::CreateIcon(eAddonType t)
{
	if(m_addons[t])				return;
	m_addons[t]					= xr_new<CUIStatic>();	
	m_addons[t]->SetAutoDelete	(true);
	AttachChild					(m_addons[t]);
	m_addons[t]->SetShader		(InventoryUtilities::GetEquipmentIconsShader());

	u32 color = GetColor		();
	m_addons[t]->SetColor		(color);
}

void CUIWeaponCellItem::DestroyIcon(eAddonType t)
{
	DetachChild		(m_addons[t]);
	m_addons[t]		= NULL;
}

CUIStatic* CUIWeaponCellItem::GetIcon(eAddonType t)
{
	return m_addons[t];
}

void CUIWeaponCellItem::RefreshOffset() {
	Fvector2 addon_corr;					// see the ctor: the icon-wide shift applies to these sprites too
	GWR_AddonsCorrection(object(), addon_corr);

	if (object()->SilencerAttachable()) {
		m_addon_offset[eSilencer].set(object()->GetSilencerX() + addon_corr.x, object()->GetSilencerY() + addon_corr.y);
	}

	if (object()->ScopeAttachable()) {
		m_addon_offset[eScope].set(object()->GetScopeX() + addon_corr.x, object()->GetScopeY() + addon_corr.y);
	}

	if (object()->GrenadeLauncherAttachable()) {
		m_addon_offset[eLauncher].set(object()->GetGrenadeLauncherX() + addon_corr.x, object()->GetGrenadeLauncherY() + addon_corr.y);
	}
}

// ---- Gunslinger layered weapon icon: shared config logic ----------------------------------------
// Split out of the cell item so every UI that shows a weapon icon can use the same rules (the pickup
// indicator needs them too: with GS's deliberately empty inv_grid slot, drawing only the raw rect
// shows nothing). Pure config work -- no widgets are touched here.
namespace
{
	struct gwr_raw_layer
	{
		shared_str	section;
		Fvector2	offset;
		shared_str	hide;			// sections this layer hides while it is shown
		u8			banned_mask;	// hidden while any of these addons is on (GetAddonsState bits)
		bool		always_front;
		bool		enabled;
	};

	void gwr_push(xr_vector<gwr_raw_layer>& v, LPCSTR section, const Fvector2& off,
				  LPCSTR hide, u8 mask, bool front, const Fvector2& correction)
	{
		if (!section || !section[0] || !pSettings->section_exist(section))	return;
		gwr_raw_layer L;
		L.section		= section;
		L.offset.set	(off.x + correction.x, off.y + correction.y);
		L.hide			= hide ? hide : "";
		L.banned_mask	= mask;
		L.always_front	= front;
		L.enabled		= true;
		v.push_back(L);
	}

	// mark every section named in `csv` as hidden
	void gwr_apply_hide(xr_vector<gwr_raw_layer>& v, const gwr_raw_layer& src)
	{
		if (!src.hide.size())	return;
		string128 name;
		LPCSTR p = src.hide.c_str();
		while (*p)
		{
			while (*p == ' ' || *p == ',')	++p;
			LPCSTR s = p;
			while (*p && *p != ',')			++p;
			u32 n = (u32)(p - s);
			while (n && s[n-1] == ' ')		--n;
			if (n && n < sizeof(name))
			{
				strncpy_s(name, sizeof(name), s, n);	name[n] = 0;
				for (gwr_raw_layer& T : v)
					if (&T != &src && T.section == name)	T.enabled = false;
			}
		}
	}
}

// GS additional_icon_element: a weapon-specific extra sprite drawn when an addon is attached (a scope
// that swaps the handle / hides the iron sights, a GL that adds a tac foregrip). GS reads it from the
// addon's config section, else from `additional_icon_element_<addon>` on the weapon. read_sect = where
// to look for the bare key (scope: the per-weapon scope section; sil/GL: the item section); suffix_sect
// = the key suffix for the weapon override. No-op unless the config defines it (only oc14/svu/sig550 do).
static void gwr_addon_extra(xr_vector<gwr_raw_layer>& v, const shared_str& weapon_sect,
							const shared_str& read_sect, const shared_str& suffix_sect, const Fvector2& correction)
{
	if (!read_sect.size())	return;
	LPCSTR icon = nullptr;
	Fvector2 off;	off.set(0.f, 0.f);

	if (pSettings->section_exist(read_sect) && pSettings->line_exist(read_sect, "additional_icon_element"))
	{
		icon  = pSettings->r_string(read_sect, "additional_icon_element");
		off.x = READ_IF_EXISTS(pSettings, r_float, read_sect, "additional_icon_element_offset_x", 0.f);
		off.y = READ_IF_EXISTS(pSettings, r_float, read_sect, "additional_icon_element_offset_y", 0.f);
	}
	else if (suffix_sect.size())
	{
		string256 key;
		xr_sprintf(key, "additional_icon_element_%s", suffix_sect.c_str());
		if (pSettings->line_exist(weapon_sect, key))
		{
			icon = pSettings->r_string(weapon_sect, key);
			xr_sprintf(key, "additional_icon_element_offset_x_%s", suffix_sect.c_str());	off.x = READ_IF_EXISTS(pSettings, r_float, weapon_sect, key, 0.f);
			xr_sprintf(key, "additional_icon_element_offset_y_%s", suffix_sect.c_str());	off.y = READ_IF_EXISTS(pSettings, r_float, weapon_sect, key, 0.f);
		}
	}
	if (icon && icon[0])	gwr_push(v, icon, off, nullptr, 0, false, correction);
}

// inv_addons_correction_* shifts EVERYTHING drawn on the cell icon at once (GS uses it to re-centre a
// composed icon). An installed upgrade can contribute its own: the bm16 saw-off shrinks the cell by one
// grid column (inv_grid_width -1) and carries inv_addons_correction_x = -50 to slide the picture back into
// it; the winchester's stock upgrade grows it and shifts by +50. So the value is the weapon section's plus
// the sum over installed upgrades -- and it must be applied to the STOCK addon icons (scope / silencer /
// launcher) too, not just the composed layers, or an upgrade that moves the picture leaves the scope sprite
// behind (the offsets stop matching the icon after upgrades).
void GWR_AddonsCorrection(CWeapon* wpn, Fvector2& out)
{
	out.set(0.f, 0.f);
	if (!wpn)	return;
	const shared_str& sect = wpn->cNameSect();
	out.set(
		(float)READ_IF_EXISTS(pSettings, r_s32, sect, "inv_addons_correction_x", 0),
		(float)READ_IF_EXISTS(pSettings, r_s32, sect, "inv_addons_correction_y", 0));
	for (const shared_str& up : wpn->get_upgrades())
	{
		if (!up.size() || !pSettings->section_exist(*up))	continue;
		LPCSTR csrc = *up;
		if (pSettings->line_exist(*up, "section"))
		{
			LPCSTR e = pSettings->r_string(*up, "section");
			if (e && e[0] && pSettings->section_exist(e))	csrc = e;
		}
		out.x += (float)READ_IF_EXISTS(pSettings, r_s32, csrc, "inv_addons_correction_x", 0);
		out.y += (float)READ_IF_EXISTS(pSettings, r_s32, csrc, "inv_addons_correction_y", 0);
	}
}

void GWR_CollectIconLayers(CWeapon* wpn, xr_vector<GWR_IconLayer>& out)
{
	out.clear();
	if (!wpn)	return;
	const shared_str& sect = wpn->cNameSect();

	Fvector2 correction;
	GWR_AddonsCorrection(wpn, correction);

	xr_vector<gwr_raw_layer> v;
	string128 key;

	// 1. the weapon's own parts, element_icon_<N>, scanned until the first gap
	for (int i = 0; ; ++i)
	{
		xr_sprintf(key, "element_icon_%d", i);
		if (!pSettings->line_exist(sect, key))	break;
		LPCSTR isect = pSettings->r_string(sect, key);

		Fvector2 off;
		xr_sprintf(key, "element_icon_offset_x_%d", i);	off.x = READ_IF_EXISTS(pSettings, r_float, sect, key, 0.f);
		xr_sprintf(key, "element_icon_offset_y_%d", i);	off.y = READ_IF_EXISTS(pSettings, r_float, sect, key, 0.f);
		xr_sprintf(key, "element_icon_banned_addons_mask_%d", i);	u8 mask = (u8)READ_IF_EXISTS(pSettings, r_s32, sect, key, 0);
		xr_sprintf(key, "element_icon_always_front_%d", i);			bool front = !!READ_IF_EXISTS(pSettings, r_bool, sect, key, FALSE);
		xr_sprintf(key, "element_icon_hide_%d", i);
		LPCSTR hide = pSettings->line_exist(sect, key) ? pSettings->r_string(sect, key) : nullptr;

		gwr_push(v, isect, off, hide, mask, front, correction);
	}
	if (v.empty())	return;			// weapon doesn't use the layered icon -- nothing to draw

	// 2. one layer per installed upgrade. GS keeps the keys in the upgrade's EFFECT section, reached
	//    from the node through its `section` key; older CS upgrades put them on the node -- read both.
	//    An upgrade may be hide-ONLY: it removes a part instead of adding one (the bm16 saw-off drops the
	//    barrel/stock layer) so it carries upgrade_addon_icons_hide with NO upgrade_addon_icon of its own.
	//    Those have no sprite to push, so their hide lists are collected here and applied with the rest.
	xr_vector<shared_str> hide_only;
	for (const shared_str& up : wpn->get_upgrades())
	{
		if (!up.size() || !pSettings->section_exist(*up))	continue;
		LPCSTR src = *up;
		if (pSettings->line_exist(*up, "section"))
		{
			LPCSTR e = pSettings->r_string(*up, "section");
			if (e && e[0] && pSettings->section_exist(e) &&
				(pSettings->line_exist(e, "upgrade_addon_icon") || pSettings->line_exist(e, "upgrade_addon_icons_hide")))
				src = e;
		}
		LPCSTR hide = pSettings->line_exist(src, "upgrade_addon_icons_hide")
						? pSettings->r_string(src, "upgrade_addon_icons_hide") : nullptr;
		if (!pSettings->line_exist(src, "upgrade_addon_icon"))
		{
			if (hide && hide[0])	hide_only.push_back(hide);
			continue;
		}

		Fvector2 off;
		off.x = READ_IF_EXISTS(pSettings, r_float, src, "upgrade_addon_icon_offset_x", 0.f);
		off.y = READ_IF_EXISTS(pSettings, r_float, src, "upgrade_addon_icon_offset_y", 0.f);
		u8   mask  = (u8)READ_IF_EXISTS(pSettings, r_s32,  src, "upgrade_addon_icon_banned_addons_mask", 0);
		bool front = !!READ_IF_EXISTS(pSettings, r_bool, src, "upgrade_addon_always_front", FALSE);

		gwr_push(v, pSettings->r_string(src, "upgrade_addon_icon"), off, hide, mask, front, correction);
	}

	// 2b. per-addon extra sprite (GS additional_icon_element): weapon-specific piece when an addon is on.
	//     Scope reads its per-weapon scope section (GS oc14/svu); silencer/GL the item section (GS sig550
	//     uses the GL item suffix on the weapon). No-op for weapons that don't define it (e.g. ak74).
	if (wpn->IsScopeAttached())
		gwr_addon_extra(v, sect, wpn->GetCurrentScopeSection(), wpn->GetAttachedScopeName(), correction);
	if (wpn->IsSilencerAttached())
		gwr_addon_extra(v, sect, wpn->GetSilencerName(), wpn->GetSilencerName(), correction);
	if (wpn->IsGrenadeLauncherAttached())
		gwr_addon_extra(v, sect, wpn->GetGrenadeLauncherName(), wpn->GetGrenadeLauncherName(), correction);

	// 3. unique-weapon overlay
	if (pSettings->line_exist(sect, "uniq_icon"))
	{
		Fvector2 off;
		off.x = READ_IF_EXISTS(pSettings, r_float, sect, "uniq_icon_offset_x", 0.f);
		off.y = READ_IF_EXISTS(pSettings, r_float, sect, "uniq_icon_offset_y", 0.f);
		gwr_push(v, pSettings->r_string(sect, "uniq_icon"), off, nullptr, 0, false, correction);
	}

	// suppression, in GS's order: the addon mask first, then the hide lists of whatever is still up
	const u8 flags = wpn->GetAddonsState();
	for (gwr_raw_layer& L : v)
		L.enabled = !(L.banned_mask & flags);
	// hide-only upgrades have no sprite of their own; being installed IS the condition, so they suppress
	// unconditionally (a saw-off must drop the full-length part even though it adds nothing to draw).
	for (const shared_str& h : hide_only)
	{
		gwr_raw_layer tmp;
		tmp.section		= "";			// not in v, so gwr_apply_hide considers every entry
		tmp.hide		= h;
		tmp.banned_mask	= 0;
		tmp.always_front= false;
		tmp.enabled		= true;
		tmp.offset.set	(0.f, 0.f);
		gwr_apply_hide(v, tmp);
	}
	for (const gwr_raw_layer& L : v)
		if (L.enabled)	gwr_apply_hide(v, L);

	// emit in draw order: normal layers first, always_front last
	for (int pass = 0; pass < 2; ++pass)
		for (const gwr_raw_layer& L : v)
		{
			if (!L.enabled || (L.always_front ? 0 : 1) != pass)	continue;
			GWR_IconLayer o;
			o.section	= L.section;
			o.offset	= L.offset;
			out.push_back(o);
		}
}

void GWR_CalcGridResize(CWeapon* wpn, Ivector2& size_dt, Ivector2& lt_dt)
{
	size_dt.set(0, 0);
	lt_dt.set(0, 0);
	if (!wpn)												return;
	if (!pSettings->line_exist(wpn->cNameSect(), "element_icon_0"))	return;	// not a layered icon

	// Same upgrade iteration as GWR_CollectIconLayers: read the keys from the upgrade's EFFECT section
	// (node -> `section`), falling back to the node itself for older CS-style upgrades.
	for (const shared_str& up : wpn->get_upgrades())
	{
		if (!up.size() || !pSettings->section_exist(*up))	continue;
		LPCSTR src = *up;
		if (pSettings->line_exist(*up, "section"))
		{
			LPCSTR e = pSettings->r_string(*up, "section");
			if (e && e[0] && pSettings->section_exist(e))	src = e;
		}
		size_dt.x += READ_IF_EXISTS(pSettings, r_s32, src, "inv_grid_width",  0);
		size_dt.y += READ_IF_EXISTS(pSettings, r_s32, src, "inv_grid_height", 0);
		lt_dt.x   += READ_IF_EXISTS(pSettings, r_s32, src, "inv_grid_x",      0);
		lt_dt.y   += READ_IF_EXISTS(pSettings, r_s32, src, "inv_grid_y",      0);
	}
}

// one sprite of the composed icon: `section` supplies the atlas rect, `offset` is in atlas pixels
static CUIStatic* gwr_icon_sprite(CUIStatic* parent, const shared_str& section, const Fvector2& offset,
								  float sx, float sy, u32 tex_color)
{
	if (!pSettings->section_exist(section) || !pSettings->line_exist(section, "inv_grid_width"))
		return nullptr;

	const float gw = INV_GRID_WIDTHF(GameConstants::GetUseHQ_Icons());
	const float gh = INV_GRID_HEIGHTF(GameConstants::GetUseHQ_Icons());

	CUIStatic* s = xr_new<CUIStatic>();
	s->SetAutoDelete(true);
	s->SetShader	(InventoryUtilities::GetEquipmentIconsShader());

	Frect lr = {};
	lr.lt.set(pSettings->r_u32(section, "inv_grid_x") * gw,
			  pSettings->r_u32(section, "inv_grid_y") * gh);
	lr.rb.set(pSettings->r_u32(section, "inv_grid_width")  * gw,
			  pSettings->r_u32(section, "inv_grid_height") * gh);
	lr.rb.add(lr.lt);

	s->GetStaticItem()->SetOriginalRect(lr);
	s->SetStretchTexture(true);
	s->SetWidth	 ((lr.rb.x - lr.lt.x) * sx);
	s->SetHeight ((lr.rb.y - lr.lt.y) * sy);
	s->SetWndPos (Fvector2().set(offset.x * sx, offset.y * sy));
	s->SetTextureColor(tex_color);
	parent->AttachChild(s);
	return s;
}

void GWR_AttachIconLayers(CUIStatic* parent, CWeapon* wpn, float sx, float sy,
						  xr_vector<CUIStatic*>& out, u32 tex_color, bool with_addons)
{
	if (parent)
		for (CUIStatic* s : out)	parent->DetachChild(s);
	out.clear();
	if (!parent || !wpn)	return;

	xr_vector<GWR_IconLayer> layers;
	GWR_CollectIconLayers(wpn, layers);
	for (const GWR_IconLayer& L : layers)
		if (CUIStatic* s = gwr_icon_sprite(parent, L.section, L.offset, sx, sy, tex_color))
			out.push_back(s);

	if (!with_addons)	return;

	// The attached silencer / scope / launcher, drawn the way the inventory cell draws them
	// (CUIWeaponCellItem::Update -> InitAddon): the addon item's own icon at the weapon's
	// silencer_x/y, scope_x/y, grenade_launcher_x/y, shifted by the same icon-wide correction.
	Fvector2 corr;	GWR_AddonsCorrection(wpn, corr);
	const shared_str addons[3] = {
		(wpn->SilencerAttachable()        && wpn->IsSilencerAttached())        ? wpn->GetSilencerName()        : shared_str(),
		(wpn->ScopeAttachable()           && wpn->IsScopeAttached())           ? wpn->GetAttachedScopeName()   : shared_str(),
		(wpn->GrenadeLauncherAttachable() && wpn->IsGrenadeLauncherAttached()) ? wpn->GetGrenadeLauncherName() : shared_str(),
	};
	const Fvector2 offs[3] = {
		Fvector2().set(wpn->GetSilencerX()        + corr.x, wpn->GetSilencerY()        + corr.y),
		Fvector2().set(wpn->GetScopeX()           + corr.x, wpn->GetScopeY()           + corr.y),
		Fvector2().set(wpn->GetGrenadeLauncherX() + corr.x, wpn->GetGrenadeLauncherY() + corr.y),
	};
	for (int i = 0; i < 3; ++i)
		if (addons[i].size())
			if (CUIStatic* s = gwr_icon_sprite(parent, addons[i], offs[i], sx, sy, tex_color))
				out.push_back(s);
}

// Per-frame entry (GS CellItemBuffer.Update): re-realise the statics only when the visible layer set
// actually changes, so this is a cheap compare on the frames nothing happened.
void CUIWeaponCellItem::gwr_UpdateLayers()
{
	if (!object())	return;

	xr_vector<GWR_IconLayer> now;
	GWR_CollectIconLayers(object(), now);

	bool same = (now.size() == m_gwr_shown.size());
	if (same)
		for (u32 i = 0; i < now.size(); ++i)
			if (now[i].section != m_gwr_shown[i].section ||
				!fsimilar(now[i].offset.x, m_gwr_shown[i].offset.x) ||
				!fsimilar(now[i].offset.y, m_gwr_shown[i].offset.y))
				{ same = false; break; }
	if (same && !m_gwr_icons.empty())	return;
	if (same && now.empty())			return;

	for (CUIStatic* s : m_gwr_icons)	DetachChild(s);
	m_gwr_icons.clear();
	m_gwr_shown = now;

	for (const GWR_IconLayer& L : now)
	{
		CUIStatic* s = xr_new<CUIStatic>();
		s->SetAutoDelete(true);
		AttachChild		(s);
		s->SetShader	(InventoryUtilities::GetEquipmentIconsShader());
		s->SetColor		(GetColor());
		InitAddon		(s, L.section.c_str(), L.offset, Heading());
		m_gwr_icons.push_back(s);
	}
}


void CUIWeaponCellItem::Draw() {
	inherited::Draw();

	if (m_upgrade && m_upgrade->IsShown()) {
		m_upgrade->Draw();
	}

	// The composed GWR layers are children attached AFTER the quantity counter (m_text) and the condition
	// bar (m_pConditionState), so inherited::Draw() painted them OVER those. Re-draw the count + bar on top
	// here -- exactly the same "draw again after inherited" pattern m_upgrade uses above, and (unlike a
	// DetachChild/AttachChild reorder) it never mutates the child list mid-frame, so it can't corrupt it.
	if (!m_gwr_icons.empty())
	{
		if (m_pConditionState && m_pConditionState->IsShown())	m_pConditionState->Draw();
		if (m_text && m_text->IsShown())						m_text->Draw();
	}
}

void CUIWeaponCellItem::Update()
{
	bool b						= Heading();
	inherited::Update			();

	bool bForceReInitAddons		= (b!=Heading());

	// GS layered icon (elements / upgrades / uniq). A heading flip re-lays them too, since InitAddon
	// bakes the rotation into each sprite's size and pivot.
	// a heading flip has to re-lay them too: InitAddon bakes the rotation into each sprite's size/pivot
	if (bForceReInitAddons)		{ m_gwr_shown.clear(); }
	gwr_UpdateLayers			();

	if (object()->SilencerAttachable())
	{
		if (object()->IsSilencerAttached())
		{
			if (!GetIcon(eSilencer) || bForceReInitAddons)
			{
				CreateIcon	(eSilencer);
				RefreshOffset();
				InitAddon	(GetIcon(eSilencer), *object()->GetSilencerName(), m_addon_offset[eSilencer], Heading());
			}
		}
		else
		{
			if (m_addons[eSilencer])
				DestroyIcon(eSilencer);
		}
	}

	if (object()->ScopeAttachable()){
		if (object()->IsScopeAttached())
		{
			if (!GetIcon(eScope) || bForceReInitAddons)
			{
				CreateIcon	(eScope);
				RefreshOffset();
				InitAddon	(GetIcon(eScope), *object()->GetAttachedScopeName(), m_addon_offset[eScope], Heading());
			}
		}
		else
		{
			if (m_addons[eScope])
				DestroyIcon(eScope);
		}
	}

	if (object()->GrenadeLauncherAttachable()){
		if (object()->IsGrenadeLauncherAttached())
		{
			if (!GetIcon(eLauncher) || bForceReInitAddons)
			{
				CreateIcon	(eLauncher);
				RefreshOffset();
				InitAddon	(GetIcon(eLauncher), *object()->GetGrenadeLauncherName(), m_addon_offset[eLauncher], Heading());
			}
		}
		else
		{
			if (m_addons[eLauncher])
				DestroyIcon(eLauncher);
		}
	}
}

void CUIWeaponCellItem::SetColor( u32 color )
{
	inherited::SetColor( color );
	if ( m_addons[eSilencer] )
	{
		m_addons[eSilencer]->SetColor( color );
	}
	if ( m_addons[eScope] )
	{
		m_addons[eScope]->SetColor( color );
	}
	if ( m_addons[eLauncher] )
	{
		m_addons[eLauncher]->SetColor( color );
	}
}

void CUIWeaponCellItem::OnAfterChild(CUIDragDropListEx* parent_list)
{
	if(is_silencer() && GetIcon(eSilencer))
		InitAddon	(GetIcon(eSilencer), *object()->GetSilencerName(),	m_addon_offset[eSilencer], parent_list->GetVerticalPlacement());

	if(is_scope() && GetIcon(eScope))
		InitAddon	(GetIcon(eScope),	*object()->GetAttachedScopeName(),		m_addon_offset[eScope], parent_list->GetVerticalPlacement());

	if(is_launcher() && GetIcon(eLauncher))
		InitAddon	(GetIcon(eLauncher), *object()->GetGrenadeLauncherName(),m_addon_offset[eLauncher], parent_list->GetVerticalPlacement());
}

void CUIWeaponCellItem::InitAddon(CUIStatic* s, LPCSTR section, Fvector2 addon_offset, bool b_rotate)
{
	
		Frect					tex_rect;
		Fvector2				base_scale;

		if(Heading())
		{
			base_scale.x		= GetHeight()/(INV_GRID_WIDTHF(GameConstants::GetUseHQ_Icons()) * m_grid_size.x);
			base_scale.y		= GetWidth()/(INV_GRID_HEIGHTF(GameConstants::GetUseHQ_Icons()) * m_grid_size.y);
		}
		else
		{
			base_scale.x		= GetWidth()/(INV_GRID_WIDTHF(GameConstants::GetUseHQ_Icons()) * m_grid_size.x);
			base_scale.y		= GetHeight()/(INV_GRID_HEIGHTF(GameConstants::GetUseHQ_Icons()) * m_grid_size.y);
		}
		Fvector2				cell_size;
		cell_size.x				= pSettings->r_u32(section, "inv_grid_width")*INV_GRID_WIDTHF(GameConstants::GetUseHQ_Icons());
		cell_size.y				= pSettings->r_u32(section, "inv_grid_height")*INV_GRID_HEIGHTF(GameConstants::GetUseHQ_Icons());

		tex_rect.x1				= pSettings->r_u32(section, "inv_grid_x")*INV_GRID_WIDTHF(GameConstants::GetUseHQ_Icons());
		tex_rect.y1				= pSettings->r_u32(section, "inv_grid_y")*INV_GRID_HEIGHTF(GameConstants::GetUseHQ_Icons());

		tex_rect.rb.add			(tex_rect.lt,cell_size);

		cell_size.mul			(base_scale);

		if(b_rotate)
		{
			s->SetWndSize		(Fvector2().set(cell_size.y, cell_size.x) );
			Fvector2 new_offset;
			new_offset.x		= addon_offset.y*base_scale.x;
			new_offset.y		= GetHeight() - addon_offset.x*base_scale.x - cell_size.x;
			addon_offset		= new_offset;
			addon_offset.x		*= UI()->get_current_kx();
		}else
		{
			s->SetWndSize		(cell_size);
			addon_offset.mul	(base_scale);
		}

		s->SetWndPos			(addon_offset);
		s->SetOriginalRect		(tex_rect);
		s->SetStretchTexture	(true);

		s->EnableHeading		(b_rotate);
		
		if(b_rotate)
		{
			s->SetHeading			(GetHeading());
			Fvector2 offs;
			offs.set				(0.0f, s->GetWndSize().y);
			s->SetHeadingPivot		(Fvector2().set(0.0f,0.0f), /*Fvector2().set(0.0f,0.0f)*/offs, true);
		}
}

CUIDragItem* CUIWeaponCellItem::CreateDragItem()
{
	CUIDragItem* i		= inherited::CreateDragItem();

	// The drag silhouette must show the weapon HORIZONTAL (its natural orientation), even when dragged
	// from the vertical (main-weapon) slot where the cell renders it rotated. Lay every piece FLAT and
	// scaled to the drag window (atlas-px -> window-px), the same way GWR_AttachIconLayers does for the
	// pickup/info/HUD icons. Going through the cell's own InitAddon either rotated the pieces (Heading)
	// or stretched them (false -- it scales by the cell's own, rotated, width/height).
	const float gw = INV_GRID_WIDTHF(GameConstants::GetUseHQ_Icons());
	const float gh = INV_GRID_HEIGHTF(GameConstants::GetUseHQ_Icons());
	const float dw = i->wnd()->GetWidth();
	const float dh = i->wnd()->GetHeight();
	const float sx = (m_grid_size.x > 0) ? dw / (m_grid_size.x * gw) : 1.f;
	const float sy = (m_grid_size.y > 0) ? dh / (m_grid_size.y * gh) : 1.f;
	const u32   col = i->wnd()->GetColor();

	// one flat, scaled sprite for an atlas section at an atlas-pixel offset (mirrors GWR_AttachIconLayers)
	auto lay_flat = [&](LPCSTR sect, const Fvector2& off)
	{
		if (!sect || !sect[0] || !pSettings->section_exist(sect) ||
			!pSettings->line_exist(sect, "inv_grid_width"))	return;
		CUIStatic* s = xr_new<CUIStatic>();	s->SetAutoDelete(true);
		s->SetShader(InventoryUtilities::GetEquipmentIconsShader());
		Frect lr = {};
		lr.lt.set(pSettings->r_u32(sect, "inv_grid_x") * gw,     pSettings->r_u32(sect, "inv_grid_y") * gh);
		lr.rb.set(pSettings->r_u32(sect, "inv_grid_width") * gw, pSettings->r_u32(sect, "inv_grid_height") * gh);
		lr.rb.add(lr.lt);
		s->GetStaticItem()->SetOriginalRect(lr);
		s->SetStretchTexture(true);
		s->SetWidth ((lr.rb.x - lr.lt.x) * sx);
		s->SetHeight((lr.rb.y - lr.lt.y) * sy);
		s->SetWndPos(Fvector2().set(off.x * sx, off.y * sy));
		s->SetTextureColor(col);
		i->wnd()->AttachChild(s);
	};

	// stock scope/silencer/GL overlays (their inv_grid icon at scope_x/y etc.)
	if (GetIcon(eSilencer))	lay_flat(*object()->GetSilencerName(),        m_addon_offset[eSilencer]);
	if (GetIcon(eScope))	lay_flat(*object()->GetAttachedScopeName(),   m_addon_offset[eScope]);
	if (GetIcon(eLauncher))	lay_flat(*object()->GetGrenadeLauncherName(), m_addon_offset[eLauncher]);

	// GS composed layers (body / mag / upgrade / uniq) -- the whole silhouette for a layered weapon,
	// since its own inv_grid slot is deliberately empty.
	xr_vector<CUIStatic*> tmp;
	GWR_AttachIconLayers(i->wnd(), object(), sx, sy, tmp, col);
	return				i;
}

bool CUIWeaponCellItem::EqualTo(CUICellItem* itm)
{
	if(!inherited::EqualTo(itm))	return false;

	CUIWeaponCellItem* ci			= smart_cast<CUIWeaponCellItem*>(itm);
	if(!ci)							return false;

//	bool b_addons					= ( (object()->GetAddonsState() == ci->object()->GetAddonsState()) );
	if ( object()->GetAddonsState() != ci->object()->GetAddonsState() )
	{
		return false;
	}
//	bool b_place					= ( (object()->m_eItemCurrPlace == ci->object()->m_eItemCurrPlace) );

	return true;
}

CBuyItemCustomDrawCell::CBuyItemCustomDrawCell	(LPCSTR str, CGameFont* pFont)
{
	m_pFont		= pFont;
	VERIFY		(xr_strlen(str)<16);
	xr_strcpy		(m_string,str);
}

void CBuyItemCustomDrawCell::OnDraw(CUICellItem* cell)
{
	Fvector2							pos;
	cell->GetAbsolutePos				(pos);
	UI()->ClientToScreenScaled			(pos, pos.x, pos.y);
	m_pFont->Out						(pos.x, pos.y, m_string);
	m_pFont->OnRender					();
}
