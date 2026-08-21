#include "stdafx.h"
#include "UIPdaWnd.h"
#include "../Pda.h"

#include "xrUIXmlParser.h"
#include "UIXmlInit.h"
#include "UIInventoryUtilities.h"

#include "../HUDManager.h"
#include "../level.h"
#include "../game_cl_base.h"

#include <luabind/functor.hpp>
#include "../ai_space.h"
#include "../../xrServerEntities/script_engine.h"
#include "../actor.h"
#include "../inventory.h"
#include "../HudItem.h"
#include "../Weapon.h"
#include "../WeaponMagazined.h"
#include "../UICursor.h"

#include "UIStatic.h"
#include "UIFrameWindow.h"
#include "UITabControl.h"
#include "UIPdaContactsWnd.h"
#include "UIMapWnd.h"
#include "UIFrameLineWnd.h"
#include "UIActorInfo.h"
#include "object_broker.h"
#include "UIMessagesWindow.h"
#include "UIMainIngameWnd.h"
#include "UITabButton.h"
#include "UIAnimatedStatic.h"

#include "UIHelper.h"
#include "UIHint.h"
#include "UIBtnHint.h"
#include "UITaskWnd.h"
#include "UIFactionWarWnd.h"
#include "UIRankingWnd.h"
#include "UILogsWnd.h"

#define PDA_XML		"pda.xml"

u32 g_pda_info_state = 0;

void RearrangeTabButtons(CUITabControl* pTab);

CUIPdaWnd::CUIPdaWnd()
{
	pUITaskWnd       = NULL;
	pUIFactionWarWnd = NULL;
	pUIRankingWnd    = NULL;
	pUILogsWnd       = NULL;
	m_hint_wnd       = NULL;
	Init();
}

CUIPdaWnd::~CUIPdaWnd()
{
	delete_data( pUITaskWnd );
	delete_data( pUIFactionWarWnd );
	delete_data( pUIRankingWnd );
	delete_data( pUILogsWnd );
	delete_data( m_hint_wnd );
	delete_data( UINoice );
}

void CUIPdaWnd::Init()
{
	CUIXml					uiXml;
	uiXml.Load				(CONFIG_PATH, UI_PATH, PDA_XML);

	m_pActiveDialog			= NULL;
	m_sActiveSection		= "";

	CUIXmlInit::InitWindow	(uiXml, "main", 0, this);

	UIMainPdaFrame			= UIHelper::CreateStatic( uiXml, "background_static", this );
	m_caption				= UIHelper::CreateStatic( uiXml, "caption_static", this );
	m_caption_const._set	( m_caption->GetText() );

	m_anim_static			= xr_new<CUIAnimatedStatic>();
	AttachChild				(m_anim_static);
	m_anim_static->SetAutoDelete(true);
	CUIXmlInit::InitAnimatedStatic(uiXml, "anim_static", 0, m_anim_static);

	m_btn_close				= UIHelper::Create3tButtonEx( uiXml, "close_button", this );
	m_hint_wnd				= UIHelper::CreateHint( uiXml, "hint_wnd" );
//	m_btn_close->set_hint_wnd( m_hint_wnd );


	if ( IsGameTypeSingle() )
	{
		pUITaskWnd					= xr_new<CUITaskWnd>();
		pUITaskWnd->hint_wnd		= m_hint_wnd;
		pUITaskWnd->Init			();

		pUIFactionWarWnd				= xr_new<CUIFactionWarWnd>();
		pUIFactionWarWnd->hint_wnd		= m_hint_wnd;
		pUIFactionWarWnd->Init			();

		pUIRankingWnd					= xr_new<CUIRankingWnd>();
		pUIRankingWnd->Init				();

		pUILogsWnd						= xr_new<CUILogsWnd>();
		pUILogsWnd->Init				();

	}

	UITabControl					= xr_new<CUITabControl>();
	UITabControl->SetAutoDelete		(true);
	AttachChild						(UITabControl);
	CUIXmlInit::InitTabControl		(uiXml, "tab", 0, UITabControl);
	UITabControl->SetMessageTarget	(this);

	UINoice					= xr_new<CUIStatic>();
	UINoice->SetAutoDelete	( true );
	CUIXmlInit::InitStatic	( uiXml, "noice_static", 0, UINoice );

	RearrangeTabButtons		(UITabControl);
}

void CUIPdaWnd::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
	switch ( msg )
	{
	case TAB_CHANGED:
		{
			if ( pWnd == UITabControl )
			{
				SetActiveSubdialog			(UITabControl->GetActiveId());
			}
			break;
		}
	case BUTTON_CLICKED:
		{
			if ( pWnd == m_btn_close )
			{
				HUD().GetUI()->StartStopMenu( this, true );
			}
			break;
		}
	default:
		{
			R_ASSERT						(m_pActiveDialog);
			m_pActiveDialog->SendMessage	(pWnd, msg, pData);
		}
	};
}

