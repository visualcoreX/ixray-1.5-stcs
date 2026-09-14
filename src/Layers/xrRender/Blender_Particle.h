// Blender_Screen_SET.h: interface for the Blender_Screen_SET class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_BLENDER_SCREEN_SET_H__A215FA40_D885_4D06_9032_ED934AE295E3__INCLUDED_P)
#define AFX_BLENDER_SCREEN_SET_H__A215FA40_D885_4D06_9032_ED934AE295E3__INCLUDED_P
#pragma once

class CBlender_Particle		: public IBlender  
{
	xrP_TOKEN	oBlend;
	xrP_Integer	oAREF;
	xrP_BOOL	oClamp;
public:
	// Which of the six blend modes this particle shader was authored with. The renderer needs it
	// outside Compile(): only mode 1 (BLEND -- smoke, dust, steam) may be lit, see CParticleEffect.
	IC			u32			getBlendMode() const	{ return oBlend.IDselected; }

	virtual		LPCSTR		getComment()	{ return "particles";	}
	virtual		BOOL		canBeLMAPped()	{ return FALSE;			}
	
	virtual		void		Save			(IWriter&  fs);
	virtual		void		Load			(IReader&	fs, u16 version);
	
	virtual		void		Compile			(CBlender_Compile& C);
	
	CBlender_Particle();
	virtual ~CBlender_Particle();
};

#endif // !defined(AFX_BLENDER_SCREEN_SET_H__A215FA40_D885_4D06_9032_ED934AE295E3__INCLUDED_P)
