////////////////////////////////////////////////////////////////////////////
//	Module 		: UIInvUpgrade.cpp
//	Created 	: 08.11.2007
//  Modified 	: 27.11.2007
//	Author		: Evgeniy Sokolov
//	Description : inventory upgrade UI class implementation
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "object_broker.h"
#include "../string_table.h"

#include "UIInvUpgrade.h"

#include "xrUIXmlParser.h"
#include "UIXmlInit.h"

#include "ai_space.h"
#include "alife_simulator.h"
#include "inventory_upgrade_manager.h"
#include "inventory_upgrade.h"

#include "UIInventoryUpgradeWnd.h"

UIUpgrade::UIUpgrade( CUIInventoryUpgradeWnd* parent_wnd )
{
	VERIFY( parent_wnd );
	m_parent_wnd = parent_wnd;

	m_item   = xr_new<CUIStatic>();	m_item->SetAutoDelete(   true );	AttachChild( m_item   );
	m_color  = xr_new<CUIStatic>();	m_color->SetAutoDelete(  true );	AttachChild( m_color  );
	m_border = xr_new<CUIStatic>();	m_border->SetAutoDelete( true );	AttachChild( m_border );
	m_ink    = xr_new<CUIStatic>();	m_ink->SetAutoDelete(    true );	AttachChild( m_ink    );
	m_lamp   = xr_new<CUIStatic>();	m_lamp->SetAutoDelete(   true );	AttachChild( m_lamp   );
	m_lamp_mode = false;
	m_lamp_shown = NULL;

	m_upgrade_id = NULL;
	Reset();
}

UIUpgrade::~UIUpgrade()
{
}

void UIUpgrade::init_upgrade( LPCSTR upgrade_id, CInventoryItem& item )
{
	VERIFY( upgrade_id && xr_strcmp( upgrade_id, "" ) );
	m_upgrade_id = upgrade_id;
//-	m_inv_item = &item;
	
	m_prev_state = STATE_COUNT; // no defined
	update_item( &item );
}

UIUpgrade::Upgrade_type* UIUpgrade::get_upgrade()
{
	Upgrade_type* res = ai().alife().inventory_upgrade_manager().get_upgrade( m_upgrade_id );
	VERIFY( res );
	return res;
}

/*UIUpgrade::Property_type* UIUpgrade::get_property()
{
	Property_type* res = ai().alife().inventory_upgrade_manager().get_property(
		get_upgrade()->get_property_name() );
	VERIFY( res );
	return res;
}*/

void UIUpgrade::Reset()
{
	offset.set( 0.0f, 0.0f );
	
	m_prev_state = STATE_UNKNOWN;
	m_state      = STATE_ENABLED;
	m_button_state = BUTTON_FREE;
	m_state_lock   = false;

	m_ink->Show( false );
	m_color->Show( false );
	m_lamp->Show( false );
	m_lamp_shown = NULL;
		
	inherited::Reset();
}

// -----------------------------------------------------------------------------------

void UIUpgrade::load_from_xml( CUIXml& ui_xml, int i_column, int i_cell, Frect const& t_cell_border, Frect const& t_cell_item )
{
	m_scheme_index.x = i_column;
	m_scheme_index.y = i_cell; // row

	CUIXmlInit::InitWindow( ui_xml, "cell", i_cell, this );

	Fvector2 f2;
	
	f2.set( t_cell_item.x1, t_cell_item.y1 );
	m_item->SetWndPos( f2 );
	m_color->SetWndPos( f2 );
	f2.set( t_cell_item.width(), t_cell_item.height() );
	m_item->SetWndSize( f2 );
	m_color->SetWndSize( f2 );
	SetWndSize( f2 );

	f2.set( t_cell_border.x1, t_cell_border.y1 );
	m_border->SetWndPos( f2 );
	m_ink->SetWndPos( f2 );
	f2.set( t_cell_border.width(), t_cell_border.height() );
	m_border->SetWndSize( f2 );
	m_ink->SetWndSize( f2 );

	m_item->SetStretchTexture( true );
	m_color->SetStretchTexture( true );
	m_border->SetStretchTexture( true );
	m_ink->SetStretchTexture( true );

	// The CoP lamp: its 5x38 strip sits 3 px in from the icon's left edge of a 90x44 cell. Ours is 70x40
	// in 4:3 and 58x40 in the 16:9 layout, so the width follows the cell's own horizontal scale.
	// Vertically it keeps CoP's proportion of the cell (38 of 44, 3 from the top), not a fixed 38.
	const float k  = t_cell_item.width() / 70.0f;
	const float kh = t_cell_item.height() / 44.0f;
	f2.set( t_cell_item.x1 + 3.0f * k, t_cell_item.y1 + 3.0f * kh );
	m_lamp->SetWndPos( f2 );
	f2.set( 5.0f * k, 38.0f * kh );
	m_lamp->SetWndSize( f2 );
	m_lamp->SetStretchTexture( true );
}

