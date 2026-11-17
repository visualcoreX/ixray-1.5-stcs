#include "stdafx.h"
#pragma hdrstop

// Convolution reverb. Instead of the EAX zones a level's geometry would have to be dressed in, every sound
// goes through the recorded impulse response (IR) of the space the listener is in: outdoors, indoors, or
// underground on the levels configs\sound_reverb.ltx pins to it. The game says a few times a second whether
// the listener is indoors (set_reverb_listener: the same roof-and-walls ray test that picks indoor shots).
//
// Each space is an aux slot with OpenAL Soft's convolution effect and its IR in a buffer. A source sends to
// all three slots on its sends 0..2 (the context asks for four sends instead of the default two), and the
// slots' gains cross-fade from one space to the next, equal-power, over fade_time. One convolution per
// space, not per sound: the cost does not grow with the number of sounds playing.
//
// The effect is AL_SOFTX_convolution_reverb in OpenAL Soft 1.23 (AL_EFFECT_CONVOLUTION_REVERB_SOFT) and
// AL_SOFT_convolution_effect from 1.24 (AL_EFFECT_CONVOLUTION_SOFT); the enum is looked up by name, so
// either works. Without it, or without the config, the EAX zones stay as they were.

#include "SoundRender_CoreA.h"
#include "SoundRender_TargetA.h"

#include <AL/alext.h>

// AL_EFFECTSLOT_GAIN tops out at 1, so the IR is stored this much louder and the slot gain divided by it:
// gain_* and snd_reverb_gain can then reach +12 dB over a normalized IR
static const float	conv_headroom	= 4.f;

static LPCSTR const	space_name[srs_count] = { "outdoor", "indoor", "underground" };

// A RIFF WAVE file to float samples, interleaved. PCM 8/16/24/32 bit, float 32/64, plain or
// WAVE_FORMAT_EXTENSIBLE; mono or stereo.
static bool load_ir_wav(IReader* F, xr_vector<float>& out, u32& channels, u32& rate, LPCSTR& error)
{
	const u8*	p		= (const u8*)F->pointer();
	const u32	len		= F->length();
	if (len<12 || memcmp(p,"RIFF",4) || memcmp(p+8,"WAVE",4))	{ error = "not a RIFF WAVE file";	return false; }

	u16			tag		= 0, bits = 0;
	channels			= 0;
	rate				= 0;
	const u8*	data	= nullptr;
	u32			data_len= 0;
	for (u32 pos=12; pos+8<=len; )
	{
		const u8*	ck		= p+pos;
		u32			ck_len	= *(const u32*)(ck+4);
		if (ck_len > len-pos-8)		ck_len = len-pos-8;		// a truncated last chunk: take what is there
		if (!memcmp(ck,"fmt ",4) && ck_len>=16)
		{
			tag				= *(const u16*)(ck+8);
			channels		= *(const u16*)(ck+10);
			rate			= *(const u32*)(ck+12);
			bits			= *(const u16*)(ck+22);
			if (tag==0xFFFE && ck_len>=40)
				tag			= *(const u16*)(ck+8+24);		// the sub-format GUID starts with the real tag
		}
		else if (!memcmp(ck,"data",4))
		{
			data			= ck+8;
			data_len		= ck_len;
		}
		pos					+= 8+ck_len+(ck_len&1);
	}

	if (!channels || !rate)							{ error = "no fmt chunk";							return false; }
	if (!data || !data_len)							{ error = "no data";								return false; }
	if (channels!=1 && channels!=2)					{ error = "only mono and stereo IRs are supported";	return false; }
	const bool	is_int	= (tag==1) && (bits==8 || bits==16 || bits==24 || bits==32);
	const bool	is_flt	= (tag==3) && (bits==32 || bits==64);
	if (!is_int && !is_flt)							{ error = "unsupported sample format";				return false; }

	const u32	bps		= bits/8;
	const u32	frames	= data_len/(bps*channels);
	if (!frames)									{ error = "no data";								return false; }

	out.resize			(frames*channels);
	for (u32 i=0, n=frames*channels; i<n; ++i)
	{
		const u8*	s	= data+i*bps;
		float		v;
		if (is_flt)		v = (bits==32) ? *(const float*)s : float(*(const double*)s);
		else switch (bits)
		{
		case 8:			v = (float(s[0])-128.f)/128.f;															break;
		case 16:		v = float(*(const s16*)s)/32768.f;														break;
		case 24:		v = float(s32((u32(s[0])<<8) | (u32(s[1])<<16) | (u32(s[2])<<24))>>8)/8388608.f;			break;
		default:		v = float(double(*(const s32*)s)/2147483648.0);											break;
		}
		out[i]			= v;
	}
	return				true;
}

