#pragma once

// Discord Rich Presence.
//
// Ported from the rpc4stalker ASI plugin (Tosox), but without its two moving parts: there is no
// external .asi and no %temp%\rpc4stalker.json -- the presence is built straight from the engine,
// so it also works while the player sits in the main menu, and it cannot go stale.
//
// What the player's friends see:
//   details    = "<level> | <storyline task>"  both halves come from the string table, so the whole
//                                              line follows whatever localization the game runs in
//   state      = the side task, on a line of its own (empty when there is none)
//   small icon = the faction patch, its tooltip = "<community> | <rank>", both localized
//
// discord_game_sdk.dll is loaded lazily with LoadLibrary and every entry point is resolved by hand:
// a missing or broken dll must never keep xrGame.dll itself from loading, and a player without
// Discord installed must not pay for any of this.
//
// The whole thing is gated by AF_DISCORD_RPC (console: discord_rpc, checkbox in the video options,
// ON by default). Clearing the bit tears the connection down, not just the updates.

struct IDiscordCore;
struct IDiscordActivityManager;

class CDiscordRPC
{
public:
					CDiscordRPC			();

	// Called every frame from CGamePersistent::OnFrame, BEFORE it returns for "no level yet" --
	// the menu is a state worth showing too.
	void			OnFrame				();
	void			Shutdown			();

private:
	bool			Connect				();
	void			Disconnect			();
	void			BuildPresence		();
	void			PushPresence		(LPCSTR details, LPCSTR state, LPCSTR small_image, LPCSTR small_text);

	HMODULE					m_dll;
	void*					m_create_fn;		// EDiscordResult (__stdcall*)(DiscordVersion, DiscordCreateParams*, IDiscordCore**)
	IDiscordCore*			m_core;
	IDiscordActivityManager* m_activity;

	bool					m_dll_missing;		// LoadLibrary already failed once -- do not retry it every frame
	s64						m_started_at;		// unix time of the session, for the "elapsed" counter
	u32						m_next_connect;		// Device.dwTimeGlobal gate for the reconnect attempts
	u32						m_next_refresh;		// ...and for rebuilding the strings

	// last pushed values -- update_activity is a network round trip, so it only happens on a change
	string256				m_details;
	string256				m_state;
	string128				m_small_image;
	string256				m_small_text;
};

extern CDiscordRPC& DiscordRPC();
