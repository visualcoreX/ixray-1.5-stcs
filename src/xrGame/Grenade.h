#pragma once
#include "missile.h"
#include "explosive.h"
#include "../xrEngine/feel_touch.h"

class CGrenade :
	public CMissile,
	public CExplosive
{
	typedef CMissile		inherited;
public:
							CGrenade							();
	virtual					~CGrenade							();


	virtual void			Load								(LPCSTR section);
	
	virtual BOOL 			net_Spawn							(CSE_Abstract* DC);
	virtual void 			net_Destroy							();
	virtual void 			net_Relcase							(CObject* O );

	virtual void 			OnH_B_Independent					(bool just_before_destroy);
	virtual void 			OnH_A_Independent					();
	virtual void 			OnH_A_Chield						();
	
	virtual void 			OnEvent								(NET_Packet& P, u16 type);
	virtual bool			DropGrenade							();			//in this case if grenade state is eReady, it should Throw
	
	virtual void 			OnAnimationEnd						(u32 state);
	virtual void 			UpdateCL							();

	virtual void 			Throw();
	virtual void 			Destroy();

	
	virtual bool			Action								(s32 cmd, u32 flags);
	virtual bool			Useful								() const;
	virtual void			State								(u32 state);

	virtual void			OnH_B_Chield						()				{inherited::OnH_B_Chield();}

	virtual	void			Hit									(SHit* pHDS);

	virtual bool			NeedToDestroyObject					() const; 
	virtual ALife::_TIME_ID	TimePassedAfterIndependant			() const;

			void			PutNextToSlot						();

	virtual void			DeactivateItem						();
	virtual void			GetBriefInfo						(xr_string& str_name, xr_string& icon_sect_name, xr_string& str_count, string16& fire_mode);

	virtual void			SendHiddenItem						();	//same as OnHiddenItem but for client... (sends message to a server)...
protected:
	ALife::_TIME_ID			m_dwGrenadeRemoveTime;
	ALife::_TIME_ID			m_dwGrenadeIndependencyTime;
protected:
	ESoundTypes				m_eSoundCheckout;
private:
	float					m_grenade_detonation_threshold_hit;
	bool					m_thrown;

	// ---- GS impact grenades, ported from wpnpatch Throwable.pas (RGN / RGO use all of this).
	// CONTACT fuse (CMissile__ExitContactCallback): a thrown grenade that touches something has its
	// destroy time rewritten -- detonate now, keep burning, or go dud -- instead of always running
	// the timer down. Nothing happens for a section that does not opt in, so F1/RGD5 are untouched.
	bool					m_bExplosionOnKick;			// explosion_on_kick
	float					m_fMinExplosionSpeed;		// min_explosion_speed (m/s, 0 = any)
	bool					m_bDeactivateOnMinSpeed;	// deactivate_on_minimal_speed_contact
	u32						m_dwSafeTime;				// safe_time  (ms after the throw: contact = dud)
	u32						m_dwDelayTime;				// delay_time (ms after the throw: contact ignored)
	// HIT fuse (CheckGrenadeExplosionByHit): detonate when damaged, by hit type
	bool					m_bExplosionOnHit;			// explosion_on_hit
	bool					m_bExplosiveWhileNotActivated;	// explosive_while_not_activated
	bool					m_bHasExplosiveWhileKey;
	bool					m_bHelpExplosiveInfo;		// help_explosive_info (log every hit, for tuning)
	xr_vector<u32>			m_ExplosionHitTypes;		// explosion_hit_types (empty = explosion only)
	static void				ImpactContactCallback	(bool& do_colide, bool bo1, dContact& c,
													 SGameMtl* material_1, SGameMtl* material_2);
			bool			CheckExplosionByHit		(const SHit* pHDS) const;
public:
	virtual void			activate_physic_shell	();
private:
	// ---- GS grenade type switch: put the current grenade AWAY first (anm_hide), swap on the
	// animation end, so the next type is drawn instead of appearing in the hand instantly.
	u16						m_pending_next_id;
protected:
	virtual	void			UpdateXForm							()		{ CMissile::UpdateXForm(); };
public:

	virtual BOOL			UsedAI_Locations					();
	virtual CExplosive		*cast_explosive						()	{return this;}
	virtual CMissile		*cast_missile						()	{return this;}
	virtual CHudItem		*cast_hud_item						()	{return this;}
	virtual CGameObject		*cast_game_object					()	{return this;}
	virtual IDamageSource	*cast_IDamageSource					()	{return CExplosive::cast_IDamageSource();}
};
