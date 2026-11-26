#include "stdafx.h"
#pragma hdrstop
#include "TextureDescrManager.h"
#include "ETextureParams.h"

// eye-params
float					r__dtex_range	= 50;
class cl_dt_scaler		: public R_constant_setup {
public:
	float				scale;

	cl_dt_scaler		(float s) : scale(s)	{};
	virtual void setup	(R_constant* C)
	{
		RCache.set_c	(C,scale,scale,scale,1/r__dtex_range);
	}
};

void fix_texture_thm_name(LPSTR fn)
{
	LPSTR _ext = strext(fn);
	if(  _ext					&&
	  (0==_stricmp(_ext,".tga")	||
		0==_stricmp(_ext,".thm")	||
		0==_stricmp(_ext,".dds")	||
		0==_stricmp(_ext,".bmp")	||
		0==_stricmp(_ext,".ogm")	) )
		*_ext = 0;
}

// Disk cache of the texture descriptions.
//
// Every start of every renderer opened each texture's .thm (thousands of them, most packed and
// compressed inside the archives) just to learn its detail texture, bump and material. On a fast PC
// that is half a second; on a weak laptop (Celeron N4000, eMMC) the step behind the
// "blenders parsed (shaders.xr)" stamp took 27 s of a 68 s start. The result is a few hundred KB, so
// it is kept in $app_data_root$\thm_cache\ and read back while the .thm files are unchanged.
//
// The cache is keyed by the list itself: every .thm's name, CRC (inside an archive), size and date
// (a loose file). Adding, removing or changing any .thm -- a patch, a mod, a new archive -- makes a
// different key and the files are read again, as before. Nothing ever needs clearing by hand.
namespace
{
	const u32	thm_cache_magic		= 0x4D485458;	// "XTHM"
	const u32	thm_cache_version	= 1;

	u64 thm_fnv(const void* data, size_t size, u64 h)
	{
		const u8* p = (const u8*)data;
		for (size_t i = 0; i < size; ++i)	{ h ^= p[i]; h *= 0x100000001b3ull; }
		return h;
	}

	struct thm_reader
	{
		const u8* p; const u8* end;
		bool get(void* dst, size_t n)	{ if (size_t(end - p) < n) return false; CopyMemory(dst, p, n); p += n; return true; }
		bool str(xr_string& s)
		{
			u16 len;
			if (!get(&len, 2) || size_t(end - p) < size_t(len))	return false;
			s.assign((const char*)p, len);
			p += len;
			return true;
		}
	};

	struct thm_writer
	{
		xr_vector<u8> out;
		void put(const void* p, size_t n)	{ out.insert(out.end(), (const u8*)p, (const u8*)p + n); }
		void str(const xr_string& s)		{ const u16 len = u16(s.size()); put(&len, 2); put(s.c_str(), len); }
	};

	bool thm_read_file(LPCSTR path, xr_vector<u8>& out)
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

	// Written aside and swapped in, so a crash mid-write never leaves a torn file under the real name.
	void thm_write_file(LPCSTR path, const xr_vector<u8>& data)
	{
		string_path		tmp;
		xr_sprintf		(tmp, "%s.tmp", path);
		HANDLE f		= CreateFileA(tmp, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
		if (f == INVALID_HANDLE_VALUE)	return;
		DWORD written	= 0;
		const BOOL ok	= WriteFile(f, &data[0], DWORD(data.size()), &written, 0) && written == data.size();
		CloseHandle		(f);
		if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING))
			DeleteFileA	(tmp);
	}
}

void CTextureDescrMngr::ApplyTHM(const thm_record& r)
{
	texture_desc& desc			= m_texture_details[r.name.c_str()];

	if (r.has_assoc)
	{
		if (desc.m_assoc)
			xr_delete				(desc.m_assoc);
		desc.m_assoc				= xr_new<texture_assoc>();
		desc.m_assoc->detail_name	= r.detail_name.c_str();
		desc.m_assoc->cs			= xr_new<cl_dt_scaler>(r.detail_scale);
		desc.m_assoc->usage			= r.usage;
	}
	if (desc.m_spec)
		xr_delete					(desc.m_spec);
	desc.m_spec						= xr_new<texture_spec>();
	desc.m_spec->m_material			= r.material;
	desc.m_spec->m_use_steep_parallax = r.steep_parallax;
	if (!r.bump_name.empty())
		desc.m_spec->m_bump_name	= r.bump_name.c_str();
}

