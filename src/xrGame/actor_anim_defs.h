#pragma once

#include "../Include/xrRender/KinematicsAnimated.h"

// Leg cycles for one movement speed. Stock CS knows a single, braced set ("_0"); the xrMPE pack
// ships a second, relaxed one next to it ("_1" -- feet under the hips instead of planted apart,
// knees far less bent) for norm and cr alike, and pairs it with <base>_turn_safe. Its engine builds
// both families (the _fwd_0/_fwd_1/_back_0/_back_1/... suffix table is in its xrGame.dll), so which
// one plays is a runtime choice, not an asset swap -- here that choice is `actor_legs_relaxed`.
struct SAnimState
{
	MotionID	legs_fwd;
	MotionID	legs_back;
	MotionID	legs_ls;
	MotionID	legs_rs;
	// Diagonals. Stock CS has none -- its if-chain gives forward/back priority over the strafe, so
	// walking forward-and-left plays the plain forward cycle. The pack ships the four blends the
	// xrMPE engine asks for (`_fwd_ls_0`, `_fwd_rs_0`, `_back_ls_0`, `_back_rs_0` and their _1 twins).
	MotionID	legs_fwd_ls;
	MotionID	legs_fwd_rs;
	MotionID	legs_back_ls;
	MotionID	legs_back_rs;

	MotionID	relaxed_fwd;
	MotionID	relaxed_back;
	MotionID	relaxed_ls;
	MotionID	relaxed_rs;
	MotionID	relaxed_fwd_ls;
	MotionID	relaxed_fwd_rs;
	MotionID	relaxed_back_ls;
	MotionID	relaxed_back_rs;
	// true only when the WHOLE relaxed set resolved -- the stock animations carry a lone
	// norm_walk_fwd_1 / norm_run_fwd_1 with no back/strafe twin, and half a set would walk forwards
	// relaxed and sidestep braced. The diagonals are NOT part of this test: they fall back one by one.
	bool		has_relaxed;

	IC const MotionID&	Pick	(const MotionID& base, const MotionID& rel, bool relaxed) const
	{ return (relaxed && has_relaxed && rel) ? rel : base; }

	IC const MotionID&	Fwd		(bool relaxed) const	{ return Pick(legs_fwd , relaxed_fwd , relaxed); }
	IC const MotionID&	Back	(bool relaxed) const	{ return Pick(legs_back, relaxed_back, relaxed); }
	IC const MotionID&	LS		(bool relaxed) const	{ return Pick(legs_ls  , relaxed_ls  , relaxed); }
	IC const MotionID&	RS		(bool relaxed) const	{ return Pick(legs_rs  , relaxed_rs  , relaxed); }
	// A missing diagonal degrades to the straight cycle it leans on = exactly the stock behaviour.
	IC const MotionID&	FwdLS	(bool relaxed) const
	{ const MotionID& d = Pick(legs_fwd_ls , relaxed_fwd_ls , relaxed); return d ? d : Fwd (relaxed); }
	IC const MotionID&	FwdRS	(bool relaxed) const
	{ const MotionID& d = Pick(legs_fwd_rs , relaxed_fwd_rs , relaxed); return d ? d : Fwd (relaxed); }
	IC const MotionID&	BackLS	(bool relaxed) const
	{ const MotionID& d = Pick(legs_back_ls, relaxed_back_ls, relaxed); return d ? d : Back(relaxed); }
	IC const MotionID&	BackRS	(bool relaxed) const
	{ const MotionID& d = Pick(legs_back_rs, relaxed_back_rs, relaxed); return d ? d : Back(relaxed); }

	void		Create								(IKinematicsAnimated* K, LPCSTR base0, LPCSTR base1);
};

struct STorsoWpn{
	enum eMovingState{eIdle, eWalk, eRun, eSprint, eTotal};
	MotionID	moving[eTotal];
	// The relaxed ("weapon down") twin of moving[]: <set>_idle_1 / _walk_1 / _run_1, which the pack
	// ships for EVERY set, not just the empty-handed slot 0 -- the same distinction its leg cycles
	// draw between "_0" and "_1". It must switch together with the legs: braced legs under a relaxed
	// chest (or the reverse) is a pose the pack never holds, and it reads as a twisted torso.
	// The sprint has no relaxed variant -- escape_0 is shared -- so that slot just mirrors moving[].
	MotionID	relaxed_moving[eTotal];
	bool		has_relaxed;

	IC const MotionID&	Moving	(eMovingState s, bool relaxed) const
	{ return (relaxed && has_relaxed && relaxed_moving[s]) ? relaxed_moving[s] : moving[s]; }

	MotionID	zoom;
	MotionID	holster;
	MotionID	draw;
	// The DEVICE going in or out of the off hand, as opposed to the weapon: the pack ships
	// <set>_drawdevice_0 / _holsterdevice_0 in every "+detector" set, plus _drawall_0 / _holsterall_0
	// for taking both out or putting both away at once. Nothing outside those sets defines them, so
	// they stay invalid everywhere else and the actor keeps its plain draw/holster.
	MotionID	draw_device;
	MotionID	holster_device;
	MotionID	draw_all;
	MotionID	holster_all;
	MotionID	drop;
	MotionID	reload;
	MotionID	reload_1;
	MotionID	reload_2;
	// Partial reload -- the magazine still had rounds in it. The stock animations have no such thing
	// (the actor always played the full one); the xrMPE pack ships `<base>_torso<base1>_reload_half_0`
	// for 21 of its sets, plus _1/_2 for the tri-state shotgun slot. Invalid when absent, and then the
	// full reload is used exactly as before.
	MotionID	reload_half;
	MotionID	reload_half_1;
	MotionID	reload_half_2;
	MotionID	attack;
	MotionID	attack_zoom;
	MotionID	fire_idle;
	MotionID	fire_end;