void CSoundRender_CoreA::conv_init()
{
	m_conv_type			= 0;
	if (!m_is_supported)				return;
	if (!s_reverb.enabled)				return;

	alGetError			();
	ALenum	t			= alGetEnumValue("AL_EFFECT_CONVOLUTION_SOFT");			// 1.24+
	if (!t || t==-1)	t = alGetEnumValue("AL_EFFECT_CONVOLUTION_REVERB_SOFT");	// 1.23, experimental
	alGetError			();
	if (!t || t==-1)
	{
		Msg				("! SOUND: this OpenAL has no convolution effect, the reverb stays on the EAX zones");
		return;
	}
	m_conv_type			= t;

	if (m_max_sends<srs_count)
		Msg				("! SOUND: OpenAL gives %d aux send(s) a source, %d wanted: the spaces past it get no reverb", m_max_sends, int(srs_count));

	for (int i=0; i<srs_count; ++i)
	{
		xr_strcpy		(m_rv_console[i], psSoundReverbIR[i]);
		conv_build		(i, conv_wanted_ir(i));
	}
}

void CSoundRender_CoreA::conv_destroy()
{
	for (int i=0; i<srs_count; ++i)
	{
		conv_release	(i);
		m_conv[i].ir	= 0;
	}
	m_conv_type			= 0;
}

void CSoundRender_CoreA::conv_release(int space)
{
	conv_space&	C		= m_conv[space];
	if (C.slot)			alDeleteAuxiliaryEffectSlots(1, &C.slot);
	if (C.effect)		alDeleteEffects(1, &C.effect);
	if (C.buffer)		alDeleteBuffers(1, &C.buffer);
	alGetError			();
	C.slot				= 0;
	C.effect			= 0;
	C.buffer			= 0;
	C.ok				= false;
	C.applied_gain		= -1.f;
}

// snd_reverb_ir_<space> when set (and not "default"), else the config's; lower case, no extension
shared_str CSoundRender_CoreA::conv_wanted_ir(int space) const
{
	LPCSTR		con		= psSoundReverbIR[space];
	if (!con[0] || 0==_stricmp(con,"default"))
		return			s_reverb.ir[space];

	string_path	v;
	xr_strcpy			(v, con);
	_strlwr				(v);
	for (char* c=v; *c; ++c)
		if (*c=='/')	*c = '\\';
	if (strext(v))		*strext(v) = 0;
	return				shared_str(v);
}

// Loads <ir> into space <space>'s slot, dropping whatever it had. Every source must have stopped sending
// to the old slot first (clear_target_sends), or OpenAL refuses to delete it.
void CSoundRender_CoreA::conv_build(int space, const shared_str& ir)
{
	conv_space&	C		= m_conv[space];
	conv_release		(space);
	C.ir				= ir;
	if (!m_conv_type || !ir.size())		return;

	string_path			fn, name;
	strconcat			(sizeof(name), name, *ir, ".wav");
	if (!FS.exist(fn, "$game_sounds$", name))
	{
		Msg				("! SOUND: reverb %s: no %s", space_name[space], name);
		return;
	}
	IReader*	F		= FS.r_open(fn);
	if (!F)
	{
		Msg				("! SOUND: reverb %s: can't open %s", space_name[space], fn);
		return;
	}
	xr_vector<float>	data;
	u32					channels, rate;
	LPCSTR				error = "";
	const bool	read	= load_ir_wav(F, data, channels, rate, error);
	FS.r_close			(F);
	if (!read)
	{
		Msg				("! SOUND: reverb %s: %s: %s", space_name[space], name, error);
		return;
	}

	// cut the start off: the direct sound, which the dry signal already is
	u32			frames	= data.size()/channels;
	const u32	skip	= _min(u32(s_reverb.skip_ms[space]*0.001f*float(rate)), frames>0 ? frames-1 : 0);
	if (skip)
	{
		data.erase		(data.begin(), data.begin()+skip*channels);
		frames			-= skip;
	}

	// unit energy per channel: then the reverb of a steady sound comes out about as loud as the sound
	// itself, and gain_* is the wet level in plain terms whichever IR is in
	double		energy	= 0.0;
	for (u32 i=0; i<data.size(); ++i)
		energy			+= double(data[i])*double(data[i]);
	energy				/= double(channels);
	float		scale	= conv_headroom;
	if (s_reverb.normalize && energy>1e-12)
		scale			/= float(sqrt(energy));
	for (u32 i=0; i<data.size(); ++i)
		data[i]			*= scale;

	alGetError			();
	alGenBuffers		(1, &C.buffer);
	alBufferData		(C.buffer, channels==1 ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_STEREO_FLOAT32, &data.front(), ALsizei(data.size()*sizeof(float)), ALsizei(rate));
	ALenum		err		= alGetError();
	if (err!=AL_NO_ERROR)
	{
		Msg				("! SOUND: reverb %s: %s: buffer refused: %s", space_name[space], name, alGetString(err));
		conv_release	(space);
		return;
	}

	alGenAuxiliaryEffectSlots	(1, &C.slot);
	alGenEffects		(1, &C.effect);
	alAuxiliaryEffectSlotf	(C.slot, AL_EFFECTSLOT_GAIN, 0.f);
	alEffecti			(C.effect, AL_EFFECT_TYPE, m_conv_type);
	err					= alGetError();
	if (err!=AL_NO_ERROR)
	{
		Msg				("! SOUND: reverb %s: can't create the convolution effect: %s", space_name[space], alGetString(err));
		conv_release	(space);
		return;
	}

	// The IR goes in before the effect: OpenAL Soft takes a buffer only while the slot is still in its
	// initial state, and attaching the effect starts it playing (the effect then loads the slot's buffer).
	// Should an OpenAL want it the other way round, try that.
	alAuxiliaryEffectSloti	(C.slot, AL_BUFFER, C.buffer);
	const ALenum err_buf= alGetError();
	alAuxiliaryEffectSloti	(C.slot, AL_EFFECTSLOT_EFFECT, C.effect);
	err					= alGetError();
	if (err==AL_NO_ERROR && err_buf!=AL_NO_ERROR)
	{
		alAuxiliaryEffectSloti	(C.slot, AL_BUFFER, C.buffer);
		err				= alGetError();
	}
	if (err!=AL_NO_ERROR)
	{
		Msg				("! SOUND: reverb %s: the slot refused the IR: %s", space_name[space], alGetString(err));
		conv_release	(space);
		return;
	}

	C.ok				= true;
	C.applied_gain		= 0.f;
	Msg					("SOUND: reverb %s: %s, %s, %d Hz, %.2f s%s", space_name[space], name, channels==1?"mono":"stereo", rate, float(frames)/float(rate),
						 s_reverb.normalize ? make_string(", normalized %+.1f dB", 20.f*log10f(scale/conv_headroom)).c_str() : "");
}

