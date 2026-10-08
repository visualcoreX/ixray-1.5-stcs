//////////////////////////////////////////////////////////////////////
// ShellCasing.cpp:	a spent cartridge case thrown out of a weapon on a shot
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ShellCasing.h"
#include "PhysicsShell.h"
#include "PhysicsCommon.h"
#include "level.h"
#include "xrserver_objects.h"
#include "xrmessages.h"
#include "Hit.h"
#include "ExtendedGeom.h"
#include "PHSoundPlayer.h"
#include "MathUtils.h"
#include "PHWorld.h"
#include "../xrEngine/gamemtllib.h"
#include "../Include/xrRender/Kinematics.h"

extern ENGINE_API float psHUD_FOV;	// hud fov as a fraction of the world fov

// every case alive, oldest first -- for the cap and for the hud pass
static xr_vector<CShellCasing*>	s_casings;
// the last frame the hud pass ran (RenderHud); 0 = never
static u32						s_hud_pass_frame	= 0;
// cases that have appeared so far
static u32						s_spawned			= 0;

CShellCasing::CShellCasing()
{
	m_life_time			= 30000;
	m_destroy_time		= 0;
	m_mass				= 0.f;
	m_linear_scale		= 1.002f;
	m_angular_scale		= 1.01f;
	m_angular_limit		= 60.f;
	m_hud				= false;
	m_scale_from		= 1.f;
	m_draw_scale		= 1.f;
	m_scale_time		= 400;
	m_model_scale		= 1.f;
	m_max_count			= 0;
	m_hud_time			= 500;
	m_spawn_time		= 0;
	m_phys_xform.identity();
	m_launch_linear.set	(0.f, 0.f, 0.f);
	m_launch_angular.set(0.f, 0.f, 0.f);
	m_launch_time		= 0;
	m_char_collide_delay= 750;
	m_char_collide_at	= 0;
	m_hit_max_speed		= 8.f;
	m_snd_speed			= 1.f;
	m_snd_full_speed	= 6.f;
	m_snd_volume.set	(0.15f, 1.f);
	m_snd_gap			= 0.03f;
	m_contact_step		= 0;
	m_contact_fresh		= true;
	m_asleep			= false;
	m_snd_budget		= 4;
	m_snd_budget_window	= 100;
	m_processing		= false;
}

CShellCasing::~CShellCasing()
{
}

