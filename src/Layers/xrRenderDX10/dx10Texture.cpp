// Texture.cpp: implementation of the CTexture class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#include <D3DX10Tex.h>

#include "../xrRender/dxRenderDeviceRender.h"
#include "../../xrCore/hitch_trace.h"

// #include "std_classes.h"
// #include "xr_avi.h"

void fix_texture_name(LPSTR fn)
{
	LPSTR _ext = strext(fn);
	if(  _ext					&&
		(0==_stricmp(_ext,".tga")	||
		0==_stricmp(_ext,".dds")	||
		0==_stricmp(_ext,".bmp")	||
		0==_stricmp(_ext,".ogm")	) )
		*_ext = 0;
}

int get_texture_load_lod(LPCSTR fn)
{
	CInifile::Sect& sect	= pSettings->r_section("reduce_lod_texture_list");
	CInifile::SectCIt it_	= sect.Data.begin();
	CInifile::SectCIt it_e_	= sect.Data.end();

	CInifile::SectCIt it	= it_;
	CInifile::SectCIt it_e	= it_e_;

	for(;it!=it_e;++it)
	{
		if( strstr(fn, it->first.c_str()) )
		{
			if(psTextureLOD<1)
				return 0;
			else
				if(psTextureLOD<3)
					return 1;
				else
					return 2;
		}
	}

	if(psTextureLOD<2)
		return 0;
	else
		if(psTextureLOD<4)
			return 1;
		else
			return 2;
}

u32 calc_texture_size(int lod, u32 mip_cnt, u32 orig_size)
{
	if(1==mip_cnt)
		return orig_size;

	int _lod		= lod;
	float res		= float(orig_size);

	while(_lod>0){
		--_lod;
		res		-= res/1.333f;
	}
	return iFloor	(res);
}

const float		_BUMPHEIGH = 8.f;
//////////////////////////////////////////////////////////////////////
// Utility pack
//////////////////////////////////////////////////////////////////////
IC u32 GetPowerOf2Plus1	(u32 v)
{
	u32 cnt=0;
	while (v) {v>>=1; cnt++; };
	return cnt;
}
IC void	Reduce				(int& w, int& h, int& l, int& skip)
{
	while ((l>1) && skip)
	{
		w /= 2;
		h /= 2;
		l -= 1;

		skip--;
	}
	if (w<1)	w=1;
	if (h<1)	h=1;
}

IC void	Reduce(UINT& w, UINT& h, int l, int skip)
{
	while ((l>1) && skip)
	{
		w /= 2;
		h /= 2;
		l -= 1;

		skip--;
	}
	if (w<1)	w=1;
	if (h<1)	h=1;
}

