#include "stdafx.h"

#include "HudSound.h"

float psHUDSoundVolume			= 1.0f;
void InitHudSoundSettings()
{
	psHUDSoundVolume		= pSettings->r_float("hud_sound", "hud_sound_vol_k");
}

// GS (wpnpatch, WeaponSoundLoader.pas) moved the real volume out of the sound line and into a
// separate per-alias key, in percent: "volume_snd_silncer_shot = 80". Absent = full volume.
static float LoadSndVolume(LPCSTR section, LPCSTR line)
{
	string256					volume_line;
	strconcat					(sizeof(volume_line),volume_line,"volume_",line);
	if (!pSettings->line_exist(section,volume_line))
		return					(1.0f);

	int							volume = pSettings->r_s32(section,volume_line);
	clamp						(volume, 0, 200);
	return						(float(volume) / 100.0f);
}

void HUD_SOUND_ITEM::LoadSound(	LPCSTR section, LPCSTR line,
							HUD_SOUND_ITEM& hud_snd, int type)
{
	hud_snd.m_activeSnd		= NULL;
	hud_snd.sounds.clear	();
	hud_snd.m_volume		= LoadSndVolume(section, line);

	string256	sound_line;
	xr_strcpy		(sound_line,line);
	int k=0;
	while( pSettings->line_exist(section, sound_line) ){
		hud_snd.sounds.push_back( SSnd() );
		SSnd& s = hud_snd.sounds.back();

		LoadSound	(section, sound_line, s.snd, type, &s.unlock_freq, &s.delay);
		xr_sprintf		(sound_line,"%s%d",line,++k);
	}//while
}

void  HUD_SOUND_ITEM::LoadSound(LPCSTR section,
								LPCSTR line,
								ref_sound& snd,
								int type,
								float* unlock_freq,
								float* delay)
{
	LPCSTR str = pSettings->r_string(section, line);
	string256 buf_str;

	int	count = _GetItemCount	(str);
	R_ASSERT(count);

	_GetItem(str, 0, buf_str);
	snd.create(buf_str, st_Effect,type);


	if(unlock_freq != NULL)
	{
		*unlock_freq = 1.f;
		if(count>1)
		{
			_GetItem (str, 1, buf_str);
			if(xr_strlen(buf_str)>0)
				*unlock_freq = (float)atof(buf_str);
		}
	}

	if(delay != NULL)
	{
		*delay = 0;
		if(count>2)
		{
			_GetItem (str, 2, buf_str);
			if(xr_strlen(buf_str)>0)
				*delay = (float)atof(buf_str);
		}
	}
}

void HUD_SOUND_ITEM::DestroySound(HUD_SOUND_ITEM& hud_snd)
{
	xr_vector<SSnd>::iterator it = hud_snd.sounds.begin();
	for(;it!=hud_snd.sounds.end();++it)
		(*it).snd.destroy();
	hud_snd.sounds.clear	();
	
	hud_snd.m_activeSnd		= NULL;
}

void HUD_SOUND_ITEM::PlaySound(	HUD_SOUND_ITEM&		hud_snd,
								const Fvector&	position,
								const CObject*	parent,
								bool			b_hud_mode,
								bool			looped,
								u8 index,
								bool			b_force_unlock)
{
	if (hud_snd.sounds.empty())	return;

	u32 flags = b_hud_mode?sm_2D:0;
	if(looped)
		flags |= sm_Looped;

	if(index==u8(-1))
		index = (u8)Random.randI(hud_snd.sounds.size());

	SSnd&		s			= hud_snd.sounds[ index ];

	// The second number on the config line, read GS's way (WeaponSoundLoader.pas): the sign says
	// whether the sound is unlocked, the modulus is the pitch spread -- and only when it is over
	// 1.0, so -0.9 means "unlock, leave the pitch alone" and -1.1 means "unlock, +-10% pitch".
	const float	freq_eps	= 0.001f;		// GS's own threshold
	const float	deviation	= _abs(s.unlock_freq);
	const bool	vary_freq	= (deviation - 1.0f > freq_eps);
	const bool	unlocked	= (s.unlock_freq < 0.0f) || b_force_unlock;

	float		freq		= 1.0f;
	if (vary_freq)
	{
		float	delta		= deviation - 1.0f;
		if (delta > 0.9f)	delta = 0.9f;
		freq				= 1.0f + Random.randF(-delta, delta);
	}

	float		volume		= hud_snd.m_volume * (b_hud_mode?psHUDSoundVolume:1.0f);

	// A locked sound keeps the one shared object per alias: it can be stopped, moved and re-tuned
	// afterwards, but starting it again cuts whatever it was playing -- which is why a burst used to
	// sound like a single shot being retriggered. An unlocked one is a detached instance, so several
	// ring at once; the price is that there is no handle at all, hence no m_activeSnd bookkeeping and
	// no way to stop it. That rules it out for anything looped or exclusive.
	if (!unlocked || hud_snd.m_b_exclusive || looped)
	{
		hud_snd.m_activeSnd		= NULL;
		StopSound				(hud_snd);

		hud_snd.m_activeSnd		= &s;
		s.snd.play_at_pos		(	const_cast<CObject*>(parent),
									flags&sm_2D?Fvector().set(0,0,0):position,
									flags,
									s.delay);

		s.snd.set_volume		(volume);
		if (vary_freq)
			s.snd.set_frequency	(freq);
	}
	else
	{
		Fvector	pos				= (flags&sm_2D) ? Fvector().set(0,0,0) : position;
		s.snd.play_no_feedback	(const_cast<CObject*>(parent), flags, s.delay, &pos, &volume, vary_freq?&freq:NULL);
	}
}