// Gunslinger-style 3D PDA: tell the script to put the PDA hud phantom in the actor's hands while
// this window is open (gwr_eatable.script spawns it in slot 10 and restores the previous slot on
// close). No-op if the script side isn't present.
static void gwr_call_pda(LPCSTR fn_name)
{
	luabind::functor<void>	fn;
	if (ai().script_engine().functor(fn_name, fn))
		fn();
}

// ---- 3D PDA cursor state (see Update() for the direction mapping) ----
extern int		g_pda_cursor_dir;
static Fvector2	s_pda_cur_last		= {0.f, 0.f};
static Fvector2	s_pda_cur_acc		= {0.f, 0.f};
static u32		s_pda_cur_tm		= 0;
static u32		s_pda_last_click	= 0;	// last seen CUIWindow::m_dwLastClickTime
static u32		s_pda_click_until	= 0;	// the click one-shot owns the hand until this time
// "lookout" = the mouse looks around instead of driving the PDA cursor. Follows the zoom (lowered
// PDA -> look around, PDA at the face -> cursor), and MMB flips it by hand. Gunslinger's naming.
static bool		s_pda_lookout		= true;
static bool		s_pda_zoom_init		= false;	// the open-zoomed-in has been applied for this open
// A freshly spawned phantom reports eIdle/!IsPending for a few frames BEFORE its draw starts, which
// is indistinguishable from "the draw has finished" -- and zooming there is worse than useless: the
// draw that follows resets the zoom, while s_pda_zoom_init is already spent, so the PDA ends up
// never zoomed at all. Wait until we've actually watched the draw begin.
static bool		s_pda_draw_seen		= false;
static u32		s_pda_open_tm		= 0;		// when the window opened (grace while the phantom is drawn)
static bool		s_pda_shown			= false;	// the PDA window is open right now
int				g_pda_dbg			= 0;		// hud-fov trace, opt-in via the g_pda_dbg console command
static u32		s_pda_dbg_tm		= 0;
ENGINE_API extern float psHUD_FOV;

// Flip between driving the PDA cursor and looking around. CUIDialogWnd::IR_OnMouseMove routes the
// delta to the actor (= the camera) when the cursor is out of play, so do NOT try to do it by
// refusing the input (returning false just drops it: no cursor AND no camera). We keep the cursor
// VISIBLE and frozen in place while looking around, so the flag -- not Hide() -- is what that
// routing tests (gwr_pda_lookout below).
//
// Freezing is enough to hold the DRAWN cursor still (UIDialogWnd is the only caller of
// UpdateCursorPosition, so skipping it pins vPos), but the OS cursor keeps drifting underneath
// while we steer the camera. Snap it back on the way out, or the cursor would jump to wherever
// the mouse wandered to.
bool gwr_pda_screen_active();			// defined below
extern bool g_pda_rt_pass;				// ...and so is this
static Fvector2	s_pda_cur_frozen;
static void pda_set_lookout(bool on)
{
	if (on == s_pda_lookout)	return;
	s_pda_lookout = on;
	if (on)	s_pda_cur_frozen = GetUICursor()->GetCursorPosition();
	else	GetUICursor()->SetUICursorPosition(s_pda_cur_frozen);
}

// Asked by CUIDialogWnd::IR_OnMouseMove to decide cursor-vs-camera.
bool gwr_pda_lookout()
{
	return s_pda_lookout && gwr_pda_screen_active();
}

// True only while OnRenderPdaUI is capturing the PDA screen, so callers can tell "draw me onto the
// model" from the ordinary full-screen pass.
bool gwr_pda_rt_pass_now()
{
	return g_pda_rt_pass;
}

// Aspect factor the map spots need while they are drawn onto the 3D PDA's screen.
//
// Spots are authored in square UI units and CMapSpot::Load narrows them by get_current_kx() so
// they come out square on the MONITOR, which stretches the 1024x768 UI space non-uniformly. The
// PDA model's screen is 4:3 -- the proportion the UI is authored in -- so the capture is shown
// there undistorted and that correction must be undone, or every icon sits 25% too narrow.
// Returns 1.0 outside the capture, so the ordinary full-screen PDA is bit-for-bit unchanged.
float g_pda_map_kx = 0.87f;		// matched against the 2D PDA by eye; 0.75 = no compensation,
								// 1.0 = fully undo the monitor correction (would be exact for a 4:3 screen)

float gwr_pda_map_kx()
{
	// deliberately the whole-frame predicate, not gwr_pda_rt_pass_now(): GetAspectKX() is also
	// asked during layout (OptimalFit / CalcOpenRect / scrolling), and a factor that differed
	// between layout and draw would slide the icons against the terrain.
	if (!gwr_pda_screen_active())		return 1.0f;
	float kx = UI()->get_current_kx();
	if (kx < EPS_S)					return 1.0f;
	return g_pda_map_kx / kx;
}

