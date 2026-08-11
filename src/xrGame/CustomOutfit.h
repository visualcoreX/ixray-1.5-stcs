#pragma once

#include "inventory_item_object.h"

struct SBoneProtections;

class CCustomOutfit: public CInventoryItemObject {
private:
    typedef	CInventoryItemObject inherited;
public:
							CCustomOutfit		(void);
	virtual					~CCustomOutfit		(void);

	virtual void			Load				(LPCSTR section);
	
	//����������� ������ ����, ��� ������, ����� ������ ����� �� ���������
	virtual void			Hit					(float P, ALife::EHitType hit_type);

	//������������ �� ������� ����������� ���
	//��� ��������������� ���� �����������
	//���� �� ��������� ����� ������
	float					GetHitTypeProtection(ALife::EHitType hit_type, s16 element);
	float					GetDefHitTypeProtection(ALife::EHitType hit_type);
	float					GetBoneArmor(s16 element);

	float					HitThroughArmor		(float hit_power, s16 element, float ap, bool& add_wound );
	//����������� �� ������� ����������� ������ ����
	//���� �� ��������� ����� ������
	float					GetPowerLoss		();


	virtual void			OnMoveToSlot		();
	virtual void			OnMoveToRuck		(EItemPlace prev);

protected:
	HitImmunity::HitTypeSVec		m_HitTypeProtection;
	float							m_fPowerLoss;

	shared_str				m_ActorVisual;
	shared_str				m_full_icon_name;
	SBoneProtections*		m_boneProtection;	
protected:
	u32						m_ef_equipment_type;
	u32						m_artefact_count;

public:
	float					m_additional_weight;
	float					m_additional_weight2;

	float					m_fHealthRestoreSpeed; // as from artefact
	float 					m_fRadiationRestoreSpeed;
	float 					m_fSatietyRestoreSpeed;
	float					m_fPowerRestoreSpeed;
	float					m_fBleedingRestoreSpeed;

	shared_str				m_NightVisionSect;
	shared_str				m_BonesProtectionSect;

	// The headlamp is no longer something the actor simply owns: the light works only while a suit
	// that carries one is worn (`torch_enabled`, off by default, switched on by the suit's flashlight
	// upgrade -- exactly like m_NightVisionSect gates the goggles). CActor::SwitchTorch reads it.
	bool					m_bTorch;

	virtual u32				ef_equipment_type		() const;
	virtual	BOOL			BonePassBullet			(int boneID);
	const shared_str&		GetFullIconName			() const	{ return m_full_icon_name; }
	u32						get_artefact_count		() const	{ return m_artefact_count; }

	virtual BOOL			net_Spawn				(CSE_Abstract* DC);
	virtual void			net_Export				(NET_Packet& P);
	virtual void			net_Import				(NET_Packet& P);
			void			ApplySkinModel			(CActor* pActor, bool bDress, bool bHUDOnly);
			void			ReloadBonesProtection	(CActor* pActor);

protected:
	virtual bool			install_upgrade_impl( LPCSTR section, bool test );
};