void				TW_Save	(ID3DTexture2D* T, LPCSTR name, LPCSTR prefix, LPCSTR postfix)
{
	string256		fn;		strconcat	(sizeof(fn),fn,name,"_",prefix,"-",postfix);
	for (int it=0; it<int(xr_strlen(fn)); it++)	
		if ('\\'==fn[it])	fn[it]	= '_';
	string256		fn2;	strconcat	(sizeof(fn2),fn2,"debug\\",fn,".dds");
	Log						("* debug texture save: ",fn2);
	R_CHK					(D3DX10SaveTextureToFile(T, D3DX10_IFF_DDS, fn2));
}
/*
ID3DTexture2D*	TW_LoadTextureFromTexture
(
 ID3DTexture2D*		t_from,
 D3DFORMAT&				t_dest_fmt,
 int						levels_2_skip,
 u32&					w,
 u32&					h
 )
{
	// Calculate levels & dimensions
	ID3DTexture2D*		t_dest			= NULL;
	D3DSURFACE_DESC			t_from_desc0	;
	R_CHK					(t_from->GetLevelDesc	(0,&t_from_desc0));
	int levels_exist		= t_from->GetLevelCount();
	int top_width			= t_from_desc0.Width;
	int top_height			= t_from_desc0.Height;
	Reduce					(top_width,top_height,levels_exist,levels_2_skip);

	// Create HW-surface
	if (D3DX_DEFAULT==t_dest_fmt)	t_dest_fmt = t_from_desc0.Format;
	R_CHK					(D3DXCreateTexture(
		HW.pDevice,
		top_width,top_height,
		levels_exist,0,t_dest_fmt,
		D3DPOOL_MANAGED,&t_dest
		));

	// Copy surfaces & destroy temporary
	ID3DTexture2D* T_src= t_from;
	ID3DTexture2D* T_dst= t_dest;

	int		L_src			= T_src->GetLevelCount	()-1;
	int		L_dst			= T_dst->GetLevelCount	()-1;
	for (; L_dst>=0; L_src--,L_dst--)
	{
		// Get surfaces
		IDirect3DSurface9		*S_src, *S_dst;
		R_CHK	(T_src->GetSurfaceLevel	(L_src,&S_src));
		R_CHK	(T_dst->GetSurfaceLevel	(L_dst,&S_dst));

		// Copy
		R_CHK	(D3DXLoadSurfaceFromSurface(S_dst,NULL,NULL,S_src,NULL,NULL,D3DX_FILTER_NONE,0));

		// Release surfaces
		_RELEASE				(S_src);
		_RELEASE				(S_dst);
	}

	// OK
	w						= top_width;
	h						= top_height;
	return					t_dest;
}

template	<class _It>
IC	void	TW_Iterate_1OP
(
 ID3DTexture2D*		t_dst,
 ID3DTexture2D*		t_src,
 const _It				pred
 )
{
	DWORD mips							= t_dst->GetLevelCount();
	R_ASSERT							(mips == t_src->GetLevelCount());
	for (DWORD i = 0; i < mips; i++)	{
		D3DLOCKED_RECT				Rsrc,Rdst;
		D3DSURFACE_DESC				desc,descS;

		t_dst->GetLevelDesc			(i, &desc);
		t_src->GetLevelDesc			(i, &descS);
		VERIFY						(desc.Format==descS.Format);
		VERIFY						(desc.Format==D3DFMT_A8R8G8B8);
		t_src->LockRect				(i,&Rsrc,0,0);
		t_dst->LockRect				(i,&Rdst,0,0);
		for (u32 y = 0; y < desc.Height; y++)	{
			for (u32 x = 0; x < desc.Width; x++)	{
				DWORD&	pSrc	= *(((DWORD*)((BYTE*)Rsrc.pBits + (y * Rsrc.Pitch)))+x);
				DWORD&	pDst	= *(((DWORD*)((BYTE*)Rdst.pBits + (y * Rdst.Pitch)))+x);
				pDst			= pred(pDst,pSrc);
			}
		}
		t_dst->UnlockRect			(i);
		t_src->UnlockRect			(i);
	}
}
template	<class _It>
IC	void	TW_Iterate_2OP
(
 ID3DTexture2D*		t_dst,
 ID3DTexture2D*		t_src0,
 ID3DTexture2D*		t_src1,
 const _It				pred
 )
{
	DWORD mips							= t_dst->GetLevelCount();
	R_ASSERT							(mips == t_src0->GetLevelCount());
	R_ASSERT							(mips == t_src1->GetLevelCount());
	for (DWORD i = 0; i < mips; i++)	{
		D3DLOCKED_RECT				Rsrc0,Rsrc1,Rdst;
		D3DSURFACE_DESC				desc,descS0,descS1;

		t_dst->GetLevelDesc			(i, &desc);
		t_src0->GetLevelDesc		(i, &descS0);
		t_src1->GetLevelDesc		(i, &descS1);
		VERIFY						(desc.Format==descS0.Format);
		VERIFY						(desc.Format==descS1.Format);
		VERIFY						(desc.Format==D3DFMT_A8R8G8B8);
		t_src0->LockRect			(i,&Rsrc0,	0,0);
		t_src1->LockRect			(i,&Rsrc1,	0,0);
		t_dst->LockRect				(i,&Rdst,	0,0);
		for (u32 y = 0; y < desc.Height; y++)	{
			for (u32 x = 0; x < desc.Width; x++)	{
				DWORD&	pSrc0	= *(((DWORD*)((BYTE*)Rsrc0.pBits + (y * Rsrc0.Pitch)))+x);
				DWORD&	pSrc1	= *(((DWORD*)((BYTE*)Rsrc1.pBits + (y * Rsrc1.Pitch)))+x);
				DWORD&	pDst	= *(((DWORD*)((BYTE*)Rdst.pBits  + (y * Rdst.Pitch)))+x);
				pDst			= pred(pDst,pSrc0,pSrc1);
			}
		}
		t_dst->UnlockRect			(i);
		t_src0->UnlockRect			(i);
		t_src1->UnlockRect			(i);
	}
}

IC u32 it_gloss_rev		(u32 d, u32 s)	{	return	color_rgba	(
	color_get_A(s),		// gloss
	color_get_B(d),
	color_get_G(d),
	color_get_R(d)		);
}
IC u32 it_gloss_rev_base(u32 d, u32 s)	{	
	u32		occ		= color_get_A(d)/3;
	u32		def		= 8;
	u32		gloss	= (occ*1+def*3)/4;
	return	color_rgba	(
		gloss,			// gloss
		color_get_B(d),
		color_get_G(d),
		color_get_R(d)
		);
}
IC u32 it_difference	(u32 d, u32 orig, u32 ucomp)	{	return	color_rgba(
	128+(int(color_get_R(orig))-int(color_get_R(ucomp)))*2,		// R-error
	128+(int(color_get_G(orig))-int(color_get_G(ucomp)))*2,		// G-error
	128+(int(color_get_B(orig))-int(color_get_B(ucomp)))*2,		// B-error
	128+(int(color_get_A(orig))-int(color_get_A(ucomp)))*2	);	// A-error	
}
IC u32 it_height_rev	(u32 d, u32 s)	{	return	color_rgba	(
	color_get_A(d),					// diff x
	color_get_B(d),					// diff y
	color_get_G(d),					// diff z
	color_get_R(s)	);				// height
}
IC u32 it_height_rev_base(u32 d, u32 s)	{	return	color_rgba	(
	color_get_A(d),					// diff x
	color_get_B(d),					// diff y
	color_get_G(d),					// diff z
	(color_get_R(s)+color_get_G(s)+color_get_B(s))/3	);	// height
}
*/
// Direct upload of a plain .dds: header parsed here, every mip handed to CreateTexture2D as-is.
// Measured in game: D3DX10 + the staging copy cost ~1.5 ms a texture (41 ms for an uncompressed
// 8 MB bump, which D3DX converts BGRA->RGBA texel by texel), and lazy textures come in bursts of
// 20-40 when an NPC with a fully kitted weapon walks into view. Only the cases that are a straight
// copy are handled -- DXT1/3/5 and 32-bit A8R8G8B8 -- anything else (cubes, volumes, DX10 headers,
// other formats, a LOD-reduced load, sizes D3D10 rejects) returns 0 and takes the D3DX path as before.
namespace
{
#pragma pack(push,1)
	struct dds_pixelformat	{ u32 size, flags, fourcc, rgb_bits, r_mask, g_mask, b_mask, a_mask; };
	struct dds_header		{ u32 size, flags, height, width, pitch, depth, mips, reserved1[11];
							  dds_pixelformat pf; u32 caps, caps2, caps3, caps4, reserved2; };
#pragma pack(pop)

