#ifndef SoundRender_SourceH
#define SoundRender_SourceH
#pragma once

#include "SoundRender_Cache.h"

#include <vorbis\vorbisfile.h>

// A distance curve of our own, given to the sound files listed in configs\sound_attenuation.ltx; every
// other file keeps OpenAL's inverse-distance rolloff untouched. Full volume up to min_distance, then a
// decay that is linear in dB (falloff_db decibels across the whole min..max span, so a smaller value
// means a flatter curve) and is pulled down to exactly zero at max_distance. The first <shoulder>
// metres past min_distance round the plateau off into the slope instead of breaking it at a corner.
struct CSoundRender_Attenuation
{
	shared_str				profile;
	float					min_distance;			// < 0: keep the one from the ogg comment
	float					max_distance;			// < 0: keep the one from the ogg comment
	float					falloff_db;
	float					shoulder;

	CSoundRender_Attenuation() : min_distance(-1.f), max_distance(-1.f), falloff_db(20.f), shoulder(0.f) {}
	float					gain					(float dist, float min_d, float max_d) const;
};

class XRSOUND_EDITOR_API 	CSoundRender_Source	: public CSound_source
{
public:
	shared_str				pname;
	shared_str				fname;
	cache_cat				CAT;

	float					fTimeTotal;
	u32						dwBytesTotal;

	WAVEFORMATEX			m_wformat;

	float					m_fBaseVolume;
	float					m_fMinDist;
	float					m_fMaxDist;
	float					m_fMaxAIDist;
	u32						m_uGameType;

	bool					m_bCustomAttenuation;	// m_Attenuation is in effect, OpenAL rolloff is off
	CSoundRender_Attenuation m_Attenuation;
private:
	OggVorbis_File			m_ovf;
	IReader*				m_wave;

	void 					i_decompress_fr			(OggVorbis_File* ovf, char* dest, u32 size);    
	void					LoadWave 				(LPCSTR name);
public:
							CSoundRender_Source		();
							~CSoundRender_Source	();

	void					load					(LPCSTR name);
    void					unload					();
	void					decompress				(u32 line, OggVorbis_File* ovf);
	
	virtual	float			length_sec				() const	{return fTimeTotal;}
	virtual u32				game_type				() const	{return m_uGameType;}
	virtual LPCSTR			file_name				() const	{return *fname;}
	virtual float			base_volume				() const	{return m_fBaseVolume;}
	virtual u16				channels_num			() const	{return m_wformat.nChannels;}
	virtual u32				bytes_total				() const	{return dwBytesTotal;}
};
#endif