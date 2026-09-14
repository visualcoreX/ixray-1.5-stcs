#pragma once
#include "../xrEngine/gamemtllib.h"

class CPhysicsShellHolder;

class CPHSoundPlayer
{
		ref_sound					m_sound																		;
		CPhysicsShellHolder			*m_object;
		// When the next collision sound of this object may start: the moment the current one is
		// PLAY_OVERLAP_K through itself, not when it ends. Waiting for the end swallowed the second
		// and third knock of a weapon settling on the floor; letting every contact through turned one
		// drop into a chorus. See CPHSoundPlayer::Play.
		u32							m_next_play_tm;
public:
		// One collision sound per object at a time (the _feedback test in Play). `volume` is the
		// impact volume the contact computed; 0 or less means "leave the sound at its own level".
		void						Play					(SGameMtlPair* mtl_pair,const Fvector& pos, float volume = -1.f);
									CPHSoundPlayer			(CPhysicsShellHolder *m_object)									;
virtual								~CPHSoundPlayer			()													;


private:
};