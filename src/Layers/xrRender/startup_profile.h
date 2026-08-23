#pragma once

// Startup profiling for device creation.
//
// Measured 2026-08-23: reaching the main menu takes ~28 s on the DX9 renderers against ~7.5 s on R3,
// and ALL of the difference sits inside CRenderDevice::_Create (see the "* startup [...]" stamps in
// device.cpp / Device_create.cpp). The engine's log carries no timings of its own, and xrCore flushes
// the log in bulk, so watching the file cannot separate the phases -- these counters can.
//
// Wrap each shader_compile call in begin/end; the totals are printed once, at the end of
// dxRenderDeviceRender::OnDeviceCreate. Each renderer DLL gets its own copies (the DX9 and DX10
// resource managers are separate translation units).

extern u32		g_sh_compile_count;		// shaders handed to the compiler so far
extern u32		g_sh_compile_ms;		// milliseconds spent inside it
extern CTimer	g_sh_compile_timer;

inline void	startup_compile_begin	()					{ g_sh_compile_timer.Start(); }

inline void	startup_compile_end		(LPCSTR name)
{
	const u32 ms		= g_sh_compile_timer.GetElapsed_ms();
	g_sh_compile_count	++;
	g_sh_compile_ms		+= ms;
	// name the expensive ones: if a handful dominate, that is a different fix than "all of them are slow"
	if (ms >= 100)		Msg("* slow shader: %s -- %u ms", name, ms);
}