// The SAME idea for the map CANVAS (CUI*Map::GetAspectKX), kept as a separate knob on purpose.
// Icon size and the distance BETWEEN icons are driven by different things -- an icon's own
// width vs the map's zoom -- so one number cannot satisfy both. Default 0.75 == the monitor
// kx == no compensation at all, i.e. spacing stays exactly as it is on the 2D PDA; raise it
// toward 1.0 to widen the terrain if the canvas looks squeezed.
float g_pda_map_body_kx = 0.87f;	// canvas compensated like the icons, as asked

// The highlight ring around the ACTIVE side quest is the <static_border> of the composite spot
// (map_spots_complex*.xml, texture ui_pda2_stask_last_02a). It is a plain CUIStatic child, so
// CMapSpot::SetWndSize now stretches it by whatever horizontal factor the spot itself got --
// i.e. it follows g_pda_map_kx and matches the icon it wraps. This is a TRIM on top of that:
// 1.0 = exactly as wide as the icon's compensation, >1 wider, <1 narrower.
// (NOT the same thing the old value of this variable meant -- it used to be an absolute kx aimed
// at level_map_spot_border, which is the WRONG object: that one is drawn with <texture a="0">,
// i.e. fully transparent, which is why the knob appeared to do nothing at all.)
float g_pda_border_kx = 1.0f;

float gwr_pda_border_trim()
{
	if (!gwr_pda_screen_active())		return 1.0f;
	return g_pda_border_kx;
}

float gwr_pda_map_body_kx()
{
	if (!gwr_pda_screen_active())		return 1.0f;
	float kx = UI()->get_current_kx();
	if (kx < EPS_S)					return 1.0f;
	return g_pda_map_body_kx / kx;
}

// Gunslinger's _need_pda_zoom: set while the open-zoomed is still owed, cleared once it lands.
// CWeaponMagazined::PlayAnimShow reads it to pick the draw-to-the-face anim.
bool gwr_pda_need_fastzoom()
{
	return s_pda_shown && !s_pda_zoom_init;
}

// --- GS options pda_autozoom / pda_savezoomstate (gunsl_config.pas:1695/1700) ------------------
// autozoom  : the PDA opens ALREADY at the face instead of held down in the hand.
// savezoom  : ignore that option and reopen in whatever state the player last left it in.
// GS: NeedFastPdaZoom() = savezoom ? _last_pda_zoom_state : IsFastPdaZoom() (ActorUtils.pas:1995),
// with _last_pda_zoom_state seeded from the option at actor spawn (:2761) and re-recorded while the
// PDA is up (:2273). Both default to the behaviour we shipped before they existed: always zoom.
// Both are bits of psActorFlags (AF_PDA_AUTOZOOM / AF_PDA_SAVEZOOM) so the options menu can bind a
// checkbox straight to them; the defaults are set with the rest of psActorFlags in console_commands.cpp.
static bool s_pda_last_zoom_state = true;	// GS _last_pda_zoom_state

// The tutorial opens the PDA itself and then points at fixed spots on its screen (part_1_pda, the
// "how to use the PDA" lesson in the swamps). Lowered it would be pointing at nothing while the
// player looks around instead -- so a tutorial-driven open goes to the face whatever the two options
// say. Set by CUISequenceSimpleItem::Start right before it shows the window, dropped when it closes.
static bool s_pda_force_zoom = false;

void gwr_pda_force_zoom(bool on)
{
	s_pda_force_zoom = on;
}

static bool pda_need_fast_zoom()
{
	if (s_pda_force_zoom)	return true;
	return psActorFlags.test(AF_PDA_SAVEZOOM)
			? s_pda_last_zoom_state
			: !!psActorFlags.test(AF_PDA_AUTOZOOM);
}

// The glass shows only a CROP of the screen, so the cursor -- which is free to roam the whole
// 1024x768 UI space -- can walk off the visible area. Fence it in.
// Derived from the ui_tc line in shaders\r3\model_pda_screen.ps (and its r2 copy), but held a
// touch INSIDE it on X so the cursor stops before the very edge: shader crops 0.0915..0.9085,
// we fence 0.1..0.9. Retune the shader crop and these want a look too.
static const float	PDA_UI_X = 0.1000f, PDA_UI_W = 0.8000f;
static const float	PDA_UI_Y = 0.0388f, PDA_UI_H = 0.9095f;

// The slice of the 1024x768 UI space that actually reaches the model's screen. Anything that lays
// itself out against the full screen (button tooltips) must use this instead: by the full screen's
// reckoning it "fits" while sitting past the edge of the crop, i.e. invisible. False = PDA not up.
bool gwr_pda_visible_rect(Frect& r)
{
	if (!gwr_pda_screen_active())	return false;
	r.set(	 PDA_UI_X				* UI_BASE_WIDTH,	 PDA_UI_Y				* UI_BASE_HEIGHT,
			(PDA_UI_X + PDA_UI_W)	* UI_BASE_WIDTH,	(PDA_UI_Y + PDA_UI_H)	* UI_BASE_HEIGHT);
	return true;
}

