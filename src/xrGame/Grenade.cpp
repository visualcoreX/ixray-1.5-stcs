#include "stdafx.h"
#include "grenade.h"
#include "PhysicsShell.h"
#include "ExtendedGeom.h"		// dxGeomUserData / retrieveGeomUserData for the contact fuse
//.#include "WeaponHUD.h"
#include "entity.h"
#include "ParticlesObject.h"
#include "actor.h"
#include "inventory.h"
#include "CustomDetector.h"		// GS RestoreLastActorDetector after a quick throw
#include "level.h"
#include "xrmessages.h"
#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "xrserver_objects_alife.h"

#define GRENADE_REMOVE_TIME		30000
const float default_grenade_detonation_threshold_hit=100;
CGrenade::CGrenade(void)
{

	m_eSoundCheckout = ESoundTypes(SOUND_TYPE_WEAPON_RECHARGING);
	m_bExplosionOnKick			= false;
	m_fMinExplosionSpeed		= 0.f;
	m_bDeactivateOnMinSpeed		= false;
	m_dwSafeTime				= 0;
	m_dwDelayTime				= 0;
	m_bExplosionOnHit			= false;
	m_bExplosiveWhileNotActivated = false;
	m_bHasExplosiveWhileKey		= false;
	m_bHelpExplosiveInfo		= false;
	m_pending_next_id			= u16(-1);
}

CGrenade::~CGrenade(void) 
{
}

void CGrenade::Load(LPCSTR section) 
{
	inherited::Load(section);
	CExplosive::Load(section);

	m_sounds.LoadSound(section,"snd_checkout","sndCheckout",m_eSoundCheckout);

	// GS controller-suicide grenade sounds (optional per section; the scene plays them through
	// CMissile's suicide branches). Without these the aliases exist nowhere and the scene is silent.
	if (pSettings->line_exist(section, "snd_suicide_begin"))
		m_sounds.LoadSound(section, "snd_suicide_begin", "sndSuicideBegin", m_eSoundCheckout);
	if (pSettings->line_exist(section, "snd_suicide_throw"))
		m_sounds.LoadSound(section, "snd_suicide_throw", "sndSuicideThrow", m_eSoundCheckout);
	if (pSettings->line_exist(section, "snd_suicide_stop"))
		m_sounds.LoadSound(section, "snd_suicide_stop", "sndSuicideStop", m_eSoundCheckout);

	//////////////////////////////////////
	//����� �������� ������ � ������
	if(pSettings->line_exist(section,"grenade_remove_time"))
		m_dwGrenadeRemoveTime = pSettings->r_u32(section,"grenade_remove_time");
	else
		m_dwGrenadeRemoveTime = GRENADE_REMOVE_TIME;
	m_grenade_detonation_threshold_hit=READ_IF_EXISTS(pSettings,r_float,section,"detonation_threshold_hit",default_grenade_detonation_threshold_hit);
	// ---- GS impact grenades (wpnpatch Throwable.pas). All opt-in: a section without these keys
	// behaves exactly as before, which is why F1/RGD5 are unaffected.
	m_bExplosionOnKick		= !!READ_IF_EXISTS(pSettings, r_bool,  section, "explosion_on_kick", FALSE);
	m_fMinExplosionSpeed	=   READ_IF_EXISTS(pSettings, r_float, section, "min_explosion_speed", 0.f);
	m_bDeactivateOnMinSpeed	= !!READ_IF_EXISTS(pSettings, r_bool,  section, "deactivate_on_minimal_speed_contact", FALSE);
	m_dwSafeTime			=   READ_IF_EXISTS(pSettings, r_u32,   section, "safe_time",  0);
	m_dwDelayTime			=   READ_IF_EXISTS(pSettings, r_u32,   section, "delay_time", 0);

	m_bExplosionOnHit		= !!READ_IF_EXISTS(pSettings, r_bool,  section, "explosion_on_hit", FALSE);
	m_bHasExplosiveWhileKey	= !!pSettings->line_exist(section, "explosive_while_not_activated");
	m_bExplosiveWhileNotActivated = m_bHasExplosiveWhileKey
								&& !!pSettings->r_bool(section, "explosive_while_not_activated");
	m_bHelpExplosiveInfo	= !!READ_IF_EXISTS(pSettings, r_bool,  section, "help_explosive_info", FALSE);
	m_ExplosionHitTypes.clear();
	if (pSettings->line_exist(section, "explosion_hit_types"))
	{
		LPCSTR s = pSettings->r_string(section, "explosion_hit_types");
		string64 tmp;
		for (int i = 0, n = _GetItemCount(s); i < n; ++i)
			m_ExplosionHitTypes.push_back(u32(atoi(_GetItem(s, i, tmp))));
	}
}

