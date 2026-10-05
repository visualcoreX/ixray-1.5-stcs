#include "stdafx.h"
#include "r3_shader_cache.h"

namespace
{
	const u32	cache_magic		= 0x43535258;	// "XRSC"
	const u32	cache_version	= 2;		// 2: bytecode hash after the size (a damaged entry is recompiled, not created)

	u64 fnv(const void* data, size_t size, u64 h = 0xcbf29ce484222325ull)
	{
		const u8* p = (const u8*)data;
		for (size_t i = 0; i < size; ++i)	{ h ^= p[i]; h *= 0x100000001b3ull; }
		return h;
	}
	u64 fnv_str(LPCSTR s, u64 h)	{ return fnv(s ? s : "", s ? xr_strlen(s) + 1 : 1, h); }

	// Include text hashes, kept a few seconds so a burst of compiles (startup, a level load) reads
	// common.h once instead of once per shader, while an edit made later in the session still counts.
	struct include_hash { u64 hash; u32 time; bool ok; };
	xr_map<xr_string, include_hash>	s_include_hashes;

	bool hash_include(ID3DInclude* includer, LPCSTR name, u64& out)
	{
		const u32 now	= GetTickCount();
		auto it			= s_include_hashes.find(name);
		if (it != s_include_hashes.end() && now - it->second.time < 10000)
		{
			out			= it->second.hash;
			return		it->second.ok;
		}
		LPCVOID data	= 0;
		UINT	bytes	= 0;
		include_hash	ih;
		ih.ok			= SUCCEEDED(includer->Open(D3D10_INCLUDE_LOCAL, name, 0, &data, &bytes));
		ih.hash			= ih.ok ? fnv(data, bytes) : 0;
		ih.time			= now;
		if (ih.ok)		includer->Close(data);
		s_include_hashes[name] = ih;
		out				= ih.hash;
		return			ih.ok;
	}

	// Passes everything through and remembers what the compiler opened.
	class recording_includer : public ID3DInclude
	{
		ID3DInclude*	m_inner;
	public:
		xr_vector<std::pair<xr_string, u64> >	opened;

		recording_includer(ID3DInclude* inner) : m_inner(inner) {}

		HRESULT __stdcall Open(D3D10_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* ppData, UINT* pBytes)
		{
			HRESULT hr	= m_inner->Open(type, name, parent, ppData, pBytes);
			if (SUCCEEDED(hr))
			{
				const u64 h	= fnv(*ppData, *pBytes);
				opened.push_back(std::make_pair(xr_string(name), h));
				include_hash ih;	ih.hash = h; ih.time = GetTickCount(); ih.ok = true;
				s_include_hashes[name] = ih;
			}
			return hr;
		}
		HRESULT __stdcall Close(LPCVOID data)	{ return m_inner->Close(data); }
	};

	void cache_path(u64 key, string_path& out)
	{
		string_path	dir;
		FS.update_path	(dir, "$app_data_root$", "shader_cache_r3\\");
		CreateDirectoryA(dir, 0);
		xr_sprintf		(out, "%s%016I64x.bin", dir, key);
	}

