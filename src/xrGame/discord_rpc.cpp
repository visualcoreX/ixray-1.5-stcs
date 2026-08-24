#include "stdafx.h"
#include "discord_rpc.h"

#include "actor_flags.h"
#include "Level.h"
#include "Actor.h"
#include "InventoryOwner.h"
#include "character_info.h"
#include "GametaskManager.h"
#include "GameTask.h"
#include "string_table.h"

#include "discord_game_sdk.h"

#include <ctime>

namespace
{
// This mod's own Discord application -- ITS NAME is what Discord prints as the title
// ("Playing ..."), and it can only be changed in the developer portal, never from here. The art
// (stalker_icon_0 plus one stalker_patch_* per faction) has to exist as Art Assets of the same
// application. Overridable through the optional [discord_rpc] config section below.
const s64		DEFAULT_CLIENT_ID	= 1541182300086345779ll;
LPCSTR			DEFAULT_LARGE_IMAGE	= "csga_dirt_skull";	// the mod's own logo, uploaded as an Art Asset
LPCSTR			DEFAULT_LARGE_TEXT	= "Clear Sky Gunslinger Addon";
LPCSTR			DEFAULT_PATCH		= "stalker_patch_stalker";

LPCSTR			CFG_SECTION			= "discord_rpc";

// Clear Sky's six actor communities (creatures\game_relations.ltx, [actor_communities]) mapped
// onto the patch art. "actor" is the community the player carries while in no faction, and this
// mod reads it as a mercenary throughout -- ui_st_pda.xml translates it to "Наёмник" and the
// inventory logo points at the mercenary art too, so the patch follows suit.
struct community_patch { LPCSTR community; LPCSTR image; };
const community_patch COMMUNITY_TABLE[] =
{
	{ "actor",			"stalker_patch_killer"	},
	{ "actor_stalker",	"stalker_patch_stalker"	},
	{ "actor_bandit",	"stalker_patch_bandit"	},
	{ "actor_csky",		"stalker_patch_csky"	},
	{ "actor_dolg",		"stalker_patch_dolg"	},
	{ "actor_freedom",	"stalker_patch_freedom"	},
};

const u32		CONNECT_RETRY_MS	= 15000;	// Discord is not running (or not running yet)
const u32		REFRESH_MS			= 2000;		// how often the strings are rebuilt and compared

typedef enum EDiscordResult (DISCORD_API *discord_create_fn)(DiscordVersion, struct DiscordCreateParams*, struct IDiscordCore**);

void DISCORD_API activity_callback(void* /*data*/, enum EDiscordResult /*result*/)
{
	// Nothing to do: a failed presence update is not worth a log line every two seconds.
}

// The string table is stored in the code page of the localization the game runs in; Discord wants
// UTF-8. Everything the mod ships (rus/eng) is cp1251, the rest of the stock locales are cp1250.
u32 localization_codepage()
{
	LPCSTR lang = pSettings->line_exist("string_table", "language") ?
					pSettings->r_string("string_table", "language") : "rus";

	if (!xr_strcmp(lang, "cz") || !xr_strcmp(lang, "pol") || !xr_strcmp(lang, "hg"))
		return 1250;

	return 1251;	// rus and, harmlessly, every latin-1 locale: ASCII passes through untouched
}

void to_utf8(LPCSTR src, LPSTR dst, u32 dst_size)
{
	dst[0]			= 0;
	if (!src || !src[0] || dst_size < 2)
		return;

	WCHAR			wide[1024];
	int wide_len	= MultiByteToWideChar(localization_codepage(), 0, src, -1, wide, sizeof(wide)/sizeof(wide[0]));
	if (0 == wide_len)
		return;		// cannot be represented -- better empty than raw cp1251 bytes down the wire

	--wide_len;		// drop the terminator: everything below counts characters, not bytes

	// Cut whole characters, never bytes: Discord throws a field away if it is not valid UTF-8.
	while (wide_len > 0 &&
		   WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, NULL, 0, NULL, NULL) > (int)(dst_size - 1))
		--wide_len;

