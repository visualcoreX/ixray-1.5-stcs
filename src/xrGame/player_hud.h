#pragma once
#include "firedeps.h"

#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "actor_defs.h"

struct InertionData;
struct HudRecoilData;

class player_hud;
class CHudItem;
class CMotionDef;

struct motion_descr
{
	MotionID		mid;
	shared_str		name;
};

struct player_hud_motion
{
	shared_str				m_alias_name;
	shared_str				m_base_name;
	shared_str				m_additional_name;
	xr_vector<motion_descr>	m_animations;
};

struct player_hud_motion_container
{
	xr_vector<player_hud_motion>	m_anims;
	player_hud_motion*				find_motion(const shared_str& name);
	void		load				(IKinematicsAnimated* model, const shared_str& sect);
};

struct hud_item_measures
{
	enum{e_fire_point=(1<<0), e_fire_point2=(1<<1), e_shell_point=(1<<2), e_16x9_mode_now=(1<<3)};
	Flags8							m_prop_flags;

	Fvector							m_item_attach[2];//pos,rot

	Fvector							m_hands_offset[2][3];//pos,rot/ normal,aim,GL

	u16								m_fire_bone;
	Fvector							m_fire_point_offset;
	u16								m_fire_bone2;
	Fvector							m_fire_point2_offset;
	u16								m_shell_bone;
	Fvector							m_shell_point_offset;

	Fvector							m_hands_attach[2];//pos,rot

	// The hip shot's ray in the hands model's own space (player_hud::update_fire_ray): hip_fire_point /
	// hip_fire_dir from the hud section or else the weapon's own section (load_hip_fire), or -- without
	// them -- taken once off the weapon's fire point and
	// barrel while it stands at rest, and kept: the shot is not tied to the bone after that, so the
	// weapon's animations do not throw it about. hip_fire_converge > 0 turns the ray onto the middle of
	// the view that far ahead (m) instead of along the barrel.
	Fvector							m_hip_fire_point;
	Fvector							m_hip_fire_dir;
	float							m_hip_fire_converge;
	bool							m_hip_fire_set;		// the ray above is known (configured or taken)
	bool							m_hip_fire_cfg;		// configured: never taken off the barrel
	bool							m_hip_fire_dir_cfg;	// hip_fire_dir configured: the barrel gives the point only
	shared_str						m_hip_fire_wpn;		// the weapon section the keys were last looked up in
	void load_hip_fire				(const shared_str& sect_name, const shared_str& wpn_sect);

	// uniform scale of the ITEM model only (the hands keep their size). `item_scale` in the
	// hud section; 1.0 = as exported. For models that came out of the editor at the wrong unit
	// scale, which is otherwise only fixable by re-exporting the mesh.
	float							m_item_scale;

	void load						(const shared_str& sect_name, IKinematics* K);
};

struct attachable_hud_item
{
	player_hud*						m_parent;
	CHudItem*						m_parent_hud_item;
	shared_str						m_sect_name;
	IKinematics*					m_model;
	u16								m_attach_place_idx;
	hud_item_measures				m_measures;

	//runtime positioning
	Fmatrix							m_attach_offset;
	Fmatrix							m_item_transform;

	player_hud_motion_container		m_hand_motions;
			
			attachable_hud_item		(player_hud* pparent):m_parent(pparent),m_upd_firedeps_frame(u32(-1)),m_parent_hud_item(NULL){}
			~attachable_hud_item	();
	void load						(const shared_str& sect_name);
	void update						(bool bForce);
	void update_hud_additional		(Fmatrix& trans);
	void setup_firedeps				(firedeps& fd);
	void render						();	
	void render_item_ui				();
	bool render_item_ui_query		();
	bool need_renderable			();
	void set_bone_visible			(const shared_str& bone_name, BOOL bVisibility, BOOL bSilent=FALSE);
	void debug_draw_firedeps		();

	//hands bind position
	Fvector&						hands_attach_pos();
	Fvector&						hands_attach_rot();

	//hands runtime offset
	Fvector&						hands_offset_pos();
	Fvector&						hands_offset_rot();

//props
	u32								m_upd_firedeps_frame;
	void		tune				(Ivector values);
	u32			anim_play			(const shared_str& anim_name, BOOL bMixIn, const CMotionDef*& md, u8& rnd);

};

