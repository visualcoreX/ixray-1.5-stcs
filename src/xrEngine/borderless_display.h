#pragma once

// A borderless window at a resolution the desktop does not have -- see device.cpp.
// The renderers call these from CHW (xrRender\HW.cpp and xrRenderDX10\dx10HW.cpp).

// Put the desktop into (or take it out of) the mode the current settings ask for. To be called
// BEFORE the device is reset, so that an exclusive mode never starts on a desktop this code has
// switched.
ENGINE_API void	borderless_sync_display		(HWND hWnd);
// Size and centre the borderless window for a w x h back buffer.
ENGINE_API void	borderless_place_window		(HWND hWnd, u32 w, u32 h);
// Give the desktop back if it was switched. Safe to call at any time.
ENGINE_API void	borderless_restore_display	();
