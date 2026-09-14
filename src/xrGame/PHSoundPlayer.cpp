#include "stdafx.h"

#include "PHSoundPlayer.h"
#include "PhysicsShellHolder.h"
// How far into the current collision sound the next one of the same object may begin: 0.5 = from its
// middle. 1.0 would be the old "only when it has finished", 0.0 no limit at all.
static const float PLAY_OVERLAP_K	= 0.5f;

CPHSoundPlayer::CPHSoundPlayer(CPhysicsShellHolder* obj)
{

	m_object=obj;
	m_next_play_tm=0;
}

CPHSoundPlayer::~CPHSoundPlayer()
{
	m_sound.stop();
	m_object=NULL;
}

void CPHSoundPlayer::Play(SGameMtlPair* mtl_pair,const Fvector& pos, float volume)
{

	// The gate is time, not the end of the sound: a new one is allowed once the current is
	// PLAY_OVERLAP_K through. A sound that has already finished is past its own deadline, so this
	// covers the "nothing is playing" case too. Cloning over m_sound does not cut the previous one --
	// the emitter holds its own reference to the sound data (CSoundRender_Core::clone) and plays out.
	if(Device.dwTimeGlobal >= m_next_play_tm)
	{
		Fvector vel;m_object->PHGetLinearVell(vel);
		if(vel.square_magnitude()>0.01f)
		{
			CLONE_MTL_SOUND(m_sound, mtl_pair, CollideSounds);
			m_sound.play_at_pos(smart_cast<CPhysicsShellHolder*>(m_object),pos);
			// The contact works out how hard the hit was; keep that, it is what tells a weapon
			// tumbling to a stop from one thrown at a wall.
			if(volume>0.f)	m_sound.set_volume(volume);

			const float len = m_sound.get_length_sec();		// known from the clone, before it plays
			m_next_play_tm	= Device.dwTimeGlobal + u32(len * PLAY_OVERLAP_K * 1000.f);
		}
	}
}