void CShellCasing::Load(LPCSTR section)
{
	inherited::Load		(section);
	// real seconds a case lies around before it is removed
	m_life_time			= iFloor(1000.f * READ_IF_EXISTS(pSettings, r_float, section, "lifetime", 30.f));
	m_mass				= READ_IF_EXISTS(pSettings, r_float, section, "ph_mass", 0.f);
	// velocity is divided by these every physics step (100 a second): 1 = no damping at all
	m_linear_scale		= READ_IF_EXISTS(pSettings, r_float, section, "ph_linear_scale", 1.002f);
	m_angular_scale		= READ_IF_EXISTS(pSettings, r_float, section, "ph_angular_scale", 1.01f);
	m_angular_limit		= READ_IF_EXISTS(pSettings, r_float, section, "ph_angular_limit", 60.f);	// rad/s
	m_scale_time		= iFloor(1000.f * READ_IF_EXISTS(pSettings, r_float, section, "scale_time", 0.4f));
	m_hud_time			= iFloor(1000.f * READ_IF_EXISTS(pSettings, r_float, section, "hud_draw_time", 0.5f));
	// a model cut smaller than the part of the hud model it stands in for (the magazines: 0.7): it starts out
	// drawn that much larger, so it still takes over at the hud's size, and comes down to its own
	m_model_scale		= READ_IF_EXISTS(pSettings, r_float, section, "model_scale", 1.f);
	if (m_model_scale < 0.1f)	m_model_scale = 0.1f;
	// a cap of their own: count_group = <name> and max_count = <n> (the magazines are not to push the cases
	// out, nor the other way round); none = the cases' shared one
	m_count_group		= READ_IF_EXISTS(pSettings, r_string, section, "count_group", "shell_casings");
	m_max_count			= (u32)READ_IF_EXISTS(pSettings, r_s32, section, "max_count", 0);
	// it is kept out of the characters' way while it leaves the gun (the shooter it appears next to would
	// shove it about), then they kick it about when they walk into it; < 0 = never
	const float ccd		= READ_IF_EXISTS(pSettings, r_float, section, "character_collide_delay", 0.75f);
	m_char_collide_delay= (ccd < 0.f) ? u32(-1) : u32(iFloor(1000.f * ccd));
	m_hit_max_speed		= READ_IF_EXISTS(pSettings, r_float, section, "hit_max_speed", 8.f);
	// the collide sound, by how fast the case hits the surface (see ContactSound)
	m_snd_speed			= READ_IF_EXISTS(pSettings, r_float, section, "collide_sound_speed", 1.f);
	m_snd_full_speed	= READ_IF_EXISTS(pSettings, r_float, section, "collide_sound_full_speed", 6.f);
	m_snd_volume		= READ_IF_EXISTS(pSettings, r_fvector2, section, "collide_sound_volume", Fvector2().set(0.15f, 1.f));
	// a contact is a hit only after this long without touching the level (s): one lying, sliding, rolling or
	// squeezed under a foot touches it every step and stays silent
	m_snd_gap			= READ_IF_EXISTS(pSettings, r_float, section, "collide_sound_gap", 0.03f);
	// no more than this many collide sounds of the same count_group within the window (s), all the cases together;
	// [shell_casings] holds the default, a section may have its own
	const u32 budget	= (u32)READ_IF_EXISTS(pSettings, r_s32, "shell_casings", "collide_sound_budget", 4);
	const float window	= READ_IF_EXISTS(pSettings, r_float, "shell_casings", "collide_sound_budget_window", 0.1f);
	m_snd_budget		= (u32)READ_IF_EXISTS(pSettings, r_s32, section, "collide_sound_budget", (s32)budget);
	m_snd_budget_window	= u32(iFloor(1000.f * READ_IF_EXISTS(pSettings, r_float, section, "collide_sound_budget_window", window)));
}

u32 CShellCasing::Count()
{
	return u32(s_casings.size());
}

u32 CShellCasing::Spawned()
{
	return s_spawned;
}

void CShellCasing::Spawn(LPCSTR section, const Fmatrix& xform, const Fvector& linear_vel, const Fvector& angular_vel, float hud_scale, u32 launch_time)
{
	if (!IsGameTypeSingle() || OnClient())	return;

	CSE_Abstract*		D = F_entity_Create(section);
	R_ASSERT2			(D, section);
	CSE_Temporary*		T = smart_cast<CSE_Temporary*>(D);
	R_ASSERT2			(T, "a shell casing section must be of a CSE_Temporary class (class = SHELL_CS)");
	T->m_tNodeID		= u32(-1);				// not on the AI map, see UsedAI_Locations
	D->s_name			= section;
	D->set_name_replace	("");
	D->s_RP				= 0xff;
	D->ID				= 0xffff;
	D->ID_Parent		= 0xffff;
	D->ID_Phantom		= 0xffff;
	D->s_flags.assign	(M_SPAWN_OBJECT_LOCAL);
	D->RespawnTime		= 0;
	D->o_Position.set	(xform.c);
	xform.getXYZ		(D->o_Angle);			// CGameObject::net_Spawn turns it back with setXYZ

	// the launch velocities, the hud size and the launch time ride in client_data and come back through net_Load
	float				v[7] = { linear_vel.x, linear_vel.y, linear_vel.z, angular_vel.x, angular_vel.y, angular_vel.z, hud_scale };
	D->client_data.resize(sizeof(v) + sizeof(u32));
	CopyMemory			(&*D->client_data.begin(), v, sizeof(v));
	CopyMemory			(&*D->client_data.begin() + sizeof(v), &launch_time, sizeof(u32));

	NET_Packet			P;
	D->Spawn_Write		(P, TRUE);
	Level().Send		(P, net_flags(TRUE));
	F_entity_Destroy	(D);
}

