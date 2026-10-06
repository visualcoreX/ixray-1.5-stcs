#include "stdafx.h"
#pragma hdrstop

#include "SoundRender_Emitter.h"
#include "SoundRender_Core.h"
#include "SoundRender_Source.h"
#include "SoundRender_TargetA.h"

extern	u32				psSoundModel;
extern	float			psSoundVEffects;

void CSoundRender_Emitter::set_position(const Fvector &pos)	
{ 
	if(source()->channels_num()==1)
		p_source.position	= pos; 
	else
		p_source.position.set(0,0,0); 

	bMoved				= TRUE;					
}

CSoundRender_Emitter::CSoundRender_Emitter(void)
{

#ifdef DEBUG
	static	u32			incrementalID = 0;
	dbg_ID				= ++incrementalID;
#endif
	target						= NULL;
//.	source						= NULL;
	owner_data					= NULL;
	smooth_volume				= 1.f;
	occluder_volume				= 1.f;
	fade_volume					= 1.f;
	curve_volume				= 1.f;
	envelope_volume				= 1.f;
	fade_out.start				= 0.f;
	fade_out.end				= 0.f;
	fade_out.db					= 0.f;
	cut.start					= 0.f;
	cut.end						= 0.f;
	cut.db						= 0.f;
	occluder[0].set				(0,0,0);
	occluder[1].set				(0,0,0);
	occluder[2].set				(0,0,0);
	m_current_state				= stStopped;
	set_cursor					(0);
	bMoved						= TRUE;
	b2D							= FALSE;
	bStopping					= FALSE;
	bRewind						= FALSE;
	iPaused						= 0;
	fTimeStarted				= 0.0f;
	fTimeToStop				= 0.0f;
	fTimeToPropagade			= 0.0f;
	marker						= 0xabababab;
	starting_delay				= 0.f;
	priority_scale				= 1.f;
	m_cur_handle_cursor			= 0;
}

CSoundRender_Emitter::~CSoundRender_Emitter(void)
{
	// try to release dependencies, events, for example
	Event_ReleaseOwner	();
}

//////////////////////////////////////////////////////////////////////
void CSoundRender_Emitter::Event_ReleaseOwner()
{
	if	(!(owner_data))			return;

	for (u32 it=0; it<SoundRender->s_events.size(); it++){
		if (owner_data == SoundRender->s_events[it].first){
			SoundRender->s_events.erase(SoundRender->s_events.begin()+it);
			it	--;
		}
	}
}

void CSoundRender_Emitter::Event_Propagade	()
{
	fTimeToPropagade			+= ::Random.randF	(s_f_def_event_pulse-0.030f, s_f_def_event_pulse+0.030f);
	if (!(owner_data))			return;
	if (0==owner_data->g_type)	return;
	if (0==owner_data->g_object)return;
	if (0==SoundRender->Handler)return;

	VERIFY						(_valid(p_source.volume));
	// Calculate range
	float	clip				= p_source.max_ai_distance*p_source.volume;
	float	range				= _min(p_source.max_ai_distance,clip);
	if (range<0.1f)				return;

	// Inform objects
	SoundRender->s_events.push_back	(std::make_pair(owner_data,range));
}

void CSoundRender_Emitter::set_fade_out(const sound_fade_out& fade)
{
	fade_out					= fade;
	if (!_valid(fade_out.start) || !_valid(fade_out.end) || !_valid(fade_out.db))
		fade_out.end			= 0.f;
	fade_out.start				= _max(fade_out.start, 0.f);
	if (fade_out.end<fade_out.start+EPS_L)	// no ramp: a hard cut at <end>
		fade_out.start			= _max(fade_out.end, 0.f);
}