	const int written = WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, dst, (int)dst_size - 1, NULL, NULL);
	dst[(written > 0) ? written : 0] = 0;
}

// The Discord side of every activity field is 128 bytes; the same "whole characters only" rule
// applies when the composed line has to be squeezed into it.
void copy_field(LPSTR dst, u32 dst_size, LPCSTR src)
{
	dst[0]			= 0;
	if (!src || !src[0])
		return;

	u32 len			= xr_strlen(src);
	if (len > dst_size - 1)
	{
		len			= dst_size - 1;
		while (len && (0x80 == (src[len] & 0xC0)))		// landed inside a sequence -> back to its start
			--len;
	}

	CopyMemory		(dst, src, len);
	dst[len]		= 0;
}

// Everything the presence prints goes through here: an id that has no entry comes back as the id
// itself (CStringTable::translate), which is exactly the fallback we want for a level or a faction
// some other mod added.
void translate_utf8(LPCSTR string_id, LPSTR dst, u32 dst_size)
{
	if (!string_id || !string_id[0])
	{
		dst[0]		= 0;
		return;
	}

	to_utf8			(CStringTable().translate(string_id).c_str(), dst, dst_size);
}

// X-Ray's ini reader (_parse, xrCore\Xr_ini.cpp) throws away EVERY space that is not inside
// double quotes, so "S.T.A.L.K.E.R.: CSGM" written bare in the ltx arrives as "S.T.A.L.K.E.R.:CSGM".
// A caption with blanks therefore has to be quoted there -- and the quotes then survive into the
// value (r_string keeps them; only r_string_wb strips them). Peel them off here.
void unquote(LPCSTR src, LPSTR dst, u32 dst_size)
{
	dst[0]			= 0;
	if (!src || !src[0])
		return;

	u32 len			= xr_strlen(src);
	if ((len >= 2) && ('"' == src[0]) && ('"' == src[len-1]))
	{
		++src;
		len			-= 2;
	}

	if (len > dst_size - 1)
		len			= dst_size - 1;

	CopyMemory		(dst, src, len);
	dst[len]		= 0;
}

LPCSTR cfg_string(LPCSTR key, LPCSTR def)
{
	if (pSettings->section_exist(CFG_SECTION) && pSettings->line_exist(CFG_SECTION, key))
		return		pSettings->r_string(CFG_SECTION, key);

	return			def;
}

LPCSTR community_patch_image(LPCSTR community)
{
	if (!community || !community[0])
		return		DEFAULT_PATCH;

	// a config override wins, so a mod can add its own faction without touching the engine
	if (pSettings->section_exist(CFG_SECTION) && pSettings->line_exist(CFG_SECTION, community))
		return		pSettings->r_string(CFG_SECTION, community);

	for (u32 i=0; i<sizeof(COMMUNITY_TABLE)/sizeof(COMMUNITY_TABLE[0]); ++i)
		if (!xr_strcmp(COMMUNITY_TABLE[i].community, community))
			return	COMMUNITY_TABLE[i].image;

	return			DEFAULT_PATCH;
}
} // namespace

CDiscordRPC& DiscordRPC()
{
	static CDiscordRPC single;
	return			single;
}

CDiscordRPC::CDiscordRPC()
{
	m_dll			= NULL;
	m_create_fn		= NULL;
	m_core			= NULL;
	m_activity		= NULL;
	m_dll_missing	= false;
	m_started_at	= (s64)time(NULL);
	m_next_connect	= 0;
	m_next_refresh	= 0;
	m_details[0]	= 0;
	m_state[0]		= 0;
	m_small_image[0]= 0;
	m_small_text[0]	= 0;
}

