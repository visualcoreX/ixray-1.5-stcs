#pragma once

struct	firedeps
{
	Fmatrix				m_FireParticlesXForm;	//направление для партиклов огня и дыма
	Fvector				vLastFP; 				//fire point
	Fvector				vLastFP2;				//fire point2
	Fvector				vLastFD;				//fire direction
	Fvector				vLastSP;				//shell point	
	Fvector				vLastFXP;				//muzzle effects point (flame, smoke, light): fire point + silencer's muzzle_fx_offset

	firedeps			()			{	m_FireParticlesXForm.identity();
										vLastFP.set			(0,0,0);
										vLastFP2.set		(0,0,0);
										vLastFD.set			(0,0,0);
										vLastSP.set			(0,0,0);
										vLastFXP.set		(0,0,0);}
};
