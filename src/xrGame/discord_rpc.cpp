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
#include "ui/UIInventoryUtilities.h"

#include <ctime>

namespace
{
// This mod's own Discord application -- ITS NAME is what Discord prints as the title
// ("Playing ..."), and it can only be changed in the developer portal, never from here. The art
// (stalker_icon_0 plus one stalker_patch_* per faction) has to exist as Art Assets of the same
// application. Overridable through the optional [discord_rpc] config section below.
const s64		DEFAULT_CLIENT_ID	= 1541182300086345779ll;
LPCSTR			DEFAULT_LARGE_IMAGE	= "csga_dirt_skull";	// the mod's own logo, uploaded as an Art Asset
LPCSTR			DEFAULT_LARGE_TEXT	= "Clear Sky Gunslinger Mod";
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

// ---- transport ---------------------------------------------------------------------------------
// Discord's local RPC socket, spoken directly: \\.\pipe\discord-ipc-N, frames of
// [u32 opcode][u32 length][JSON, UTF-8]. This used to go through the Game SDK
// (discord_game_sdk.dll), but its DiscordActivity has no BUTTONS at all -- the field does not exist
// there -- while the RPC protocol carries them. It also takes the extra dll out of the picture.
enum { OP_HANDSHAKE = 0, OP_FRAME = 1, OP_CLOSE = 2, OP_PING = 3, OP_PONG = 4 };

const u32		MAX_FRAME			= 64 * 1024;	// the READY answer is a couple of KB; anything past this is not ours
const u32		READY_TIMEOUT_MS	= 10000;		// handshake sent, no READY: drop the pipe and start over
const u32		FIELD_SIZE			= 128;			// every text field of an activity, bytes with the terminator

// The button under the presence card. Discord shows at most two, a label of up to 32 characters
// each; the link opens in the viewer's browser. (Discord does not let you click your OWN buttons --
// look at the profile from another account to try it.)
LPCSTR			DEFAULT_BUTTON_LABEL = "CSGM Discord";
LPCSTR			DEFAULT_BUTTON_URL	= "discord.com/invite/jQJ8rgfSbY";

void json_string(xr_string& out, LPCSTR s)
{
	out				+= '"';
	for (; s && *s; ++s)
	{
		const unsigned char c = (unsigned char)*s;
		if (('"' == c) || ('\\' == c))	{ out += '\\'; out += (char)c; }
		else if (c < 0x20)				{ string16 u; xr_sprintf(u, "\\u%04x", (unsigned)c); out += u; }
		else							out += (char)c;		// UTF-8 goes through as it is
	}
	out				+= '"';
}

// "name":"value", with the comma the previous member needs. Empty values are left out altogether:
// Discord rejects the whole activity over a text field shorter than two characters.
void json_member(xr_string& out, LPCSTR name, LPCSTR value, bool& first)
{
	if (!value || !value[0] || !value[1])
		return;

	if (!first)		out += ',';
	first			= false;
	out				+= '"';
	out				+= name;
	out				+= "\":";
	json_string		(out, value);
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
	m_pipe			= INVALID_HANDLE_VALUE;
	m_ready			= false;
	m_ready_deadline= 0;
	m_nonce			= 0;
	m_pushed		= false;
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
	// Discord listens on the first free one of ten pipes (a second client, PTB or Canary, takes the next)
	for (int i = 0; i < 10; ++i)
	{
		string64	name;
		xr_sprintf	(name, "\\\\.\\pipe\\discord-ipc-%d", i);
		HANDLE h	= CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (INVALID_HANDLE_VALUE == h)
			continue;

		// NOWAIT: neither a read nor a write may ever stall the game's frame on a busy Discord
		DWORD mode	= PIPE_READMODE_BYTE | PIPE_NOWAIT;
		SetNamedPipeHandleState(h, &mode, NULL, NULL);
		m_pipe		= h;
		break;
	}
	if (INVALID_HANDLE_VALUE == m_pipe)
		return		false;		// Discord is not running

	// the id travels as a string; keep the digits only so a stray blank or quote cannot break the JSON
	string64		id;
	u32 n			= 0;
	for (LPCSTR p = cfg_string("client_id", ""); *p && (n < sizeof(id) - 1); ++p)
		if ((*p >= '0') && (*p <= '9'))
			id[n++]	= *p;
	id[n]			= 0;
	if (!n)
		xr_sprintf	(id, "%I64d", DEFAULT_CLIENT_ID);

	xr_string		hello = "{\"v\":1,\"client_id\":\"";
	hello			+= id;
	hello			+= "\"}";
	if (!SendFrame(OP_HANDSHAKE, hello.c_str(), (u32)hello.size()))
	{
		Disconnect	();
		return		false;
	}

	m_ready			= false;
	m_ready_deadline= Device.dwTimeGlobal + READY_TIMEOUT_MS;

	// force the first push through the "did anything change" filter below
	m_details[0]	= 0;
	m_state[0]		= 0;
	m_small_image[0]= 0;
	m_small_text[0]	= 0;
	m_pushed		= false;
	return			true;
}

void CDiscordRPC::Disconnect()
{
	if (INVALID_HANDLE_VALUE != m_pipe)
	{
		CloseHandle	(m_pipe);
		m_pipe		= INVALID_HANDLE_VALUE;
	}
	m_ready			= false;
}

void CDiscordRPC::Shutdown()
{
	if (INVALID_HANDLE_VALUE != m_pipe)
		SendActivity(NULL);		// take the card down rather than leave it to Discord's own timeout
	Disconnect		();
}

// One frame, header and payload in a single write so it can never be split by another writer.
bool CDiscordRPC::SendFrame(u32 opcode, LPCSTR json, u32 len)
{
	if (INVALID_HANDLE_VALUE == m_pipe)
		return		false;

	xr_string		frame;
	frame.reserve	(8 + len);
	frame.append	((const char*)&opcode, 4);
	frame.append	((const char*)&len, 4);
	frame.append	(json, len);

	DWORD written	= 0;
	return			WriteFile(m_pipe, frame.data(), (DWORD)frame.size(), &written, NULL) && (written == frame.size());
}

// SET_ACTIVITY; activity == NULL clears the presence.
bool CDiscordRPC::SendActivity(LPCSTR activity)
{
	string64		tail;
	xr_sprintf		(tail, "},\"nonce\":\"%u\"}", ++m_nonce);

	string64		head;
	xr_sprintf		(head, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%u", (u32)GetCurrentProcessId());

	xr_string		msg = head;
	if (activity)
	{
		msg			+= ",\"activity\":";
		msg			+= activity;
	}
	msg				+= tail;
	return			SendFrame(OP_FRAME, msg.c_str(), (u32)msg.size());
}

// Whatever Discord has sent: the READY that follows the handshake, the answer to every command, a ping
// now and then. All of it is read and dropped -- an unread pipe fills up and the writes start failing.
// false = the pipe is gone (Discord closed) or Discord turned the handshake down.
bool CDiscordRPC::Pump()
{
	for (;;)
	{
		DWORD avail	= 0;
		if (!PeekNamedPipe(m_pipe, NULL, 0, NULL, &avail, NULL))
			return	false;
		if (avail < 8)
			return	true;

		u32 header[2];
		DWORD got	= 0;
		if (!PeekNamedPipe(m_pipe, header, 8, &got, NULL, NULL) || (got < 8))
			return	false;

		const u32 opcode	= header[0];
		const u32 len		= header[1];
		if (len > MAX_FRAME)
			return	false;
		if (avail < 8 + len)
			return	true;		// the rest of the frame is still on its way

		xr_string	frame;
		frame.resize(8 + len);
		if (!ReadFile(m_pipe, &frame[0], 8 + len, &got, NULL) || (got != 8 + len))
			return	false;

		switch (opcode)
		{
		case OP_FRAME:
			if (!m_ready)
			{
				m_ready	= true;
				Msg		("* [discord] rich presence connected");
			}
			// a command Discord turned down (a button address it does not like, a field too long):
			// say so, with its own words -- otherwise the card just silently fails to change
			if (strstr(frame.c_str() + 8, "\"evt\":\"ERROR\""))
				Msg		("! [discord] %.300s", frame.c_str() + 8);
			break;
		case OP_PING:
			if (!SendFrame(OP_PONG, frame.c_str() + 8, len))
				return	false;
			break;
		case OP_CLOSE:
			return	false;		// e.g. an application id Discord does not know
		}
	}
}

void CDiscordRPC::PushPresence(LPCSTR details, LPCSTR state, LPCSTR small_image, LPCSTR small_text)
{
	const bool changed	= !m_pushed ||
						  (0 != xr_strcmp(m_details, details)) ||
						  (0 != xr_strcmp(m_state, state)) ||
						  (0 != xr_strcmp(m_small_image, small_image)) ||
						  (0 != xr_strcmp(m_small_text, small_text));
	if (!changed)
		return;

	xr_strcpy		(m_details, details);
	xr_strcpy		(m_state, state);
	xr_strcpy		(m_small_image, small_image);
	xr_strcpy		(m_small_text, small_text);
	m_pushed		= true;

	// every text field is cut to Discord's 128 bytes on whole characters (copy_field)
	char			f_details[FIELD_SIZE], f_state[FIELD_SIZE];
	char			f_large_image[FIELD_SIZE], f_large_text[FIELD_SIZE];
	char			f_small_image[FIELD_SIZE], f_small_text[FIELD_SIZE];
	copy_field		(f_details, sizeof(f_details), details);
	copy_field		(f_state, sizeof(f_state), state);
	copy_field		(f_large_image, sizeof(f_large_image), cfg_string("large_image", DEFAULT_LARGE_IMAGE));
	// config captions can carry blanks and non-ASCII: they are text, not asset keys
	string256		raw;
	string256		utf;
	unquote			(cfg_string("large_text", DEFAULT_LARGE_TEXT), raw, sizeof(raw));
	to_utf8			(raw, utf, sizeof(utf));
	copy_field		(f_large_text, sizeof(f_large_text), utf);
	copy_field		(f_small_image, sizeof(f_small_image), small_image);
	copy_field		(f_small_text, sizeof(f_small_text), small_text);

	xr_string		a = "{";
	bool first		= true;
	json_member		(a, "details", f_details, first);
	json_member		(a, "state", f_state, first);

	string64		ts;
	xr_sprintf		(ts, "%s\"timestamps\":{\"start\":%I64d}", first ? "" : ",", m_started_at);
	a				+= ts;

	a				+= ",\"assets\":{";
	first			= true;
	json_member		(a, "large_image", f_large_image, first);
	json_member		(a, "large_text", f_large_text, first);
	json_member		(a, "small_image", f_small_image, first);
	json_member		(a, "small_text", f_small_text, first);
	a				+= "}";

	// ---- the button --------------------------------------------------------------------------
	// [discord_rpc] button_label / button_url; an empty url (or label) = no button. The config
	// keeps the address WITHOUT its scheme: the ini reader takes "//" for the start of a comment
	// and would cut "https://..." down to "https:".
	string256		label;
	unquote			(cfg_string("button_label", DEFAULT_BUTTON_LABEL), raw, sizeof(raw));
	to_utf8			(raw, utf, sizeof(utf));
	copy_field		(label, 33, utf);				// 32 bytes is the most Discord takes

	string512		url;
	unquote			(cfg_string("button_url", DEFAULT_BUTTON_URL), raw, sizeof(raw));
	if (raw[0] && !strstr(raw, "://"))	strconcat((int)sizeof(url), url, "https://", raw);
	else								xr_strcpy(url, raw);

	if (label[0] && label[1] && url[0])
	{
		a			+= ",\"buttons\":[{\"label\":";
		json_string	(a, label);
		a			+= ",\"url\":";
		json_string	(a, url);
		a			+= "}]";
	}

	a				+= ",\"instance\":false}";

	if (!SendActivity(a.c_str()))
	{
		Disconnect	();
		m_next_connect = Device.dwTimeGlobal + CONNECT_RETRY_MS;
	}
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
	// caption is localized for free; an unknown level falls back to printing its id. The line it
	// goes on is composed further down: it shares it with the storyline task.
	string256		level_name;
	translate_utf8	(Level().name().c_str(), level_name, sizeof(level_name));

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

			// Tooltip is "<faction> | <rank>". GetRankAsText turns the rank value into the
			// game_relations `rating_names` id (novice / experienced / veteran / master), which is
			// a string table id exactly like the community one, so both halves stay localized.
			string256	community_name;
			string256	rank_name;
			translate_utf8(community.c_str(), community_name, sizeof(community_name));
			translate_utf8(InventoryUtilities::GetRankAsText(Actor()->Rank()), rank_name, sizeof(rank_name));

			if (rank_name[0])
				strconcat((int)sizeof(small_text), small_text, community_name, " | ", rank_name);
			else
				xr_strcpy(small_text, community_name);
		}
	}