	//�������� ��� ����� ��� ����� ���� (����� �� ����� �� �����)
	MotionID	all_attack_0;
	MotionID	all_attack_1;
	MotionID	all_attack_2;
	// Take every action this set does not define from `src`. The xrMPE "<x>+detector" sets are
	// partial -- only pistol+detector has a reload, none has drop/_all_ -- and an invalid MotionID
	// here does not fall back on its own: it drops the actor to the empty-hands idle mid-action.
	void		FillGapsFrom						(const STorsoWpn& src);

	void		Create								(IKinematicsAnimated* K, LPCSTR base0, LPCSTR base1);
};

#define _total_anim_slots_ 13

struct SActorState
{
	// xrMPE-style per-weapon torso sets. Stock CS numbers them (`norm_torso_1_aim_1` ... `_13_`) and
	// the weapon picks one with `animation_slot`; the xrMPE animation pack instead ships NAMED sets --
	// norm_torso_ar_*, _abakan_*, _bullpup_*, _groza_*, _svd_*, _p90_*, _bizon_*, _machinegun_*,
	// _8_mini_*, plus a _gloff/_glon pair for every rifle that takes a launcher. Same action suffixes,
	// so STorsoWpn::Create eats them unchanged -- only the "base1" token differs.
	// Built on demand (a weapon names its set in `actor_anim_group`) and cached per actor state.
	using TorsoNamedMap = xr_map<shared_str, STorsoWpn>;
	TorsoNamedMap	m_torso_named;
	string16		m_anim_base;			// "norm" / "cr" -- the base0 the sets above are built with
	STorsoWpn*		TorsoNamed						(IKinematicsAnimated* K, const shared_str& group);

	MotionID		legs_idle;
	MotionID		jump_begin;
	MotionID		jump_idle;
	MotionID		landing[2];
	MotionID		legs_turn;
	// Relaxed twins of legs_idle / legs_turn: <base>_idle_1 and <base>_turn_safe. Both are gated on
	// m_walk.has_relaxed, not on their own existence -- the stock animations do have norm_idle_1 and
	// cr_idle_1, so keying off those alone would flip the stance on vanilla too.
	MotionID		legs_idle_relaxed;
	MotionID		legs_turn_relaxed;
	MotionID		death;
	SAnimState		m_walk;
	SAnimState		m_run;

	IC const MotionID&	IdleLegs(bool relaxed) const
	{ return (relaxed && m_walk.has_relaxed && legs_idle_relaxed) ? legs_idle_relaxed : legs_idle; }
	IC const MotionID&	TurnLegs(bool relaxed) const
	{ return (relaxed && m_walk.has_relaxed && legs_turn_relaxed) ? legs_turn_relaxed : legs_turn; }
	STorsoWpn		m_torso[_total_anim_slots_];
	MotionID		m_torso_idle;
	// Empty hands, by movement state. Stock CS holds the single `<base>_torso_0_aim_0` pose no matter
	// what the actor is doing; the xrMPE pack ships a real set for slot 0 -- idle_1 / walk_1 / run_1
	// (+ escape_0 for the sprint) -- and its aim_0 is an AIMING pose, which is why keeping the stock
	// choice with that pack looked wrong both standing and moving. Any of these that the loaded
	// animations do not define stays invalid and the code falls back to m_torso_idle = stock behaviour.
	MotionID		m_torso_none[STorsoWpn::eTotal];
	MotionID		m_head_idle;

	MotionID		m_damage[DAMAGE_FX_COUNT];
	void			Create							(IKinematicsAnimated* K, LPCSTR base);
	void			CreateClimb						(IKinematicsAnimated* K);
};

struct SActorSprintState 
{
	//leg anims
	MotionID		legs_fwd;
	MotionID		legs_ls;
	MotionID		legs_rs;
	
	MotionID		legs_jump_fwd;
	MotionID		legs_jump_ls;
	MotionID		legs_jump_rs;

	void Create		(IKinematicsAnimated* K);
};

struct SActorMotions
{
	MotionID			m_dead_stop;
	SActorState			m_normal;
	SActorState			m_crouch;
	SActorState			m_climb;
	SActorSprintState	m_sprint;
	void				Create(IKinematicsAnimated* K);
};

//vehicle anims
struct	SVehicleAnimCollection
{
	static const u16 MAX_IDLES = 3;
	u16				idles_num;
	MotionID		idles[MAX_IDLES];
	MotionID		steer_left;
	MotionID		steer_right;
					SVehicleAnimCollection	();
	void			Create				(IKinematicsAnimated* K,u16 num);
};
struct SActorVehicleAnims
{
	static const int TYPES_NUMBER=2;
	SVehicleAnimCollection m_vehicles_type_collections	[TYPES_NUMBER];
						SActorVehicleAnims				();
	void				Create							(IKinematicsAnimated* K);
};


