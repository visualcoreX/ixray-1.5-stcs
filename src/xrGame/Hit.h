#pragma once

struct SHit
{
	SHit(float powerA, float power_criticalA, Fvector &dirA, CObject *whoA, u16 elementA, Fvector p_in_bone_spaceA,\
		float impulseA, ALife::EHitType hit_typeA, float armor_piercingA=0.0f, bool AimBullet=false);

	SHit										();	
	bool				is_valide				()		const	;	
	void				invalidate				()				;
IC	float				damage					()		const	{VERIFY(is_valide());return power;}
IC	float				critical_power			()		const	{VERIFY(is_valide());return power_critical;}
IC	const Fvector		&direction				()		const	{VERIFY(is_valide());return dir;}
IC	const CObject		*initiator				()		const	{VERIFY(is_valide());return who;}
IC			u16			bone					()		const	{VERIFY(is_valide());return boneID;}
IC	const Fvector		&bone_space_position	()		const	{VERIFY(is_valide());return p_in_bone_space;}
IC			float		phys_impulse			()		const	{VERIFY(is_valide());return impulse;}
IC	ALife::EHitType		type					()		const	{VERIFY(is_valide());return hit_type;}								
	void				Read_Packet				(NET_Packet	P);
	void				Read_Packet_Cont		(NET_Packet	P);
	void				Write_Packet			(NET_Packet	&P);
	void				Write_Packet_Cont		(NET_Packet	&P);

	void				GenHeader				(u16 PacketType, u16 ID);
//private:
	//GE_HIT
	u32					Time;
	u16					PACKET_TYPE;
	u16					DestID;
	
	float				power;
	float				power_critical;
	Fvector				dir;
	CObject				*who;
	u16					whoID;
	u16					weaponID;
	u16					boneID;
	Fvector				p_in_bone_space;
	float				impulse;
	
	ALife::EHitType		hit_type;
	float				armor_piercing;
	// Ammo's k_armor_frac (1 = stock): multiplies the hit_fraction a stalker's bone armour or the
	// actor's outfit applies when it stops the round (ap <= armour). Monster skin ignores it.
	// Bullet hits only; on the wire in single player after armor_piercing.
	float				armor_frac;
	// Ammo's k_skin_frac (1 = stock): multiplies a mutant's skin hit_fraction_monster against the
	// round, both the share a stopped round keeps and the least a piercing one deals (see
	// CBaseMonster::Hit). Bullet hits only; on the wire in single player after armor_frac.
	float				skin_frac;
	bool				add_wound;
	bool				aim_bullet;
	//---------------------------------------------------
	//GE_HIT_STATISTIC
	u32					BulletID;
	u32					SenderID;
#ifdef DEBUG
	void				_dump				();
#endif
};