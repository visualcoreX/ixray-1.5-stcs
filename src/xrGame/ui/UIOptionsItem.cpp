#include "StdAfx.h"
#include "UIOptionsItem.h"
#include "UIOptionsManager.h"
#include "../../xrEngine/xr_ioconsole.h"

CUIOptionsManager CUIOptionsItem::m_optionsManager;
bool CUIOptionsItem::s_allow_restart_req = false;

// MSAA is fixed when the renderer is CREATED (R3 CRender::create: o.dx10_msaa, the sample count, the shader
// variants compiled for them); the vid_restart that depend="vid" orders only rebuilds the render targets with
// those. So the two anti-aliasing entries also ask for a GAME restart -- but only when the MSAA setup they
// leave behind differs from the one the game started with: Off / FXAA / SMAA are a per-frame filter and keep
// switching on the spot, without the warning.
static xr_string	s_msaa_at_launch;
static bool			s_msaa_at_launch_known = false;

static bool is_msaa_entry(const shared_str& entry)
{
	return !xr_strcmp(entry.c_str(), "r_aa_mode") || !xr_strcmp(entry.c_str(), "r3_msaa");
}

static xr_string msaa_setup_now()
{
	LPCSTR mode = Console->GetToken("r_aa_mode");
	if (!mode || xr_strcmp(mode, "ui_mm_aa_msaa"))	return "off";
	LPCSTR samples = Console->GetToken("r3_msaa");
	return xr_string("msaa ") + (samples ? samples : "?");
}

CUIOptionsItem::CUIOptionsItem()
{
	m_dep = sdNothing;
}

CUIOptionsItem::~CUIOptionsItem()
{
	m_optionsManager.UnRegisterItem(this);
}

void CUIOptionsItem::AssignProps(const shared_str& entry, const shared_str& group)
{
	m_optionsManager.RegisterItem	(this, group);
	m_entry							= entry;

	// the first time the options screen is built, the values are still the ones the renderer was created with
	if (!s_msaa_at_launch_known && is_msaa_entry(m_entry))
	{
		s_msaa_at_launch		= msaa_setup_now();
		s_msaa_at_launch_known	= true;
	}
}

void CUIOptionsItem::SendMessage2Group(LPCSTR group, LPCSTR message)
{
	m_optionsManager.SendMessage2Group(group,message);
}

void CUIOptionsItem::OnMessage(LPCSTR message)
{
	// do nothing
}

LPCSTR CUIOptionsItem::GetOptStringValue()
{
	return Console->GetString(m_entry.c_str());
}

void CUIOptionsItem::SaveOptStringValue(LPCSTR val)
{
	xr_string command	= m_entry.c_str();
	command				+= " ";
	command				+= val;
	Console->Execute	(command.c_str());
}

void CUIOptionsItem::GetOptIntegerValue(int& val, int& min, int& max)
{
	val = Console->GetInteger(m_entry.c_str(), min, max);
}

void CUIOptionsItem::SaveOptIntegerValue(int val)
{
	string512			command;
	xr_sprintf			(command, "%s %d", m_entry.c_str(), val);
	Console->Execute	(command);
}

void CUIOptionsItem::GetOptFloatValue(float& val, float& min, float& max)
{
	val = Console->GetFloat(m_entry.c_str(), min, max);
}

void CUIOptionsItem::SaveOptFloatValue(float val)
{
	string512			command;
	xr_sprintf				(command, "%s %f", m_entry.c_str(), val);
	Console->Execute	(command);
}

bool CUIOptionsItem::GetOptBoolValue()
{
	return Console->GetBool( m_entry.c_str() );
}

void CUIOptionsItem::SaveOptBoolValue(bool val)
{
	string512		command;
	xr_sprintf		(command, "%s %s", m_entry.c_str(), (val)?"1":"0");
	Console->Execute(command);
}

LPCSTR CUIOptionsItem::GetOptTokenValue()
{
	return Console->GetToken(m_entry.c_str());
}

xr_token* CUIOptionsItem::GetOptToken()
{
	return Console->GetXRToken(m_entry.c_str());
}

void CUIOptionsItem::SaveOptTokenValue(LPCSTR val)
{
	SaveOptStringValue(val);
}

void CUIOptionsItem::SaveValue()
{
	// Not an explicit save (see CUIOptionsItem::BeginSave) -- this is the widget writing its value
	// back while it is being filled in or restored, which must not order a renderer restart.
	if(!s_allow_restart_req)	return;

	// MSAA switched on/off or to another sample count: that needs the game restarted (see msaa_setup_now)
	if (is_msaa_entry(m_entry) && s_msaa_at_launch_known && msaa_setup_now() != s_msaa_at_launch)
	{
		Msg("~ options: [%s] changed the MSAA setup (%s -> %s) -> system restart", m_entry.c_str(),
			s_msaa_at_launch.c_str(), msaa_setup_now().c_str());
		m_optionsManager.DoSystemRestart();
	}

	// DIAGNOSTIC (cheap, once per changed item on Apply): which option is the one that costs a
	// four-second vid_restart. Only items the UI reported as CHANGED ever get here.
	if(m_dep==sdVidRestart || m_dep==sdSndRestart || m_dep==sdSystemRestart)
		Msg("~ options: [%s] changed -> %s restart", m_entry.c_str(),
			(m_dep==sdVidRestart) ? "vid" : ((m_dep==sdSndRestart) ? "snd" : "system"));

	if(m_dep==sdVidRestart)
		m_optionsManager.DoVidRestart();
	else
	if(m_dep==sdSndRestart)
		m_optionsManager.DoSndRestart();
	else
	if(m_dep==sdSystemRestart)
		m_optionsManager.DoSystemRestart();
/*
	if (	m_entry == "vid_mode"		||	+
			m_entry == "_preset"		||	+
			m_entry == "rs_fullscreen"	||	+
			m_entry == "r__supersample"	||	+
			m_entry == "rs_refresh_60hz"||	+
			m_entry == "rs_no_v_sync"	||	+
			m_entry == "texture_lod")		+
	m_optionsManager.DoVidRestart();

	if (m_entry == "snd_efx")				+
		m_optionsManager.DoSndRestart();
*/
}
