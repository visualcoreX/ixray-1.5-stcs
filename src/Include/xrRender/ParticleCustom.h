#ifndef	ParticleCustom_included
#define	ParticleCustom_included
#pragma once

class IParticleCustom
{
public:
	virtual ~IParticleCustom() {;}

	virtual void 	OnDeviceCreate		()=0;
	virtual void 	OnDeviceDestroy		()=0;

	virtual void	UpdateParent		(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM)=0;
	virtual void	OnFrame				(u32 dt)=0;

	virtual void	Play				()=0;
	virtual void	Stop				(BOOL bDefferedStop=TRUE)=0;
	virtual BOOL	IsPlaying			()=0;

	virtual u32		ParticlesCount		()=0;

	virtual float	GetTimeLimit		()=0;
	virtual BOOL	IsLooped			(){return GetTimeLimit()<0.f;}
	
	virtual const shared_str	Name		()=0;
	virtual void	SetHudMode			(BOOL b)=0;
	virtual BOOL	GetHudMode			()=0;

	// A world effect that starts at the first-person weapon (muzzle smoke and the like): it lives in the
	// world, but while it is near the eye it is drawn in the hud's depth slice, so the weapon covers it only
	// where it really is in front of it (see CParticleEffect::Render).
	virtual void	SetHudWorld			(BOOL b)=0;
	virtual BOOL	GetHudWorld			()=0;
};

#endif	//	ParticleCustom_included