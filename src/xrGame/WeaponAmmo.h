#pragma once
#include "inventory_item_object.h"
#include "Explosive.h"
#include "anticheat_dumpable_object.h"

struct SCartridgeParam
{
	float	kDist, kDisp, kHit, kCritical, kImpulse, kAP, kAirRes;
	int		buckShot;
	float	impair;
	float	fWallmarkSize;
	u8		u8ColorID;

	IC void Init()
	{
		kDist = kDisp = kHit = kImpulse = 1.0f;
		kCritical = 0.0f;
		kAP       = 0.0f;
		kAirRes   = 0.0f;
		buckShot  = 1;
		impair    = 1.0f;
		fWallmarkSize = 0.0f;
		u8ColorID     = 0;
	}
};

class CCartridge : public IAnticheatDumpable
{
public:
	CCartridge();
	void Load(LPCSTR section, u8 LocalAmmoType);

	shared_str	m_ammoSect;
	enum{
		cfTracer				= (1<<0),
		cfRicochet				= (1<<1),
		cfCanBeUnlimited		= (1<<2),
		cfExplosive				= (1<<3),
	};
	SCartridgeParam param_s;

	u8		m_LocalAmmoType;

	u16		bullet_material_idx;
	Flags8	m_flags;

	shared_str	m_InvShortName;
	virtual void				DumpActiveParams		(shared_str const & section_name, CInifile & dst_ini) const;
	virtual shared_str const 	GetAnticheatSectionName	() const { return m_ammoSect; };
};

// A box of ammunition -- and, when its section asks for it, an explosive one. Underbarrel and
// rocket rounds carry a live warhead whether they are in a launcher or on the ground, so they
// detonate when shot, exactly like a hand grenade (CExplosive's hit fuse). Ammunition that does
// not opt in never touches any of it: cast_explosive() returns nothing, so nothing in the game --
// the AI's explosive-danger sense included -- treats a box of rifle rounds as a bomb.
class CWeaponAmmo :	public CInventoryItemObject, public CExplosive {
	typedef CInventoryItemObject		inherited;
public:
									CWeaponAmmo			(void);
	virtual							~CWeaponAmmo		(void);

	virtual CWeaponAmmo				*cast_weapon_ammo	()	{return this;}
	virtual void					Load				(LPCSTR section);
	virtual BOOL					net_Spawn			(CSE_Abstract* DC);
	virtual void					net_Destroy			();
	virtual void					net_Export			(NET_Packet& P);
	virtual void					net_Import			(NET_Packet& P);
	virtual void					OnH_B_Chield		();
	virtual void					OnH_B_Independent	(bool just_before_destroy);
	virtual void					UpdateCL			();
	virtual void					renderable_Render	();
	virtual void					net_Relcase			(CObject* O);
	virtual void					OnEvent				(NET_Packet& P, u16 type);
	virtual	void					Hit					(SHit* pHDS);

	virtual CGameObject				*cast_game_object	()	{return this;}
	// nothing but a live round answers this -- see the note on the class
	virtual CExplosive				*cast_explosive		()	{return m_bExplosiveRound ? this : NULL;}
	virtual IDamageSource			*cast_IDamageSource	()	{return CExplosive::cast_IDamageSource();}

	virtual bool					Useful				() const;
	virtual float					Weight				();

	bool							Get					(CCartridge &cartridge);

	SCartridgeParam cartridge_param;

	// TRUE once CExplosive has been loaded for this section (an underbarrel or rocket round).
	// Everything explosive about this object is gated on it.
	bool		m_bExplosiveRound;
	u16			m_boxSize;
	u16			m_boxCurr;
	bool		m_tracer;

public:
	virtual CInventoryItem *can_make_killing	(const CInventory *inventory) const;
};