void CShellCasing::net_Load(IReader& ireader)
{
	// deliberately not inherited: this is not saved object state, it is what Spawn() wrote
	if (ireader.length() >= int(2 * sizeof(Fvector)))
	{
		ireader.r_fvector3	(m_launch_linear);
		ireader.r_fvector3	(m_launch_angular);
		if (ireader.elapsed() >= int(sizeof(float)))
		{
			const float s	= ireader.r_float();
			m_hud			= (s > 0.f);
			m_scale_from	= m_hud ? s / m_model_scale : 1.f;
			clamp			(m_scale_from, 1.f, 4.f);
		}
		if (ireader.elapsed() >= int(sizeof(u32)))
			m_launch_time	= ireader.r_u32();
	}
}

BOOL CShellCasing::net_Spawn(CSE_Abstract* DC)
{
	if (!inherited::net_Spawn(DC))
		return				FALSE;

	IKinematics* K			= smart_cast<IKinematics*>(Visual());
	R_ASSERT3				(K, "a shell casing needs a skeleton model with a bone shape", *cNameVisual());

	// The server hands the object over a frame or two after it was asked for. A case thrown from where an
	// animated bone was is moved on by that much, along its velocities -- or it appears a step behind the
	// bone it replaces, and seems to go back before it goes on.
	if (m_launch_time && (Device.dwTimeGlobal > m_launch_time))
	{
		float dt			= 0.001f * float(Device.dwTimeGlobal - m_launch_time);
		clamp				(dt, 0.f, 0.1f);
		Fmatrix& X			= XFORM();
		X.c.mad				(m_launch_linear, dt);
		const float wm		= m_launch_angular.magnitude();
		if (wm > EPS)
		{
			Fvector a;		a.div(m_launch_angular, wm);
			Fmatrix R;		R.rotation(a, wm * dt);
			R.transform_dir	(X.i);
			R.transform_dir	(X.j);
			R.transform_dir	(X.k);
		}
	}

	VERIFY					(!m_pPhysicsShell);
	m_pPhysicsShell			= P_build_Shell(this, false);
	if (m_mass > 0.f)
		m_pPhysicsShell->setMass		(m_mass);
	// a case must not be shoved out of the shooter it appears next to: no characters until it is clear (UpdateCL)
	m_pPhysicsShell->DisableCharacterCollision	();
	m_char_collide_at		= (m_char_collide_delay == u32(-1)) ? 0 : Device.dwTimeGlobal + m_char_collide_delay;
	// The stock "air resistance" is a torque of -w*k per step whatever the body weighs. Sized for crates
	// and barrels, on a body with the inertia of a cartridge case it overshoots hundreds of times over:
	// every step throws the spin to the other side, the limiter below cuts it, and the case neither
	// tumbles in the air nor comes to rest -- it hops about on the ground. A thrown grenade switches it
	// off for the same reason (CMissile::activate_physic_shell); the per-step scales below do the damping.
	m_pPhysicsShell->SetAirResistance	(0.f, 0.f);
	m_pPhysicsShell->set_DynamicScales	(m_linear_scale, m_angular_scale);
	// the stock spin limit is 9.8 rad/s, a turn and a half a second -- a case tumbles several times faster
	m_pPhysicsShell->set_DynamicLimits	(default_l_limit, m_angular_limit);
	// An activated shell is only half there: every element completes its activation in its FIRST bone
	// callback (CPHElement::BonesCallBack, flActivating -> SetTransform -> spatial_move). Left alone, that
	// callback comes from the renderer's CalculateBones, on the main thread, while the physics thread is
	// stepping this very object -- two threads re-inserting it into the physics spatial tree at once, and
	// a crash in ISpatial_DB::remove. So run it here and now, as every other shell owner does right after
	// the build; the velocities go in after it.
	K->CalculateBones_Invalidate	();
	K->CalculateBones		(TRUE);
	m_pPhysicsShell->set_LinearVel	(m_launch_linear);
	m_pPhysicsShell->set_AngularVel	(m_launch_angular);
	m_pPhysicsShell->set_ContactCallback(ContactSound);

	setVisible				(TRUE);
	setEnabled				(TRUE);
	// UpdateCL every frame for the whole (short) life: it moves the model after the physics body
	// and it is what counts the life down
	processing_activate		();
	m_processing			= true;

	m_destroy_time			= Device.dwTimeGlobal + m_life_time;
	m_spawn_time			= Device.dwTimeGlobal;
	m_phys_xform.set		(XFORM());
	m_draw_scale			= m_scale_from;
	m_spawn_pos.set			(Position());
	m_reported				= false;
	++s_spawned;

	// the cap: over it, the oldest of the same group go first
	s_casings.push_back		(this);
	if (m_max_count || pSettings->section_exist("shell_casings"))
	{
		const u32 max_count	= m_max_count ? m_max_count : (u32)READ_IF_EXISTS(pSettings, r_s32, "shell_casings", "max_count", 150);
		u32 alive			= 0;
		for (u32 i = 0; i < s_casings.size(); ++i)
			if (s_casings[i]->m_destroy_time && (s_casings[i]->m_count_group == m_count_group))	++alive;
		for (u32 i = 0; i < s_casings.size() && alive > max_count; ++i)
			if (s_casings[i]->m_destroy_time && (s_casings[i]->m_count_group == m_count_group))	{ s_casings[i]->m_destroy_time = 0; --alive; }
	}
	return					TRUE;
}

