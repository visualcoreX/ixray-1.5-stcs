////////////////////////////////////////////////////////////////////////////
//	Module 		: UIInventoryUpgradeWnd.h
//	Created 	: 06.10.2007
//	Author		: Evgeniy Sokolov
//	Description : inventory upgrade UI window class
////////////////////////////////////////////////////////////////////////////

#ifndef UI_INVENTORY_UPGRADE_WND_H_INCLUDED
#define UI_INVENTORY_UPGRADE_WND_H_INCLUDED

#include "UIStatic.h"
#include "UIInvUpgrade.h"

extern const LPCSTR g_inventory_upgrade_xml;
#define		MAX_UI_UPGRADE_CELLS		25

namespace inventory { namespace upgrade {
	class Manager;
	class Upgrade;
	class Property;
} } // namespace upgrade, inventory

class UIUpgrade;
class CInventoryItem;
class CUIItemInfo;
class CUIFrameLineWnd;
class CUI3tButtonEx;

class CUIInventoryUpgradeWnd : public CUIWindow
{
private:
	typedef CUIWindow	inherited;
	
	typedef inventory::upgrade::Manager		Manager_type;
	typedef inventory::upgrade::Upgrade 	Upgrade_type;
	typedef inventory::upgrade::Property 	Property_type;
	typedef xr_vector<UIUpgrade*>			UI_Upgrades_type;

private: // sub-classes
	struct Scheme
	{
		shared_str			name;
		UI_Upgrades_type	cells;

						Scheme();
		virtual			~Scheme();
	};
	typedef xr_vector<Scheme*>	SCHEMES;

public: // func
							CUIInventoryUpgradeWnd();
	virtual					~CUIInventoryUpgradeWnd();

	virtual void			Init();
			void			InitInventory( CInventoryItem* item, bool can_upgrade );

	IC CInventoryItem const*	get_inventory() const { return m_inv_item; }
	IC LPCSTR				get_cell_texture( UIUpgrade::ViewState state ) const { return m_cell_textures[state].c_str(); }
	// the CoP lamp for a state (<lamp_texture> in <cell_states>), NULL = no lamp in that state
	IC LPCSTR				get_lamp_texture( UIUpgrade::ViewState state ) const { return m_lamp_textures[state].size() ? m_lamp_textures[state].c_str() : NULL; }
//	IC u32					get_cell_color(   UIUpgrade::ViewState state ) const { return m_cell_colors[state]; }
//-	   u32					get_cell_color2(  UIUpgrade::ViewState state ) const;

	virtual void			Show( bool status );
	virtual void			Update();
	virtual void			Reset();
			void			UpdateAllUpgrades();

			bool			DBClickOnUIUpgrade( Upgrade_type const* upgr );
			void			AskUsing( LPCSTR text, LPCSTR upgrade_name );
			void			OnMesBoxYes();

			void			HighlightHierarchy( shared_str const& upgrade_id );
			void			ResetHighlight();
			void			set_info_cur_upgrade( Upgrade_type* upgrade );
	UIUpgrade*				FindUIUpgrade( Upgrade_type const* upgr );
	// weapon lamps: the installable upgrade under the cursor, so its group mates can show red (see refresh_lamp)
			void			set_lamp_hover( Upgrade_type* upgr );
			Upgrade_type*	lamp_hover_upgrade() const;

private:
			void			LoadCellsBacks( CUIXml& uiXml );
			void			LoadCellStates( LPCSTR state_str, LPCSTR texture_name, u32 color );
	UIUpgrade::ViewState	SelectCellState( LPCSTR state_str );
			void			SetCellState( UIUpgrade::ViewState state, LPCSTR texture_name, u32 color );
			bool			VerirfyCells();

			void			LoadSchemes( CUIXml& uiXml );
			void			SetCurScheme( const shared_str& id );
			bool			install_item( CInventoryItem& inv_item, bool can_upgrade );
			Manager_type&	get_manager();
public:
	CUI3tButtonEx*			m_btn_repair;

protected:
	CUIStatic*				m_background;
//	CUIFrameLineWnd*		m_delimiter;
	CUIItemInfo*			m_item_info;
//	Fvector2				m_info_orig_pos;
	CInventoryItem*			m_inv_item;

	shared_str				m_cell_textures[UIUpgrade::STATE_COUNT];
	shared_str				m_lamp_textures[UIUpgrade::STATE_COUNT];
//	u32						m_cell_colors[UIUpgrade::STATE_COUNT];
	shared_str				m_border_texture;
	shared_str				m_ink_texture;

	SCHEMES					m_schemes;
	Scheme*					m_current_scheme;
	LPCSTR					m_cur_upgrade_id;
	CUIWindow*				m_scheme_wnd;
	Upgrade_type*			m_lamp_hover;		// see set_lamp_hover
	u32						m_lamp_hover_frame;

}; // class CUIInventoryUpgradeWnd

#endif // UI_INVENTORY_UPGRADE_WND_H_INCLUDED
