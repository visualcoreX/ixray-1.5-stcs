#include "StdAfx.h"
#include "GameSpy_Full.h"

#include "GameSpy_Available.h"
#include "GameSpy_Patching.h"
#include "GameSpy_HTTP.h"
#include "GameSpy_Browser.h"

#include "../MainMenu.h"
#include "object_broker.h"


CGameSpy_Full::CGameSpy_Full()	
{
	m_pGSA	= NULL;
	m_pGS_Patching = NULL;
	m_pGS_HTTP = NULL;
	m_pGS_SB = NULL;

	m_hGameSpyDLL	= NULL;
	m_bServicesAlreadyChecked	= false;

	LoadGameSpy();
	//---------------------------------------
	m_pGSA = xr_new<CGameSpy_Available>(m_hGameSpyDLL);
	//-----------------------------------------------------
	// The online-services check USED TO RUN HERE, synchronously, on the main thread: CheckAvailableServices
	// spins `while (GSIAvailableCheckThink() == GSIACWaiting) msleep(5)` and inside that the GameSpy SDK
	// resolves <game>.available.gamespy.com -- a backend that has been dead since 2014. So every launch
	// blocked the whole engine on DNS queries to a host that cannot answer, waiting out the resolver's
	// timeout and retries, only to conclude what we already know. The object stays (the console patch
	// command reaches through m_pGSA's siblings); only the startup call is gone.
	// Flagged as "already checked" so Update() does not raise the ErrGSServiceFailed dialog for it.
	m_bServicesAlreadyChecked = true;
	//-----------------------------------------------------
	m_pGS_Patching = xr_new<CGameSpy_Patching>(m_hGameSpyDLL);
	m_pGS_HTTP  = xr_new<CGameSpy_HTTP>(m_hGameSpyDLL);
	m_pGS_SB = xr_new<CGameSpy_Browser>(m_hGameSpyDLL);
}

CGameSpy_Full::~CGameSpy_Full()
{
	delete_data(m_pGSA);
	delete_data(m_pGS_Patching);
	delete_data(m_pGS_HTTP);
	delete_data(m_pGS_SB);

	if (m_hGameSpyDLL)
	{
		FreeLibrary(m_hGameSpyDLL);
		m_hGameSpyDLL = NULL;
	}
}

void	CGameSpy_Full::LoadGameSpy()
{
	LPCSTR			g_name	= "xrGameSpy.dll";
	Log				("Loading DLL:",g_name);
	m_hGameSpyDLL			= LoadLibrary	(g_name);
	if (0==m_hGameSpyDLL)	R_CHK			(GetLastError());
	R_ASSERT2		(m_hGameSpyDLL,"GameSpy DLL raised exception during loading or there is no game DLL at all");

	HMODULE	hGameSpyDLL = m_hGameSpyDLL;
	GAMESPY_LOAD_FN(xrGS_GetGameVersion);
}

void	CGameSpy_Full::Update	()
{
	if (!m_bServicesAlreadyChecked)
	{
		m_bServicesAlreadyChecked = true;
		MainMenu()->SetErrorDialog(CMainMenu::ErrGSServiceFailed);
	}
	m_pGS_HTTP->Think();
	m_pGS_SB->Update();
};

const	char*	CGameSpy_Full::GetGameVersion	(const	char*result)
{
	return xrGS_GetGameVersion(result);
};