static void pda_clamp_cursor()
{
	if (!gwr_pda_screen_active() || s_pda_lookout)	return;		// frozen while looking around anyway
	Fvector2 p = GetUICursor()->GetCursorPosition();
	Fvector2 c = p;
	clamp(c.x, PDA_UI_X * UI_BASE_WIDTH,  (PDA_UI_X + PDA_UI_W) * UI_BASE_WIDTH);
	clamp(c.y, PDA_UI_Y * UI_BASE_HEIGHT, (PDA_UI_Y + PDA_UI_H) * UI_BASE_HEIGHT);
	if (!fsimilar(c.x, p.x) || !fsimilar(c.y, p.y))
		GetUICursor()->SetUICursorPosition(c);	// drags the OS cursor along, so it can't drift off
}

// Zoom the PDA in/out. CWeapon::OnZoomIn only flips the WEAPON's flag -- the aim factor that
// actually drives hud_fov_aim (and the aim hud offset) is stepped in CWeapon::UpdateHudAdditonal
// from **pActor->IsZoomAimingMode()**, so without telling the ACTOR the factor just decays back to
// 0 and hud_fov_aim never applies at all (symptom: "even max g_pda_hud_fov_aim changes nothing").
// That's exactly why Gunslinger injects the kWPN_ZOOM *action* instead of calling OnZoomIn.
static void pda_set_zoom(CHudItem* hi, bool on)
{
	CWeapon* w = smart_cast<CWeapon*>(hi);
	if (!w)	return;
	if (on == !!w->IsZoomed())	return;

	if (on)	w->OnZoomIn();
	else	w->OnZoomOut();
	// GS records the state the PDA is in while it is up, so `pda_savezoomstate` can reopen it the same
	// way. Every route into the zoom goes through here (the auto-zoom on open and the RMB toggle alike).
	s_pda_last_zoom_state = on;
	// The indicators are killed wholesale by StartMenu(bDoHideIndicators) when the window opens;
	// re-derive them from the zoom instead, so a lowered PDA still shows health/stamina. StopMenu
	// restores whatever it saved at open, so this stays local to the PDA being up.
	if (HUD().GetUI())
		HUD().GetUI()->ShowGameIndicators(!on);
	// no SetZoomAimingMode here: CActor::UpdateCL re-derives it from pWeapon->IsZoomed() every
	// frame (Actor.cpp ~914/934), so setting it by hand is both redundant and immediately stomped
	pda_set_lookout(!on);			// at the face -> cursor; lowered -> look around
}

// The PDA phantom, if it's the item currently in hand.
// The level guard is NOT optional: Actor() does R_ASSERT2(GameID()==eGameIDSingle) (Actor_Network.cpp)
// which fires in RELEASE too, and this is reached from CUICursor::OnRender -- which also runs in the
// main menu, where there is no game yet. Without it the game dies with a fatal error on startup.
static CHudItem* pda_hud_item()
{
	if (!g_pGameLevel || !g_pGameLevel->bReady)	return NULL;
	CActor* a = Actor();
	PIItem  it = a ? a->inventory().ActiveItem() : NULL;
	CHudItem* hi = it ? it->cast_hud_item() : NULL;
	return (hi && hi->UsesPdaCursorAnims()) ? hi : NULL;
}

// ms after the window opens during which the PDA still "owns" the UI even though the phantom isn't
// in hand yet -- whatever was in hand has to holster first, and for those frames the vanilla
// full-screen window AND cursor used to flash. If the phantom never arrives (e.g. the script was
// busy eating), the grace expires and the plain UI comes back rather than leaving the player blind.
static const u32 PDA_SPAWN_GRACE = 2000;

// True while the UI (cursor included) belongs on the PDA model's screen and must NOT also be
// painted over the viewport. Used by CUIPdaWnd::Draw and CUICursor::OnRender -- they must agree,
// or one of them flashes for the frames the other doesn't.
bool gwr_pda_screen_active()
{
	// AF_PDA_3D off = the stock Clear Sky PDA. This one function is the whole switch: the window
	// draw, the cursor, the RT capture, the tutorial/talk suppression and the lookout routing all
	// ask it, so saying "no" here restores the vanilla full-screen behaviour everywhere at once.
	// (The phantom is what it really tests, and Show() doesn't spawn one when the flag is off --
	// this is the belt to that braces, and it also covers the spawn grace below.)
	if (!psActorFlags.test(AF_PDA_3D))	return false;
	if (pda_hud_item())	return true;
	return s_pda_shown && (Device.dwTimeGlobal - s_pda_open_tm < PDA_SPAWN_GRACE);
}