void CShellCasing::net_Destroy()
{
	xr_vector<CShellCasing*>::iterator it = std::find(s_casings.begin(), s_casings.end(), this);
	if (it != s_casings.end())
		s_casings.erase		(it);

	if (m_processing)
	{
		processing_deactivate	();
		m_processing		= false;
	}
	inherited::net_Destroy	();		// takes the physics shell with it
}

void CShellCasing::UpdateCL()
{
	if (m_pPhysicsShell && m_pPhysicsShell->isActive())
		m_pPhysicsShell->InterpolateGlobalTransform(&XFORM());
	m_phys_xform.set		(XFORM());
	// a disabled body gets no contacts at all, so the gap ContactSound looks for would open up while it just
	// lies there -- and the first contact after it is woken (a foot on it) would count as a hit
	if (m_pPhysicsShell && !m_pPhysicsShell->isEnabled())
		m_asleep			= true;

	inherited::UpdateCL		();

	if (m_char_collide_at && (Device.dwTimeGlobal >= m_char_collide_at))
	{
		m_char_collide_at	= 0;
		if (m_pPhysicsShell)	m_pPhysicsShell->EnableCharacterCollision();
	}

	// A case thrown out of a gun in the hands is seen next to that gun, and the gun is drawn with the
	// narrower hud projection: at its real size the case would look a good third smaller than the shell
	// of the hud model it has just replaced. So it starts out drawn larger by the ratio of the two
	// projections and comes down to its real size while it flies, to lie on the ground as small as it is.
	// Only the drawing: the transform is rebuilt from the physics body every frame (above), the body and
	// its collision shape never see the scale.
	m_draw_scale			= 1.f;
	if (m_scale_from > 1.f)
	{
		const float t	= m_scale_time ? float(Device.dwTimeGlobal - m_spawn_time) / float(m_scale_time) : 1.f;
		if (t >= 1.f)
			m_scale_from = 1.f;
		else
		{
			const float k	= 1.f - t * t * (3.f - 2.f * t);		// eased: 1 at the spawn, 0 at the end
			m_draw_scale	= 1.f + (m_scale_from - 1.f) * k;
			Fmatrix S;		S.scale(m_draw_scale, m_draw_scale, m_draw_scale);
			XFORM().mulB_43	(S);
		}
	}

	// debug = true in [shell_casings]: where the case has got to four seconds after the throw
	if (!m_reported && (Device.dwTimeGlobal - m_spawn_time > 4000))
	{
		m_reported			= true;
		if (pSettings->section_exist("shell_casings") && READ_IF_EXISTS(pSettings, r_bool, "shell_casings", "debug", FALSE))
		{
			Fvector v;		v.set(0.f, 0.f, 0.f);
			if (m_pPhysicsShell)	m_pPhysicsShell->get_LinearVel(v);
			const Fvector& p = m_phys_xform.c;
			Msg				("~ shell_casing [%s] after 4 s: moved (%.2f, %.2f, %.2f) from the spawn point, %.2f m from the camera, speed %.2f m/s, body %s",
							cNameSect().c_str(), p.x - m_spawn_pos.x, p.y - m_spawn_pos.y, p.z - m_spawn_pos.z,
							p.distance_to(Device.vCameraPosition), v.magnitude(),
							(m_pPhysicsShell && m_pPhysicsShell->isEnabled()) ? "moving" : "at rest");
		}
	}

	if (Device.dwTimeGlobal >= m_destroy_time)
		DestroyObject		();		// once: it flags the object as removed itself
}