class player_hud
{
public: 
					player_hud			();
					~player_hud			();
	void			load				(const shared_str& model_name);
	void			load_default		(){load("actor_hud");};
	// GS defers a hands change until the hands are EMPTY (ActorUtils.pas player_hud__onloadrequest +
	// the request pump in the actor update): reloading the model under a drawn weapon restarts its
	// animation dead. Instead the weapon is holstered normally, the model is swapped while nothing is
	// on screen, and the weapon is drawn again -- what the player sees is an ordinary holster/draw.
	void			update_pending_load	();
private:
	void			load_now			(const shared_str& model_name);
public:
	void			update				(const Fmatrix& trans);
	void			render_hud			();
	// "hud_dbg_anim 1": top-left overlay listing every active blend of the hands model and of
	// each attached hud item -- source .omf, motion name, play time and blend state.
	void			draw_anim_debug		();
	void			render_item_ui		();
	bool			render_item_ui_query();
	u32				anim_play			(u16 part, const MotionID& M, BOOL bMixIn, const CMotionDef*& md, float speed, float blend_accrue = 0.f);
	const shared_str& section_name		() const {return m_sect_name;}

	attachable_hud_item* create_hud_item(const shared_str& sect);

	void			attach_item			(CHudItem* item);
	bool			allow_activation	(CHudItem* item);
	attachable_hud_item* attached_item	(u16 item_idx)	{return m_attached_items[item_idx];};
	void			detach_item_idx		(u16 idx);
	void			detach_item			(CHudItem* item);
	void			detach_all_items	(){m_attached_items[0]=NULL; m_attached_items[1]=NULL;};

	void			calc_transform		(u16 attach_slot_idx, const Fmatrix& offset, Fmatrix& result);
	void			tune				(Ivector values);
	u32				motion_length		(const MotionID& M, const CMotionDef*& md, float speed);
	u32				motion_length		(const shared_str& anim_name, const shared_str& hud_name, const CMotionDef*& md);
	void			OnMovementChanged	(ACTOR_DEFS::EMoveCommand cmd)	;
	// A hands bone's frame as seen from the transform the hud items' UpdateHudAdditonal receives: the hands
	// attach offset times the bone's model-space matrix (last calculated pose). For layering a motion on top
	// of that bone (CWeapon::UpdateLoweredPose). False if there is no hands model or no such bone.
	bool			hands_bone_frame	(LPCSTR bone, Fmatrix& out) const;
	// CHudItem::UpdateLoweredPose reports the hud transform just before and just after it put the weapon-down
	// motion on it. With a detector in the left hand, that hand takes the motion back (see update()).
	void			set_lowered_delta	(const Fmatrix& before, const Fmatrix& after)
										{ m_lower_before.set(before); m_lower_after.set(after); m_lower_reported = true; }

	// The weapon's own recoil (HudRecoilData.h). CActor::on_weapon_shot_start hands over each shot's
	// camera kick (rad: up, and sideways as the shot effector counts it); IR_OnMouseMove passes the mouse
	// through recoil_mouse (rad: right, up), which keeps the part the weapon takes inside its zone.
	// over_vert/over_horz: the part of the camera's share the weapon took that its zone had no room for
	// (camera_split), handed back to the camera in the shot effector's own terms.
	void			recoil_shot			(float kick_vert, float kick_horz, float& over_vert, float& over_horz);
	void			recoil_mouse		(float& right, float& up);
	void			recoil_reset		();
	// Where a shot of the hud weapon goes, in the hud camera's own axes and in world terms (the hud's fov
	// taken out, so the line covers the barrel's pixels): from the hip along the barrel, aiming from the
	// eye, blended by the aim factor -- carried by the sway and the recoil, never by the weapon's
	// animation. False when there is none for this item (another item, nothing fresh).
	bool			fire_ray_local		(const CHudItem* item, Fvector& pos, Fvector& dir) const;
	// console: take the hip ray off the barrel again / print it for the hud section
	void			fire_ray_resample	();
	void			fire_ray_dump		();
	// console: bake the hip ray off the FIRST frame of anm_idle (bake_hip_ray) -- the weapon in hand,
	// applied at once, or every gun's hud section into $logs$\hud_fire_rays.ltx
	void			fire_ray_bake		();
	void			fire_ray_bake_all	();
private:
	void			update_inertion		(Fmatrix& trans);
	void			update_additional	(Fmatrix& trans);
private:
	const Fvector&	attach_rot			() const;
	const Fvector&	attach_pos			() const;