void CTextureDescrMngr::LoadTHM(LPCSTR initial)
{
	CTimer					T;
	T.Start					();

	FS_FileSet				flist;
	FS.file_list			(flist, initial, FS_ListFiles, "*.thm");
	if (flist.empty())		return;

	// The key: where they are, and every file as the file system knows it -- no file is opened for it.
	string_path				fn;
	FS.update_path			(fn, initial, "");
	u64 key					= thm_fnv(fn, xr_strlen(fn), 0xcbf29ce484222325ull);
	const u64 dir_hash		= key;
	for (FS_FileSetIt It = flist.begin(); It != flist.end(); ++It)
	{
		FS.update_path		(fn, initial, It->name.c_str());
		const CLocatorAPI::file* F	= FS.exist(fn);
		const u32 crc		= F ? F->crc : 0;
		const u32 size		= F ? F->size_real : u32(It->size);
		const u32 modif		= F ? F->modif : u32(It->time_write);
		key					= thm_fnv(It->name.c_str(), It->name.size() + 1, key);
		key					= thm_fnv(&crc, 4, key);
		key					= thm_fnv(&size, 4, key);
		key					= thm_fnv(&modif, 4, key);
	}

	// One cache file per folder ($game_textures$, $level$), replaced whenever its key changes.
	string_path				dir, cache;
	FS.update_path			(dir, "$app_data_root$", "thm_cache\\");
	CreateDirectoryA		(dir, 0);
	xr_sprintf				(cache, "%s%016I64x.bin", dir, dir_hash);

	xr_vector<thm_record>	records;
	bool					cached = false;
	{
		xr_vector<u8>		file;
		if (thm_read_file(cache, file))
		{
			thm_reader	r	= { &file[0], &file[0] + file.size() };
			u32			magic = 0, version = 0, count = 0;
			u64			file_key = 0;
			// count is checked against the list before anything is sized by it: a damaged file is reread
			cached			= r.get(&magic, 4) && magic == thm_cache_magic
							&& r.get(&version, 4) && version == thm_cache_version
							&& r.get(&file_key, 8) && file_key == key
							&& r.get(&count, 4) && count <= flist.size();
			if (cached)
			{
				records.resize	(count);
				for (u32 i = 0; cached && i < count; ++i)
				{
					thm_record&	t	= records[i];
					u8			fl = 0;
					cached			= r.str(t.name) && r.get(&fl, 1) && r.str(t.detail_name)
									&& r.get(&t.detail_scale, 4) && r.get(&t.usage, 1)
									&& r.get(&t.material, 4) && r.str(t.bump_name);
					t.has_assoc		= !!(fl & 1);
					t.steep_parallax= !!(fl & 2);
				}
				cached			= cached && r.p == r.end;
			}
			if (!cached)		records.clear();
		}
	}

	if (!cached)
	{
		STextureParams		tp;
		for (FS_FileSetIt It = flist.begin(); It != flist.end(); ++It)
		{
			FS.update_path	(fn, initial, It->name.c_str());
			IReader* F		= FS.r_open(fn);
			xr_strcpy		(fn, It->name.c_str());
			fix_texture_thm_name(fn);

			R_ASSERT		(F->find_chunk(THM_CHUNK_TYPE));
			F->r_u32		();
			tp.Clear		();
			tp.Load			(*F);
			FS.r_close		(F);
			if (STextureParams::ttImage		!= tp.type &&
				STextureParams::ttTerrain	!= tp.type &&
				STextureParams::ttNormalMap	!= tp.type	)
				continue;

			records.push_back(thm_record());
			thm_record&	t	= records.back();
			t.name			= fn;
			t.has_assoc		= tp.detail_name.size() && tp.flags.is_any(STextureParams::flDiffuseDetail|STextureParams::flBumpDetail);
			t.detail_name	= t.has_assoc ? tp.detail_name.c_str() : "";
			t.detail_scale	= tp.detail_scale;
			t.usage			= 0;
			if (tp.flags.is(STextureParams::flDiffuseDetail))	t.usage |= (1<<0);
			if (tp.flags.is(STextureParams::flBumpDetail))		t.usage |= (1<<1);
			t.material		= tp.material + tp.material_weight;
			const bool bump	= tp.bump_mode == STextureParams::tbmUse || tp.bump_mode == STextureParams::tbmUseParallax;
			t.bump_name		= bump && tp.bump_name.size() ? tp.bump_name.c_str() : "";
			t.steep_parallax= tp.bump_mode == STextureParams::tbmUseParallax;
		}

		thm_writer			w;
		const u32 count		= u32(records.size());
		w.put				(&thm_cache_magic, 4);
		w.put				(&thm_cache_version, 4);
		w.put				(&key, 8);
		w.put				(&count, 4);
		for (u32 i = 0; i < count; ++i)
		{
			const thm_record& t	= records[i];
			const u8 fl		= u8((t.has_assoc ? 1 : 0) | (t.steep_parallax ? 2 : 0));
			w.str			(t.name);
			w.put			(&fl, 1);
			w.str			(t.detail_name);
			w.put			(&t.detail_scale, 4);
			w.put			(&t.usage, 1);
			w.put			(&t.material, 4);
			w.str			(t.bump_name);
		}
		thm_write_file		(cache, w.out);
	}

	for (u32 i = 0; i < records.size(); ++i)
		ApplyTHM			(records[i]);

	Msg						("* texture descriptions [%s]: %u .thm, %s, %u ms", initial, u32(flist.size()),
								cached ? "from cache" : "read", T.GetElapsed_ms());
}