// The object that actually flies is the spawned copy (CMissile::spawn_fake_missile) and CMissile
// activates its shell here; hook the contact on top of the inherited setup.
void CGrenade::activate_physic_shell()
{
	inherited::activate_physic_shell();
	if (m_bExplosionOnKick && m_pPhysicsShell && m_pPhysicsShell->isActive())
		m_pPhysicsShell->add_ObjectContactCallback(ImpactContactCallback);
}

// GS CMissile__ExitContactCallback: rewrite the fuse on contact.
//   * still inside safe_time  -> the grenade is a DUD (destroy time cleared, it never goes off)
//   * still inside delay_time -> ignore the contact, keep the normal fuse
//   * otherwise, with explosion_on_kick -> detonate NOW, unless it is crawling slower than
//     min_explosion_speed (then either dud it or let the fuse run, per the config)
// Runs inside the physics step, so it only moves the destroy TIME -- CMissile::shedule_Update does
// the actual Destroy() a tick later, exactly like the timed path.
void CGrenade::ImpactContactCallback(bool& /*do_colide*/, bool /*bo1*/, dContact& c,
									 SGameMtl* /*material_1*/, SGameMtl* /*material_2*/)
{
	dxGeomUserData* ud1 = retrieveGeomUserData(c.geom.g1);
	dxGeomUserData* ud2 = retrieveGeomUserData(c.geom.g2);
	CGrenade* g = ud1 ? smart_cast<CGrenade*>(ud1->ph_ref_object) : NULL;
	if (!g)	g = ud2 ? smart_cast<CGrenade*>(ud2->ph_ref_object) : NULL;
	if (!g || !g->m_bExplosionOnKick)	return;

	// SAME CLOCK as the one the fuse was armed with: CMissile::set_destroy_time() stamps
	// Device.dwTimeGlobal, so measuring `time from throw` against Level().timeServer() gave a
	// nonsense age -- safe_time/delay_time never applied and an RGN/RGO blew up on the first bounce
	// even when the victim had thrown it away.
	const u32 now = Device.dwTimeGlobal;
	const u32 dt  = g->destroy_time();
	if (dt == 0xffffffff || dt <= now)	return;			// not armed, or already due to go off

	const u32 time_from_throw = g->m_dwDestroyTimeMax - (dt - now);

	if (g->m_dwSafeTime && g->m_dwSafeTime > time_from_throw)
	{
		g->m_dwDestroyTime = 0xffffffff;				// hit too early -> dud
		return;
	}
	if (g->m_dwDelayTime && g->m_dwDelayTime > time_from_throw)
		return;											// inside the arming delay -> keep the fuse

	u32 new_destroy_time = now;
	if (g->m_fMinExplosionSpeed > 0.f)
	{
		Fvector vel;
		g->PHGetLinearVell(vel);
		if (vel.magnitude() < g->m_fMinExplosionSpeed)
			new_destroy_time = g->m_bDeactivateOnMinSpeed ? 0xffffffff : dt;
	}
	g->m_dwDestroyTime = new_destroy_time;
}

