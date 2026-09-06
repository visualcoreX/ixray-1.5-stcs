#ifndef SoundRender_TargetAH
#define SoundRender_TargetAH
#pragma once

#include "soundrender_Target.h"
#include "soundrender_CoreA.h"

class CSoundRender_TargetA: public CSoundRender_Target
{
	typedef CSoundRender_Target	inherited;

// OpenAL
    ALuint						pSource;
	ALuint						pBuffers[sdef_target_count];
    float						cache_gain;
    float						cache_pitch;
	// the aux slot this source is currently sending to; ALuint(-1) = "not applied yet", which is
	// neither a real slot nor AL_EFFECTSLOT_NULL, so the first fill always writes it
	ALuint						cache_efx_slot;

    ALuint						buf_block;
private:
	void						fill_block				(ALuint BufferID);
public:
								CSoundRender_TargetA	();
	virtual 					~CSoundRender_TargetA	();

	virtual BOOL				_initialize				();
	virtual void				_destroy				();
	virtual void				_restart				();

	virtual void				start					(CSoundRender_Emitter* E);
	virtual void				render					();
	virtual void				rewind					();
	virtual void				stop					();
	virtual void				update					();
	virtual void				fill_parameters			();
			void				source_changed			();
};
#endif