	// ---- task ----------------------------------------------------------------------------
	// Only once there is an actor: before that the task registry behind ActiveTask has not been
	// read out of the save yet. The storyline task shares the first line with the level, the side
	// task gets the second one to itself -- that is how three things fit into the two single-line
	// labels Discord gives us (details/state; a newline inside one does not split it).
	CGameTask* story	= NULL;
	CGameTask* side		= NULL;
	if (Actor())
	{
		story		= Level().GameTaskManager().ActiveTask(eTaskTypeStoryline);
		side		= Level().GameTaskManager().ActiveTask(eTaskTypeAdditional);
	}

	string256		story_title;
	string256		side_title;
	story_title[0]	= 0;
	side_title[0]	= 0;

	if (story && story->m_Title.size())
		translate_utf8(story->m_Title.c_str(), story_title, sizeof(story_title));
	if (side && side->m_Title.size())
		translate_utf8(side->m_Title.c_str(), side_title, sizeof(side_title));

	// The "no task" caption belongs in the task slot only when there is NO active task at all: with a
	// side task running below, printing it next to the level would contradict the line underneath.
	if (story_title[0])
		strconcat	((int)sizeof(details), details, level_name, " | ", story_title);
	else if (side_title[0])
		xr_strcpy	(details, level_name);
	else
	{
		string256	no_task;
		translate_utf8("ui_st_discord_no_task", no_task, sizeof(no_task));
		strconcat	((int)sizeof(details), details, level_name, " | ", no_task);
	}