static void pda_reset_cursor(u32 click_tm)
{
	g_pda_cursor_dir  = 0;
	s_pda_cur_acc.set(0.f, 0.f);
	s_pda_cur_last	  = GetUICursor()->GetCursorPosition();
	s_pda_cur_frozen  = s_pda_cur_last;		// seed it: leaving lookout restores this, garbage would teleport the cursor
	s_pda_cur_tm	  = Device.dwTimeGlobal;
	s_pda_last_click  = click_tm;	// seed it, else the first Update would fire a phantom click
	s_pda_click_until = 0;
	// GS `_need_pda_zoom := NeedFastPdaZoom()` at open. Not owing a zoom is expressed by marking it
	// already done: gwr_pda_need_fastzoom() then reads false from the very first frame, so the draw
	// plays the ordinary anm_show instead of anm_show_fastzoom and nothing lifts the PDA afterwards.
	s_pda_zoom_init   = !pda_need_fast_zoom();
	s_pda_draw_seen   = false;		// this open's phantom hasn't started drawing yet
	s_pda_open_tm     = Device.dwTimeGlobal;
}

void CUIPdaWnd::Show()
{
	InventoryUtilities::SendInfoToActor	("ui_pda");
	if (g_pda_dbg)	Msg("~ pda: Show()");
	const bool pda_3d					= !!psActorFlags.test(AF_PDA_3D);
	if (pda_3d)
		gwr_call_pda					("gwr_eatable.on_pda_show");
	pda_reset_cursor					(m_dwLastClickTime);
	s_pda_shown							= true;
	if (pda_3d)
		pda_set_lookout					(true);		// opens lowered -> look around until RMB lifts it
	inherited::Show						();
	
	if ( !m_pActiveDialog )
	{
		SetActiveSubdialog				("eptTasks");
	}
	m_pActiveDialog->Show				(true);
	m_btn_close->Show					(true);
}

void CUIPdaWnd::Hide()
{
	inherited::Hide						();
	InventoryUtilities::SendInfoToActor	("ui_pda_hide");
	// NOT gated on AF_PDA_3D: if the option was switched off while a phantom was in hand, this is
	// the only thing that ever puts it away. The script side is a no-op when nothing was spawned.
	gwr_call_pda						("gwr_eatable.on_pda_hide");
	s_pda_shown							= false;
	s_pda_force_zoom					= false;	// the tutorial's hold on the zoom ends with this open
	pda_reset_cursor					(m_dwLastClickTime);	// don't leave a stale direction for the next open
	GetUICursor()->Show					();						// we may have hidden it; every other menu needs it back
	HUD().GetUI()->UIMainIngameWnd->SetFlashIconState_(CUIMainIngameWnd::efiPdaTask, false);
	m_pActiveDialog->Show				(false);
	m_btn_close->Show					(false);
	g_btnHint->Discard					();
}

// ---- 3D PDA: turn mouse movement into one of 8 hand directions (Gunslinger's cursor "joystick") ----
// The deltas are summed over g_pda_cursor_period and only then classified, so a jittery mouse can't
// flip the animation every frame. Under PDA_CURSOR_MOVE_TRESHOLD of travel = the hand rests (idle).
// Values (period/treshold, and the sector table) are Gunslinger's.
int				g_pda_cursor_period	 = 30;		// ms; GS: animation_update_period
int				g_pda_cursor_treshold = 2;		// px of accumulated travel; GS: PDA_CURSOR_MOVE_TREASURE
int				g_pda_use_clicks	 = 0;		// play pda_click on a UI click (off, as GS ships it)

// angle (rad, 0 = right, growing clockwise because screen Y points down) -> direction index.
// Sectors are pi/4 wide, offset by pi/8 so "right" straddles 0. Mirrors GS's GetPDADirByAngle.
static int pda_dir_by_angle(float a)
{
	if (a >= 0.393f && a < 1.18f)	return 4;	// down_right
	if (a >= 1.18f  && a < 1.96f)	return 5;	// down
	if (a >= 1.96f  && a < 2.74f)	return 6;	// down_left
	if (a >= 2.74f  && a < 3.53f)	return 7;	// left
	if (a >= 3.53f  && a < 4.32f)	return 8;	// up_left
	if (a >= 4.32f  && a < 5.10f)	return 1;	// up
	if (a >= 5.10f  && a < 5.89f)	return 2;	// up_right
	return 3;									// right
}

// Re-select the hud item's idle right away, so the hand follows the cursor without waiting for the
// current idle cycle to end. Returns true if the anim was actually (re)started.
static bool pda_apply_cursor_dir(CHudItem* hi, int dir)
{
	if (g_pda_cursor_dir == dir)	return false;
	g_pda_cursor_dir = dir;

	if (hi && hi->GetState() == CHUDState::eIdle && !hi->IsPending())
	{
		// ...but "eIdle and not pending" isn't the whole story: the aim-in transition runs exactly
		// like that on purpose (no SetPending, so firing can cut it), and for the PDA that transition
		// IS the second half of the draw (pda_aim_draw_2ndpart). Replaying an idle over it is what
		// made the draw blend oddly the moment the mouse moved.
		CWeaponMagazined* w = smart_cast<CWeaponMagazined*>(hi);
		if (w && w->IsAimTransitionPlaying())	return false;

		// don't replay the cursor idle over the emission glitch (anm_blowout) -- it plays in eIdle without
		// SetPending, so this loop would otherwise clobber it a frame after it starts (see CHudItem).
		if (hi->IsBlowoutAnimPlaying())			return false;

		hi->PlayAnimIdle();
		return true;
	}
	return false;
}

