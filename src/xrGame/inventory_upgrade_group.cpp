////////////////////////////////////////////////////////////////////////////
//	Module 		: inventory_upgrade_group.cpp
//	Created 	: 22.10.2007
//  Modified 	: 27.11.2007
//	Author		: Evgeniy Sokolov
//	Description : inventory upgrade group class implementation
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "inventory_upgrade_group.h"
#include "inventory_upgrade.h"		// Upgrade (parent_group_id) for the require_all_parent_groups gate

namespace inventory
{
namespace upgrade
{

Group::Group()
{
	m_require_all_parent_groups = false;
}

Group::~Group()
{
}

void Group::construct( const shared_str& group_id, UpgradeBase& parent_upgrade, Manager& manager_r )
{
	m_id._set( group_id );
	add_parent_upgrade( parent_upgrade );

	VERIFY2( pSettings->section_exist( m_id ),
		make_string( "Upgrade <%s> : group section [%s] does not exist!" , parent_upgrade.id_str(), m_id.c_str() ) );
	
	// DEFAULT since 2026-08-03 (user: a node fed by two branches must wait for BOTH, "и так для всех
	// апгрейдов"). Still per-GROUP, never per-parent: mutually-exclusive siblings inside one group
	// would deadlock under a literal "all parents" rule. A tree can opt out with = false.
	m_require_all_parent_groups = !!READ_IF_EXISTS( pSettings, r_bool, m_id, "require_all_parent_groups", true );

	LPCSTR	upgrades_str = pSettings->r_string(m_id, "elements");
	VERIFY2( upgrades_str, make_string( "in upgrade group <%s> elements are empty!", m_id.c_str() ) );

	PSTR	temp  = (PSTR)_alloca( (xr_strlen(upgrades_str) + 1) * sizeof(char) );
	for ( int n = _GetItemCount(upgrades_str), i = 0; i < n; ++i )
	{
		UpgradeBase* upgrade_p = (UpgradeBase*)manager_r.add_upgrade( _GetItem( upgrades_str, i, temp ), *this );
		m_included_upgrades.push_back( upgrade_p );
	}
}

void Group::add_parent_upgrade( UpgradeBase& parent_upgrade )
{
	if ( std::find( m_parent_upgrades.begin(), m_parent_upgrades.end(), &parent_upgrade ) == m_parent_upgrades.end() )
	{
		m_parent_upgrades.push_back( &parent_upgrade );
	}
}

#ifdef DEBUG

void Group::log_hierarchy( LPCSTR nest )
{
	u32 sz = (xr_strlen(nest) + 4) * sizeof(char);
	PSTR	nest2 = (PSTR)_alloca( sz );
	xr_strcpy( nest2, sz, nest );
	strcat( nest2, "   " );
	Msg( "%s(g) %s", nest2, m_id.c_str() );

	Upgrades_type::iterator ib = m_included_upgrades.begin();
	Upgrades_type::iterator ie = m_included_upgrades.end();
	for ( ; ib != ie ; ++ib )
	{
		(*ib)->log_hierarchy( nest2 );
	}
}

#endif // DEBUG

void Group::fill_root( Root* root )
{
	Upgrades_type::iterator ib = m_included_upgrades.begin();
	Upgrades_type::iterator ie = m_included_upgrades.end();
	for ( ; ib != ie ; ++ib )
	{
		(*ib)->fill_root_container( root );
	}
}

UpgradeStateResult Group::can_install( CInventoryItem& item, UpgradeBase& test_upgrade, bool loading )
{
	// Parent gate. DEFAULT (since 2026-08-03): one installed non-root parent per DISTINCT parent GROUP --
	// a node fed by two branches waits for both. `require_all_parent_groups = false` restores the old
	// "any parent is enough".
	// Stock xray required ALL parents, but GS/CS trees are DAGs where several mutually-exclusive sibling
	// upgrades each point their `effects` at the SAME child group -- e.g. usm_accuracy/usm_rpm/usm_rpm_down
	// all unlock vartree_ak74_body, and mag45_brown/mag45_black both unlock vartree_ak74_mag60. Under the
	// old "all parents" rule those merges deadlock (the siblings can't coexist), so use "any parent". For a
	// normal single-parent group this is identical to the old behaviour.
	// OPT-IN require_all_parent_groups: require one installed non-root parent per DISTINCT parent GROUP,
	// so every contributing branch must have a pick (colt1911 systems: barrel AND usm AND zatvor).
	bool parents_ok;
	Upgrades_type::iterator ib = m_parent_upgrades.begin();
	Upgrades_type::iterator ie = m_parent_upgrades.end();
	if ( m_require_all_parent_groups )
	{
		xr_vector<shared_str>	grp_ids;
		xr_vector<bool>			grp_has;
		for ( ; ib != ie ; ++ib )
		{
			if ( (*ib)->is_root() )	continue;
			shared_str gid = static_cast<Upgrade*>( *ib )->parent_group_id();
			int idx = -1;
			for ( u32 k = 0; k < grp_ids.size(); ++k )
				if ( grp_ids[k] == gid ) { idx = (int)k; break; }
			if ( idx < 0 ) { grp_ids.push_back( gid ); grp_has.push_back( false ); idx = (int)grp_ids.size() - 1; }
			if ( item.has_upgrade( (*ib)->id() ) )	grp_has[idx] = true;
		}
		parents_ok = true;
		for ( u32 k = 0; k < grp_has.size(); ++k )
			if ( !grp_has[k] ) { parents_ok = false; break; }
	}
	else
	{
		bool any_non_root_parent = false;
		bool a_parent_installed  = false;
		for ( ; ib != ie ; ++ib )
		{
			if ( (*ib)->is_root() )	continue;
			any_non_root_parent = true;
			if ( item.has_upgrade( (*ib)->id() ) ) { a_parent_installed = true; break; }
		}
		parents_ok = !any_non_root_parent || a_parent_installed;
	}
	if ( !parents_ok )
	{
		if ( loading )
		{
			FATAL( make_string( "Loading item: Upgrade <%s> of inventory item [%s] (id = %d) can`t be installed! Error = result_e_parents",
				test_upgrade.id_str(), item.m_section_id.c_str(), item.object_id() ).c_str() );
		}
		return result_e_parents;
	}
	
	ib = m_included_upgrades.begin();
	ie = m_included_upgrades.end();
	for ( ; ib != ie ; ++ib )
	{
		if ( (*ib) == &test_upgrade )
		{
			continue;
		}
		if ( item.has_upgrade( (*ib)->id() ) )
		{
			if ( loading )
			{
				FATAL( make_string( "Loading item: Upgrade <%s> of inventory item [%s] (id = %d) can`t be installed! Error = result_e_group",
					test_upgrade.id_str(), item.m_section_id.c_str(), item.object_id() ).c_str() );
			}
			return result_e_group;
		}
	}

	return result_ok;
}

void Group::highlight_up()
{
	Upgrades_type::iterator ib = m_included_upgrades.begin();
	Upgrades_type::iterator ie = m_included_upgrades.end();
	for ( ; ib != ie ; ++ib )
	{
		(*ib)->highlight_up();
	}
}

void Group::highlight_down()
{
	Upgrades_type::iterator ib = m_parent_upgrades.begin();
	Upgrades_type::iterator ie = m_parent_upgrades.end();
	for ( ; ib != ie ; ++ib )
	{
		(*ib)->highlight_down();
	}
}

} // namespace upgrade
} // namespace inventory