	bool bgra_sampling_supported()
	{
		static int	s_state	= -1;
		if (s_state < 0)
		{
			UINT	support	= 0;
			s_state			= SUCCEEDED(HW.pDevice->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &support))
				&& (support & D3D10_FORMAT_SUPPORT_TEXTURE2D) && (support & D3D10_FORMAT_SUPPORT_SHADER_SAMPLE) ? 1 : 0;
		}
		return s_state == 1;
	}

	ID3DBaseTexture* dds_create_direct(const void* data, u32 size)
	{
		if (size < 4 + sizeof(dds_header))							return 0;
		const u8*			p	= (const u8*)data;
		if (*(const u32*)p != MAKEFOURCC('D','D','S',' '))			return 0;
		const dds_header&	h	= *(const dds_header*)(p + 4);
		if (h.size != 124 || h.pf.size != 32)						return 0;
		if (h.caps2 & (0x200 | 0x200000))							return 0;	// cubemap / volume

		DXGI_FORMAT			fmt;
		u32					block	= 0;	// bytes per 4x4 block, compressed formats
		u32					texel	= 0;	// bytes per texel, uncompressed
		if (h.pf.flags & 0x4)										// DDPF_FOURCC
		{
			switch (h.pf.fourcc)
			{
			case MAKEFOURCC('D','X','T','1'):	fmt = DXGI_FORMAT_BC1_UNORM;	block = 8;	break;
			case MAKEFOURCC('D','X','T','3'):	fmt = DXGI_FORMAT_BC2_UNORM;	block = 16;	break;
			case MAKEFOURCC('D','X','T','5'):	fmt = DXGI_FORMAT_BC3_UNORM;	block = 16;	break;
			default:							return 0;
			}
		}
		else if ((h.pf.flags & 0x40) && (h.pf.flags & 0x1) && h.pf.rgb_bits == 32	// DDPF_RGB | DDPF_ALPHAPIXELS
			&& h.pf.r_mask == 0x00ff0000 && h.pf.g_mask == 0x0000ff00 && h.pf.b_mask == 0x000000ff
			&& h.pf.a_mask == 0xff000000)
		{
			if (!bgra_sampling_supported())							return 0;
			fmt		= DXGI_FORMAT_B8G8R8A8_UNORM;
			texel	= 4;
		}
		else														return 0;

		const u32	width	= h.width, height = h.height;
		if (!width || !height)										return 0;
		if (block && ((width & 3) || (height & 3)))					return 0;	// D3D10 wants BC tops in 4x4 blocks
		const u32	mips	= ((h.flags & 0x20000) && h.mips) ? h.mips : 1;	// DDSD_MIPMAPCOUNT
		if (mips > 16)												return 0;

		D3D10_SUBRESOURCE_DATA	sub[16];
		const u8*	cur		= p + 4 + sizeof(dds_header);
		const u8*	end		= p + size;
		u32			mw		= width, mh = height;
		for (u32 i = 0; i < mips; ++i)
		{
			const u32 pitch	= block ? _max(1u, (mw + 3) / 4) * block : mw * texel;
			const u32 rows	= block ? _max(1u, (mh + 3) / 4) : mh;
			const u32 bytes	= pitch * rows;
			if (u32(end - cur) < bytes)								return 0;	// truncated file: let D3DX judge it
			sub[i].pSysMem			= cur;
			sub[i].SysMemPitch		= pitch;
			sub[i].SysMemSlicePitch	= bytes;
			cur				+= bytes;
			mw				= _max(1u, mw / 2);
			mh				= _max(1u, mh / 2);
		}

		D3D10_TEXTURE2D_DESC	desc;
		desc.Width				= width;
		desc.Height				= height;
		desc.MipLevels			= mips;
		desc.ArraySize			= 1;
		desc.Format				= fmt;
		desc.SampleDesc.Count	= 1;
		desc.SampleDesc.Quality	= 0;
		desc.Usage				= D3D10_USAGE_DEFAULT;
		desc.BindFlags			= D3D10_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags		= 0;
		desc.MiscFlags			= 0;
		ID3DTexture2D*			T	= 0;
		if (FAILED(HW.pDevice->CreateTexture2D(&desc, sub, &T)))	return 0;
		return					T;
	}
}