// Play the click gesture once for something that did NOT come through the window's own mouse
// handling -- the map's camp-names key (kALIFE_CMD) is the case this exists for. Direction 9 is the
// click slot, and the item picks pda_click or pda_aim_click from it depending on whether the PDA is
// being held to the face. Unlike the mouse path this ignores g_pda_use_clicks: the gesture was asked
// for on this action specifically, not as a general "click the screen" flourish. g_pda_cursor_dir is
// cleared first so a second press replays it instead of being swallowed as "already in that state".
void pda_play_click()
{
	CHudItem* hi = pda_hud_item();
	if (!hi)											return;
	if (Device.dwTimeGlobal < s_pda_click_until)		return;	// one is still on screen

	g_pda_cursor_dir = -1;
	if (pda_apply_cursor_dir(hi, 9))
		s_pda_click_until = hi->MotionEndTm();

	s_pda_cur_acc.set	(0.f, 0.f);
	s_pda_cur_tm		= Device.dwTimeGlobal;
}

// RMB = hold the PDA to the face / lower it; MMB = flip the mouse between cursor and looking around.
// The window is a dialog, so it gets the input first and the item would never see these itself --
// Gunslinger injects the same two actions for exactly this reason.
bool CUIPdaWnd::IR_OnKeyboardPress(int dik)
{
	CHudItem* hi = pda_hud_item();
	if (hi)
	{
		if (is_binded(kWPN_ZOOM, dik))
		{
			CWeapon* w = smart_cast<CWeapon*>(hi);
			if (w && hi->GetState() == CHUDState::eIdle && !hi->IsPending())
				pda_set_zoom(hi, !w->IsZoomed());
			return true;
		}
		if (dik == MOUSE_3)
		{
			pda_set_lookout(!s_pda_lookout);
			return true;
		}
		// GS: toggle the headlamp / night vision while the PDA is out -> the PDA plays its pda_headflash
		// gesture (see CActor::SwitchTorch/SwitchNightVision). The dialog otherwise eats these keys, so
		// inject them here exactly like kWPN_ZOOM above.
		if (is_binded(kNIGHT_VISION, dik))	{ if (CActor* a = Actor())	a->SwitchNightVision();	return true; }
		if (is_binded(kTORCH, dik))			{ if (CActor* a = Actor())	a->SwitchTorch();		return true; }
	}
	return inherited::IR_OnKeyboardPress(dik);
}

// false = we don't consume it, so it falls through to the camera (that IS "look around" mode).
bool CUIPdaWnd::IR_OnMouseMove(int dx, int dy)
{
	// only sum it while the cursor is what's moving; in look-around mode the delta is the camera's
	if (pda_hud_item() && !s_pda_lookout)
	{
		// drives the directional idles; raw mouse delta, like Gunslinger's accumulator
		s_pda_cur_acc.x += (float)dx;
		s_pda_cur_acc.y += (float)dy;
	}
	// the base sends it to the cursor or to the camera depending on the cursor's visibility
	bool res = inherited::IR_OnMouseMove(dx, dy);
	pda_clamp_cursor();		// ...and it just moved the cursor, so rein it back into the visible crop
	return res;
}

