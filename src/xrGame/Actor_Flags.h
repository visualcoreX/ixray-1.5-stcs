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
};

extern Flags32 psActorFlags;

extern BOOL		GodMode	();	

