#include "stdafx.h"
#pragma hdrstop

#include <msacm.h>

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"
#include "ogg_utils.h"
#include "../xrCore/hitch_trace.h"

void CSoundRender_Source::decompress(u32 line, OggVorbis_File* ovf)
{
	VERIFY	(ovf);
	// decompression of one cache-line
	u32		line_size		= SoundRender->cache.get_linesize();
	char*	dest			= (char*)SoundRender->cache.get_dataptr	(CAT,line);
	u32		buf_offs		= (line*line_size) / 2 / m_wformat.nChannels;
	u32		left_file		= dwBytesTotal - buf_offs;
	u32		left			= (u32)_min	(left_file,line_size);

	// seek
	u32	cur_pos				= u32(ov_pcm_tell(ovf));
	if (cur_pos!=buf_offs)
		ov_pcm_seek			(ovf,buf_offs);

	// decompress
	i_decompress_fr(ovf,dest,left);
}

void CSoundRender_Source::LoadWave	(LPCSTR pName)
{
	pname					= pName;
	ZeroMemory				(&m_wformat, sizeof(WAVEFORMATEX));

	// Only the description is read here. Playing a sound opens the file again with its own decoder
	// (CSoundRender_Target::attach), so the source used to keep a mapped file and a live decoder per
	// sound for nothing -- thousands of them by mid-game, in a 32-bit process. Read and let go.
	m_wave					= 0;
	IReader* wave			= FS.r_open(pname.c_str());
	R_ASSERT3				(wave && wave->length(),"Can't open wave file:",pname.c_str());

	ogg_quick_info			info;
	if (!ogg_quick_read((const u8*)wave->pointer(), wave->length(), info))
	{
		// Anything the page reader does not expect goes through the full decoder, as it always did.
		ov_callbacks			ovc;
		ovc.read_func			= ov_read_func;
		ovc.seek_func			= ov_seek_func;
		ovc.close_func			= ov_close_func;
		ovc.tell_func			= ov_tell_func;
		OggVorbis_File			ovf;
		ov_open_callbacks		(wave, &ovf, NULL, 0, ovc);

		vorbis_info* ovi		= ov_info(&ovf, -1);
		R_ASSERT3				(ovi, "Invalid source info:", pname.c_str());
		info.channels			= ovi->channels;
		info.rate				= ovi->rate;
		info.pcm_total			= ov_pcm_total(&ovf,-1);
		vorbis_comment*	ovm		= ov_comment(&ovf,-1);
		info.has_comment		= ovm->comments > 0;
		info.comment_len		= info.has_comment ? _min(u32(ovm->comment_lengths[0]), u32(sizeof(info.comment))) : 0;
		if (info.comment_len)	CopyMemory(info.comment, ovm->user_comments[0], info.comment_len);
		ov_clear				(&ovf);
	}
	FS.r_close				(wave);

	// verify
	R_ASSERT3				(info.rate==44100, "Invalid source rate:", pname.c_str());

#ifdef DEBUG
	if(info.channels==2)
	{
		Msg("stereo sound source [%s]", pname.c_str());
	}
#endif // #ifdef DEBUG

	m_wformat.nSamplesPerSec	= (info.rate); //44100;
	m_wformat.wFormatTag		= WAVE_FORMAT_PCM;
	m_wformat.nChannels			= u16(info.channels);
	m_wformat.wBitsPerSample	= 16;

	m_wformat.nBlockAlign		= (m_wformat.nChannels * m_wformat.wBitsPerSample) / 8;
	m_wformat.nAvgBytesPerSec	= m_wformat.nSamplesPerSec * m_wformat.nBlockAlign;

	s64 pcm_total				= info.pcm_total;
	dwBytesTotal				= u32(pcm_total*m_wformat.nBlockAlign); 
	fTimeTotal					= s_f_def_source_footer + dwBytesTotal/float(m_wformat.nAvgBytesPerSec);

	if (info.has_comment)
	{
		IReader F			(info.comment,info.comment_len);
		u32 vers			= F.r_u32	();
        if (vers==0x0001){
			m_fMinDist		= F.r_float	();
			m_fMaxDist		= F.r_float	();
	        m_fBaseVolume	= 1.0f;
			m_uGameType		= F.r_u32	();
			m_fMaxAIDist	= m_fMaxDist;
		}else if (vers==0x0002){
			m_fMinDist		= F.r_float	();
			m_fMaxDist		= F.r_float	();
			m_fBaseVolume	= F.r_float	();
			m_uGameType		= F.r_u32	();
			m_fMaxAIDist	= m_fMaxDist;
		}else if (vers==OGG_COMMENT_VERSION){
			m_fMinDist		= F.r_float	();
			m_fMaxDist		= F.r_float	();
            m_fBaseVolume	= F.r_float	();
			m_uGameType		= F.r_u32	();
			m_fMaxAIDist	= F.r_float	();
		}else{
			Log				("! Invalid ogg-comment version, file: ", pname.c_str());
		}
	}else{
		Log					("! Missing ogg-comment, file: ", pname.c_str());
	}
	R_ASSERT3((m_fMaxAIDist>=0.1f)&&(m_fMaxDist>=0.1f),"Invalid max distance.", pname.c_str());
}

void CSoundRender_Source::load(LPCSTR name)
{
	hitch::load			hitch_load("sound", name);
	string_path			fn,N;
	xr_strcpy				(N,name);
	_strlwr				(N);
	if (strext(N))		*strext(N) = 0;

	fname				= N;

	strconcat			(sizeof(fn),fn,N,".ogg");
	if (!FS.exist("$level$",fn))	FS.update_path	(fn,"$game_sounds$",fn);

	if (!FS.exist(fn)) { 
		FS.update_path	(fn,"$game_sounds$","$no_sound.ogg");
		Msg("! Can't find sound '%s'", fname.c_str());
    }

	LoadWave			(fn);
	SoundRender->cache.cat_create	(CAT, dwBytesTotal);

	// the curve's own distances, when it gives them, win over the ogg comment's (AI hearing range stays)
	if (const CSoundRender_Attenuation* A = SoundRender->atten_find(*fname))
	{
		const float	min_d		= (A->min_distance>=0.f) ? A->min_distance : m_fMinDist;
		const float	max_d		= (A->max_distance>0.f)  ? A->max_distance : m_fMaxDist;
		if (max_d>min_d)
		{
			m_bCustomAttenuation= true;
			m_Attenuation		= *A;
			m_fMinDist			= min_d;
			m_fMaxDist			= max_d;
		}
		else
			Msg					("! SOUND: [%s] max_distance %.1f <= min_distance %.1f for '%s', curve ignored", *A->profile, max_d, min_d, *fname);
	}
}

void CSoundRender_Source::unload()
{
	SoundRender->cache.cat_destroy	(CAT);
    fTimeTotal						= 0.0f;
    dwBytesTotal					= 0;

	if (m_wave) {
		ov_clear(&m_ovf);
		FS.r_close(m_wave);
	}
}