void CUIPdaWnd::Update()
{
	inherited::Update();
	m_pActiveDialog->Update();

	// only bother while the PDA phantom is the item in hand
	CHudItem* hi = pda_hud_item();

	// Fire the emission glitch anim from HERE (every frame), not only via the cursor idle: standing perfectly
	// still with the cursor centred never re-enters TryPlayAnimIdle, so the anim would otherwise wait until the
	// player moved. TryPlayBlowoutAnim latches (fires once) and only acts in eIdle, so polling it is safe.
	if (hi)	hi->TryPlayBlowoutAnim();

	if (g_pda_dbg && Device.dwTimeGlobal - s_pda_dbg_tm > 700)
	{
		s_pda_dbg_tm = Device.dwTimeGlobal;
		if (!hi)
		{
			// no phantom in hand -> everything below is skipped, which is itself the answer
			CActor* a = (g_pGameLevel && g_pGameLevel->bReady) ? Actor() : NULL;
			PIItem  it = a ? a->inventory().ActiveItem() : NULL;
			Msg("~ pda: NO phantom. active=[%s]", it ? it->object().cNameSect().c_str() : "none");
		}
		else
		{
			CWeapon* w = smart_cast<CWeapon*>(hi);
			Msg("~ pda: zoomed=%d  GetHudFov=%.3f  psHUD_FOV=%.3f  Device.fFOV=%.1f  -> hud %.1f deg",
				w ? (int)w->IsZoomed() : -1, hi->GetHudFov(), psHUD_FOV, Device.fFOV, psHUD_FOV*Device.fFOV);
		}
	}

	if (hi)
	{
		// Open already held to the face, like Gunslinger (its anm_show_fastzoom / pda_autozoom).
		// Applied ONCE per open, as soon as the draw settles -- RMB still toggles it afterwards.
		// the draw is under way (or done) -- from here on eIdle really does mean "settled"
		if (hi->GetState() == CHUDState::eShowing || hi->IsPending())
			s_pda_draw_seen = true;

		if (!s_pda_zoom_init && s_pda_draw_seen && hi->GetState() == CHUDState::eIdle && !hi->IsPending())
		{
			// Zoom FIRST, clear the flag after: OnZoomIn plays the aim-in transition synchronously,
			// and its selector has to still see gwr_pda_need_fastzoom() to pick the 2nd half of the
			// draw. Clearing first would silently give us the ordinary pda_aim_start. GS relies on
			// the same order (ActorUtils.pas: virtual_Action(kWPN_ZOOM), then _need_pda_zoom:=false).
			pda_set_zoom(hi, true);
			s_pda_zoom_init = true;
			if (g_pda_dbg)	Msg("~ pda: ZOOM applied t=%d (%d ms after open)",
								Device.dwTimeGlobal, Device.dwTimeGlobal - s_pda_open_tm);
		}

	}

	// INDICATORS FOLLOW THE ZOOM -- decided from the moment the window is up, not from when the phantom
	// lands, and not "hide first, then maybe show". StartMenu blanks them wholesale on open; pda_set_zoom
	// used to be what brought them back, which only ever worked because the PDA always went to the face.
	// Hidden iff the PDA IS at the face, or a zoom is still OWED for this open (pda_autozoom on, the draw
	// still playing) -- that second term is what stops the HUD flashing back in for the length of the
	// draw and then vanishing again. With pda_autozoom off nothing is owed, so the HUD simply stays up.
	// Outside the `if (hi)` above on purpose: it has to hold before the phantom exists too.
	// ShowGameIndicators only sets a flag, so calling it every frame costs nothing.
	// With the 3D PDA switched off there is nothing to lower the PDA into view for, so leave the
	// indicators to StartMenu/StopMenu -- stock behaviour is to blank them for the full-screen window.
	if (HUD().GetUI() && psActorFlags.test(AF_PDA_3D))
	{
		CWeapon* pw = smart_cast<CWeapon*>(hi);
		const bool zoom_now  = (pw && !!pw->IsZoomed());
		const bool zoom_owed = !s_pda_zoom_init;
		HUD().GetUI()->ShowGameIndicators(!(zoom_now || zoom_owed));
	}

	if (hi)
	{

		// (the movement is summed in IR_OnMouseMove, which sees the raw dx/dy)
		if (Device.dwTimeGlobal < s_pda_click_until)
		{
			// a click one-shot is on screen: leave the hand alone until it has played out, otherwise
			// the very next classification (30ms!) would cut it off. Movement keeps accumulating, so
			// the direction resumes from wherever the cursor got to.
		}
		else if (g_pda_use_clicks && m_dwLastClickTime != s_pda_last_click)
		{
			// the PDA window registered a click (same source Gunslinger reads)
			s_pda_last_click = m_dwLastClickTime;
			if (pda_apply_cursor_dir(hi, 9))
				s_pda_click_until = hi->MotionEndTm();
			s_pda_cur_acc.set(0.f, 0.f);
			s_pda_cur_tm = Device.dwTimeGlobal;
		}
		else if (Device.dwTimeGlobal - s_pda_cur_tm >= (u32)g_pda_cursor_period)
		{
			s_pda_cur_tm = Device.dwTimeGlobal;
			int dir = 0;
			if (_abs(s_pda_cur_acc.x) >= (float)g_pda_cursor_treshold || _abs(s_pda_cur_acc.y) >= (float)g_pda_cursor_treshold)
				dir = pda_dir_by_angle(angle_normalize(atan2f(s_pda_cur_acc.y, s_pda_cur_acc.x)));
			pda_apply_cursor_dir(hi, dir);
			s_pda_cur_acc.set(0.f, 0.f);
		}
	}

	Device.seqParallel.push_back	(fastdelegate::FastDelegate0<>(pUILogsWnd,&CUILogsWnd::PerformWork));
}

void CUIPdaWnd::SetActiveSubdialog(const shared_str& section)
{
	if ( m_sActiveSection == section ) return;

	if ( m_pActiveDialog )
	{
		UIMainPdaFrame->DetachChild( m_pActiveDialog );
		m_pActiveDialog->Show( false );
	}

	if ( section == "eptTasks" )
	{
		m_pActiveDialog = pUITaskWnd;
	}
	else if ( section == "eptFractionWar" )
	{
		m_pActiveDialog = pUIFactionWarWnd;
	}
	else if ( section == "eptRanking" )
	{
		m_pActiveDialog = pUIRankingWnd;
	}
	else if ( section == "eptLogs" )
	{
		m_pActiveDialog = pUILogsWnd;
	}

	R_ASSERT						(m_pActiveDialog);
	UIMainPdaFrame->AttachChild		(m_pActiveDialog);
	m_pActiveDialog->Show			(true);

	if ( UITabControl->GetActiveId() != section )
	{
		UITabControl->SetActiveTab( section );
	}
	m_sActiveSection = section;
	SetActiveCaption();
}