void UIUpgrade::init_property( Fvector2 const& pos, Fvector2 const& size )
{
//	m_prop->SetWndPos( pos );
//	m_prop->SetWndSize( size );
}

void UIUpgrade::set_texture( Layer layer, LPCSTR texture )
{
	switch( layer )
	{
	case LAYER_ITEM:   VERIFY( texture ); m_item->InitTexture( texture ); break;
	case LAYER_COLOR:
		{
			if ( m_lamp_mode )				// the lamp carries the state, the icon stays clean
			{
				m_color->Show( false );
				break;
			}
			if ( texture )
			{
				m_color->InitTexture( texture );
				m_color->Show( true );
			}
			else
			{
				m_color->Show( false );
			}
			break;
		}
	case LAYER_BORDER: VERIFY( texture ); m_border->InitTexture( texture ); break;
	case LAYER_INK:    VERIFY( texture ); m_ink->InitTexture(    texture ); break;
	default: 
		NODEFAULT;
	}
}

void UIUpgrade::Draw()
{
	if ( get_upgrade() )
	{
		inherited::Draw();
	}
}

void UIUpgrade::Update()
{
	inherited::Update();
	
	update_upgrade_state();
	
	if ( m_prev_state != m_state )
	{
		update_mask();
	}
	
	// the upgrade under the cursor tells the window, so its (not installed) group mates can light up red --
	// the alternatives to it. Not for an installed one: there is nothing it would change.
	if ( m_lamp_mode && m_bCursorOverWindow && m_state != STATE_SELECTED )
		m_parent_wnd->set_lamp_hover( get_upgrade() );

	if ( m_lamp_mode )
		refresh_lamp();

	// Clear Sky's branch lamps on the cell frame. Weapons show the Call of Pripyat lamp beside the icon
	// instead (set_lamp_mode), so the branch lighting stays off for them; outfits keep it.
	m_ink->Show( !m_lamp_mode && get_upgrade()->get_highlight() );
}

void UIUpgrade::update_upgrade_state()
{
	if ( m_bCursorOverWindow )
	{
		on_over_window();
	}
	else
	{
		m_button_state = BUTTON_FREE;
	}

	if ( m_state_lock )
	{
		return;
	}

	switch ( m_button_state )
	{
	case BUTTON_FREE:
		m_state = STATE_ENABLED;
		break;
	case BUTTON_FOCUSED:
		m_state = STATE_FOCUSED;
		break;
	case BUTTON_PRESSED:
	case BUTTON_DPRESSED:
		m_state = STATE_TOUCHED;
		break;
	}
}

void UIUpgrade::update_mask()
{
	if ( m_state < STATE_ENABLED || STATE_COUNT <= m_state )
	{
		R_ASSERT2( 0, "Unknown state UIUpgrade!" );
	}

	set_texture( LAYER_COLOR, m_parent_wnd->get_cell_texture( m_state ) );

	refresh_lamp();

	m_prev_state = m_state;
}

void UIUpgrade::set_lamp_mode( bool on )
{
	// The scheme cells are shared by every item that uses the scheme, so this is set afresh each time
	// the window takes an item; update_mask/update_item then draw whichever look it says.
	m_lamp_mode = on;
	if ( !on )
		m_lamp->Show( false );
	else
		m_ink->Show( false );
	m_lamp_shown = NULL;
}