// GS CheckGrenadeExplosionByHit: a damaged grenade cooks off, by hit TYPE rather than only by the
// explosion type the stock check hardcodes.
bool CGrenade::CheckExplosionByHit(const SHit* pHDS) const
{
	// GS `help_explosive_info`: opt-in per section, off everywhere unless you are tuning the
	// threshold -- it only fires when the grenade is actually hit, so it is not a hot path.
	if (m_bHelpExplosiveInfo)
		Msg("~ [grenade %s] hit type %d, power %f, impulse %f, threshold %f",
			cNameSect().c_str(), int(pHDS->hit_type), pHDS->damage(), pHDS->phys_impulse(),
			m_grenade_detonation_threshold_hit);

	if (!m_bExplosionOnHit)								return false;
	if (m_grenade_detonation_threshold_hit >= pHDS->damage())	return false;
	// an armed (thrown) grenade always cooks off; one still lying around only if the config says so
	if (Useful() && m_bHasExplosiveWhileKey && !m_bExplosiveWhileNotActivated)	return false;
	if (m_ExplosionHitTypes.empty())
		return ALife::eHitTypeExplosion == pHDS->hit_type;
	for (u32 t : m_ExplosionHitTypes)
		if (t == u32(pHDS->hit_type))	return true;
	return false;
}

void CGrenade::Hit					(SHit* pHDS)
{
	// GS CGrenade__OnHit_CanExplode_Patch REPLACES the stock condition rather than extending it, and
	// that turns out to be the whole point: the stock gate `CExplosive::Initiator()==u16(-1)` can
	// never be true, because Initiator() substitutes the grenade's OWN id whenever the parent id is
	// unset (Explosive.cpp). So the branch was dead code -- a grenade lying in the world took bullets
	// without ever cooking off, no matter what the config asked for. What is left is the config-driven
	// test, plus GS's null check on the hit source (`cmp edi, 0`) and a guard so a second hit landing
	// in the same frame cannot queue the explode event twice (GenExplodeEvent asserts on that).
	// Note the stock "explosion hit over the threshold" rule is intentionally gone: GS lists only
	// `6, 8` (chemical_burn + fire_wound) in explosion_hit_types, so grenades do NOT chain-detonate.
	if( CExplosive::Useful() && pHDS->who && CheckExplosionByHit(pHDS) )
	{
		CExplosive::SetCurrentParentID(pHDS->who->ID());
		Destroy();
	}
	inherited::Hit(pHDS);
}

BOOL CGrenade::net_Spawn(CSE_Abstract* DC) 
{
	m_dwGrenadeIndependencyTime			= 0;
	BOOL ret= inherited::net_Spawn		(DC);
	Fvector box;BoundingBox().getsize	(box);
	float max_size						= _max(_max(box.x,box.y),box.z);
	box.set								(max_size,max_size,max_size);
	box.mul								(3.f);
	CExplosive::SetExplosionSize		(box);
	m_thrown							= false;
	return								ret;
}

void CGrenade::net_Destroy() 
{
	inherited::net_Destroy				();
	CExplosive::net_Destroy				();
}

void CGrenade::OnH_B_Independent(bool just_before_destroy) 
{
	inherited::OnH_B_Independent(just_before_destroy);
}

void CGrenade::OnH_A_Independent() 
{
	m_dwGrenadeIndependencyTime			= Level().timeServer();
	inherited::OnH_A_Independent		();	
}

void CGrenade::OnH_A_Chield()
{
	m_dwGrenadeIndependencyTime			= 0;
	m_dwDestroyTime						= 0xffffffff;
	inherited::OnH_A_Chield				();
}

void CGrenade::State(u32 state) 
{
	switch (state)
	{
	case eThrowStart:
		{
			Fvector						C;
			Center						(C);
			PlaySound					("sndCheckout", C);
		}break;
	case eThrowEnd:
		{
			if(m_thrown)
			{
				if (m_pPhysicsShell)
					m_pPhysicsShell->Deactivate();
				xr_delete	( m_pPhysicsShell );
				m_dwDestroyTime			= 0xffffffff;
				PutNextToSlot			();
				if (Local())
				{
#ifndef MASTER_GOLD
					Msg( "Destroying local grenade[%d][%d]", ID(), Device.dwFrame );
#endif // #ifndef MASTER_GOLD
					DestroyObject();
				}
				
			};
		}break;
	};
	inherited::State( state );
}