	shared_str							m_sect_name;
	u32								m_want_frames;		// frames the hands have disagreed with the worn outfit
	u32								m_restore_slot;		// slot to draw again afterwards, NO_ACTIVE_SLOT = none
	bool							m_restore_detector;	// a detector was out and has to come back

	Fmatrix								m_attach_offset;

	// spring-damper sway (update_inertion), camera space
	struct SSway
	{
		Fvector		rot, rot_v;		// yaw, pitch, roll (rad) and their rates
		Fvector		pos, pos_v;		// x, y (m); z unused
		void		reset			();
		void		step			(const InertionData& in, const Fvector& rate, float dt);
		void		xform			(const InertionData& in, Fmatrix& L) const;	// v -> R (v - pivot) + pivot + shift
	};
	SSway								m_sway;						// the hud base: weapon, or the detector alone
	SSway								m_sway_left;				// the detector next to a weapon (left arm)
	bool								m_sway_split;				// m_sway_left drives the left arm this frame
	Fmatrix								m_sway_left_corr;			// world: weapon's sway -> detector's, for m_left_hold
	Fvector								m_sway_prev_dir;			// last frame's view direction
	u32									m_sway_frame;				// Device.dwFrame of the last step, 0 = none
	void			reset_sway			();

	// the weapon's own recoil: a lead over the camera (screen terms, rad), chased by a spring
	struct SRecoil
	{
		Fvector2	tgt;				// where the weapon heads for: x right, y up
		Fvector2	x, v;				// where it is, and how fast it moves
		float		last_shot;			// Device.fTimeGlobal of the last kick
		float		last_input;			// ...of the last kick or mouse move
		// the slide back (push_back): built up by the kicks alone, so steering the weapon with the
		// mouse does not take it away; m, where it heads for, where it is, how fast it moves
		float		push_tgt, push, push_v;
		struct SWave { float t, len, amp; };	// one roll wave per shot (elapsed, length, signed peak)
		enum { MAX_WAVES = 16 };
		SWave		waves[MAX_WAVES];
		int			wave_count;
		void		reset				();
		void		step				(const HudRecoilData& d, float dt);
		float		roll				() const;
	};
	SRecoil								m_recoil;
	const CHudItem*						m_recoil_item;		// whose recoil m_recoil is
	void			recoil_xform		(const HudRecoilData& d, const InertionData& in, Fmatrix& L) const;

	// this frame's hud camera and the camera with the sway and the recoil on it, before the item's own
	// offsets (aim, lowering); the shot's ray (fire_ray_local), its frame and item
	Fmatrix								m_cam_trans;
	Fmatrix								m_proc_trans;
	Fvector								m_fire_lpos, m_fire_ldir;
	u32									m_fire_frame;
	const CHudItem*						m_fire_item;
	float								m_fire_rest_since;	// the weapon stands at rest since (hip ray sample)
	void			update_fire_ray		();
	bool			bake_hip_ray		(attachable_hud_item* hi, Fvector& pos, Fvector& dir);
	void			bake_restore		();

	Fmatrix								m_transform;
	IKinematicsAnimated*				m_model;
	xr_vector<u16>						m_ancors;

	// Weapon down with a detector out: the left arm is kept where it would be without the motion.
	Fmatrix								m_lower_before, m_lower_after;	// see set_lowered_delta
	bool								m_lower_reported;
	Fmatrix								m_left_hold;		// model space, put on the left arm's root bones
	bool								m_left_hold_on;
	xr_vector<u16>						m_left_roots;		// bones of the "left_hand" part whose parent is outside it
	void								setup_left_hold		();
	static void							left_hold_callback	(CBoneInstance* B);
	attachable_hud_item*				m_attached_items[2];
	xr_vector<attachable_hud_item*>		m_pool;

};

extern player_hud* g_player_hud;