void HUD_SOUND_ITEM::StopSound(HUD_SOUND_ITEM& hud_snd)
{
	xr_vector<SSnd>::iterator it = hud_snd.sounds.begin();
	for(;it!=hud_snd.sounds.end();++it)
		(*it).snd.stop		();
	hud_snd.m_activeSnd		= NULL;
}

//----------------------------------------------------------
HUD_SOUND_COLLECTION::~HUD_SOUND_COLLECTION()
{
	xr_vector<HUD_SOUND_ITEM>::iterator it		= m_sound_items.begin();
	xr_vector<HUD_SOUND_ITEM>::iterator it_e	= m_sound_items.end();

	for(;it!=it_e;++it)
	{
		HUD_SOUND_ITEM::StopSound		(*it);
		HUD_SOUND_ITEM::DestroySound	(*it);
	}

	m_sound_items.clear();
}

HUD_SOUND_ITEM* HUD_SOUND_COLLECTION::FindSoundItem(LPCSTR alias, bool b_assert)
{
	xr_vector<HUD_SOUND_ITEM>::iterator it	= std::find(m_sound_items.begin(),m_sound_items.end(),alias);
	
	if(it!=m_sound_items.end())
		return &*it;
	else{
		R_ASSERT3(!b_assert,"sound item not found in collection", alias);
		return NULL;
	}
}

void HUD_SOUND_COLLECTION::PlaySound(	LPCSTR alias, 
										const Fvector& position,
										const CObject* parent,
										bool hud_mode,
										bool looped,
										u8 index,
										bool b_force_unlock)
{
	xr_vector<HUD_SOUND_ITEM>::iterator it		= m_sound_items.begin();
	xr_vector<HUD_SOUND_ITEM>::iterator it_e	= m_sound_items.end();
	for(;it!=it_e;++it)
	{
		if(it->m_b_exclusive)
			HUD_SOUND_ITEM::StopSound	(*it);
	}


	HUD_SOUND_ITEM* snd_item		= FindSoundItem(alias, true);
	HUD_SOUND_ITEM::PlaySound		(*snd_item, position, parent, hud_mode, looped, index, b_force_unlock);
}

void HUD_SOUND_COLLECTION::StopSound(LPCSTR alias)
{
	HUD_SOUND_ITEM* snd_item		= FindSoundItem(alias, true);
	HUD_SOUND_ITEM::StopSound		(*snd_item);
}

void HUD_SOUND_COLLECTION::SetPosition(LPCSTR alias, const Fvector& pos)
{
	HUD_SOUND_ITEM* snd_item		= FindSoundItem(alias, true);
	if(snd_item->playing())
		snd_item->set_position		(pos);
}

void HUD_SOUND_COLLECTION::RemoveSounds(LPCSTR alias_prefix)
{
	if (!alias_prefix || !alias_prefix[0])	return;
	const size_t len = xr_strlen(alias_prefix);
	for (xr_vector<HUD_SOUND_ITEM>::iterator it = m_sound_items.begin(); it != m_sound_items.end(); )
	{
		if (it->m_alias.size() && 0 == strncmp(it->m_alias.c_str(), alias_prefix, len))
		{
			HUD_SOUND_ITEM::StopSound	(*it);
			it = m_sound_items.erase	(it);
		}
		else
			++it;
	}
}

void HUD_SOUND_COLLECTION::StopAllSounds()
{
	xr_vector<HUD_SOUND_ITEM>::iterator it		= m_sound_items.begin();
	xr_vector<HUD_SOUND_ITEM>::iterator it_e	= m_sound_items.end();

	for(;it!=it_e;++it)
	{
		HUD_SOUND_ITEM::StopSound	(*it);
	}
}

void HUD_SOUND_COLLECTION::LoadSound(	LPCSTR section, 
										LPCSTR line,
										LPCSTR alias,
										bool exclusive,
										int type)
{
	R_ASSERT					(NULL==FindSoundItem(alias, false));
	m_sound_items.resize		(m_sound_items.size()+1);
	HUD_SOUND_ITEM& snd_item	= m_sound_items.back();
	HUD_SOUND_ITEM::LoadSound	(section, line, snd_item, type);
	snd_item.m_alias			= alias;
	snd_item.m_b_exclusive		= exclusive;
}
