#pragma once
#include "../state.h"

template<typename _Object>
class	CStateBloodsuckerVampireExecute : public CState<_Object> {
	typedef CState<_Object>		inherited;

	enum {
		eActionApproach,
		eActionPrepare,
		eActionContinue,
		eActionFire,
		eActionWaitTripleEnd,
		eActionCompleted
	} m_action;

	u32					time_vampire_started;
	
	bool				m_effector_activated;

public:
						CStateBloodsuckerVampireExecute	(_Object *obj) : inherited(obj), m_hud_hidden(false) {}

	virtual void		initialize						();
	virtual	void		execute							();
	virtual	void		finalize						();
	virtual	void		critical_finalize				();
	virtual bool		check_start_conditions			();
	virtual bool		check_completion				();
	virtual void		remove_links					(CObject* object_) { inherited::remove_links(object_);}

private:
			void		execute_vampire_prepare			();
			void		execute_vampire_continue		();
			void		execute_vampire_hit				();

			void		look_head						();
			void		cleanup							();

	bool				m_hud_hidden;
	u32					m_time_approach_started;
	float				m_approach_best_dist;		// closest it has got so far
	u32					m_approach_best_time;		// when it last got closer
};

#include "bloodsucker_vampire_execute_inline.h"