// The lamp for the current state -- and, while the cursor is on an upgrade that cannot be taken right
// now (the one below it not installed, no money, a quest condition), the red one: the player is asking
// about it and the answer is no. Those states have no lamp of their own, so without the cursor they
// stay dark like in CoP. Re-evaluated every frame (the hover is not a state change); the texture is
// only re-bound when it actually changes.
void UIUpgrade::refresh_lamp()
{
	LPCSTR lamp = m_lamp_mode ? m_parent_wnd->get_lamp_texture( m_state ) : NULL;
	// The cell under the cursor answers for itself: yellow if it can be taken, red if not. Nothing else
	// lights up for the branch -- the only other cells that react are the ones installing it would block
	// (below). Installed (green) and blocked-by-group/unknown (red) keep the lamp they already have.
	const bool asked = ( m_button_state != BUTTON_FREE );
	if ( m_lamp_mode && !lamp && asked )
	{
		if ( m_state == STATE_ENABLED )
			lamp = m_parent_wnd->get_lamp_texture( STATE_FOCUSED );
		else if ( m_state == STATE_DISABLED_PARENT || m_state == STATE_DISABLED_PREC_MONEY || m_state == STATE_DISABLED_PREC_QUEST )
			lamp = m_parent_wnd->get_lamp_texture( STATE_DISABLED_GROUP );
	}
	// ...and what installing the hovered upgrade would shut out: the other upgrades of its GROUP (one pick
	// per group -- Group::can_install answers result_e_group for the rest). Red over whatever they showed,
	// unless already installed. Deliberately not cascaded to their children: sibling branches often
	// unlock the same next group, so those stay reachable.
	// An INSTALLED mate always keeps its green -- even when the hovered one is blocked because of it.
	if ( m_lamp_mode && m_state != STATE_SELECTED )
	{
		Upgrade_type* hov = m_parent_wnd->lamp_hover_upgrade();
		if ( hov && hov != get_upgrade() && hov->parent_group() == get_upgrade()->parent_group() )
			lamp = m_parent_wnd->get_lamp_texture( STATE_DISABLED_GROUP );
	}
	if ( lamp == m_lamp_shown )
		return;
	m_lamp_shown = lamp;
	if ( lamp )
	{
		m_lamp->InitTexture( lamp );
		m_lamp->Show( true );
	}
	else
		m_lamp->Show( false );
}

bool UIUpgrade::OnMouseAction( float x, float y, EUIMessages mouse_action )
{
//	m_bButtonClicked = false;
	if( CUIWindow::OnMouseAction( x, y, mouse_action ) )
	{
		return true;
	}

	if ( ( mouse_action == WINDOW_LBUTTON_DOWN || mouse_action == WINDOW_LBUTTON_UP ||
		   mouse_action == WINDOW_RBUTTON_DOWN || mouse_action == WINDOW_RBUTTON_UP ) &&
		   HasChildMouseHandler() )
	{
		return false;
	}

	if ( m_bCursorOverWindow )
	{
		//if( Device.dwTimeGlobal > ( m_dwFocusReceiveTime + 500 ) )
		highlight_relation( true );
		if ( mouse_action == WINDOW_LBUTTON_DOWN )
		{
			OnClick();
			return true;
		}
		if ( mouse_action == WINDOW_LBUTTON_DB_CLICK )
		{
			OnDbClick();
			return true;
		}
		if ( mouse_action == WINDOW_RBUTTON_DOWN )
		{
			OnRClick();
			return true;
		}
	}// m_bCursorOverWindow

	if ( mouse_action == WINDOW_LBUTTON_UP || mouse_action == WINDOW_RBUTTON_UP )
	{
		m_button_state = BUTTON_FREE;
		//highlight_relation( false );
		return true;
	}

	return false;
}

void UIUpgrade::OnFocusReceive()
{
	inherited::OnFocusReceive();
	update_mask();
	//info
	//	OnFocusFirst();

	highlight_relation( true );
	//m_parent_wnd->set_info_cur_upgrade( get_upgrade() );
}

