#pragma once

enum{
		AF_GODMODE			=(1<<0),
		AF_INVISIBLE		=(1<<1),
		AF_ALWAYSRUN		=(1<<2),
		AF_UNLIMITEDAMMO	=(1<<3),
		AF_RUN_BACKWARD		=(1<<4),
		AF_AUTOPICKUP		=(1<<5),
		AF_PSP				=(1<<6),
		AF_DYNAMIC_MUSIC	=(1<<7),
		AF_GODMODE_RT		=(1<<8),
		AF_AUTORELOAD		=(1<<9),
		// GS gameplay options for the 3D PDA (gunsl_config.pas _mask_pdaautozoom / _mask_pdasavezoomstate).
		// They live here because an options-menu CHECKBOX binds to a CCC_Mask bit, not to an int command.
		// Each bit is its own console command, so adding to this word touches nothing that was saved.
		AF_PDA_AUTOZOOM		=(1<<10),	// the PDA opens already at the face instead of down in the hand
		AF_PDA_SAVEZOOM		=(1<<11),	// ...or rather: reopen it in whatever state it was last left
		// OFF by default: a trigger pull made while the weapon is still sitting out the post-shot delay
		// (rpm gap / recharge_time) is dropped. Set it to bring the stock behaviour back -- the press is
		// remembered and the round leaves the instant the delay expires. See CWeaponMagazined::FireStart.
		AF_WPN_SHOT_QUEUE	=(1<<12),
		// CoP: the finished loading screen waits for a keypress before the game starts (console
		// name kept CoP's). ON by default. See arm_load_keypress_gate / CRenderDevice::End.
		AF_KEYPRESS_ON_START=(1<<13),
		// Master switch for the Gunslinger-style 3D PDA. ON by default. Off = the stock Clear Sky
		// PDA: no hud phantom in the hands, no render-to-texture, the window is simply drawn
		// full-screen again. Everything else keys off gwr_pda_screen_active(), which this gates.
		AF_PDA_3D			=(1<<14),
		// GS lens_enabled: master switch for the 3D PiP scope lens. ON by default. Off = the stock
		// full-screen 2D scope picture. Gates CWeapon::IsLensedScope, which is what every other
		// lens decision asks (GS does exactly this in its LensConditions / IsForceHideZoomTexture).
		AF_LENS_3D			=(1<<15),
		// GS npc_lasers: an NPC-carried weapon keeps its laser beam lit. ON by default. Was the int
		// cvar g_npc_lasers; moved onto a mask bit so the options menu can bind a checkbox to it.
		AF_NPC_LASERS		=(1<<16),
		// Draw the quick-use slot icons (with their counters and key labels) on the hud. ON by default.
		// The slots keep working either way -- this is display only, and on master the hud has none of
		// it whatever the bit says. See CUIMainIngameWnd::UpdateQuickSlots.
		AF_SHOW_QUICK_SLOTS	=(1<<17),
	// Discord Rich Presence: level, faction and active task shown to the player's friends.
	// ON by default; the checkbox lives in the video options. Clearing the bit disconnects
	// from Discord instead of merely freezing the last state. See discord_rpc.cpp.
	AF_DISCORD_RPC		=(1<<18),
	// First-person body/legs (console: g_legs, checkbox in the ADVANCED video options).
	// OFF by default. On the mask rather than an int cvar because an options-menu checkbox
	// binds to a CCC_Mask bit. See player_legs.cpp.
	AF_LEGS				=(1<<19),
};

extern Flags32 psActorFlags;

extern BOOL		GodMode	();	