bool CSoundRender_CoreA::conv_active() const
{
	if (!m_conv_type)							return false;
	if (!psSoundFlags.test(ss_ConvReverb))		return false;
	for (int i=0; i<srs_count; ++i)
		if (m_conv[i].ok)						return true;
	return										false;
}

void CSoundRender_CoreA::clear_target_sends()
{
	for (u32 it=0; it<s_targets.size(); it++)
		static_cast<CSoundRender_TargetA*>(s_targets[it])->clear_sends();
}

void CSoundRender_CoreA::set_reverb_listener(LPCSTR level, bool indoor)
{
	m_rv_indoor			= indoor;
	if (!level)			level = "";
	if (m_rv_level.size() && 0==xr_strcmp(*m_rv_level, level))
		return;
	m_rv_level			= level;
	m_rv_level_space	= reverb_level_space(level);
	if (m_rv_level_space>=0)
		Msg				("SOUND: reverb: level '%s' is heard as %s throughout", level, space_name[m_rv_level_space]);
}

void CSoundRender_CoreA::reverb_update(float dt)
{
	if (!m_conv_type)					return;

	// snd_reverb_ir_*: swap an IR on the fly, to compare variants
	bool		cleared	= false;
	for (int i=0; i<srs_count; ++i)
	{
		if (0==xr_strcmp(m_rv_console[i], psSoundReverbIR[i]))	continue;
		xr_strcpy		(m_rv_console[i], psSoundReverbIR[i]);
		const shared_str want = conv_wanted_ir(i);
		if (want==m_conv[i].ir)			continue;
		if (!cleared)
		{
			clear_target_sends	();
			cleared		= true;
		}
		conv_build		(i, want);
	}

	// the space the listener is in; one with no IR borrows the next one up (underground -> indoor -> outdoor)
	int			target	= (psSoundReverbSpace>=0)	? _min(psSoundReverbSpace, int(srs_count)-1)
						: (m_rv_level_space>=0)		? m_rv_level_space
						: (m_rv_indoor ? int(srs_Indoor) : int(srs_Outdoor));
	while (target>0 && !m_conv[target].ok)	--target;

	// cross-fade, linear in power; straight there when nothing is sounding yet (a level just loaded)
	float		total	= 0.f;
	for (int i=0; i<srs_count; ++i)
		total			+= m_conv[i].weight;
	const float	step	= (total<EPS_S || s_reverb.fade_time<EPS_S) ? 1.f : dt/s_reverb.fade_time;
	const bool	active	= conv_active();
	for (int i=0; i<srs_count; ++i)
	{
		conv_space&	C	= m_conv[i];
		const float	goal= (i==target) ? 1.f : 0.f;
		if (C.weight<goal)		C.weight = _min(C.weight+step, goal);
		else if (C.weight>goal)	C.weight = _max(C.weight-step, goal);
		if (!C.ok)				continue;

		float		g	= active ? _sqrt(C.weight)*s_reverb.gain[i]*psSoundReverbGain/conv_headroom : 0.f;
		clamp			(g, 0.f, 1.f);
		if (_abs(g-C.applied_gain)>0.0005f)
		{
			C.applied_gain	= g;
			alAuxiliaryEffectSlotf	(C.slot, AL_EFFECTSLOT_GAIN, g);
		}
	}
}