bool CDiscordRPC::Connect()
{
	if (m_dll_missing)
		return		false;

	if (!m_dll)
	{
		m_dll		= LoadLibraryA("discord_game_sdk.dll");
		if (!m_dll)
		{
			// Not an error: the dll simply is not deployed. Say it once and never look again.
			Msg		("~ [discord] discord_game_sdk.dll not found, rich presence disabled");
			m_dll_missing = true;
			return	false;
		}

		m_create_fn	= (void*)GetProcAddress(m_dll, "DiscordCreate");
		if (!m_create_fn)
		{
			Msg		("! [discord] discord_game_sdk.dll has no DiscordCreate, rich presence disabled");
			FreeLibrary(m_dll);
			m_dll	= NULL;
			m_dll_missing = true;
			return	false;
		}
	}

	DiscordCreateParams	params;
	DiscordCreateParamsSetDefault(&params);
	params.client_id	= (DiscordClientId)_atoi64(cfg_string("client_id", "1541182300086345779"));
	if (0 == params.client_id)
		params.client_id = (DiscordClientId)DEFAULT_CLIENT_ID;
	// NoRequireDiscord: with the client absent DiscordCreate still succeeds and run_callbacks
	// answers NotRunning, which is how the reconnect below notices Discord starting up later.
	params.flags		= DiscordCreateFlags_NoRequireDiscord;

	IDiscordCore*	core = NULL;
	const enum EDiscordResult res = ((discord_create_fn)m_create_fn)(DISCORD_VERSION, &params, &core);
	if ((DiscordResult_Ok != res) || !core)
		return		false;

	m_core			= core;
	m_activity		= m_core->get_activity_manager(m_core);
	if (!m_activity)
	{
		Disconnect	();
		return		false;
	}

	// force the first push through the "did anything change" filter below
	m_details[0]	= 0;
	m_state[0]		= 0;
	m_small_image[0]= 0;
	m_small_text[0]	= 0;

	Msg				("* [discord] rich presence connected");
	return			true;
}

void CDiscordRPC::Disconnect()
{
	if (m_core)
	{
		m_core->destroy(m_core);
		m_core		= NULL;
	}
	m_activity		= NULL;
}

void CDiscordRPC::Shutdown()
{
	Disconnect		();

	if (m_dll)
	{
		FreeLibrary	(m_dll);
		m_dll		= NULL;
		m_create_fn	= NULL;
	}
}

void CDiscordRPC::PushPresence(LPCSTR details, LPCSTR state, LPCSTR small_image, LPCSTR small_text)
{
	const bool changed	= (0 != xr_strcmp(m_details, details)) ||
						  (0 != xr_strcmp(m_state, state)) ||
						  (0 != xr_strcmp(m_small_image, small_image)) ||
						  (0 != xr_strcmp(m_small_text, small_text));
	if (!changed)
		return;

	xr_strcpy		(m_details, details);
	xr_strcpy		(m_state, state);
	xr_strcpy		(m_small_image, small_image);
	xr_strcpy		(m_small_text, small_text);

	DiscordActivity	activity;
	ZeroMemory		(&activity, sizeof(activity));
	activity.type	= DiscordActivityType_Playing;
	activity.instance = false;
	activity.timestamps.start = (DiscordTimestamp)m_started_at;

	copy_field		(activity.details, sizeof(activity.details), details);
	copy_field		(activity.state, sizeof(activity.state), state);
	copy_field		(activity.assets.large_image, sizeof(activity.assets.large_image), cfg_string("large_image", DEFAULT_LARGE_IMAGE));
	// the only config string that can carry blanks and non-ASCII: it is a caption, not an asset key
	string256		large_text_raw;
	string256		large_text;
	unquote			(cfg_string("large_text", DEFAULT_LARGE_TEXT), large_text_raw, sizeof(large_text_raw));
	to_utf8			(large_text_raw, large_text, sizeof(large_text));
	copy_field		(activity.assets.large_text, sizeof(activity.assets.large_text), large_text);
	copy_field		(activity.assets.small_image, sizeof(activity.assets.small_image), small_image);
	copy_field		(activity.assets.small_text, sizeof(activity.assets.small_text), small_text);

	m_activity->update_activity(m_activity, &activity, NULL, activity_callback);
}