bool CGrenade::DropGrenade()
{
	EMissileStates grenade_state = static_cast<EMissileStates>(GetState());
	if (((grenade_state == eThrowStart) ||
		(grenade_state == eReady) ||
		(grenade_state == eThrow)) &&
		(!m_thrown)
		)
	{
		Throw();
		return true;
	}
	return false;
}

void CGrenade::SendHiddenItem						()
{
	if (GetState()==eThrow)
	{
		Msg("MotionMarks !!![%d][%d]", ID(), Device.dwFrame);
		Throw				();
	}
	CActor* pActor = smart_cast<CActor*>( m_pInventory->GetOwner());
	if (pActor && (GetState()==eReady || GetState()==eThrow))
	{
		return;
	}

	inherited::SendHiddenItem();
}

void CGrenade::Throw() 
{
	if (m_thrown)
		return;

	if (!m_fake_missile)
		return;

	CGrenade					*pGrenade = smart_cast<CGrenade*>( m_fake_missile );
	VERIFY						(pGrenade);
	
	if (pGrenade) 
	{
		// The FAKE missile is the object that flies and that the contact fuse runs on, so it needs the
		// same destroy_time_MAX as the one we just armed it with. Without this it kept its own config
		// value (2500) while the fuse was set to e.g. the 700 ms suicide_fail_destroy_time, so
		// `time from throw` came out ~1800 ms -- past delay_time -- and an RGN/RGO thrown clear of a
		// broken controller grab still detonated on its first bounce.
		pGrenade->m_dwDestroyTimeMax = m_dwDestroyTimeMax;
		pGrenade->set_destroy_time(m_dwDestroyTimeMax);
//���������� ID ���� ��� ����� �������
		pGrenade->SetInitiator( H_Parent()->ID() );
	}
	inherited::Throw			();
	m_fake_missile->processing_activate();//@sliph
	m_thrown = true;
}



void CGrenade::Destroy() 
{
	//Generate Expode event
	Fvector						normal;
	FindNormal					(normal);
	CExplosive::GenExplodeEvent	(Position(), normal);
}



bool CGrenade::Useful() const
{

	bool res = (/* !m_throw && */ m_dwDestroyTime == 0xffffffff && CExplosive::Useful() && TestServerFlag(CSE_ALifeObject::flCanSave));

	return res;
}

void CGrenade::OnEvent(NET_Packet& P, u16 type) 
{
	inherited::OnEvent			(P,type);
	CExplosive::OnEvent			(P,type);
}