// A bullet's impulse is sized for crates and bodies: on a 20 g case it would be thousands of m/s, the speed
// limit would cut it, and the case would just vanish. As a real one would, it is flicked away -- at up to
// hit_max_speed (cform = skeleton in the section is what lets the bullets and the blasts find it at all).
void CShellCasing::Hit(SHit* pHDS)
{
	if (!pHDS || !m_pPhysicsShell)	return;
	SHit H				= *pHDS;
	const float cap		= m_pPhysicsShell->getMass() * m_hit_max_speed;
	if (H.impulse > cap)
		H.impulse		= cap;
	inherited::Hit		(&H);
}

// The collide sounds started lately, per count_group: the budget of ContactSound. Only the physics thread
// (the contact callbacks) touches it.
struct SShellSoundBudget
{
	shared_str	group;
	u32			window_start;	// Device.dwTimeGlobal
	u32			count;			// sounds started since then
};
static xr_vector<SShellSoundBudget>	s_sound_budget;

// The stock contact callback (ContactShotMark) weighs a hit as speed * sqrt(mass) against a threshold sized
// for crates and bodies: on a 20 g case that takes some 70 m/s into the floor, so a case never made a sound.
// This one goes by the speed alone -- collide_sound_speed .. collide_sound_full_speed m/s along the surface
// normal, at collide_sound_volume x .. y -- and leaves the rest as it was: the sound comes from the case's
// material pair with the level's surface, through the case's own sound player (one at a time). The same
// threshold holds for a passable surface (water): a case resting in it is not splashing all the time.
// No particles and no wallmarks: the case pairs carry none.
//
// The speed alone is not enough to tell a hit, though. A case a character stands on is squeezed between the
// foot and the floor (70 kg on 20 g), and every physics step throws it into the floor faster than any
// threshold -- a scatter of cases underfoot went off all at once. So a contact counts only when it comes
// after collide_sound_gap s off the level: a case lying, sliding, rolling or squeezed touches it every step
// and is silent, one landing after its flight or a hop is heard. And all the cases of a count_group together
// start no more than collide_sound_budget sounds within collide_sound_budget_window s.
void CShellCasing::ContactSound(CDB::TRI* T, dContactGeom* c)
{
	dBodyID b				= dGeomGetBody(c->g1);
	dxGeomUserData* data	= NULL;
	if (b)
		data				= dGeomGetUserData(c->g1);
	else
	{
		b					= dGeomGetBody(c->g2);
		data				= dGeomGetUserData(c->g2);
	}
	if (!b || !data)	return;

	CShellCasing* self		= smart_cast<CShellCasing*>(data->ph_ref_object);
	if (!self)			return;

	// a new run of contacts or the same one as the last step's -- worked out once per step, the first contact
	// of the step decides (a case touches down on several points at once)
	const u32 step			= u32(ph_world->m_steps_num);
	if (step != self->m_contact_step)
	{
		const u32 gap_steps	= u32(_max(1, iCeil(self->m_snd_gap / fixed_step)));
		self->m_contact_fresh = !self->m_asleep && (step - self->m_contact_step > gap_steps);
		self->m_asleep		= false;
		self->m_contact_step = step;
	}
	if (!self->m_contact_fresh)	return;

	static const float SOUND_DIST = 70.f;		// as for any other object (physics_game.cpp)
	if (Device.vCameraPosition.distance_to_sqr(cast_fv(c->pos)) > SOUND_DIST * SOUND_DIST)	return;

	dVector3 vel;
	dBodyGetPointVel		(b, c->pos[0], c->pos[1], c->pos[2], vel);
	const float speed		= dFabs(dDOT(vel, c->normal));
	if (speed <= self->m_snd_speed)	return;

	SGameMtlPair* mtl_pair	= GMLib.GetMaterialPair(T->material, data->material);
	if (!mtl_pair || mtl_pair->CollideSounds.empty())	return;

	float k					= (self->m_snd_full_speed > self->m_snd_speed) ?
							  (speed - self->m_snd_speed) / (self->m_snd_full_speed - self->m_snd_speed) : 1.f;
	clamp					(k, 0.f, 1.f);
	const float volume		= self->m_snd_volume.x + k * (self->m_snd_volume.y - self->m_snd_volume.x);
	if (volume <= 0.f)	return;

	SShellSoundBudget* budget = NULL;
	if (self->m_snd_budget)
	{
		for (u32 i = 0; i < s_sound_budget.size() && !budget; ++i)
			if (s_sound_budget[i].group == self->m_count_group)	budget = &s_sound_budget[i];
		if (!budget)
		{
			SShellSoundBudget B;	B.group = self->m_count_group;	B.window_start = 0;	B.count = 0;
			s_sound_budget.push_back(B);
			budget			= &s_sound_budget.back();
		}
		if (Device.dwTimeGlobal - budget->window_start >= self->m_snd_budget_window)
		{
			budget->window_start = Device.dwTimeGlobal;
			budget->count	= 0;
		}
		if (budget->count >= self->m_snd_budget)	return;
	}

	// the hit is used up whether it is heard or not: the next contact of this run is not a new one
	self->m_contact_fresh	= false;
	if (self->ph_sound_player()->Play(mtl_pair, cast_fv(c->pos), volume) && budget)
		++budget->count;
}