ID3DBaseTexture*	CRender::texture_load(LPCSTR fRName, u32& ret_msize, bool bStaging)
{
	//	Moved here just to avoid warning
	D3DX10_IMAGE_INFO			IMG;
	ZeroMemory(&IMG, sizeof(IMG));

	//	Staging control
	static bool bAllowStaging = !strstr(Core.Params,"-no_staging");
	bStaging &= bAllowStaging;

	ID3DBaseTexture*		pTexture2D		= NULL;
	//IDirect3DCubeTexture9*	pTextureCUBE	= NULL;
	string_path				fn;
	//u32						dwWidth,dwHeight;
	u32						img_size		= 0;
	int						img_loaded_lod	= 0;
	//D3DFORMAT				fmt;
	u32						mip_cnt=u32(-1);
	// validation
	R_ASSERT				(fRName);
	R_ASSERT				(fRName[0]);

	// make file name
	string_path				fname;
	xr_strcpy(fname,fRName); //. andy if (strext(fname)) *strext(fname)=0;
	fix_texture_name		(fname);
	IReader* S				= NULL;
	// The hitch tracer splits a load into these zones (an in-game load measured 5-10 ms where the
	// same file takes ~1 ms in a bare D3D10 program -- this says which part is the difference).
	int		where			= 0;
	{
		hitch::zone			hz("tex/fs_exist");
		if (!FS.exist(fn,"$game_textures$",	fname,	".dds")	&& strstr(fname,"_bump"))	where = 1;
		else if (FS.exist(fn,"$level$",			fname,	".dds"))					where = 2;
		else if (FS.exist(fn,"$game_saves$",		fname,	".dds"))				where = 2;
		else if (FS.exist(fn,"$game_textures$",	fname,	".dds"))					where = 2;
	}
	if (where==1)			goto _BUMP_from_base;
	if (where==2)			goto _DDS;


#ifdef _EDITOR
	ELog.Msg(mtError,"Can't find texture '%s'",fname);
	return 0;
#else

	Msg("! Can't find texture '%s'",fname);
	R_ASSERT(FS.exist(fn,"$game_textures$",	"ed\\ed_not_existing_texture",".dds"));
	goto _DDS;

	//	Debug.fatal(DEBUG_INFO,"Can't find texture '%s'",fname);

#endif

_DDS:
	{
		// Load and get header

		{
			hitch::zone			hz("tex/r_open");
			S					= FS.r_open	(fn);
		}
#ifdef DEBUG
		Msg						("* Loaded: %s[%d]b",fn,S->length());
#endif // DEBUG
		img_size				= S->length	();
		R_ASSERT				(S);
		//R_CHK2					(D3DXGetImageInfoFromFileInMemory	(S->pointer(),S->length(),&IMG), fn);
		{
			hitch::zone			hz("tex/d3dx_info");
			R_CHK2 (D3DX10GetImageInfoFromMemory(S->pointer(),S->length(), 0, &IMG, 0), fn);
		}
		//if (IMG.ResourceType	== D3DRTYPE_CUBETEXTURE)			goto _DDS_CUBE;
		if (IMG.MiscFlags & D3D10_RESOURCE_MISC_TEXTURECUBE)			goto _DDS_CUBE;
		else														goto _DDS_2D;

_DDS_CUBE:
		{
			//R_CHK(D3DXCreateCubeTextureFromFileInMemoryEx(
			//	HW.pDevice,
			//	S->pointer(),S->length(),
			//	D3DX_DEFAULT,
			//	IMG.MipLevels,0,
			//	IMG.Format,
			//	D3DPOOL_MANAGED,
			//	D3DX_DEFAULT,
			//	D3DX_DEFAULT,
			//	0,&IMG,0,
			//	&pTextureCUBE
			//	));

			//	Inited to default by provided default constructor
			D3DX10_IMAGE_LOAD_INFO LoadInfo;
			//LoadInfo.Usage = D3D10_USAGE_IMMUTABLE;
			if (bStaging)
			{
				LoadInfo.Usage = D3D10_USAGE_STAGING;
				LoadInfo.BindFlags = 0;
				LoadInfo.CpuAccessFlags = D3D10_CPU_ACCESS_WRITE;
			}
			else
			{
				LoadInfo.Usage = D3D10_USAGE_DEFAULT;
				LoadInfo.BindFlags = D3D10_BIND_SHADER_RESOURCE;
			}
			
			LoadInfo.pSrcInfo = &IMG;

			R_CHK(D3DX10CreateTextureFromMemory(
				HW.pDevice,
				S->pointer(),S->length(),
				&LoadInfo,
				0,
				&pTexture2D,
				0
				));

			FS.r_close				(S);

			// OK
			mip_cnt					= IMG.MipLevels;
			ret_msize				= calc_texture_size(img_loaded_lod, mip_cnt, img_size);
			return					pTexture2D;
		}
_DDS_2D:
		{
			// Check for LMAP and compress if needed
			_strlwr					(fn);


			// Load   SYS-MEM-surface, bound to device restrictions
			//ID3DTexture2D*		T_sysmem;
			//R_CHK2(D3DXCreateTextureFromFileInMemoryEx
			//	(
			//	HW.pDevice,S->pointer(),S->length(),
			//	D3DX_DEFAULT,D3DX_DEFAULT,
			//	IMG.MipLevels,0,
			//	IMG.Format,
			//	D3DPOOL_SYSTEMMEM,
			//	D3DX_DEFAULT,
			//	D3DX_DEFAULT,
			//	0,&IMG,0,
			//	&T_sysmem
			//	), fn);

			{
				hitch::zone			hz("tex/load_lod");
				img_loaded_lod		= get_texture_load_lod(fn);
			}

			if (!img_loaded_lod)
			{
				{
					hitch::zone		hz("tex/direct_create");
					pTexture2D		= dds_create_direct(S->pointer(), S->length());
				}
				if (pTexture2D)
				{
					hitch::zone		hz("tex/r_close");
					FS.r_close		(S);
					mip_cnt			= IMG.MipLevels;
					ret_msize		= calc_texture_size(img_loaded_lod, mip_cnt, img_size);
					return			pTexture2D;
				}
			}

			//	Inited to default by provided default constructor
			D3DX10_IMAGE_LOAD_INFO LoadInfo;
			//LoadInfo.FirstMipLevel = img_loaded_lod;
			LoadInfo.MipLevels = IMG.MipLevels;
			LoadInfo.Width	= IMG.Width;
			LoadInfo.Height	= IMG.Height;

			if (img_loaded_lod)
			{
				Reduce(LoadInfo.Width, LoadInfo.Height, IMG.MipLevels, img_loaded_lod);
				// BUGFIX: Reduce() shrinks W/H but takes the mip count by VALUE, so LoadInfo.MipLevels kept the
				// ORIGINAL count. A LOD-reduced texture (e.g. 2048^2/12mips -> 512^2) was then asked for 12 mips,
				// which exceeds the max for the smaller size -> D3DX10CreateTextureFromMemory fails and the engine
				// FATALs. Hit any large full-mip texture whenever LOD reduction is on -- e.g. re-uploading on the
				// device reset that "Apply video settings" triggers (crash on wpn_m203.dds). Clamp mips to the size.
				UINT maxdim = (LoadInfo.Width > LoadInfo.Height) ? LoadInfo.Width : LoadInfo.Height;
				UINT maxmip = 1; while (maxdim > 1) { maxdim >>= 1; ++maxmip; }
				if (LoadInfo.MipLevels > maxmip)	LoadInfo.MipLevels = maxmip;
			}

			//LoadInfo.Usage = D3D10_USAGE_IMMUTABLE;
			if (bStaging)
			{
				LoadInfo.Usage = D3D10_USAGE_STAGING;
				LoadInfo.BindFlags = 0;
				LoadInfo.CpuAccessFlags = D3D10_CPU_ACCESS_WRITE;
			}
			else
			{
				LoadInfo.Usage = D3D10_USAGE_DEFAULT;
				LoadInfo.BindFlags = D3D10_BIND_SHADER_RESOURCE;
			}
			LoadInfo.pSrcInfo = &IMG;

			{
				hitch::zone			hz(bStaging ? "tex/d3dx_create_staging" : "tex/d3dx_create");
				R_CHK2(D3DX10CreateTextureFromMemory
					(
					HW.pDevice,S->pointer(),S->length(),
					&LoadInfo,
					0,
					&pTexture2D,
					0
					), fn);
			}
			{
				hitch::zone			hz("tex/r_close");
				FS.r_close			(S);
			}
			mip_cnt					= IMG.MipLevels;
			// OK
			ret_msize				= calc_texture_size(img_loaded_lod, mip_cnt, img_size);
			return					pTexture2D;
		}
	}

_BUMP_from_base:
	{
		//Msg			("! auto-generated bump map: %s",fname);
		Msg			("! Fallback to default bump map: %s",fname);
		//////////////////
		if (strstr(fname,"_bump#"))			
		{
			R_ASSERT2	(FS.exist(fn,"$game_textures$",	"ed\\ed_dummy_bump#",	".dds"), "ed_dummy_bump#");
			S						= FS.r_open	(fn);
			R_ASSERT2				(S, fn);
			img_size				= S->length	();
			goto		_DDS_2D;
		}
		if (strstr(fname,"_bump"))			
		{
			R_ASSERT2	(FS.exist(fn,"$game_textures$",	"ed\\ed_dummy_bump",	".dds"),"ed_dummy_bump");
			S						= FS.r_open	(fn);

			R_ASSERT2	(S, fn);

			img_size				= S->length	();
			goto		_DDS_2D;
		}
		//////////////////
	}

	return 0;
}