void CDiscordRPC::BuildPresence()
{
	string256		details;
	string256		state;
	string128		small_image;
	string256		small_text;

	// An empty small_image means "no patch at all" -- that is the right look in the menu and
	// while a save is still loading, when there is no actor to ask for a faction yet.
	details[0] = 0; state[0] = 0; small_text[0] = 0; small_image[0] = 0;

	const bool in_game	= g_pGameLevel && g_pGameLevel->bReady && !g_dedicated_server;

	if (!in_game)
	{
		// Menu, loading screen, credits -- anything that is not a live level.
		translate_utf8("ui_st_discord_in_menu", details, sizeof(details));
		PushPresence(details, state, small_image, small_text);
		return;
	}

	// ---- location ------------------------------------------------------------------------
	// The level ids double as string table ids (ui_st_pda.xml: "marsh", "escape", ...), so the
	// caption is localized for free; an unknown level falls back to printing its id.
	string256		level_name;
	translate_utf8	(Level().name().c_str(), level_name, sizeof(level_name));

	string256		prefix;
	translate_utf8	("ui_st_discord_exploring", prefix, sizeof(prefix));
	strconcat		((int)sizeof(details), details, prefix, " ", level_name);

	// ---- faction -------------------------------------------------------------------------
	// NO_COMMUNITY_INDEX is not just "no faction" -- CHARACTER_COMMUNITY::id() would run it
	// through GetByIndex and hit Debug.fatal. The actor holds it until net_Spawn fills the
	// character info in, and the very first frames of a level land inside that window.
	if (Actor() && (NO_COMMUNITY_INDEX != Actor()->CharacterInfo().Community().index()))
	{
		shared_str const& community = Actor()->CharacterInfo().Community().id();
		if (community.size())
		{
			xr_strcpy(small_image, community_patch_image(community.c_str()));
			translate_utf8(community.c_str(), small_text, sizeof(small_text));
		}
	}

	// ---- task ----------------------------------------------------------------------------
	// Only once there is an actor: before that the task registry behind ActiveTask has not been
	// read out of the save yet. The storyline task is the interesting one, a side task is what
	// gets shown while the main line is between assignments.
	CGameTask* task	= NULL;
	if (Actor())
	{
		task		= Level().GameTaskManager().ActiveTask(eTaskTypeStoryline);
		if (!task)
			task	= Level().GameTaskManager().ActiveTask(eTaskTypeAdditional);
	}

	if (task && task->m_Title.size())
		translate_utf8(task->m_Title.c_str(), state, sizeof(state));
	else
		translate_utf8("ui_st_discord_no_task", state, sizeof(state));

	PushPresence	(details, state, small_image, small_text);
}

void CDiscordRPC::OnFrame()
{
	if (g_dedicated_server)
		return;

	if (!psActorFlags.test(AF_DISCORD_RPC))
	{
		// Unticking the checkbox has to actually take the presence down, not just freeze it.
		if (m_core)
		{
			m_activity->clear_activity(m_activity, NULL, activity_callback);
			m_core->run_callbacks(m_core);
			Disconnect();
		}
		return;
	}

	if (!m_core)
	{
		if (Device.dwTimeGlobal < m_next_connect)
			return;

		if (!Connect())
		{
			m_next_connect = Device.dwTimeGlobal + CONNECT_RETRY_MS;
			return;
		}
	}

	const enum EDiscordResult res = m_core->run_callbacks(m_core);
	if (DiscordResult_Ok != res)
	{
		// Discord was closed (or never started). Drop the core and try again in a while.
		Disconnect	();
		m_next_connect = Device.dwTimeGlobal + CONNECT_RETRY_MS;
		return;
	}

	if (Device.dwTimeGlobal < m_next_refresh)
		return;

	m_next_refresh	= Device.dwTimeGlobal + REFRESH_MS;
	BuildPresence	();
}