	bool read_file(LPCSTR path, xr_vector<u8>& out)
	{
		HANDLE f	= CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, 0);
		if (f == INVALID_HANDLE_VALUE)	return false;
		const DWORD size	= GetFileSize(f, 0);
		bool ok				= size != INVALID_FILE_SIZE && size > 0;
		if (ok)
		{
			out.resize		(size);
			DWORD read		= 0;
			ok				= ReadFile(f, &out[0], size, &read, 0) && read == size;
		}
		CloseHandle			(f);
		return				ok;
	}

	struct reader
	{
		const u8* p; const u8* end;
		bool get(void* dst, size_t n)	{ if (size_t(end - p) < n) return false; CopyMemory(dst, p, n); p += n; return true; }
	};

	bool try_load(LPCSTR path, ID3DInclude* includer, ID3DBlob** ppShader)
	{
		xr_vector<u8>	file;
		if (!read_file(path, file))	return false;

		reader	r		= { &file[0], &file[0] + file.size() };
		u32		magic, version, n_inc;
		if (!r.get(&magic, 4) || magic != cache_magic)		return false;
		if (!r.get(&version, 4) || version != cache_version)	return false;
		if (!r.get(&n_inc, 4))									return false;
		for (u32 i = 0; i < n_inc; ++i)
		{
			u16		len;
			u64		hash, now;
			if (!r.get(&len, 2) || size_t(r.end - r.p) < size_t(len))	return false;
			xr_string	name((const char*)r.p, len);
			r.p		+= len;
			if (!r.get(&hash, 8))									return false;
			if (!includer || !hash_include(includer, name.c_str(), now) || now != hash)
				return false;										// an include changed: recompile
		}
		u32		blob_size;
		u64		blob_hash;
		if (!r.get(&blob_size, 4) || !r.get(&blob_hash, 8))	return false;
		if (!blob_size || size_t(r.end - r.p) != size_t(blob_size))	return false;
		// A file damaged on disk would otherwise go straight to CreateVertexShader as garbage.
		if (fnv(r.p, blob_size) != blob_hash)					return false;

		ID3DBlob*	blob	= 0;
		if (FAILED(D3D10CreateBlob(blob_size, &blob)) || !blob)	return false;
		CopyMemory		(blob->GetBufferPointer(), r.p, blob_size);
		*ppShader		= blob;
		return			true;
	}

	void store(LPCSTR path, const recording_includer& rec, ID3DBlob* blob)
	{
		xr_vector<u8>	out;
		auto put		= [&out](const void* p, size_t n) { out.insert(out.end(), (const u8*)p, (const u8*)p + n); };
		const u32 n_inc	= u32(rec.opened.size());
		put				(&cache_magic, 4);
		put				(&cache_version, 4);
		put				(&n_inc, 4);
		for (u32 i = 0; i < n_inc; ++i)
		{
			const u16 len	= u16(rec.opened[i].first.size());
			put				(&len, 2);
			put				(rec.opened[i].first.c_str(), len);
			put				(&rec.opened[i].second, 8);
		}
		const u32 blob_size	= u32(blob->GetBufferSize());
		const u64 blob_hash	= fnv(blob->GetBufferPointer(), blob_size);
		put				(&blob_size, 4);
		put				(&blob_hash, 8);
		put				(blob->GetBufferPointer(), blob_size);

		// Write aside and swap in, so a crash mid-write never leaves a torn entry under the real name.
		string_path		tmp;
		xr_sprintf		(tmp, "%s.tmp", path);
		HANDLE f		= CreateFileA(tmp, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
		if (f == INVALID_HANDLE_VALUE)	return;
		DWORD written	= 0;
		const BOOL ok	= WriteFile(f, &out[0], DWORD(out.size()), &written, 0) && written == out.size();
		CloseHandle		(f);
		if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING))
			DeleteFileA	(tmp);
	}
}

HRESULT	r3_compile_shader_cached(LPCSTR name, LPCSTR src, UINT src_len, const D3D_SHADER_MACRO* defines,
	ID3DInclude* includer, LPCSTR entry, LPCSTR target, DWORD flags, ID3DBlob** ppShader, ID3DBlob** ppErrorMsgs)
{
	// The format version is checked inside the entry, not hashed into the name: a newer format then
	// overwrites the old entry in place instead of leaving it behind.
	u64 key		= fnv_str(name, 0xcbf29ce484222325ull);
	key			= fnv_str(entry, key);
	key			= fnv_str(target, key);
	key			= fnv(&flags, sizeof(flags), key);
	for (const D3D_SHADER_MACRO* d = defines; d && d->Name; ++d)
	{
		key		= fnv_str(d->Name, key);
		key		= fnv_str(d->Definition, key);
	}
	key			= fnv(src, src_len, key);

	string_path	path;
	cache_path	(key, path);

	if (try_load(path, includer, ppShader))
	{
		if (ppErrorMsgs)	*ppErrorMsgs = 0;
		return				S_OK;
	}

	recording_includer	rec(includer);
	HRESULT hr	= D3DX10CompileFromMemory(src, src_len,
		"",		// NVPerfHUD bug workaround.
		defines, includer ? &rec : 0, entry, target, flags, 0, NULL, ppShader, ppErrorMsgs, NULL);

	if (SUCCEEDED(hr) && ppShader && *ppShader)
		store	(path, rec, *ppShader);
	return		hr;
}