	xr_strcpy		(state, side_title);	// empty when there is none -- Discord then shows no second line

	PushPresence	(details, state, small_image, small_text);
}

void CDiscordRPC::OnFrame()
{
	if (g_dedicated_server)
		return;

	if (!psActorFlags.test(AF_DISCORD_RPC))
	{
		// Unticking the checkbox has to actually take the presence down, not just freeze it.
		if (INVALID_HANDLE_VALUE != m_pipe)
		{
			SendActivity(NULL);
			Disconnect	();
		}
		return;
	}

	if (INVALID_HANDLE_VALUE == m_pipe)
	{
		if (Device.dwTimeGlobal < m_next_connect)
			return;

		if (!Connect())
		{
			m_next_connect = Device.dwTimeGlobal + CONNECT_RETRY_MS;
			return;
		}
	}

	// Discord was closed, turned the handshake down, or never answered it: drop the pipe and try
	// again in a while.
	if (!Pump() || (!m_ready && (Device.dwTimeGlobal > m_ready_deadline)))
	{
		Disconnect	();
		m_next_connect = Device.dwTimeGlobal + CONNECT_RETRY_MS;
		return;
	}

	if (!m_ready)
		return;		// the handshake is still on its way back

	if (Device.dwTimeGlobal < m_next_refresh)
		return;

	m_next_refresh	= Device.dwTimeGlobal + REFRESH_MS;
	BuildPresence	();
}