void CUIPdaWnd::SetActiveCaption()
{
	TABS_VECTOR*	btn_vec		= UITabControl->GetButtonsVector();
	TABS_VECTOR::iterator it_b	= btn_vec->begin();
	TABS_VECTOR::iterator it_e	= btn_vec->end();
	for ( ; it_b != it_e; ++it_b )
	{
		if ( (*it_b)->m_btn_id._get() == m_sActiveSection._get() )
		{
			LPCSTR cur = (*it_b)->GetText();
			string256 buf;
			strconcat( sizeof(buf), buf, m_caption_const.c_str(), cur );
			SetCaption( buf );
			return;
		}
	}
}

// Put the map back to the view the tutorial's highlights were authored against. CUIMapWnd::Activated
// only re-centres when the actor has moved >3m since last time, so a player who zoomed or panned and
// then stands still through the tutorial keeps their own view -- and every arrow points at nothing.
void CUIPdaWnd::ResetMapView()
{
	if (pUITaskWnd && pUITaskWnd->MapWnd())
		pUITaskWnd->MapWnd()->ResetToDefaultView();
}

void CUIPdaWnd::Show_SecondTaskWnd( bool status )
{
	if ( status )
	{
		SetActiveSubdialog( "eptTasks" );
	}
	pUITaskWnd->Show_SecondTasksWnd( status );
}

void CUIPdaWnd::Show_MapLegendWnd( bool status )
{
	if ( status )
	{
		SetActiveSubdialog( "eptTasks" );
	}
	pUITaskWnd->ShowMapLegend( status );
}

// The PDA window is drawn into the "$user$ui" render target before the scene (CLevel::OnRender ->
// gwr_pda_capture_ui) and the PDA model's screen material samples it -- so it already appears ON
// the model. Drawing it again in the normal UI pass would paste it over the whole viewport, which
// is exactly what Gunslinger's CUIPdaWnd::Draw "DisableMultiRender" patch suppresses.
//   g_pda_draw_ui 1 = also draw it full-screen (debug / fallback if the model path misbehaves).
int  g_pda_draw_ui = 0;
bool g_pda_rt_pass = false;		// true only while we're drawing into the RT

void CUIPdaWnd::Draw()
{
	// suppress ONLY the normal on-screen pass; the RT pass (g_pda_rt_pass) must always go through
	if (!g_pda_draw_ui && !g_pda_rt_pass && gwr_pda_screen_active())	return;
	inherited::Draw();
//.	DrawUpdatedSections();
	DrawHint();
	UINoice->Draw(); // over all
}

void CUIPdaWnd::DrawHint()
{
	if ( m_pActiveDialog == pUITaskWnd )
	{
		pUITaskWnd->DrawHint();
	}
	else if ( m_pActiveDialog == pUIFactionWarWnd )
	{
//		m_hint_wnd->Draw();
	}
	else if ( m_pActiveDialog == pUIRankingWnd )
	{

	}
	else if ( m_pActiveDialog == pUILogsWnd )
	{

	}
	m_hint_wnd->Draw();
}

void CUIPdaWnd::UpdatePda()
{
	pUILogsWnd->UpdateNews();

	if ( m_pActiveDialog == pUITaskWnd )
	{
		pUITaskWnd->ReloadTaskInfo();
	}
}

void CUIPdaWnd::Reset()
{
	inherited::ResetAll		();

	if ( pUITaskWnd )		pUITaskWnd->ResetAll();
	if ( pUIFactionWarWnd )	pUITaskWnd->ResetAll();
	if ( pUIRankingWnd )	pUITaskWnd->ResetAll();
	if ( pUILogsWnd )		pUITaskWnd->ResetAll();
}

void CUIPdaWnd::SetCaption( LPCSTR text )
{
	m_caption->SetText( text );
}

void RearrangeTabButtons(CUITabControl* pTab)
{
	TABS_VECTOR *	btn_vec		= pTab->GetButtonsVector();
	TABS_VECTOR::iterator it	= btn_vec->begin();
	TABS_VECTOR::iterator it_e	= btn_vec->end();

	Fvector2					pos;
	pos.set						((*it)->GetWndPos());
	float						size_x;

	for ( ; it != it_e; ++it )
	{
		(*it)->SetWndPos		(pos);
		(*it)->AdjustWidthToText();
		size_x					= (*it)->GetWndSize().x + 30.0f;
		(*it)->SetWidth			(size_x);
		pos.x					+= size_x - 6.0f;
	}
	
	pTab->SetWidth( pos.x + 5.0f );
	pos.x = pTab->GetWndPos().x - pos.x;
	pos.y = pTab->GetWndPos().y;
	pTab->SetWndPos( pos );
}