void CGrenade::PutNextToSlot()
{
	if (OnClient()) return;
//	Msg ("* PutNextToSlot : %d", ID());	
	VERIFY									(!getDestroy());
	//�������� ������� �� ���������
	NET_Packet						P;
	if (m_pInventory)
	{
		m_pInventory->Ruck					(this);
//.		m_pInventory->SetActiveSlot			(NO_ACTIVE_SLOT);

		this->u_EventGen				(P, GEG_PLAYER_ITEM2RUCK, this->H_Parent()->ID());
		P.w_u16							(this->ID());
		this->u_EventSend				(P);
	}
	else
		Msg ("! PutNextToSlot : m_pInventory = NULL [%d][%d]", ID(), Device.dwFrame);	

	if (smart_cast<CInventoryOwner*>(H_Parent()) && m_pInventory)
	{
		CGrenade *pNext						= smart_cast<CGrenade*>(	m_pInventory->Same(this,true)		);
		if(!pNext) pNext					= smart_cast<CGrenade*>(	m_pInventory->SameSlot(GRENADE_SLOT, this, true)	);

		VERIFY								(pNext != this);

		if(pNext && m_pInventory->Slot(pNext) ){

			pNext->u_EventGen				(P, GEG_PLAYER_ITEM2SLOT, pNext->H_Parent()->ID());
			P.w_u16							(pNext->ID());
			pNext->u_EventSend				(P);
//			if(IsGameTypeSingle())
				m_pInventory->SetActiveSlot			(pNext->GetSlot());
		}

		// GS CMissile__PutNextToSlot: a QUICK throw goes back to whatever was in your hands, it does
		// not leave you standing there holding the next grenade. The next one is still slotted above
		// (so the following quick throw has something to pull), it just never gets drawn.
		if (m_quick_throw_ret_slot != NO_ACTIVE_SLOT)
		{
			const u32 ret			= m_quick_throw_ret_slot;
			m_quick_throw_ret_slot	= NO_ACTIVE_SLOT;
			if (m_pInventory->ItemFromSlot(ret))		// the slot was active when the key was pressed
				m_pInventory->Activate	(ret);
		}
		// hands are handed back -- release the slot lock the key raised (see CMissile::QuickThrowBusy)
		ClearQuickThrowBusy();
		// GS RestoreLastActorDetector, called from the same place: drawing the grenade made the
		// detector incompatible and CheckCompatibility holstered it, which clears m_bNeedActivation --
		// so nothing remembered to bring it back. A quick throw is not a deliberate switch away from
		// the detector, so it comes back (GS forgets the auto-hide only on a NORMAL grenade draw,
		// Throwable.pas:304).
		if (m_quick_throw_had_det)
		{
			m_quick_throw_had_det	= false;
			CCustomDetector* det	= smart_cast<CCustomDetector*>(m_pInventory->ItemFromSlot(DETECTOR_SLOT));
			if (det)	det->RequestRestore();
		}

		m_thrown				= false;
	}
}

void CGrenade::OnAnimationEnd(u32 state)
{
	switch(state)
	{
	case eThrowEnd: SwitchState(eHidden);	break;
	case eHiding:
		{
			// the holster half of a GS type switch has finished -- now do the inventory move, which
			// puts the next grenade in the slot and makes it play its own draw
			if (m_pending_next_id != u16(-1) && m_pInventory)
			{
				CGrenade* next = NULL;
				for (TIItemContainer::iterator it = m_pInventory->m_ruck.begin();
					 it != m_pInventory->m_ruck.end(); ++it)
				{
					CGrenade* g = smart_cast<CGrenade*>(*it);
					if (g && g->ID() == m_pending_next_id)	{ next = g; break; }
				}
				m_pending_next_id = u16(-1);
				inherited::OnAnimationEnd(state);		// setVisible(FALSE) + eHidden, as usual
				if (next)
				{
					m_pInventory->Ruck				(this);
					m_pInventory->SetActiveSlot		(NO_ACTIVE_SLOT);
					m_pInventory->Slot				(next);
				}
				return;
			}
			inherited::OnAnimationEnd(state);
		} break;
	default : inherited::OnAnimationEnd(state);
	}
}


void CGrenade::UpdateCL()
{
	inherited::UpdateCL			();
	CExplosive::UpdateCL		();

	if(!IsGameTypeSingle())	make_Interpolation();
}


// Orders grenade kinds for the kWPN_NEXT cycle. By SECTION NAME on purpose: any order derived
// from the inventory changes as grenades move between the slot and the ruck, and an order that
// changes under you is exactly what stops a cycle from being one.
static bool grenade_kind_less(const CGrenade* a, const CGrenade* b)
{
	return xr_strcmp(a->cNameSect(), b->cNameSect()) < 0;
}