void CTextureDescrMngr::Load()
{
	LoadTHM					("$game_textures$");
	LoadTHM					("$level$");
}

void CTextureDescrMngr::UnLoad()
{
	map_TD::iterator I = m_texture_details.begin();
	map_TD::iterator E = m_texture_details.end();
	for(;I!=E;++I)
	{
		xr_delete(I->second.m_assoc);
		xr_delete(I->second.m_spec);
	}
	m_texture_details.clear	();
}

shared_str CTextureDescrMngr::GetBumpName(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find	(tex_name);
	if (I!=m_texture_details.end())
	{
		if(I->second.m_spec)
		{
			return I->second.m_spec->m_bump_name;
		}	
	}
	return "";
}

BOOL CTextureDescrMngr::UseSteepParallax(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find	(tex_name);
	if (I!=m_texture_details.end())
	{
		if(I->second.m_spec)
		{
			return I->second.m_spec->m_use_steep_parallax;
		}	
	}
	return FALSE;
}

float CTextureDescrMngr::GetMaterial(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find	(tex_name);
	if (I!=m_texture_details.end())
	{
		if(I->second.m_spec)
		{
			return I->second.m_spec->m_material;
		}
	}
	return 1.0f;
}

void CTextureDescrMngr::GetTextureUsage	(const shared_str& tex_name, BOOL& bDiffuse, BOOL& bBump) const
{
	map_TD::const_iterator I = m_texture_details.find	(tex_name);
	if (I!=m_texture_details.end())
	{
		if(I->second.m_assoc)
		{
			u8 usage	= I->second.m_assoc->usage;
			bDiffuse	= !!(usage & (1<<0));
			bBump		= !!(usage & (1<<1));
		}	
	}
}

BOOL CTextureDescrMngr::GetDetailTexture(const shared_str& tex_name, LPCSTR& res, R_constant_setup* &CS) const
{
	map_TD::const_iterator I = m_texture_details.find	(tex_name);
	if (I!=m_texture_details.end())
	{
		if(I->second.m_assoc)
		{
                        texture_assoc* TA = I->second.m_assoc;
			res	= TA->detail_name.c_str();
			CS	= TA->cs;
			return TRUE;
		}
	}
	return FALSE;
}

