#pragma once

class CBlender_HUD_Shadow : public IBlender
{
public:
	virtual		LPCSTR		getComment()	{ return "INTERNAL: HUD contact shadows";	}
	virtual		BOOL		canBeDetailed()	{ return FALSE;	}
	virtual		BOOL		canBeLMAPped()	{ return FALSE;	}

	virtual		void		Compile			(CBlender_Compile& C);

	CBlender_HUD_Shadow();
	virtual ~CBlender_HUD_Shadow();
};
