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
//   button     = one link under the card ([discord_rpc] button_label / button_url)
//
// It talks to Discord over its local RPC pipe (\\.\pipe\discord-ipc-N) directly: no dll to ship,
// nothing to load, and -- unlike the Game SDK this started on -- the protocol can carry buttons.
// A player without Discord running pays for one failed CreateFile every 15 seconds.
//
// The whole thing is gated by AF_DISCORD_RPC (console: discord_rpc, checkbox in the video options,
// ON by default). Clearing the bit tears the connection down, not just the updates.

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
	bool			SendFrame			(u32 opcode, LPCSTR json, u32 len);
	bool			SendActivity		(LPCSTR activity);	// NULL clears the presence
	bool			Pump				();					// read what Discord sent; false = the pipe is gone

	HANDLE					m_pipe;				// Discord's RPC pipe, INVALID_HANDLE_VALUE while not connected
	bool					m_ready;			// the handshake has been answered: commands are accepted
	u32						m_ready_deadline;	// ...or it has not, and this is when to give up on it
	u32						m_nonce;
	bool					m_pushed;			// something has been sent over THIS connection
	s64						m_started_at;		// unix time of the session, for the "elapsed" counter
	u32						m_next_connect;		// Device.dwTimeGlobal gate for the reconnect attempts
	u32						m_next_refresh;		// ...and for rebuilding the strings

	// last pushed values -- the activity is sent only when one of them changes
	string256				m_details;
	string256				m_state;
	string128				m_small_image;
	string256				m_small_text;
};

extern CDiscordRPC& DiscordRPC();