bool CShellCasing::in_hud_phase() const
{
	return m_hud && (Device.dwTimeGlobal - m_spawn_time < m_hud_time);
}

// The gun in the hands is drawn in a pass of its own, after the world and over it: whatever of the world
// lies behind the gun's picture on the screen is covered by it, however near it is. A case that leaves
// the port across the gun (a PKM throws its cases to the left, over the receiver) would be thrown
// straight behind that picture and never be seen. So for its first moments the case is drawn in that same
// pass -- see RenderHud -- and is left out of the world one.
void CShellCasing::renderable_Render()
{
	if (in_hud_phase() && s_hud_pass_frame && (s_hud_pass_frame + 1 >= Device.dwFrame))
		return;
	inherited::renderable_Render	();
}

// Drawn with the hud projection, the case has to stand where the hud would show it: the spawn point was
// moved so that the WORLD projection shows it at the port (see casing_point_hud_to_world), this is that
// correction taken back, every frame, for wherever the body has flown to. And it needs no enlarging here:
// the hud projection already shows it larger by the very ratio the world drawing makes up with a scale.
// The two drawings give the same picture, so the hand-over between them is not seen.
void CShellCasing::RenderHud()
{
	const float t_hud = tanf(deg2rad(0.5f * psHUD_FOV * Device.fFOV_HUD));
	const float t_wld = tanf(deg2rad(0.5f * Device.fFOV));
	if (t_hud <= EPS_L || t_wld <= EPS_L)	return;		// (and the world pass keeps drawing them)
	const float k = t_hud / t_wld;

	s_hud_pass_frame			= Device.dwFrame;

	const Fvector& cpos			= Device.vCameraPosition;
	const Fvector& cdir			= Device.vCameraDirection;
	Fmatrix H;					H.identity();
	H.i.set						(k + (1.f - k) * cdir.x * cdir.x, (1.f - k) * cdir.x * cdir.y, (1.f - k) * cdir.x * cdir.z);
	H.j.set						((1.f - k) * cdir.y * cdir.x, k + (1.f - k) * cdir.y * cdir.y, (1.f - k) * cdir.y * cdir.z);
	H.k.set						((1.f - k) * cdir.z * cdir.x, (1.f - k) * cdir.z * cdir.y, k + (1.f - k) * cdir.z * cdir.z);
	{
		Fvector a;				H.transform_dir(a, cpos);
		H.c.sub					(cpos, a);
	}
	for (u32 i = 0; i < s_casings.size(); ++i)
	{
		CShellCasing* C			= s_casings[i];
		if (!C->in_hud_phase() || !C->Visual())	continue;

		Fmatrix M;				M.set(C->m_phys_xform);
		Fvector v;				v.sub(M.c, cpos);
		if (v.dotproduct(cdir) <= EPS_L)	continue;		// behind the camera
		Fmatrix S;				S.scale(C->m_draw_scale, C->m_draw_scale, C->m_draw_scale);
		M.mulB_43				(S);
		// Every point of it, not just its centre, goes where the hud projection shows what the world one would:
		// across the line of sight by k, along it unchanged (x' = cpos + A(x - cpos), A = k I + (1-k) d d^T) --
		// at the end of the hud phase, so the world pass takes over seamlessly. At the start its depth is
		// squeezed by k as well (about its centre): that is how the hud model it replaces was drawn. In between
		// the one goes over into the other. (A uniform k all the way flattened the perspective; a magazine, big
		// and close to the camera, visibly turned over the moment the world pass took it on.)
		M.mulA_43				(H);
		float t					= C->m_hud_time ? float(Device.dwTimeGlobal - C->m_spawn_time) / float(C->m_hud_time) : 1.f;
		clamp					(t, 0.f, 1.f);
		const float g			= k + (1.f - k) * t * t * (3.f - 2.f * t);
		Fmatrix D;				D.identity();
		D.i.set					(1.f + (g - 1.f) * cdir.x * cdir.x, (g - 1.f) * cdir.x * cdir.y, (g - 1.f) * cdir.x * cdir.z);
		D.j.set					((g - 1.f) * cdir.y * cdir.x, 1.f + (g - 1.f) * cdir.y * cdir.y, (g - 1.f) * cdir.y * cdir.z);
		D.k.set					((g - 1.f) * cdir.z * cdir.x, (g - 1.f) * cdir.z * cdir.y, 1.f + (g - 1.f) * cdir.z * cdir.z);
		{
			Fvector a;			D.transform_dir(a, M.c);
			D.c.sub				(M.c, a);
		}
		M.mulA_43				(D);

		::Render->set_Transform	(&M);
		::Render->add_Visual	(C->Visual());
	}
}
