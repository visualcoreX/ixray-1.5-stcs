#include "stdafx.h"

#include "HudSound.h"
#include "Level.h"
#include "../xrEngine/gamemtllib.h"

// [indoor_sound] in system.ltx; every key optional
static struct SIndoorSoundParams
{
	float		ray_up;			// how high a roof may be
	float		ray_side;		// how far a wall may be
	int			side_rays;		// horizontal rays, spread evenly around
	float		side_hits;		// share of them that must hit a wall
	float		cache_time;		// seconds an answer is reused for...
	float		cache_dist;		// ...by a sound made this close
} s_indoor;

float psHUDSoundVolume			= 1.0f;
void InitHudSoundSettings()
{
	psHUDSoundVolume		= pSettings->r_float("hud_sound", "hud_sound_vol_k");

	s_indoor.ray_up			= READ_IF_EXISTS(pSettings, r_float, "indoor_sound", "ray_up",		25.f);
	s_indoor.ray_side		= READ_IF_EXISTS(pSettings, r_float, "indoor_sound", "ray_side",	25.f);
	s_indoor.side_rays		= READ_IF_EXISTS(pSettings, r_s32,	 "indoor_sound", "side_rays",	8);
	s_indoor.side_hits		= READ_IF_EXISTS(pSettings, r_float, "indoor_sound", "side_hits",	0.6f);
	s_indoor.cache_time		= READ_IF_EXISTS(pSettings, r_float, "indoor_sound", "cache_time",	0.5f);
	s_indoor.cache_dist		= READ_IF_EXISTS(pSettings, r_float, "indoor_sound", "cache_dist",	1.0f);
	clamp					(s_indoor.side_rays, 0, 32);
	clamp					(s_indoor.side_hits, 0.f, 1.f);
}

// the first solid triangle ends the ray; bushes, nets and the like let sound through and are skipped
static BOOL indoor_ray_callback(collide::rq_result& result, LPVOID params)
{
	CDB::TRI*	T			= Level().ObjectSpace.GetStaticTris()+result.element;
	if (GMLib.GetMaterialByIdx(T->material)->Flags.is(SGameMtl::flPassable))
		return				TRUE;
	*(bool*)params			= true;
	return					FALSE;
}

static bool indoor_ray(const Fvector& from, const Fvector& dir, float range)
{
	bool					hit = false;
	collide::ray_defs		RD(from, dir, range, 0, collide::rqtStatic);	// both faces: a roof may be single-sided
	collide::rq_results		RQR;
	Level().ObjectSpace.RayQuery(RQR, RD, indoor_ray_callback, &hit, NULL, NULL);
	return					hit;
}

// Indoors = a roof straight above AND walls in most directions around. The roof alone is a shed or
// an awning, and walls alone a yard between buildings; either still sounds like the open air. Only
// the static level geometry counts, so neither actors nor dropped items make a room.
bool IndoorSoundTest(const Fvector& pos)
{
	if (!g_pGameLevel)		return false;

	struct SEntry { Fvector pos; float time; bool indoor; };
	static SEntry			cache[8];
	static u32				cache_next = 0;
	const float				now = Device.fTimeGlobal;
	for (u32 i=0; i<8; ++i)
	{
		const SEntry& e		= cache[i];
		if (e.time>0.f && now>=e.time && now-e.time<s_indoor.cache_time && e.pos.distance_to_sqr(pos)<_sqr(s_indoor.cache_dist))
			return			e.indoor;
	}

	bool					indoor = indoor_ray(pos, Fvector().set(0.f,1.f,0.f), s_indoor.ray_up);
	if (indoor && s_indoor.side_rays>0)
	{
		const int			need = iCeil(s_indoor.side_hits*float(s_indoor.side_rays)-EPS_L);
		int					hits = 0;
		for (int i=0; i<s_indoor.side_rays; ++i)
		{
			const float		a = PI_MUL_2*float(i)/float(s_indoor.side_rays);
			if (indoor_ray(pos, Fvector().set(_cos(a),0.f,_sin(a)), s_indoor.ray_side))
				++hits;
			if (hits>=need || hits+(s_indoor.side_rays-1-i)<need)	// decided either way
				break;
		}
		indoor				= (hits>=need);
	}

	SEntry& e				= cache[cache_next];
	cache_next				= (cache_next+1)%8;
	e.pos					= pos;
	e.time					= now;
	e.indoor				= indoor;
	return					indoor;
}

// GS (wpnpatch, WeaponSoundLoader.pas) moved the real volume out of the sound line and into a
// separate per-alias key, in percent: "volume_snd_silncer_shot = 80". Absent = full volume.
static float LoadSndVolume(CInifile* ini, LPCSTR section, LPCSTR line)
{
	string256					volume_line;
	strconcat					(sizeof(volume_line),volume_line,"volume_",line);
	if (!ini->line_exist(section,volume_line))
		return					(1.0f);

	int							volume = ini->r_s32(section,volume_line);
	clamp						(volume, 0, 200);
	return						(float(volume) / 100.0f);
}