void UIUpgrade::OnFocusLost()
{
	inherited::OnFocusLost();
	highlight_relation( false );

	m_parent_wnd->set_info_cur_upgrade( NULL );
	m_button_state = BUTTON_FREE;

	/*if ( m_button_state == BUTTON_DPRESSED )
	{
		return;
	}*/
}

void UIUpgrade::OnClick()
{
	if ( m_state == STATE_ENABLED || m_state == STATE_FOCUSED || m_state == STATE_TOUCHED )
	{
		m_parent_wnd->AskUsing( make_string( "%s %s", CStringTable().translate( "st_upgrade_install" ).c_str(),
			get_upgrade()->name() ).c_str(), get_upgrade()->id_str() );
	}
	m_parent_wnd->set_info_cur_upgrade( NULL );
	//m_parent_wnd->set_info_cur_upgrade( get_upgrade() );
	highlight_relation( true );

	m_button_state = BUTTON_PRESSED;
}

bool UIUpgrade::OnDbClick()
{
	m_parent_wnd->set_info_cur_upgrade( NULL );
	m_button_state = BUTTON_DPRESSED;
	return true;
}

void UIUpgrade::OnRClick()
{
	//m_parent_wnd->set_info_cur_upgrade( get_upgrade() );
	m_parent_wnd->set_info_cur_upgrade( NULL );
	highlight_relation( true );
	m_button_state = BUTTON_PRESSED;
}
	
void UIUpgrade::on_over_window()
{
	if ( m_button_state == BUTTON_PRESSED )
	{
		return;
	}

	m_button_state = BUTTON_FOCUSED;
	m_parent_wnd->set_info_cur_upgrade( get_upgrade() );
}

void UIUpgrade::highlight_relation( bool enable )
{
	if ( enable )
	{
		m_parent_wnd->HighlightHierarchy( get_upgrade()->id() );
		return;
	}
	m_parent_wnd->ResetHighlight();
}

void UIUpgrade::update_item( CInventoryItem* inv_item )
{
	if ( !inv_item )
	{
		return;
	}
	VERIFY( get_upgrade() );
	VERIFY( inv_item->m_section_id.size() );

//	m_state = STATE_ENABLED;
//	m_state_lock = false;

	inventory::upgrade::UpgradeStateResult res = get_upgrade()->can_install( *inv_item, false );
	m_state_lock = true;
	
	switch( res )
	{
	case inventory::upgrade::result_ok:
		m_state = STATE_ENABLED;
		m_state_lock = false;
		break;
	case inventory::upgrade::result_e_unknown:
		m_state = STATE_UNKNOWN;
		break;
	case inventory::upgrade::result_e_installed: // has_upgrade
		m_state = STATE_SELECTED;
		break;
	case inventory::upgrade::result_e_parents:
		m_state = STATE_DISABLED_PARENT;
		break;
	case inventory::upgrade::result_e_group:
		m_state = STATE_DISABLED_GROUP;
		break;
	case inventory::upgrade::result_e_precondition_money:
		m_state = STATE_DISABLED_PREC_MONEY;
		break;
	case inventory::upgrade::result_e_precondition_quest:
		m_state = STATE_DISABLED_PREC_QUEST;
		break;

	default:
		NODEFAULT;
		break;
	}

	// CoP greys out the icon of an upgrade that cannot be taken yet (unknown, the branch below it not
	// installed, no money, no quest) and keeps it bright when it is available, installed or merely
	// blocked by its group (that one gets the red lamp instead). Clear Sky never tinted the icon, so the
	// colour is reset for everything else -- the cells are shared with the outfits.
	const bool dim = m_lamp_mode &&
		(  res == inventory::upgrade::result_e_unknown
		|| res == inventory::upgrade::result_e_parents
		|| res == inventory::upgrade::result_e_precondition_money
		|| res == inventory::upgrade::result_e_precondition_quest );
	m_item->SetTextureColor( dim ? color_rgba( 100, 100, 100, 255 ) : color_rgba( 255, 255, 255, 255 ) );
}
