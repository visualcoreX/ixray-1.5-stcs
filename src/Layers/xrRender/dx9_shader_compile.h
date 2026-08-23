#pragma once

// Compile a DX9 (vs_3_0 / ps_3_0) shader through the newest compiler on the machine.
//
// WHY (measured 2026-08-23): reaching the main menu took ~28 s on R2.5 against ~7.5 s on R3, and the
// startup stamps put ALL of it in one place -- a single shader. `combine_1` alone compiled for
// 26 683 ms out of 27 414 ms spent in the compiler; the other 49 shaders together took 0.7 s. The
// shader is not the problem: fxc from the Windows 10 SDK (D3DCompiler_47) builds that exact source
// with the same defines -- SSAO_QUALITY=3, USE_HBAO -- in 0.5 s. D3DX9 hard-links D3DCompiler_43
// (2010), whose SM3 optimiser falls apart on the nested SSAO/HBAO loops.
//
// d3dcompiler_47.dll ships with Windows (SysWOW64 for our x86 build), so this is a LoadLibrary away.
// Everything needed is layout-compatible with the D3DX9 types: D3DXMACRO == D3D_SHADER_MACRO (two
// const char*), ID3DXInclude == ID3DInclude (same two __stdcall entries, no IUnknown), ID3DXBuffer ==
// ID3DBlob (IUnknown + GetBufferPointer + GetBufferSize), and the flag values are deliberately
// identical (DEBUG 1<<0, PACKMATRIX_ROWMAJOR 1<<3, ENABLE_BACKWARDS_COMPATIBILITY 1<<12).
// Verified that a ps_3_0 blob from compiler 47 still carries the CTAB chunk, which is what
// CResourceManager::_CreatePS reads back with D3DXFindShaderComment.
//
// Falls back to D3DXCompileShader if the DLL is missing, so nothing depends on it being there.

inline HRESULT	xr_dx9_shader_compile(
	LPCSTR				pSrcData,
	UINT				SrcDataLen,
	CONST D3DXMACRO*	pDefines,
	LPD3DXINCLUDE		pInclude,
	LPCSTR				pEntry,
	LPCSTR				pTarget,
	DWORD				Flags,
	LPD3DXBUFFER*		ppShader,
	LPD3DXBUFFER*		ppErrorMsgs)
{
	typedef HRESULT (WINAPI *PFN_D3DCOMPILE)(
		LPCVOID, SIZE_T, LPCSTR, LPCVOID, LPVOID, LPCSTR, LPCSTR, UINT, UINT, LPVOID*, LPVOID*);

	static PFN_D3DCOMPILE	s_compile	= NULL;
	static bool				s_probed	= false;

	if (!s_probed)
	{
		s_probed		= true;
		HMODULE	hDll	= LoadLibraryA("d3dcompiler_47.dll");
		if (hDll)	s_compile = (PFN_D3DCOMPILE)GetProcAddress(hDll, "D3DCompile");
		Msg			("* shader compiler: %s", s_compile ? "d3dcompiler_47" : "d3dx9 (D3DCompiler_43)");
	}

	if (s_compile)
		return s_compile(
			pSrcData, SrcDataLen, NULL, (LPCVOID)pDefines, (LPVOID)pInclude,
			pEntry, pTarget, Flags, 0, (LPVOID*)ppShader, (LPVOID*)ppErrorMsgs);

	return D3DXCompileShader(
		pSrcData, SrcDataLen, pDefines, pInclude, pEntry, pTarget, Flags, ppShader, ppErrorMsgs, NULL);
}
