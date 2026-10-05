#pragma once

// Disk cache for compiled shaders, shared by R1, R2 and R3. Every renderer compiled every shader from
// source on every run -- seconds at startup, and worse, every shader first needed by a model that
// spawns mid-game (a boar, a crow, an artefact: shadow_direct_model_aref_2, model_distort_*) cost
// 50-120 ms right in the middle of play.
//
// Keyed by everything the compiler sees: shader name, source text, entry, target, flags, the full
// define list and which compiler builds it. Each entry also records every #include the compiler opened
// with a hash of its text and is used only while all of them still read the same -- so editing any
// shader file is picked up on the next run exactly as before, with nothing to clear. A hash of the
// bytecode guards against a damaged file. Entries go to $app_data_root$\<cache_dir>\.
//
// The bytecode is DXBC, which the driver translates when the shader object is created, so a driver
// update does not invalidate anything here.
typedef HRESULT (*shader_compile_fn)(LPCSTR src, UINT src_len, const D3D_SHADER_MACRO* defines,
	ID3DInclude* includer, LPCSTR entry, LPCSTR target, DWORD flags, ID3DBlob** ppShader, ID3DBlob** ppErrorMsgs);

HRESULT	shader_compile_cached(
	LPCSTR						cache_dir,		// "shader_cache_r3" etc.
	LPCSTR						compiler_id,	// mixed into the key: different compilers, different bytecode
	shader_compile_fn			compile,
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