bool CGrenade::Action(s32 cmd, u32 flags) 
{
	if(inherited::Action(cmd, flags)) return true;

	switch(cmd) 
	{
	//������������ ���� �������
	case kWPN_NEXT:
		{
            if(flags&CMD_START)
			{
				if(m_pInventory)
				{
					// GS: the switch is a HOLSTER followed by a DRAW -- the current grenade goes away
					// with its own animation and the next type is then taken out, instead of swapping
					// in the hand instantly (vanilla did the inventory move right here, so only the
					// draw was ever seen). The swap itself happens in OnAnimationEnd(eHiding).
					if (GetState() != eIdle)	return true;	// mid gesture -- ignore

					// One representative per distinct SECTION, ourselves included -- we are in the slot,
					// not in the ruck, so we would otherwise be missing from our own cycle.
					// This used to take the first ruck grenade of a different section and stop, which is
					// not a cycle at all: the ruck order shifts every swap (the grenade we put away comes
					// back into it at a new position), so with three types the sequence wandered --
					// g1 -> g2 -> g1 -> g2 -> g3 -> g1. Ordering by section name makes it independent of
					// that churn, so the same set of grenades always cycles the same way.
					xr_vector<CGrenade*>	kinds;
					kinds.push_back		(this);

					TIItemContainer::iterator it = m_pInventory->m_ruck.begin();
					TIItemContainer::iterator it_e = m_pInventory->m_ruck.end();
					for(;it!=it_e;++it)
					{
						CGrenade *pGrenade = smart_cast<CGrenade*>(*it);
						if(!pGrenade)	continue;

						bool seen = false;
						for(u32 i=0; i<kinds.size(); ++i)
							if(!xr_strcmp(kinds[i]->cNameSect(), pGrenade->cNameSect()))	{ seen = true; break; }
						if(!seen)	kinds.push_back(pGrenade);
					}

					if(kinds.size() < 2)	return true;	// only our own type -- nothing to switch to

					std::sort(kinds.begin(), kinds.end(), grenade_kind_less);

					u32 cur = 0;
					for(u32 i=0; i<kinds.size(); ++i)
						if(kinds[i] == this)	{ cur = i; break; }

					CGrenade* pNext = kinds[(cur + 1) % kinds.size()];
					if(pNext == this)	return true;

					m_pending_next_id = pNext->ID();
					SwitchState(eHiding);
					return true;
				}
			}
			return true;
		};
	}
	return false;
}


bool CGrenade::NeedToDestroyObject()	const
{
	if ( IsGameTypeSingle()			) return false;
	if ( Remote()					) return false;
	if ( TimePassedAfterIndependant() > m_dwGrenadeRemoveTime)
		return true;

	return false;
}

ALife::_TIME_ID	 CGrenade::TimePassedAfterIndependant()	const
{
	if(!H_Parent() && m_dwGrenadeIndependencyTime != 0)
		return Level().timeServer() - m_dwGrenadeIndependencyTime;
	else
		return 0;
}

BOOL CGrenade::UsedAI_Locations		()
{
#pragma todo("Dima to Yura : It crashes, because on net_Spawn object doesn't use AI locations, but on net_Destroy it does use them")
	return TRUE;//m_dwDestroyTime == 0xffffffff;
}

void CGrenade::net_Relcase(CObject* O )
{
	CExplosive::net_Relcase(O);
	inherited::net_Relcase(O);
}

void CGrenade::DeactivateItem()
{
	//Drop grenade if primed
	StopCurrentAnimWithoutCallback();
	if ( !GetTmpPreDestroy() && Local() && ( GetState()==eThrowStart || GetState()==eReady || GetState()==eThrow ) )
	{
		if (m_fake_missile)
		{
			CGrenade*		pGrenade	= smart_cast<CGrenade*>( m_fake_missile );
			if ( pGrenade )
			{
				if ( m_pInventory->GetOwner() )
				{
					CActor* pActor = smart_cast<CActor*>( m_pInventory->GetOwner() );
					if (pActor)
					{
						if ( !pActor->g_Alive() )
						{
							m_constpower			= false;
							m_fThrowForce			= 0;
						}
					}
				}				
				Throw	();
			};
		};
	};

	inherited::DeactivateItem();
}

void CGrenade::GetBriefInfo(xr_string& str_name, xr_string& icon_sect_name, xr_string& str_count, string16& fire_mode)
{
	str_name				= NameShort();
	u32 ThisGrenadeCount	= m_pInventory->dwfGetSameItemCount( *cNameSect(), true );
	string16				stmp;
	xr_sprintf				( stmp, "%d", ThisGrenadeCount );
	str_count				= stmp;
	icon_sect_name			= *cNameSect();
}
