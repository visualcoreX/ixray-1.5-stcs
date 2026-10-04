#pragma once

// Disk cache for compiled R3 shaders. R3 compiled every shader from source on every run -- ~5 s at
// startup, and worse, every shader first needed by a model that spawns mid-game (a boar, a crow, an
// artefact: shadow_direct_model_aref_2, model_distort_*) cost 50-95 ms right in the middle of play.
//
// Keyed by everything the compiler sees: shader name, source text, entry, target, flags and the full
// define list. Each entry also records every #include the compiler opened with a hash of its text, and
// is used only while all of them still read the same -- so editing any .vs/.ps/.gs/.h is picked up on
// the next run exactly as before, no cache to clear. Files go to $app_data_root$\shader_cache_r3\.
HRESULT	r3_compile_shader_cached(
	LPCSTR						name,
	LPCSTR						src,
	UINT						src_len,
	const D3D_SHADER_MACRO*		defines,
	ID3DInclude*				includer,
	LPCSTR						entry,
	LPCSTR						target,
	DWORD						flags,
	ID3DBlob**					ppShader,
	ID3DBlob**					ppErrorMsgs);