float DistantSoundBlend(const Fvector& position, float start, float end)
{
	const float	dist		= Device.vCameraPosition.distance_to(position);
	if (dist <= start)		return 0.0f;
	if (dist >= end)		return 1.0f;
	return					(dist - start) / (end - start);
}

void HUD_SOUND_ITEM::LoadSound(	LPCSTR section, LPCSTR line,
							HUD_SOUND_ITEM& hud_snd, int type, CInifile* ini)
{
	if (!ini)	ini = pSettings;
	hud_snd.m_activeSnd		= NULL;
	hud_snd.sounds.clear	();
	hud_snd.m_volume		= LoadSndVolume(ini, section, line);

	string256	sound_line;
	xr_strcpy		(sound_line,line);
	int k=0;
	while( ini->line_exist(section, sound_line) ){
		hud_snd.sounds.push_back( SSnd() );
		SSnd& s = hud_snd.sounds.back();

		LoadSound	(section, sound_line, s.snd, type, &s.unlock_freq, &s.delay, ini);
		xr_sprintf		(sound_line,"%s%d",line,++k);
	}//while
}

void  HUD_SOUND_ITEM::LoadSound(LPCSTR section,
								LPCSTR line,
								ref_sound& snd,
								int type,
								float* unlock_freq,
								float* delay,
								CInifile* ini)
{
	LPCSTR str = (ini ? ini : pSettings)->r_string(section, line);
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

bool HUD_SOUND_ITEM::LoadIndoorSound(	LPCSTR section, LPCSTR line,
									HUD_SOUND_ITEM& hud_snd, HUD_SOUND_ITEM& tail, int type, CInifile* ini)
{
	if (!ini)	ini = pSettings;
	string256					tail_line;
	strconcat					(sizeof(tail_line), tail_line, line, "_indoor");
	hud_snd.m_indoor			= !!ini->line_exist(section, tail_line);
	if (!hud_snd.m_indoor)		return false;

	LoadSound					(section, tail_line, tail, type, ini);
	hud_snd.LoadIndoorFade		(section, line, ini);
	return						true;
}

void HUD_SOUND_ITEM::LoadIndoorFade(LPCSTR section, LPCSTR line, CInifile* ini)
{
	if (!ini)	ini = pSettings;
	static LPCSTR const	keys[3]	= { "fadeout_start", "fadeout_end", "fadeout_db" };
	float* const		dst[3]	= { &m_indoor_fade.start, &m_indoor_fade.end, &m_indoor_fade.db };
	for (int i=0; i<3; ++i)
	{
		string256				own, common;
		xr_sprintf				(own, "%s_indoor_%s", line, keys[i]);
		xr_sprintf				(common, "snd_indoor_%s", keys[i]);
		if (ini->line_exist(section, own))			*dst[i] = ini->r_float(section, own);
		else if (ini->line_exist(section, common))	*dst[i] = ini->r_float(section, common);
	}
	m_indoor_fade.start			= _max(m_indoor_fade.start, 0.f);
	m_indoor_fade.end			= _max(m_indoor_fade.end, m_indoor_fade.start);
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
								bool			b_force_unlock,
								float			volume_k,
								const sound_fade_out* fade)
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

	float		volume		= hud_snd.m_volume * (b_hud_mode?psHUDSoundVolume:1.0f) * volume_k;

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
		if (fade)
			s.snd.set_fade_out	(*fade);
	}
	else
	{
		Fvector	pos				= (flags&sm_2D) ? Fvector().set(0,0,0) : position;
		s.snd.play_no_feedback	(const_cast<CObject*>(parent), flags, s.delay, &pos, &volume, vary_freq?&freq:NULL, NULL, fade);
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
										bool b_force_unlock,
										float volume_k,
										const sound_fade_out* fade)
{
	xr_vector<HUD_SOUND_ITEM>::iterator it		= m_sound_items.begin();
	xr_vector<HUD_SOUND_ITEM>::iterator it_e	= m_sound_items.end();
	for(;it!=it_e;++it)
	{
		if(it->m_b_exclusive)
			HUD_SOUND_ITEM::StopSound	(*it);
	}


	HUD_SOUND_ITEM* snd_item		= FindSoundItem(alias, true);
	HUD_SOUND_ITEM::PlaySound		(*snd_item, position, parent, hud_mode, looped, index, b_force_unlock, volume_k, fade);
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

	// the "_indoor" twin rides along under "<alias>Indoor" (re-loaded with its key on an upgrade)
	string256					tail_line;
	strconcat					(sizeof(tail_line), tail_line, line, "_indoor");
	if (!pSettings->line_exist(section, tail_line))
		return;

	string64					tail_alias;
	strconcat					(sizeof(tail_alias), tail_alias, alias, "Indoor");
	HUD_SOUND_ITEM* tail		= FindSoundItem(tail_alias, false);
	if (!tail)
	{
		m_sound_items.resize	(m_sound_items.size()+1);			// invalidates snd_item
		tail					= &m_sound_items.back();
		tail->m_alias			= tail_alias;
	}
	else
		HUD_SOUND_ITEM::StopSound	(*tail);
	HUD_SOUND_ITEM::LoadIndoorSound	(section, line, *FindSoundItem(alias, true), *tail, type);
	tail->m_b_exclusive		= false;
}
