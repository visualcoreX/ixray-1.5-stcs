// UICheckButton.cpp: ����� ������, ������� 2 ���������:
// � �������� � ���
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include ".\uicheckbutton.h"
#include "../HUDManager.h"
#include "UILines.h"
#include "UIXmlInit.h"
#include "UIHint.h"

CUICheckButton::CUICheckButton(void)
{	
	SetTextAlignment(CGameFont::alLeft);
	m_bCheckMode = true;
	m_hint_owner = NULL;
}

CUICheckButton::~CUICheckButton(void)
{
}

void CUICheckButton::SetDependControl(CUIWindow* pWnd){
	m_depend_controls.clear();
	AddDependControl(pWnd);
}

void CUICheckButton::AddDependControl(CUIWindow* pWnd){
	if (pWnd)
		m_depend_controls.push_back(pWnd);
}

void CUICheckButton::AddExclusiveControl(CUICheckButton* pBtn){
	if (pBtn && pBtn != this)
		m_exclusive_controls.push_back(pBtn);
}

void CUICheckButton::Update(){
	CUI3tButton::Update();
	if ( m_hint_owner ) m_hint_owner->Update();

	if (!m_depend_controls.empty())
	{
		// GetCheck() is the PENDING value, not the saved one, so the dependants grey out the moment
		// the master is clicked and come back if the player cancels out of the options.
		const bool on = GetCheck();
		xr_vector<CUIWindow*>::iterator it = m_depend_controls.begin();
		for (; it != m_depend_controls.end(); ++it)
			(*it)->Enable(on);
	}
}


void CUICheckButton::SetCurrentValue(){
	SetCheck(GetOptBoolValue());
}

void CUICheckButton::SaveValue(){
	CUIOptionsItem::SaveValue();
	SaveOptBoolValue(GetCheck());
}

bool CUICheckButton::IsChanged(){
	return b_backup_val != GetCheck();
}

void CUICheckButton::InitCheckButton(Fvector2 pos, Fvector2 size, LPCSTR texture_name)
{
	InitButton		(pos, size);
	InitTexture2	(texture_name);
	m_pLines->SetWndPos(pos);
	m_pLines->SetWndSize(Fvector2().set(size.x,m_background->GetE()->GetStaticItem()->GetRect().height()));
}

void CUICheckButton::InitTexture2(LPCSTR texture_name)
{
	CUI3tButton::InitTexture(texture_name); // "ui_checker"
	Frect r = m_background->GetE()->GetStaticItem()->GetOriginalRect();
	CUI3tButton::SetTextX( GetTextX() + r.width() );	
}

void CUICheckButton::SeveBackUpValue()
{
	b_backup_val = GetCheck();
}

void CUICheckButton::Undo()
{
	SetCheck		(b_backup_val);
	SaveValue		();
}

void CUICheckButton::init_hint_wnd_xml( CUIXml& xml, LPCSTR path )
{
	XML_NODE* stored_root = xml.GetLocalRoot();
	XML_NODE* new_root    = xml.NavigateToNode( path, 0 );
	xml.SetLocalRoot( new_root );

	if ( xml.NavigateToNode( "hint_text", 0 ) )
	{
		m_hint_owner = xr_new<UIHintWindow>();
		m_hint_owner->SetAutoDelete( true );
		AttachChild( m_hint_owner );
		CUIXmlInit::InitHintWindow( xml, "hint_text", 0, m_hint_owner );
		m_hint_owner->SetWndPos( Fvector2().set( 0.0f, 0.0f ) );
		m_hint_owner->SetWndSize( GetWndSize() );
	}
	xml.SetLocalRoot( stored_root );
}

void CUICheckButton::set_hint_wnd( UIHint* hint_wnd )
{
	VERIFY( m_hint_owner );
	if ( m_hint_owner )
	{
		m_hint_owner->set_hint_wnd( hint_wnd );
	}
}

void CUICheckButton::OnFocusLost()
{
	inherited::OnFocusLost();
	if ( m_hint_owner ) m_hint_owner->OnFocusLost();
}

void CUICheckButton::OnFocusReceive()
{
	inherited::OnFocusReceive();
	if ( m_hint_owner ) m_hint_owner->OnFocusReceive();
}

void CUICheckButton::Show( bool status )
{
	inherited::Show( status );
	if ( m_hint_owner ) m_hint_owner->Show( status );
}

bool CUICheckButton::OnMouseDown( int mouse_btn )
{
	if ( m_hint_owner ) m_hint_owner->disable_hint();
	const bool res = inherited::OnMouseDown( mouse_btn );

	// Handled on the CLICK, not in Update(): the two boxes name each other, and a per-frame rule
	// would have them taking turns clearing one another instead of settling.
	if ( GetCheck() )
	{
		xr_vector<CUICheckButton*>::iterator it = m_exclusive_controls.begin();
		for ( ; it != m_exclusive_controls.end(); ++it )
			(*it)->SetCheckKeepBackup( false );
	}
	return res;
}
