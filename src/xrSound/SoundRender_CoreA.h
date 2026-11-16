#ifndef SoundRender_CoreAH
#define SoundRender_CoreAH
#pragma once

#include "SoundRender_Core.h"            
#include "OpenALDeviceList.h"

#include <AL/efx.h>

#ifdef DEBUG
#	define A_CHK(expr)		{ alGetError(); 		expr; ALenum error=alGetError(); 			VERIFY2(error==AL_NO_ERROR, (LPCSTR)alGetString(error)); }
#	define AC_CHK(expr)		{ alcGetError(pDevice); expr; ALCenum error=alcGetError(pDevice); 	VERIFY2(error==ALC_NO_ERROR,(LPCSTR)alcGetString(pDevice,error)); }
#else
#	define A_CHK(expr)		{ expr; }
#	define AC_CHK(expr)		{ expr; }
#endif

#define FUNCTION_CAST(T, ptr) reinterpret_cast<T>(ptr)

class CSoundRender_CoreA: public CSoundRender_Core
{
	typedef CSoundRender_Core inherited;

	ALCdevice* 				pDevice;
    ALCcontext*				pContext;
	ALDeviceList*			pDeviceList;

	struct SListener{
		Fvector				position;
		Fvector				orientation[2];
	};
	SListener				Listener;

	/* Filter object functions */
	LPALGENFILTERS alGenFilters{};
	LPALDELETEFILTERS alDeleteFilters{};
	LPALISFILTER alIsFilter{};
	LPALFILTERI alFilteri{};
	LPALFILTERIV alFilteriv{};
	LPALFILTERF alFilterf{};
	LPALFILTERFV alFilterfv{};
	LPALGETFILTERI alGetFilteri{};
	LPALGETFILTERIV alGetFilteriv{};
	LPALGETFILTERF alGetFilterf{};
	LPALGETFILTERFV alGetFilterfv{};

	/* Effect object functions */
	LPALGENEFFECTS alGenEffects{};
	LPALDELETEEFFECTS alDeleteEffects{};
	LPALISEFFECT alIsEffect{};
	LPALEFFECTI alEffecti{};
	LPALEFFECTIV alEffectiv{};
	LPALEFFECTF alEffectf{};
	LPALEFFECTFV alEffectfv{};
	LPALGETEFFECTI alGetEffecti{};
	LPALGETEFFECTIV alGetEffectiv{};
	LPALGETEFFECTF alGetEffectf;
	LPALGETEFFECTFV alGetEffectfv;

	/* Auxiliary Effect Slot object functions */
	LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots{};
	LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots{};
	LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot{};
	LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti{};
	LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv{};
	LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf{};
	LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv{};
	LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti{};
	LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv{};
	LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf{};
	LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv{};

	ALuint effect{};
	ALuint effectfv{};
	ALuint slot{};

	// Convolution reverb: one aux slot per space (outdoor, indoor, underground), each convolving with its own
	// impulse response. A source sends to all of them on sends 0..2 and the slots' gains cross-fade, so
	// leaving a building blends one room into the other instead of switching it. See SoundRender_CoreA_Reverb.cpp.
	struct conv_space
	{
		ALuint					slot;
		ALuint					effect;
		ALuint					buffer;
		shared_str				ir;				// what is loaded (or failed to load) into it
		float					weight;			// 0..1, its share of the cross-fade
		float					applied_gain;	// last AL_EFFECTSLOT_GAIN written
		bool					ok;
	};
	conv_space				m_conv[srs_count];
	ALenum					m_conv_type{};		// AL_EFFECT_CONVOLUTION_*_SOFT as this OpenAL names it; 0 = none
	ALint					m_max_sends{1};
	shared_str				m_rv_level;
	int						m_rv_level_space{-1};
	bool					m_rv_indoor{false};
	int						m_rv_space{-1};			// the space last cross-faded to
	char					m_rv_console[srs_count][SND_REVERB_IR_LEN]{};	// snd_reverb_ir_* as last applied

	void					conv_init				();
	void					conv_destroy			();
	shared_str				conv_wanted_ir			(int space) const;
	void					conv_build				(int space, const shared_str& ir);
	void					conv_release			(int space);
	bool					conv_active				() const;
	void					clear_target_sends		();

protected:
	virtual void			update_listener			( const Fvector& P, const Fvector& D, const Fvector& N, float dt );
public:	
						    CSoundRender_CoreA		();
    virtual					~CSoundRender_CoreA		();

	int load_reverb(ALuint effect, const EFXEAXREVERBPROPERTIES* reverb);

	virtual void			_initialize				(int stage);
	virtual void			_clear					( );
	virtual void			_restart				( );
    
	virtual void			set_master_volume		( float f		);

	virtual const Fvector&	listener_position		( ){return Listener.position;}

	// The slot <E> sends to on its aux send <send>, or AL_EFFECTSLOT_NULL: none when EFX is unsupported or
	// switched off or the sound is kept dry; with the convolution reverb on, send N goes to space N's slot,
	// otherwise send 0 goes to the EAX zone reverb. See CSoundRender_TargetA::fill_parameters.
	ALuint					get_send_slot			(int send, CSoundRender_Emitter* E) const;
	ALint					max_sends				() const	{ return m_max_sends; }

	virtual void			reverb_update			(float dt);
	virtual void			set_reverb_listener		(LPCSTR level, bool indoor);

	// EFX listener
	void set_listener(const CSoundRender_Environment& env);
	void get_listener(CSoundRender_Environment& env);
	void commit();
};
extern CSoundRender_CoreA* SoundRenderA;
#endif