// Fades the instance out from <delay> seconds from now over <length>. Kept apart from fade_out so a shot
// that already fades by its config (the indoor fade) goes on doing so, the cut merely multiplying in.
void CSoundRender_Emitter::cut_out(float delay, float length)
{
	if (!_valid(delay) || !_valid(length))	return;

	float		now;			// own play time at this moment, as update_fade_out counts it
	switch (m_current_state)
	{
	case stPlaying: case stPlayingLooped: case stSimulating: case stSimulatingLooped:
		now						= SoundRender->fTimer_Value-fTimeStarted;	break;
	case stStartingDelayed: case stStartingLoopedDelayed:
		now						= -starting_delay;							break;
	case stStarting: case stStartingLooped:
		now						= 0.f;										break;
	default:					return;		// over already
	}

	const float	start			= now+_max(delay, 0.f);
	const float	end				= start+_max(length, 0.f);
	if (end<=0.f)				{ i_stop(); return; }		// would be over before it is ever heard
	if (cut.end>0.f && cut.end<=end)		return;			// an earlier cut stands

	cut.start					= _max(start, 0.f);
	cut.end						= end;
}

// How much of a sound one fade leaves at play time <t>: 1 before <start>, 0 from <end> on.
static float fade_gain(const sound_fade_out& f, float t)
{
	if (t>=f.end)				return 0.f;
	if (t<=f.start)				return 1.f;

	const float	x				= (t-f.start)/(f.end-f.start);
	float		g;
	if (f.db<=0.f)
		g						= 1.f-x;
	else
	{
		const float	floor_g		= powf(10.f, -f.db/20.f);
		g						= (powf(10.f, -f.db*x/20.f)-floor_g)/(1.f-floor_g);
	}
	clamp						(g, 0.f, 1.f);
	return						g;
}

// Seconds are the instance's own play time, so a delayed or paused sound fades from where it really is.
bool CSoundRender_Emitter::update_fade_out()
{
	envelope_volume				= 1.f;
	if (fade_out.end<=0.f && cut.end<=0.f)	return true;

	switch (m_current_state)
	{
	case stPlaying: case stPlayingLooped: case stSimulating: case stSimulatingLooped: break;
	default:					return true;		// not started yet
	}

	const float	t				= SoundRender->fTimer_Value-fTimeStarted;
	if ((fade_out.end>0.f && t>=fade_out.end) || (cut.end>0.f && t>=cut.end))
	{
		envelope_volume			= 0.f;
		return					false;
	}
	if (fade_out.end>0.f)		envelope_volume *= fade_gain(fade_out, t);
	if (cut.end>0.f)			envelope_volume *= fade_gain(cut, t);
	return						true;
}

void CSoundRender_Emitter::switch_to_2D()
{
 	b2D 						= TRUE;	
	set_priority				(100.f);
}

void CSoundRender_Emitter::switch_to_3D()						
{ 	
	b2D 						= FALSE;											
}

u32	CSoundRender_Emitter::play_time	( )
{ 
	if(m_current_state==stPlaying				|| 
			m_current_state==stPlayingLooped	|| 
			m_current_state==stSimulating		|| 
			m_current_state==stSimulatingLooped		
	)
		return iFloor((SoundRender->fTimer_Value-fTimeStarted)*1000.0f);
	else
		return 0; 
}

void CSoundRender_Emitter::set_cursor(u32 p)
{
	m_stream_cursor		= p;
	
	if(owner_data._get() && owner_data->fn_attached[0].size())
	{
		u32 bt = ((CSoundRender_Source*)owner_data->handle)->dwBytesTotal;
		if(m_stream_cursor >= m_cur_handle_cursor+bt )
		{
			SoundRender->i_destroy_source	((CSoundRender_Source*)owner_data->handle);
			owner_data->handle				= SoundRender->i_create_source(owner_data->fn_attached[0].c_str());
			owner_data->fn_attached[0]		= owner_data->fn_attached[1];
			owner_data->fn_attached[1]		= "";
			m_cur_handle_cursor				= get_cursor(true);
			
			if(target)
				((CSoundRender_TargetA*)target)->source_changed();
		}
	}
}

u32 CSoundRender_Emitter::get_cursor(bool b_absolute) const
{
	if(b_absolute)
		return 	m_stream_cursor;
	else
	{
		VERIFY(m_stream_cursor-m_cur_handle_cursor >=0);
		return m_stream_cursor - m_cur_handle_cursor;
	}
}

void CSoundRender_Emitter::move_cursor(int offset) 
{
	set_cursor(get_cursor(true)+offset);
}
