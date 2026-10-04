/* Part of Descent 1 & 2 VR, a derivative of DXX-Redux.
 * Distributed under the Parallax and D1X-Rebirth licenses in COPYING.txt:
 * non-commercial use only, and the source of any modified version must be
 * freely and publicly available. */

/* vr_descent.c - Descent's side of VR, for both games.  See vr_descent.h.
 *
 * Built on the stock DXX-Redux v1.1 engine and the GameOrDieXR layer
 * (vr/vr_xr.h).
 *
 * One file for Descent 1 and Descent 2: each game's build compiles it with
 * its own engine headers, and Descent 2's build defines VRD_DESCENT2 for the
 * few things only it has (the cockpit's small extra views, its mission).
 *
 * How an eye is drawn.  The engine transforms on the CPU in fixed point and
 * hands GL view-space vertices (x right, y up, -z ahead after its own flip).
 * For one eye we:
 *   - make the whole screen, and the 3D window in it, the eye's size, so
 *     every viewport and every 2D coordinate the engine computes lands in the
 *     eye target;
 *   - scale view space so the CPU's own +-1 clip cone (the one that culls
 *     polygons and portals) just covers the eye's widest tangents, through
 *     the stock aspect and zoom (g3_start_frame, g3_set_view_matrix);
 *   - give GL the eye's real, asymmetric frustum in that scaled space.
 * The camera is the ship's pose composed with the head's, the way the stock
 * rear view composes its turned head onto the ship.
 *
 * Everything flat goes on the panel, a quad hanging in the cockpit: the menus
 * (over the paused game, which stays in the eyes) and, in flight, the HUD.
 * Only the crosshair stays in the eyes, placed where the ship's aim line
 * meets the mine, so both eyes see one crosshair at the target's depth. */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pstypes.h"
#include "maths.h"
#include "vecmat.h"
#include "gr.h"
#include "args.h"
#include "window.h"
#include "inferno.h"
#include "screens.h"
#include "game.h"
#include "gameseq.h"
#include "mission.h"
#include "automap.h"
#include "playsave.h"
#include "player.h"
#include "object.h"
#include "fvi.h"
#include "segment.h"
#include "kconfig.h"
#include "key.h"
#include "mouse.h"
#include "timer.h"
#include "gamefont.h"
#include "newmenu.h"
#include "console.h"
#include "config.h"
#include "render.h"
#include "pngfile.h"		/* the splash */
#include "gameseg.h"
/* Last: it brings in windows.h, and a header above brings in winsock2.h,
 * which must come first. */
#include "internal.h"		/* last_width, last_height, OGL_VIEWPORT */

#include "vr_descent.h"
#include "vr_xr.h"

/* Mine units per metre of head movement.  The eye baseline and how far a
 * lean carries both follow it. */
#define VRD_UNITS_PER_METRE 1.0f

/* The panel's size and distance are VR settings (VrSet, vr_options.c). */
#define VRD_GRIP_ON		0.8f	/* a grip counts as held for the recenter chord */
#define VRD_CHORD_FRAMES	10	/* both grips this long: recenter */
#define VRD_GRIP_WAIT		6	/* a lone grip waits this long for the other before it banks */

#define VRD_STICK_DEAD		0.15f
#define VRD_GRIP_DEAD		0.10f
#define VRD_BIT			0x80	/* our bit in the engine's control states */
#define VRD_RETICLE_MAX_DIST	(F1_0 * 2000)
/* The crosshair is sized as if the game's screen were this tall, in degrees:
   about what a monitor's height covers from the chair, so it looks the size
   it does flat. */
#define VRD_RETICLE_SCREEN_DEG	40.0f

static int s_connected;		/* -vr, and a headset answered */
static int s_up;		/* the session exists */
static int s_up_failed;
static int s_centered;		/* the first recenter has been asked for */
static int s_kind;		/* this frame: VRD_FLAT, VRD_EYES or VRD_PANEL */
static int s_eye = -1;		/* the eye being drawn, or -1 */
static int s_panel = -1;	/* the panel pass being drawn, or -1 */
static int s_skip_briefing;
static vrxr_eye s_cur;
static float s_tx = 1.0f, s_ty = 1.0f;	/* the eye's widest tangent, each axis */

/* The crosshair: where the aim line meets the mine this frame, and where
 * that lands in the eye being drawn. */
static vms_vector s_aim_point;
static int s_aim_valid, s_ret_ok, s_ret_x, s_ret_y;
static float s_ret_tx, s_ret_ty;	/* the crosshair's direction in the eye, as tangents */

/* This frame's controllers, mapped (vr_handmap.h). */
static vr_hand_map s_map;
static int s_map_valid, s_pause_held, s_a_held, s_b_held, s_trig_held, s_menu_held;
static int s_map_reset;	/* the map's view goes back to the ship at its next read */
static int s_nav_dir;		/* the held menu direction key, or 0 */
static fix64 s_nav_next;	/* when it repeats */
static int s_btn_held[VRB_COUNT];	/* mappable buttons by role, last frame */
static int s_lgrip_frames, s_rgrip_frames, s_chord_done;
static float s_roll;		/* the bank the grips ask for, after the chord's wait */
static int s_photo;		/* photo mode: the menu over the paused game is hidden */
static int s_kb, s_kb_row, s_kb_col;	/* the on-screen keyboard over a text field */
/* The splash: 1 while it is up (vrsplash.png, Game Or Die's art). */
static int s_splash;
static fix64 s_splash_start;
#define VRD_SPLASH_TIME (F1_0 * 3)
static int s_kb_shut;	/* put away on this text box: only choosing it brings it back */
static int s_switch;		/* Switch Game: closing to start the other game */
/* The menu pointer: where the weapon hand points on the menu panel (panel
 * pixels), whether it is shown and pressed, and where the press began. */
static int s_ptr_on, s_ptr_x, s_ptr_y, s_ptr_down, s_ptr_press_x, s_ptr_press_y;
/* Aim with the controller: its ray in the ship's frame (x right, y up,
 * z ahead; metres for the origin), this frame. */
static int s_ctl_ok;
static float s_ctl_dir[3], s_ctl_pos[3];
static int s_rumbles;
static fix s_last_shields = -1;
static int s_was_dead;
/* A monitor-sized window drawn on the panel, scaled up around its draw. */
static window *s_win_scaled;
static GLint s_saved_blend[2];
static grs_canvas s_win_saved;
#ifdef VRD_DESCENT2
static int s_extra_view;	/* a cockpit extra view is being drawn inside the eye */
static fix s_extra_saved_aspect;
#endif

/* The status line: frames of each kind since the last one, and every
 * control used so far (latched, logged the moment a new one appears, so a
 * single press always reaches the log). */
static int s_frames[3], s_panels[2];
static unsigned s_seen, s_seen_logged;
static fix64 s_status_next;

/* What an eye or panel pass changes, put back afterwards. */
static struct {
	short sc_w, sc_h;
	fix sc_aspect;
	grs_bitmap screen_bm;
	grs_canvas win3d;
	int cockpit;
	float fnt_x, fnt_y;
} s_saved;

extern void game_draw_hud_stuff(void);
#ifdef VRD_DESCENT2
extern void show_extra_views(void);
#define VRD_GAME_NAME	"Descent 2 (Descent 1 & 2 VR)"
#define VRD_LOG_TITLE	"Descent 1 & 2 VR: Descent 2 (D2X-Redux) - VR log"
#define VRD_MISSION	FULL_MISSION_FILENAME
#else
#define VRD_GAME_NAME	"Descent (Descent 1 & 2 VR)"
#define VRD_LOG_TITLE	"Descent 1 & 2 VR: Descent (D1X-Redux) - VR log"
#define VRD_MISSION	D1_MISSION_FILENAME
#endif

/* --- start and stop ------------------------------------------------------------ */

void vrd_startup(void)
{
	if (!GameArg.VrEnable)
		return;		/* VR off: not even a log file */
	vrxr_log_start(VRD_LOG_TITLE);
	vrd_settings_load();
	vrxr_log("VR: settings: Leftorium %d, swap sticks %d, aim with the %s, vibration %d, observer view %s",
		VrSet.leftorium, VrSet.swap_sticks, VrSet.aim_controller ? "controller" : "ship",
		VrSet.rumble, VrSet.desktop_view ? "steady view" : "left eye");
	if (vrxr_connect(VRD_GAME_NAME)) {
		int w = 0, h = 0;
		vrxr_eye_size(&w, &h);
		s_connected = 1;
		vrxr_log("VR: headset found; eyes %dx%d", w, h);
		vrxr_dpi_aware();	/* before the window: the spectator view in real pixels */
	} else {
		vrxr_log("VR: no VR this run (%s); carrying on flat", vrxr_failure());
	}
}

int vrd_enabled(void)
{
	return s_connected;
}

int vrd_holds_context(void)
{
	return s_up;
}

int vrd_toggle_desktop(void)
{
	const int fills = vrxr_desktop_fill_monitor(!vrxr_desktop_fills_monitor());
	VrSet.desktop_windowed = !fills;
	vrd_settings_save();
	return fills;
}

int vrd_desktop_fills(void)
{
	return vrxr_desktop_fills_monitor();
}

void vrd_shutdown(void)
{
	if (s_connected || s_up) {
		vrd_settings_save();
		vrxr_log("VR: the game is closing; ending the session");
		vrxr_shutdown();
		vrxr_log("VR: shut down");
	}
	s_connected = s_up = 0;
	s_kind = VRD_FLAT;
	s_eye = s_panel = -1;
}

/* The splash, if the player's art is beside the engine (Setup installs
 * vrsplash.png there).  8-bit RGB or RGBA; anything else is skipped. */
static void splash_load(void)
{
	png_data pd;
	memset(&pd, 0, sizeof(pd));
	if (!read_png("vrsplash.png", &pd))
		return;
	if (pd.depth == 8 && !pd.paletted && (pd.channels == 3 || pd.channels == 4) &&
		vrxr_set_panel_picture(pd.data, (int)pd.width, (int)pd.height, (int)pd.channels)) {
		s_splash = 1;
		s_splash_start = 0;
		vrxr_log("VR: splash vrsplash.png %ux%u", pd.width, pd.height);
	} else
		vrxr_log("VR: vrsplash.png is not an 8-bit RGB or RGBA picture; no splash");
	if (pd.data) free(pd.data);
	if (pd.palette) free(pd.palette);
}

static void splash_end(const char *why)
{
	if (!s_splash)
		return;
	s_splash = 0;
	vrxr_set_panel_picture(NULL, 0, 0, 0);
	vrxr_log("VR: splash ended (%s)", why);
}

static void bring_up(void)
{
	vrxr_log("VR: the game is up; starting the session");
	/* The game's own "4x multisampling" option, applied to the eyes. */
	vrxr_set_msaa(GameCfg.Multisample ? 4 : 0);
	if (vrxr_bring_up()) {
		s_up = 1;
		vrxr_desktop_vsync_off();
		splash_load();
		/* The observer view fills the monitor, borderless; Alt+Enter makes
		 * it a window. */
		vrxr_desktop_fill_monitor(!VrSet.desktop_windowed);
		/* Runs before SDL's own exit handler (registered earlier), so an
		 * Error() exit still destroys the session while the context lives. */
		atexit(vrd_shutdown);
	} else {
		s_up_failed = 1;
		vrxr_log("VR: no session (%s); carrying on flat", vrxr_failure());
	}
}

/* --- who is in front -------------------------------------------------------------- */

/* The mine goes to the eyes whenever the game's own view is showing, also
 * under a menu, which then goes on the panel over the paused game. */
static int game_visible(void)
{
	return Game_wind && window_is_visible(Game_wind) && !Automap_active;
}

/* The game itself has the controls: no menu, map or message over it. */
static int game_in_front(void)
{
	return Game_wind && window_get_front() == Game_wind && !Automap_active;
}

int vrd_is_game_window(window *wind)
{
	return wind && wind == Game_wind;
}

int vrd_menu_above_game(void)
{
	window *w;
	if (!Game_wind)
		return 0;
	for (w = window_get_next(Game_wind); w; w = window_get_next(w))
		if (window_is_visible(w))
			return 1;
	return 0;
}

/* --- the frame ------------------------------------------------------------------ */

/* The aim hand's ray this frame, turned into the ship's frame the way the
 * head is: tracking -Z ahead becomes the ship's +Z. */
static void read_controller_aim(void)
{
	float q[4], pos[3], x, y, z, w;
	const int hand = VrSet.leftorium ? 0 : 1;
	s_ctl_ok = 0;
	if (!VrSet.aim_controller || !vrxr_hand_aim(hand, q, pos))
		return;
	x = q[0]; y = q[1]; z = q[2]; w = q[3];
	s_ctl_dir[0] = -2.0f * (x * z + y * w);
	s_ctl_dir[1] = -2.0f * (y * z - x * w);
	s_ctl_dir[2] = 1.0f - 2.0f * (x * x + y * y);
	s_ctl_pos[0] = pos[0];
	s_ctl_pos[1] = pos[1];
	s_ctl_pos[2] = -pos[2];
	s_ctl_ok = 1;
}

/* A frame is beginning while another is still open: a modal loop started
 * inside a pass (Descent 1's end-of-level scores open from the game's own
 * draw, inside eye 0).  Finish the open pass and frame cleanly first: a
 * frame begun inside a frame draws nothing and puts an eye's swapchain out
 * of step.  The outer code then finds no pass and no frame open and does
 * nothing more. */
static void screen_pop(void);

static void finish_open_frame(void)
{
	static int told;
	if (s_eye < 0 && s_panel < 0 && !vrxr_frame_is_open())
		return;
	if (!told) {
		told = 1;
		vrxr_log("VR: a menu opened inside a frame; finishing that frame first");
	}
	if (s_eye >= 0) {
		vrxr_eye_end(s_eye);
		screen_pop();
		s_eye = -1;
	}
	if (s_panel >= 0) {
		glBlendFunc((GLenum)s_saved_blend[0], (GLenum)s_saved_blend[1]);
		vrxr_panel_end();
		screen_pop();
		s_panel = -1;
	}
	vrxr_frame_end();
	s_kind = VRD_FLAT;
}

void vrd_frame_begin(void)
{
	if (s_up)
		finish_open_frame();
	s_kind = VRD_FLAT;
	if (!s_connected)
		return;
	if (!s_up) {
		if (s_up_failed)
			return;
		bring_up();		/* the first frame: the GL context exists now */
		if (!s_up)
			return;
	}
	if (vrxr_context_changed()) {
		/* SDL 1.2 builds a new GL context on a video mode change; the session
		 * was bound to the old one and cannot follow. */
		vrxr_log("VR: the game changed its GL context (a video mode change); VR off for this run");
		vrd_shutdown();
		return;
	}
	if (vrxr_frame_begin()) {
		static int first = 1;
		if (first) {
			first = 0;
			vrxr_log("VR: first frame in the headset");
		}
		if (!s_centered) {
			/* Face the ship's nose from wherever the player sits. */
			vrxr_recenter();
			s_centered = 1;
		}
		s_kind = game_visible() ? VRD_EYES : VRD_PANEL;
		if (s_splash && s_kind != VRD_PANEL)
			splash_end("the game is showing");
		s_aim_valid = 0;
		vrxr_set_mirror_steady(VrSet.desktop_view == 1);
		read_controller_aim();
		/* The monitor shows the left eye in flight, the panel otherwise. */
		vrxr_set_mirror(s_kind == VRD_EYES ? 0 : VRXR_MIRROR_PANEL,
			grd_curscreen->sc_w, grd_curscreen->sc_h);
	}
}

static void status_line(void)
{
	static const char *const names[] = {
		"move", "look", "grips", "fire1", "fire2", "A", "B", "X", "Y",
		"lclick", "rclick", "menu"
	};
	const fix64 now = timer_query();
	if (s_seen != s_seen_logged) {
		char buf[200] = "";
		unsigned i;
		for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
			if (s_seen & ~s_seen_logged & (1u << i)) {
				strcat(buf, " ");
				strcat(buf, names[i]);
			}
		vrxr_log("VR: controls used for the first time:%s", buf);
		s_seen_logged = s_seen;
	}
	if (now < s_status_next)
		return;
	if (s_status_next) {
		const int total = s_frames[0] + s_frames[1] + s_frames[2];
		vrxr_log("VR: last 10 s: %d frames (%.0f fps): %d in the eyes, %d panel only, %d flat;"
			" panel %d as menu, %d as HUD; %s in front; aim %s; %d vibration pulses%s%s",
			total, total / 10.0, s_frames[VRD_EYES], s_frames[VRD_PANEL], s_frames[VRD_FLAT],
			s_panels[VRD_PANEL_MENU], s_panels[VRD_PANEL_HUD],
			game_in_front() ? "the game" : (Game_wind ? "a menu over the game" : "a menu"),
			VrSet.aim_controller ? (s_ctl_ok ? "controller (tracked)" : "controller (not tracked: ship)") : "ship",
			s_rumbles, s_photo ? "; photo mode" : "", s_kb ? "; keyboard up" : "");
		s_rumbles = 0;
		{
			/* Where the frames' time went: the headroom left for 90 Hz, or 120. */
			vrxr_frame_stats st;
			vrxr_take_frame_stats(&st);
			if (st.frames > 0) {
				const double n = (double)st.frames;
				vrxr_log("VR: timing: headset %.0f Hz (%.1f ms a frame); game %.1f ms avg, %.1f max;"
					" GPU %.1f ms avg, %.1f max; outside the frame %.1f ms avg, %.1f max;"
					" waiting %.1f ms avg; %d of %d frames late",
					st.period_ms > 0.0 ? 1000.0 / st.period_ms : 0.0, st.period_ms,
					st.cpu_ms_sum / n, st.cpu_ms_max,
					st.gpu_frames ? st.gpu_ms_sum / st.gpu_frames : 0.0, st.gpu_ms_max,
					st.outside_ms_sum / n, st.outside_ms_max, st.wait_ms_sum / n, st.late, st.frames);
			}
		}
	}
	memset(s_frames, 0, sizeof(s_frames));
	memset(s_panels, 0, sizeof(s_panels));
	s_status_next = now + F1_0 * 10;
}

void vrd_frame_end(void)
{
	if (s_up) {
		s_frames[s_kind]++;
		vrxr_frame_end();	/* every frame begun is ended */
		status_line();
	}
	s_kind = VRD_FLAT;
}

int vrd_frame_kind(void)
{
	return s_kind;
}

/* Make the whole screen w x h for a pass into a VR target; put it back after. */
static void screen_push(int w, int h, fix aspect)
{
	s_saved.sc_w = grd_curscreen->sc_w;
	s_saved.sc_h = grd_curscreen->sc_h;
	s_saved.sc_aspect = grd_curscreen->sc_aspect;
	s_saved.screen_bm = grd_curscreen->sc_canvas.cv_bitmap;
	s_saved.win3d = Screen_3d_window;
	s_saved.cockpit = PlayerCfg.CurrentCockpitMode;
	s_saved.fnt_x = FNTScaleX;
	s_saved.fnt_y = FNTScaleY;

	/* The fonts were scaled for the window when it opened; scale them for
	 * this target instead, keeping their proportions as the stock does. */
	{
		const float kx = (float)w / (float)s_saved.sc_w;
		const float ky = (float)h / (float)s_saved.sc_h;
		const float k = kx < ky ? kx : ky;
		FNTScaleX = s_saved.fnt_x * k;
		FNTScaleY = s_saved.fnt_y * k;
	}

	grd_curscreen->sc_w = w;
	grd_curscreen->sc_h = h;
	grd_curscreen->sc_canvas.cv_bitmap.bm_w = w;
	grd_curscreen->sc_canvas.cv_bitmap.bm_h = h;
	grd_curscreen->sc_canvas.cv_bitmap.bm_rowsize = w;
	grd_curscreen->sc_aspect = aspect;
	gr_init_sub_canvas(&Screen_3d_window, &grd_curscreen->sc_canvas, 0, 0, w, h);
	/* No cockpit art: the eye is all view, the panel all HUD.  Put back after. */
	PlayerCfg.CurrentCockpitMode = CM_FULL_SCREEN;
	/* The viewport cache now means another target: issue the new one at
	 * once.  The 2D code divides by last_width/last_height, so a pass that
	 * draws 2D before any 3D (the panel always does) would draw everything
	 * at negative coordinates while they are still -1. */
	last_width = last_height = -1;
	OGL_VIEWPORT(0, 0, w, h);
}

static void screen_pop(void)
{
	grd_curscreen->sc_w = s_saved.sc_w;
	grd_curscreen->sc_h = s_saved.sc_h;
	grd_curscreen->sc_aspect = s_saved.sc_aspect;
	grd_curscreen->sc_canvas.cv_bitmap = s_saved.screen_bm;
	Screen_3d_window = s_saved.win3d;
	PlayerCfg.CurrentCockpitMode = s_saved.cockpit;
	FNTScaleX = s_saved.fnt_x;
	FNTScaleY = s_saved.fnt_y;
	gr_set_current_canvas(NULL);
	last_width = last_height = -1;
	OGL_VIEWPORT(0, 0, grd_curscreen->sc_w, grd_curscreen->sc_h);
}

int vrd_eye_begin(int eye)
{
	if (s_kind != VRD_EYES || s_eye >= 0 || s_panel >= 0 || !game_visible())
		return 0;
	if (!vrxr_eye_begin(eye, &s_cur))
		return 0;

	s_tx = fabsf(s_cur.tan_l) > fabsf(s_cur.tan_r) ? fabsf(s_cur.tan_l) : fabsf(s_cur.tan_r);
	s_ty = fabsf(s_cur.tan_d) > fabsf(s_cur.tan_u) ? fabsf(s_cur.tan_d) : fabsf(s_cur.tan_u);
	if (s_tx < 0.05f) s_tx = 0.05f;
	if (s_ty < 0.05f) s_ty = 0.05f;

	/* g3_start_frame takes s = aspect * H / W and scales one axis by it; the
	 * zoom (vrd_eye_zoom) scales the other.  Together they leave view space
	 * scaled by 1/tx across and 1/ty up, so the CPU's +-1 cone is the eye's
	 * widest tangents. */
	screen_push(s_cur.width, s_cur.height,
		fl2f((s_ty / s_tx) * (float)s_cur.width / (float)s_cur.height));
	s_ret_ok = 0;
	s_eye = eye;
	return 1;
}

void vrd_eye_end(int eye)
{
	if (s_eye != eye)
		return;
	vrxr_eye_end(eye);
	screen_pop();
	s_eye = -1;
}

int vrd_in_eye_pass(void)
{
	return s_eye >= 0;
}

int vrd_eye_active(void)
{
#ifdef VRD_DESCENT2
	return s_eye >= 0 && !s_extra_view;
#else
	return s_eye >= 0;
#endif
}

#ifdef VRD_DESCENT2
/* render_frame's window 0 is the view; 1 and up are the cockpit's small
 * extra views (rear, missile, buddy, marker).  Inside an eye those stay
 * flat pictures, like monitors: the stock aspect for the screen they are
 * on, the stock projection, no head. */
void vrd_view_begin(int window_num)
{
	if (s_eye < 0 || window_num == 0)
		return;
	s_extra_view = 1;
	s_extra_saved_aspect = grd_curscreen->sc_aspect;
	grd_curscreen->sc_aspect = fixdiv(grd_curscreen->sc_w * GameCfg.AspectX,
		grd_curscreen->sc_h * GameCfg.AspectY);
}

void vrd_view_end(int window_num)
{
	if (!s_extra_view || window_num == 0)
		return;
	grd_curscreen->sc_aspect = s_extra_saved_aspect;
	s_extra_view = 0;
}
#endif

int vrd_repeat_eye(void)
{
	return s_eye > 0;
}

/* --- the panel -------------------------------------------------------------------- */

int vrd_panel_begin(int what)
{
	int w = 0, h = 0;
	if (s_kind == VRD_FLAT || s_eye >= 0 || s_panel >= 0)
		return 0;
	if (what == VRD_PANEL_HUD && !game_visible())
		return 0;	/* no game, no HUD */
	if (what == VRD_PANEL_MENU && s_photo && game_visible())
		return 0;	/* photo mode: the paused game alone */
	vrxr_panel_size(&w, &h);
	if (w <= 0 || h <= 0 || !vrxr_panel_begin())
		return 0;
	screen_push(w, h, fixdiv(w * GameCfg.AspectX, h * GameCfg.AspectY));
	s_panel = what;
	/* The engine's current blending, with alpha now built up the panel's way. */
	{
		GLint src = GL_SRC_ALPHA, dst = GL_ONE_MINUS_SRC_ALPHA;
		glGetIntegerv(GL_BLEND_SRC_RGB, &src);
		glGetIntegerv(GL_BLEND_DST_RGB, &dst);
		s_saved_blend[0] = src;
		s_saved_blend[1] = dst;
		glBlendFuncSeparate((GLenum)src, (GLenum)dst, dst == GL_ONE ? GL_ZERO : GL_ONE,
			dst == GL_ONE ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
	}
	return 1;
}

void vrd_panel_end(void)
{
	static const float facing[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	int w = 0, h = 0;
	float pos[3], wm;
	if (s_panel < 0)
		return;
	{
		/* Back to one blend for colour and alpha, as the engine left it. */
		GLint src = s_saved_blend[0], dst = s_saved_blend[1];
		glGetIntegerv(GL_BLEND_SRC_RGB, &src);
		glGetIntegerv(GL_BLEND_DST_RGB, &dst);
		glBlendFunc((GLenum)src, (GLenum)dst);
	}
	if (s_splash && s_panel == VRD_PANEL_MENU) {
		const fix64 now = timer_query();
		if (!s_splash_start)
			s_splash_start = now;
		if (now - s_splash_start > VRD_SPLASH_TIME)
			splash_end("time");
		else
			vrxr_panel_picture();	/* over the menu drawn beneath it */
	}
	vrxr_panel_end();
	s_panels[s_panel == VRD_PANEL_HUD]++;
	vrxr_panel_size(&w, &h);
	/* Placed in the cockpit, which is the tracking space: ahead of where the
	 * player sat at the last recenter, facing them. */
	if (s_panel == VRD_PANEL_HUD) {
		wm = VrSet.hud_size / 100.0f;
		pos[0] = 0.0f; pos[1] = VrSet.hud_height / 100.0f; pos[2] = -VrSet.hud_dist / 100.0f;
	} else {
		wm = VrSet.menu_size / 100.0f;
		pos[0] = 0.0f; pos[1] = 0.0f; pos[2] = -VrSet.menu_dist / 100.0f;
	}
	vrxr_panel_place(facing, pos, wm, wm * (float)h / (float)w);
	screen_pop();
	s_panel = -1;
}

/* Windows that keep the canvas they were made with (the controls set-up,
 * the high scores) were laid out for the monitor and would sit in the
 * panel's corner: scale them up around their draw.  Menus and list boxes
 * lay themselves out again and are left alone. */
void vrd_window_draw_begin(window *wind)
{
	grs_canvas *c;
	int x, y, w, h;
	float kx, ky, k;
	s_win_scaled = NULL;
	if (s_panel < 0 || !wind || newmenu_window_lays_itself_out(wind))
		return;
	c = window_get_canvas(wind);
	if (!c)
		return;
	x = c->cv_bitmap.bm_x; y = c->cv_bitmap.bm_y;
	w = c->cv_bitmap.bm_w; h = c->cv_bitmap.bm_h;
	if (x + w > s_saved.sc_w || y + h > s_saved.sc_h)
		return;	/* already larger than the monitor: laid out for the panel */
	kx = (float)grd_curscreen->sc_w / (float)s_saved.sc_w;
	ky = (float)grd_curscreen->sc_h / (float)s_saved.sc_h;
	k = kx < ky ? kx : ky;
	if (k < 1.01f)
		return;
	s_win_saved = *c;
	s_win_scaled = wind;
	gr_init_sub_canvas(c, &grd_curscreen->sc_canvas, (int)(x * k), (int)(y * k),
		(int)(w * k), (int)(h * k));
}

void vrd_window_draw_end(window *wind)
{
	if (wind && wind == s_win_scaled) {
		grs_canvas *c = window_get_canvas(wind);
		if (c)
			*c = s_win_saved;
	}
	s_win_scaled = NULL;
}

/* The on-screen keyboard: four rows of characters and a row of Space,
 * Back and Done, at the foot of the panel over the menu. */
static const char *const k_kb_rows[4] = { "1234567890-", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM_." };
static const char *const k_kb_special[3] = { "Space", "Back", "Done" };
#define KB_ROWS 5

static int kb_cols(int row)
{
	return row < 4 ? (int)strlen(k_kb_rows[row]) : 3;
}

void vrd_panel_overlay(void)
{
	int row, col, top, cell_w, cell_h;
	if (s_panel == VRD_PANEL_MENU && s_ptr_on) {
		/* The pointer's dot, red while pressed (the OS cursor is not in the
		 * headset). */
		gr_set_current_canvas(NULL);
		gr_setcolor(BM_XRGB(0, 0, 0));
		gr_disk(i2f(s_ptr_x), i2f(s_ptr_y), i2f(SHEIGHT / 90));
		gr_setcolor(s_ptr_down ? BM_XRGB(31, 4, 4) : BM_XRGB(31, 31, 31));
		gr_disk(i2f(s_ptr_x), i2f(s_ptr_y), i2f(SHEIGHT / 120));
	}
	if (s_panel != VRD_PANEL_MENU || !s_kb)
		return;
	gr_set_current_canvas(NULL);
	cell_w = SWIDTH / 12;
	cell_h = SHEIGHT / 13;
	top = SHEIGHT - cell_h * KB_ROWS - cell_h / 2;
	gr_setcolor(BM_XRGB(2, 2, 4));
	gr_rect(cell_w / 2, top - cell_h / 4, SWIDTH - cell_w / 2, SHEIGHT - cell_h / 4);
	gr_set_curfont(MEDIUM1_FONT);
	for (row = 0; row < KB_ROWS; row++) {
		const int n = kb_cols(row);
		const int kw = row < 4 ? cell_w : cell_w * 3;
		const int left = (SWIDTH - n * kw) / 2;
		for (col = 0; col < n; col++) {
			char label[8];
			int lw, lh, la;
			const int x0 = left + col * kw + 4, y0 = top + row * cell_h + 4;
			const int x1 = left + (col + 1) * kw - 4, y1 = top + (row + 1) * cell_h - 4;
			const int lit = row == s_kb_row && col == s_kb_col;
			gr_setcolor(lit ? BM_XRGB(10, 22, 31) : BM_XRGB(6, 6, 9));
			gr_rect(x0, y0, x1, y1);
			if (row < 4) { label[0] = k_kb_rows[row][col]; label[1] = 0; }
			else snprintf(label, sizeof(label), "%s", k_kb_special[col]);
			gr_set_fontcolor(lit ? BM_XRGB(0, 0, 0) : BM_XRGB(28, 28, 28), -1);
			gr_get_string_size(label, &lw, &lh, &la);
			gr_string((x0 + x1 - lw) / 2, (y0 + y1 - lh) / 2, label);
		}
	}
}

int vrd_panel_alpha(void)
{
	/* Drawing on the panel, which is composited over the world by its alpha:
	 * alpha must accumulate as "over", not be scaled by itself. */
	return s_panel >= 0;
}

int vrd_hud_pass(void)
{
	return s_panel == VRD_PANEL_HUD;
}

void vrd_draw_hud(void)
{
	if (s_panel != VRD_PANEL_HUD)
		return;
	gr_set_current_canvas(&Screen_3d_window);
	game_draw_hud_stuff();	/* its crosshair stands down: see vrd_reticle */
#ifdef VRD_DESCENT2
	gr_set_current_canvas(NULL);
	show_extra_views();	/* rear, missile, buddy: small pictures on the panel */
#endif
	gr_set_current_canvas(NULL);
}

/* --- the camera ----------------------------------------------------------------- */

/* Where the ship's aim line meets the mine, once a frame: the crosshair is
 * drawn at that depth, so both eyes agree on it at the target. */
static void find_aim_point(const vms_vector *base_pos, const vms_matrix *base_orient)
{
	fvi_query fq;
	fvi_info hit;
	vms_vector end;

	vms_vector start = *base_pos, dir = base_orient->fvec;
	int seg = Viewer->segnum;

	if (s_ctl_ok && Viewer == ConsoleObject && !Rear_view) {
		/* Aim with the controller: its own ray, from where it is in the cockpit. */
		vm_vec_scale_add2(&start, (vms_vector *)&base_orient->rvec, fl2f(s_ctl_pos[0] * VRD_UNITS_PER_METRE));
		vm_vec_scale_add2(&start, (vms_vector *)&base_orient->uvec, fl2f(s_ctl_pos[1] * VRD_UNITS_PER_METRE));
		vm_vec_scale_add2(&start, (vms_vector *)&base_orient->fvec, fl2f(s_ctl_pos[2] * VRD_UNITS_PER_METRE));
		vm_vec_zero(&dir);
		vm_vec_scale_add2(&dir, (vms_vector *)&base_orient->rvec, fl2f(s_ctl_dir[0]));
		vm_vec_scale_add2(&dir, (vms_vector *)&base_orient->uvec, fl2f(s_ctl_dir[1]));
		vm_vec_scale_add2(&dir, (vms_vector *)&base_orient->fvec, fl2f(s_ctl_dir[2]));
		vm_vec_normalize_quick(&dir);
		seg = find_point_seg(&start, Viewer->segnum);
		if (seg < 0) {	/* the hand is outside the segment: cast from the ship */
			start = *base_pos;
			seg = Viewer->segnum;
		}
	}
	vm_vec_scale_add(&end, &start, &dir, VRD_RETICLE_MAX_DIST);
	memset(&fq, 0, sizeof(fq));
	fq.p0 = &start;
	fq.p1 = &end;
	fq.startseg = seg;
	fq.rad = 0;
	fq.thisobjnum = Viewer - Objects;
	fq.ignore_obj_list = NULL;
	fq.flags = FQ_TRANSWALL;
	memset(&hit, 0, sizeof(hit));
	if (find_vector_intersection(&fq, &hit) == HIT_BAD_P0)
		s_aim_point = end;
	else if (hit.hit_type == HIT_NONE)
		s_aim_point = end;
	else
		s_aim_point = hit.hit_pnt;
	s_aim_valid = 1;
}

void vrd_eye_view(vms_vector *eye_pos, vms_matrix *eye_orient,
	const vms_vector *base_pos, const vms_matrix *base_orient)
{
	const float x = s_cur.orient[0], y = s_cur.orient[1], z = s_cur.orient[2], w = s_cur.orient[3];
	float R[3][3];
	vms_matrix headm;
	float d[3];

	/* The head's rotation in tracking space (right-handed, Y up, -Z ahead). */
	R[0][0] = 1.0f - 2.0f * (y * y + z * z);
	R[0][1] = 2.0f * (x * y - z * w);
	R[0][2] = 2.0f * (x * z + y * w);
	R[1][0] = 2.0f * (x * y + z * w);
	R[1][1] = 1.0f - 2.0f * (x * x + z * z);
	R[1][2] = 2.0f * (y * z - x * w);
	R[2][0] = 2.0f * (x * z - y * w);
	R[2][1] = 2.0f * (y * z + x * w);
	R[2][2] = 1.0f - 2.0f * (x * x + y * y);

	/* The head's axes in the ship's frame (left-handed, +Z ahead): the
	 * tracking-space right, up and ahead (-Z) with Z turned round. */
	headm.rvec.x = fl2f(R[0][0]);  headm.rvec.y = fl2f(R[1][0]);  headm.rvec.z = fl2f(-R[2][0]);
	headm.uvec.x = fl2f(R[0][1]);  headm.uvec.y = fl2f(R[1][1]);  headm.uvec.z = fl2f(-R[2][1]);
	headm.fvec.x = fl2f(-R[0][2]); headm.fvec.y = fl2f(-R[1][2]); headm.fvec.z = fl2f(R[2][2]);

	/* Onto the ship: head axes, expressed in the ship's frame, into the mine. */
	vm_matrix_x_matrix(eye_orient, (vms_matrix *)base_orient, &headm);

	/* The eye's position: the tracking-space offset, in the ship's frame. */
	d[0] = s_cur.pos[0] * VRD_UNITS_PER_METRE;
	d[1] = s_cur.pos[1] * VRD_UNITS_PER_METRE;
	d[2] = -s_cur.pos[2] * VRD_UNITS_PER_METRE;
	*eye_pos = *base_pos;
	vm_vec_scale_add2(eye_pos, (vms_vector *)&base_orient->rvec, fl2f(d[0]));
	vm_vec_scale_add2(eye_pos, (vms_vector *)&base_orient->uvec, fl2f(d[1]));
	vm_vec_scale_add2(eye_pos, (vms_vector *)&base_orient->fvec, fl2f(d[2]));

	/* The crosshair in this eye: the aim point, seen from this eye.  Only
	 * from a viewer inside the mine: the flythrough's camera outside has no
	 * segment to cast from, and draws no crosshair. */
	s_ret_ok = 0;
	if (Viewer->segnum < 0 || Viewer->segnum > Highest_segment_index)
		return;
	if (!s_aim_valid)
		find_aim_point(base_pos, base_orient);
	{
		vms_vector v;
		float vx, vy, vz, tx, ty;
		vm_vec_sub(&v, &s_aim_point, eye_pos);
		vx = f2fl(vm_vec_dot(&v, &eye_orient->rvec));
		vy = f2fl(vm_vec_dot(&v, &eye_orient->uvec));
		vz = f2fl(vm_vec_dot(&v, &eye_orient->fvec));
		s_ret_ok = 0;
		if (vz > 0.01f) {
			tx = vx / vz;
			ty = vy / vz;
			if (tx > s_cur.tan_l && tx < s_cur.tan_r && ty > s_cur.tan_d && ty < s_cur.tan_u) {
				/* Canvas pixels, y down: the top edge is tan_u. */
				s_ret_x = (int)((tx - s_cur.tan_l) / (s_cur.tan_r - s_cur.tan_l) * s_cur.width);
				s_ret_y = (int)((s_cur.tan_u - ty) / (s_cur.tan_u - s_cur.tan_d) * s_cur.height);
				s_ret_tx = tx;
				s_ret_ty = ty;
				s_ret_ok = 1;
			}
		}
	}
}

/* The listener: the ship turned by the head, so a sound to your left is
 * heard on the left when you look round at it.  The engine pans every sound
 * from the viewer's orientation; this hands it the head's. */
vms_matrix *vrd_listener_orient(vms_matrix *ship)
{
	static vms_matrix m;
	float q[4], pos[3], x, y, z, w;
	vms_matrix headm;
	if (!vrd_paced() || !ship || !vrxr_head(q, pos))
		return ship;
	x = q[0]; y = q[1]; z = q[2]; w = q[3];
	headm.rvec.x = fl2f(1.0f - 2.0f * (y * y + z * z));
	headm.rvec.y = fl2f(2.0f * (x * y + z * w));
	headm.rvec.z = fl2f(-(2.0f * (x * z - y * w)));
	headm.uvec.x = fl2f(2.0f * (x * y - z * w));
	headm.uvec.y = fl2f(1.0f - 2.0f * (x * x + z * z));
	headm.uvec.z = fl2f(-(2.0f * (y * z + x * w)));
	headm.fvec.x = fl2f(-(2.0f * (x * z + y * w)));
	headm.fvec.y = fl2f(-(2.0f * (y * z - x * w)));
	headm.fvec.z = fl2f(1.0f - 2.0f * (x * x + y * y));
	vm_matrix_x_matrix(&m, ship, &headm);
	return &m;
}

int vrd_reticle(int *x, int *y)
{
	/* 1 = draw it here; 0 = do not draw it (the HUD panel never does, nor
	 * an eye while a menu is open over the game). */
	if (s_eye < 0 || !s_ret_ok || vrd_menu_above_game())
		return 0;
	*x = s_ret_x;
	*y = s_ret_y;
	return 1;
}

void vrd_reticle_screen(int *w, int *h)
{
	/* The eye pixels that span VRD_RETICLE_SCREEN_DEG through the middle of
	 * the eye, as a 4:3 screen: the stock sizes the crosshair from these. */
	const float span = 2.0f * tanf(VRD_RETICLE_SCREEN_DEG * 0.5f * 3.14159265f / 180.0f);
	float vh = (float)(int)(span / (s_cur.tan_u - s_cur.tan_d) * (float)s_cur.height + 0.5f);
	float vw;
	if (vh < 120.0f)
		vh = 120.0f;
	vw = vh * 4.0f / 3.0f;	/* at the middle */
	/* Sized by angle where it is drawn, not only at the middle: a flat eye
	 * image spreads a direction (tx, ty) off the middle by L = sqrt(1 + tx^2
	 * + ty^2) across and by L^2 outward, so a crosshair of fixed pixels
	 * would shrink and squeeze off the middle.  Scale each axis by that
	 * spread at its own place. */
	if (s_ret_ok) {
		const float r2 = s_ret_tx * s_ret_tx + s_ret_ty * s_ret_ty;
		const float L = sqrtf(1.0f + r2);
		float sx = L, sy = L;
		if (r2 > 1e-6f) {
			sx += (L * L - L) * (s_ret_tx * s_ret_tx / r2);
			sy += (L * L - L) * (s_ret_ty * s_ret_ty / r2);
		}
		vw *= sx;
		vh *= sy;
	}
	*h = (int)(vh + 0.5f);
	*w = (int)(vw + 0.5f);
}

int vrd_owns_reticle(void)
{
	return s_eye >= 0 || s_panel >= 0;
}

/* g3_set_view_matrix for cameras that set their own (the end-of-level
 * flythrough): in an eye, the head on that camera with the eye's zoom. */
void vrd_set_view(vms_vector *pos, vms_matrix *orient, fix zoom)
{
	if (s_eye >= 0) {
		vms_vector eye_pos;
		vms_matrix eye_orient;
		vrd_eye_view(&eye_pos, &eye_orient, pos, orient);
		g3_set_view_matrix(&eye_pos, &eye_orient, vrd_eye_zoom());
	} else
		g3_set_view_matrix(pos, orient, zoom);
}

fix vrd_eye_zoom(void)
{
	return fl2f(s_tx < s_ty ? s_tx : s_ty);
}

int vrd_eye_frustum(double znear, double *l, double *r, double *b, double *t)
{
#ifdef VRD_DESCENT2
	if (s_extra_view)
		return 0;	/* a cockpit extra view: the stock projection */
#endif
	if (s_eye < 0)
		return 0;
	/* View space is scaled by 1/tx across and 1/ty up (vrd_eye_begin), so
	 * the eye's tangents are too. */
	*l = s_cur.tan_l / s_tx * znear;
	*r = s_cur.tan_r / s_tx * znear;
	*b = s_cur.tan_d / s_ty * znear;
	*t = s_cur.tan_u / s_ty * znear;
	return 1;
}

int vrd_paced(void)
{
	return s_up && vrxr_active();
}

int vrd_layout_begin(void)
{
	int w = 0, h = 0;
	if (!vrd_paced() || s_eye >= 0 || s_panel >= 0)
		return 0;	/* flat, or already inside a pass at its own size */
	vrxr_panel_size(&w, &h);
	if (w <= 0 || h <= 0)
		return 0;
	screen_push(w, h, fixdiv(w * GameCfg.AspectX, h * GameCfg.AspectY));
	return 1;
}

void vrd_layout_end(int pushed)
{
	if (pushed)
		screen_pop();
}

/* --- the controllers -------------------------------------------------------------- */

static float dead(float v, float d)
{
	if (v > d) return (v - d) / (1.0f - d);
	if (v < -d) return (v + d) / (1.0f - d);
	return 0.0f;
}

static void set_bit(ubyte *state, int on)
{
	if (on) *state |= VRD_BIT;
	else *state &= (ubyte)~VRD_BIT;
}

static void tap(int keycode)
{
	key_inject(keycode, 1);
	key_inject(keycode, 0);
}

static void release_game_controls(void)
{
	set_bit(&Controls.fire_primary_state, 0);
	set_bit(&Controls.fire_secondary_state, 0);
	set_bit(&Controls.btn_slide_up_state, 0);
	set_bit(&Controls.btn_slide_down_state, 0);
	set_bit(&Controls.btn_bank_left_state, 0);
	set_bit(&Controls.btn_bank_right_state, 0);
#ifdef VRD_DESCENT2
	set_bit(&Controls.afterburner_state, 0);
	set_bit(&Controls.energy_to_shield_state, 0);
#endif
}

/* A menu, the map or a message is in front: the sticks move the highlight,
 * A or the trigger chooses, B or the menu button backs out. */
static void menu_keys(const vr_phys_input *p, int choose, int back)
{
	const fix64 now = timer_query();
	const float sx = fabsf(p->lstick_x) > fabsf(p->rstick_x) ? p->lstick_x : p->rstick_x;
	const float sy = fabsf(p->lstick_y) > fabsf(p->rstick_y) ? p->lstick_y : p->rstick_y;
	int dir = 0;

	if (sy > 0.6f) dir = KEY_UP;
	else if (sy < -0.6f) dir = KEY_DOWN;
	else if (sx < -0.6f) dir = KEY_LEFT;
	else if (sx > 0.6f) dir = KEY_RIGHT;
	if (dir != s_nav_dir) {
		s_nav_dir = dir;
		if (dir) {
			s_nav_next = now + F1_0 * 2 / 5;
			tap(dir);
		}
	} else if (dir && now >= s_nav_next) {
		s_nav_next = now + F1_0 / 8;
		tap(dir);
	}

	/* Choosing: this engine ticks a checkbox on Space and CLOSES the page on
	 * Enter, so on a checkbox or radio row A has to send Space. */
	if (choose)
		tap(newmenu_front_item_is_toggle() ? KEY_SPACEBAR : KEY_ENTER);
	else if (back)
		tap(KEY_ESC);
}

/* The weapon hand's aim ray against the menu panel (as vrd_panel_end places
 * it: centred ahead at the menu distance, facing the player), in the panel's
 * pixels.  0 = it does not land on the panel. */
static int pointer_on_panel(int *px, int *py)
{
	float q[4], o[3], d[3], t, hx, hy, u, v, wm, hm, dist;
	int pw = 0, ph = 0;
	const int hand = VrSet.leftorium ? 0 : 1;
	if (!vrxr_hand_aim(hand, q, o))
		return 0;
	vrxr_panel_size(&pw, &ph);
	if (pw <= 0 || ph <= 0)
		return 0;
	/* The ray: -Z of the aim pose, in tracking space. */
	d[0] = -2.0f * (q[0] * q[2] + q[1] * q[3]);
	d[1] = -2.0f * (q[1] * q[2] - q[0] * q[3]);
	d[2] = -(1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]));
	dist = VrSet.menu_dist / 100.0f;
	wm = VrSet.menu_size / 100.0f;
	hm = wm * (float)ph / (float)pw;
	if (d[2] > -0.05f)
		return 0;	/* pointing away from the panel */
	t = (-dist - o[2]) / d[2];
	if (t <= 0.0f)
		return 0;
	hx = o[0] + d[0] * t;
	hy = o[1] + d[1] * t;
	u = hx / wm + 0.5f;
	v = 0.5f - hy / hm;	/* the texture's rows run down */
	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
		return 0;
	*px = (int)(u * pw);
	*py = (int)(v * ph);
	return 1;
}

/* The pointer, once a frame in a menu: shown while the weapon hand's grip is
 * held; the trigger presses the left mouse button where it points and lets
 * go where it was pressed (a hand drifts while pulling a trigger, and menus
 * act on the release over the row that was pressed).  1 = it took the
 * trigger this frame. */
static int menu_pointer(const vr_phys_input *p, int trig)
{
	const float grip = VrSet.leftorium ? p->lgrip : p->rgrip;
	int x, y;
	if (grip < 0.5f || !pointer_on_panel(&x, &y)) {
		if (s_ptr_down) {
			mouse_inject_pos(s_ptr_press_x, s_ptr_press_y);
			mouse_inject_button(0);
			s_ptr_down = 0;
		}
		s_ptr_on = 0;
		return 0;
	}
	s_ptr_on = 1;
	s_ptr_x = x;
	s_ptr_y = y;
	{
		/* A window laid out for the monitor is only scaled up while it is
		 * drawn on the panel; its events use its monitor-sized layout, so the
		 * mouse goes there in the monitor's pixels. */
		window *front = window_get_front();
		int pw = 0, ph = 0;
		vrxr_panel_size(&pw, &ph);
		if (front && !newmenu_window_lays_itself_out(front) && pw > 0 && ph > 0 &&
			grd_curscreen->sc_w > 0 && grd_curscreen->sc_h > 0) {
			const float kx = (float)pw / (float)grd_curscreen->sc_w;
			const float ky = (float)ph / (float)grd_curscreen->sc_h;
			const float k = kx < ky ? kx : ky;
			if (k > 1.01f) {
				x = (int)(x / k);
				y = (int)(y / k);
			}
		}
	}
	if (!s_ptr_down)
		mouse_inject_pos(x, y);
	if (trig && !s_ptr_down) {
		s_ptr_down = 1;
		s_ptr_press_x = x;
		s_ptr_press_y = y;
		mouse_inject_button(1);
	} else if (!trig && s_ptr_down) {
		s_ptr_down = 0;
		mouse_inject_pos(s_ptr_press_x, s_ptr_press_y);
		mouse_inject_button(0);
	}
	return 1;
}

/* The keyboard over a text field: the sticks move between keys (never the
 * arrow keys: a text field reads Left as Backspace), A or a trigger types,
 * B deletes, the menu button cancels. */
static void keyboard_keys(const vr_phys_input *p, int choose, int back, int cancel)
{
	const fix64 now = timer_query();
	const float sx = fabsf(p->lstick_x) > fabsf(p->rstick_x) ? p->lstick_x : p->rstick_x;
	const float sy = fabsf(p->lstick_y) > fabsf(p->rstick_y) ? p->lstick_y : p->rstick_y;
	int dir = 0, move = 0;

	if (sy > 0.6f) dir = 1;
	else if (sy < -0.6f) dir = 2;
	else if (sx < -0.6f) dir = 3;
	else if (sx > 0.6f) dir = 4;
	if (dir != s_nav_dir) {
		s_nav_dir = dir;
		move = dir != 0;
		s_nav_next = now + F1_0 * 2 / 5;
	} else if (dir && now >= s_nav_next) {
		move = 1;
		s_nav_next = now + F1_0 / 8;
	}
	if (move) {
		if (dir == 1) s_kb_row = (s_kb_row + KB_ROWS - 1) % KB_ROWS;
		else if (dir == 2) s_kb_row = (s_kb_row + 1) % KB_ROWS;
		else if (dir == 3) s_kb_col--;
		else s_kb_col++;
		if (s_kb_col < 0) s_kb_col = kb_cols(s_kb_row) - 1;
		if (s_kb_col >= kb_cols(s_kb_row)) s_kb_col = (dir == 4) ? 0 : kb_cols(s_kb_row) - 1;
	}
	if (choose) {
		if (s_kb_row < 4)
			key_inject_char(k_kb_rows[s_kb_row][s_kb_col]);
		else if (s_kb_col == 0)
			key_inject_char(' ');
		else if (s_kb_col == 1)
			tap(KEY_BACKSP);
		else {
			tap(KEY_ENTER);
			s_kb_shut = 1;	/* Done */
		}
	} else if (back)
		tap(KEY_BACKSP);
	else if (cancel) {
		tap(KEY_ESC);
		s_kb_shut = 1;
	}
}

/* The buttons the player can map, by role (see VRB_*): the Leftorium trades
 * the hands' face buttons, Swap sticks the stick clicks. */
static void logical_buttons(const vr_phys_input *p, int held[VRB_COUNT])
{
	const int left = VrSet.leftorium, sw = VrSet.swap_sticks;
	memset(held, 0, sizeof(int) * VRB_COUNT);
	if (vrd_family() == VRD_FAMILY_FRAME) {
		/* The Frame: A/B/X/Y all on the right hand (they stay put with the
		 * Leftorium), a D-pad on the left, a shoulder button each side. */
		held[VRB_A] = p->a;
		held[VRB_B] = p->b;
		held[VRB_X] = p->frame_x;
		held[VRB_Y] = p->frame_y;
		held[VRB_MOVECLICK] = sw ? p->rclick : p->lclick;
		held[VRB_LOOKCLICK] = sw ? p->lclick : p->rclick;
		held[VRB_DPAD_UP] = p->dpad_up;
		held[VRB_DPAD_DOWN] = p->dpad_down;
		held[VRB_DPAD_LEFT] = p->dpad_left;
		held[VRB_DPAD_RIGHT] = p->dpad_right;
		held[VRB_LSHOULDER] = p->lshoulder;
		held[VRB_RSHOULDER] = p->rshoulder;
		return;
	}
	held[VRB_A] = left ? p->x : p->a;
	held[VRB_B] = left ? p->y : p->b;
	held[VRB_X] = left ? p->a : p->x;
	held[VRB_Y] = left ? p->b : p->y;
	held[VRB_MOVECLICK] = sw ? p->rclick : p->lclick;
	held[VRB_LOOKCLICK] = sw ? p->lclick : p->rclick;
}

/* A new press of any button mapped to this action. */
static int action_pressed(const int edge[VRB_COUNT], int action)
{
	const int *bind = VrSet.bind[vrd_family()];
	int b;
	for (b = 0; b < VRB_COUNT; b++)
		if (edge[b] && bind[b] == action)
			return 1;
	return 0;
}

/* The mapped actions, as the engine's own controls. */
static void apply_actions(const int held[VRB_COUNT], const int edge[VRB_COUNT])
{
	const int *bind = VrSet.bind[vrd_family()];
	int act_held[VRA_COUNT], act_edge[VRA_COUNT], b;

	memset(act_held, 0, sizeof(act_held));
	memset(act_edge, 0, sizeof(act_edge));
	for (b = 0; b < VRB_COUNT; b++) {
		const int a = bind[b];
		if (a <= VRA_NONE || a >= VRA_COUNT || !vrd_action_allowed(a))
			continue;
		act_held[a] |= held[b];
		act_edge[a] |= edge[b];
	}
	set_bit(&Controls.btn_slide_up_state, act_held[VRA_SLIDE_UP]);
	set_bit(&Controls.btn_slide_down_state, act_held[VRA_SLIDE_DOWN]);
	set_bit(&Controls.btn_bank_left_state, act_held[VRA_BANK_LEFT]);
	set_bit(&Controls.btn_bank_right_state, act_held[VRA_BANK_RIGHT]);
	if (act_edge[VRA_NEXT_PRIMARY]) Controls.cycle_primary_count++;
	if (act_edge[VRA_NEXT_SECONDARY]) Controls.cycle_secondary_count++;
	if (act_edge[VRA_FLARE]) Controls.fire_flare_count++;
	if (act_edge[VRA_BOMB]) Controls.drop_bomb_count++;
	if (act_edge[VRA_REAR_VIEW]) Controls.rear_view_count++;
	if (act_edge[VRA_AUTOMAP]) Controls.automap_count++;
	if (act_edge[VRA_RECENTER]) vrd_recenter();
#ifdef VRD_DESCENT2
	set_bit(&Controls.afterburner_state, act_held[VRA_AFTERBURNER]);
	set_bit(&Controls.energy_to_shield_state, act_held[VRA_ENERGY_SHIELD]);
	if (act_edge[VRA_HEADLIGHT]) Controls.headlight_count++;
#endif
}

/* The grips: left raises the ship, right lowers it; both held is the Game
 * Or Die recenter chord, in play and in menus.  A lone grip waits a few
 * frames for the other before it moves the ship, so a chord never starts as
 * a climb. */
static void grips(const vr_phys_input *p)
{
	const int l = p->lgrip > VRD_GRIP_DEAD, r = p->rgrip > VRD_GRIP_DEAD;
	s_lgrip_frames = l ? s_lgrip_frames + 1 : 0;
	s_rgrip_frames = r ? s_rgrip_frames + 1 : 0;
	if (p->lgrip > VRD_GRIP_ON && p->rgrip > VRD_GRIP_ON) {
		if (!s_chord_done && s_lgrip_frames >= VRD_CHORD_FRAMES && s_rgrip_frames >= VRD_CHORD_FRAMES) {
			vrxr_log("VR: both grips held: recenter");
			vrd_recenter();
			s_chord_done = 1;
		}
		s_roll = 0.0f;
		return;
	}
	if (!l && !r)
		s_chord_done = 0;
	if (s_chord_done || (l && !r && s_lgrip_frames < VRD_GRIP_WAIT) || (r && !l && s_rgrip_frames < VRD_GRIP_WAIT))
		s_roll = 0.0f;
	else
		s_roll = s_map.roll;
}

/* Results you can feel: taking damage, and dying. */
static void rumble_results(void)
{
	if (!VrSet.rumble || Player_num < 0) {
		s_last_shields = -1;
		return;
	}
	{
		const fix sh = Players[Player_num].shields;
		if (s_last_shields >= 0 && sh < s_last_shields - F1_0 / 2) {
			float amp = f2fl(s_last_shields - sh) / 25.0f;
			if (amp < 0.3f) amp = 0.3f;
			if (amp > 1.0f) amp = 1.0f;
			vrxr_haptic(0, amp, 0.15f);
			vrxr_haptic(1, amp, 0.15f);
			s_rumbles++;
		}
		s_last_shields = sh;
	}
	if (Player_is_dead && !s_was_dead) {
		vrxr_haptic(0, 0.9f, 0.5f);
		vrxr_haptic(1, 0.9f, 0.5f);
		s_rumbles++;
	}
	s_was_dead = Player_is_dead;
}

void vrd_on_fire(fix damage, int secondary)
{
	float amp;
	int hand;
	if (!VrSet.rumble || !s_up)
		return;
	/* Primary fire is the weapon hand's trigger, secondary the other's. */
	hand = (secondary ? !VrSet.leftorium : VrSet.leftorium) ? 0 : 1;
	amp = 0.25f + f2fl(damage) / 80.0f;
	if (amp > 1.0f) amp = 1.0f;
	vrxr_haptic(hand, amp, secondary ? 0.12f : 0.04f);
	s_rumbles++;
}

vms_vector vrd_shot_dir(vms_vector ship_fvec)
{
	vms_vector d;
	const vms_matrix *m;
	if (!s_ctl_ok || Rear_view || !ConsoleObject)
		return ship_fvec;
	m = &ConsoleObject->orient;
	vm_vec_zero(&d);
	vm_vec_scale_add2(&d, (vms_vector *)&m->rvec, fl2f(s_ctl_dir[0]));
	vm_vec_scale_add2(&d, (vms_vector *)&m->uvec, fl2f(s_ctl_dir[1]));
	vm_vec_scale_add2(&d, (vms_vector *)&m->fvec, fl2f(s_ctl_dir[2]));
	if (vm_vec_normalize_quick(&d) < F1_0 / 4)
		return ship_fvec;
	return d;
}

void vrd_input_frame(void)
{
	vr_phys_input p;
	int trig, choose, back, pause, b;
	int held[VRB_COUNT], edge[VRB_COUNT];

	s_map_valid = 0;
	if (s_switch) {
		/* Switch Game: one window a frame, front first, as the game closes
		 * them itself; the main loop ends when none are left. */
		window *w = window_get_front();
		release_game_controls();
		if (w)
			window_close(w);
		return;
	}
	if (!s_up || !vrxr_active() || !vrxr_read_physical(&p)) {
		release_game_controls();
		return;
	}
	vr_map_hands(&p, VrSet.leftorium, VrSet.swap_sticks, &s_map);
	s_map_valid = 1;
	s_seen |= (fabsf(p.lstick_x) + fabsf(p.lstick_y) > 0.5f ? 1u : 0u)
		| (fabsf(p.rstick_x) + fabsf(p.rstick_y) > 0.5f ? 2u : 0u)
		| (p.lgrip > 0.5f || p.rgrip > 0.5f ? 4u : 0u)
		| (p.rtrigger > 0.5f ? 8u : 0u) | (p.ltrigger > 0.5f ? 16u : 0u)
		| (p.a ? 32u : 0u) | (p.b ? 64u : 0u) | (p.x ? 128u : 0u) | (p.y ? 256u : 0u)
		| (p.lclick ? 512u : 0u) | (p.rclick ? 1024u : 0u) | (p.menu ? 2048u : 0u);

	/* Every edge first, and what is held recorded BEFORE any key is sent: a
	 * key can open a menu, every menu here runs its own loop of frames, and
	 * that loop calls back into this function while the button is still down.
	 * Recorded after, the Menu button's own press would close the menu it
	 * had just opened. */
	logical_buttons(&p, held);
	{
		/* The Leftorium and Swap sticks remap the buttons the moment they are
		 * ticked, under a finger still on A: a button already down is not a
		 * new press, or the remapped button would untick the option again. */
		static int last_left = -1, last_swap = -1;
		if (VrSet.leftorium != last_left || VrSet.swap_sticks != last_swap) {
			if (last_left >= 0)
				memcpy(s_btn_held, held, sizeof(s_btn_held));
			last_left = VrSet.leftorium;
			last_swap = VrSet.swap_sticks;
		}
	}
	for (b = 0; b < VRB_COUNT; b++) {
		edge[b] = held[b] && !s_btn_held[b];
		s_btn_held[b] = held[b];
	}
	trig = p.rtrigger > 0.5f || p.ltrigger > 0.5f;
	choose = edge[VRB_A] || edge[VRB_X] || (trig && !s_trig_held);
	back = edge[VRB_B] || edge[VRB_Y];
	pause = p.menu && !s_menu_held;
	s_trig_held = trig;
	s_menu_held = p.menu;

	if (s_splash) {
		int any = choose || back || pause;
		for (b = 0; b < VRB_COUNT; b++)
			any |= edge[b];
		if (any)
			splash_end("a button");
		release_game_controls();
		return;
	}
	grips(&p);
	rumble_results();
	if (game_in_front() || automap_in_front() || s_kb || s_photo)
		s_ptr_on = 0;
	if (!vrd_menu_above_game())
		s_photo = 0;
	{
		/* The on-screen keyboard comes up when a text box is CHOSEN, or by
		 * itself where typing is the only thing to do (a dialog that is only a
		 * text box, a save slot being named); Done or Menu puts it away. */
		const int on_text = !game_in_front() && newmenu_front_item_is_text();
		if (!on_text) {
			s_kb = 0;
			s_kb_shut = 0;
		} else if (!s_kb && ((!s_kb_shut && newmenu_front_text_wants_keyboard()) || choose)) {
			s_kb = 1;
			choose = 0;	/* the press that opened it types nothing */
		}
	}

	if (game_in_front() && Player_is_dead) {
		/* Dead: the game carries on after a key press, and the triggers and
		 * buttons are not keys, so A, X or a trigger sends one. */
		release_game_controls();
		if (choose && Player_exploded)
			tap(KEY_SPACEBAR);
		else if (pause)
			tap(KEY_ESC);
	} else if (game_in_front()) {
		set_bit(&Controls.fire_primary_state, s_map.fire_primary);
		set_bit(&Controls.fire_secondary_state, s_map.fire_secondary);
		apply_actions(held, edge);
		s_nav_dir = 0;
		if (pause) {
			vrxr_log("VR: pause button: opening the game's menu");
			release_game_controls();
			tap(KEY_ESC);		/* the game's own menu (modal: returns when it closes) */
		}
	} else if (automap_in_front()) {
		/* The map: the sticks and grips fly its view the way they fly the
		 * ship (vrd_add_axes); A, X or a trigger puts the view back on the
		 * ship; B, Menu or the map's own button close it. */
		release_game_controls();
		s_nav_dir = 0;
		if (back || pause || action_pressed(edge, VRA_AUTOMAP))
			tap(KEY_ESC);
		else if (choose)
			s_map_reset = 1;
	} else {
		release_game_controls();
		if (s_kb) {
			keyboard_keys(&p, choose, back, pause);
			if (s_kb_shut)
				s_kb = 0;	/* Done or Menu */
		} else if (s_photo) {
			/* Photo mode: look around the paused game; the move stick's
			 * click, B or the menu button bring the menu back. */
			if (edge[VRB_MOVECLICK] || back || pause) {
				s_photo = 0;
				vrxr_log("VR: photo mode off");
			}
		} else if (edge[VRB_MOVECLICK] && game_visible() && vrd_menu_above_game()) {
			s_photo = 1;
			vrxr_log("VR: photo mode on (the move stick's click, B or Menu to come back)");
		} else if (menu_pointer(&p, trig)) {
			/* Pointing: the trigger clicks; the buttons still back out. */
			if (back || pause)
				tap(KEY_ESC);
		} else {
			menu_keys(&p, choose, back || pause);
		}
	}
}

void vrd_add_axes(int speed_factor)
{
	fix ft;
	const int map = automap_in_front();
	if (!s_map_valid || !(game_in_front() || map))
		return;
	if (map && s_map_reset) {
		Controls.fire_primary_count++;	/* the map's own "back to the ship" */
		s_map_reset = 0;
	}
	ft = speed_factor * FrameTime;
	/* Full stick is the keyboard's full rate; the stock clamp follows. */
	Controls.forward_thrust_time += (fix)(dead(s_map.move_y, VRD_STICK_DEAD) * ft);
	Controls.sideways_thrust_time += (fix)(dead(s_map.move_x, VRD_STICK_DEAD) * ft);
	Controls.heading_time += (fix)(dead(s_map.look_x, VRD_STICK_DEAD) * FrameTime);
	/* Stick up lifts the nose (the engine's positive pitch dips it). */
	Controls.pitch_time -= (fix)(dead(s_map.look_y, VRD_STICK_DEAD) * FrameTime / 2);
	/* The grips raise and lower the ship: left up, right down, analog.
	 * s_roll is right minus left and has waited out the recenter chord. */
	Controls.vertical_thrust_time -= (fix)(dead(s_roll, VRD_GRIP_DEAD) * ft);
}

/* --- recenter and Switch Game ------------------------------------------------------ */

void vrd_note(const char *what)
{
	static char last[160];
	if (!s_connected || !strcmp(last, what))
		return;	/* once in a row: a note can come every frame */
	snprintf(last, sizeof(last), "%s", what);
	vrxr_log("VR: note: %s", what);
}

void vrd_recenter(void)
{
	vrxr_recenter();
}

const char *vrd_other_game_name(void)
{
#ifdef VRD_DESCENT2
	return "Descent";
#else
	return "Descent 2";
#endif
}

void vrd_switch_game(void)
{
	vrxr_log("VR: Switch Game: closing for %s", vrd_other_game_name());
	vrd_settings_save();
	s_switch = 1;
}

int vrd_switch_pending(void)
{
	return s_switch;
}

/* The other game's VR launcher, beside this game's own in its VR folder
 * (the engine runs from VR\game). */
static int other_launcher(char *path, size_t size)
{
	char dir[600];
	if (!vrxr_exe_dir(dir, sizeof(dir)))
		return 0;
#ifdef VRD_DESCENT2
	snprintf(path, size, "%s\\..\\Descent VR.cmd", dir);
#else
	snprintf(path, size, "%s\\..\\Descent 2 VR.cmd", dir);
#endif
	return 1;
}

static int other_launcher_exists(void)
{
	char path[700];
	FILE *f;
	if (!other_launcher(path, sizeof(path)))
		return 0;
	f = fopen(path, "rb");
	if (!f)
		return 0;
	fclose(f);
	return 1;
}

int vrd_switch_available(void)
{
	return s_connected && other_launcher_exists();
}

void vrd_after_exit(void)
{
	char path[700];
	if (!s_switch || !other_launcher(path, sizeof(path)))
		return;
	/* Not vrd_switch_available: this runs after vrd_shutdown, which has
	 * cleared s_connected. */
	if (!other_launcher_exists()) {
		vrxr_log("VR: Switch Game: no launcher at %s", path);
		return;
	}
	vrxr_start_program(path);
}

/* --- -startlevel ---------------------------------------------------------------- */

int vrd_start_level(void)
{
	return GameArg.VrStartLevel;
}

int vrd_skip_briefing(void)
{
	return s_skip_briefing;
}

void vrd_start_level_now(void)
{
	const int level = GameArg.VrStartLevel;
	if (level <= 0)
		return;
	if (!Players[Player_num].callsign[0]) {
		con_printf(CON_URGENT, "-startlevel %d needs a pilot (-pilot name); starting at the menu\n", level);
		return;
	}
	if (!load_mission_by_name((char *)VRD_MISSION)) {
		con_printf(CON_URGENT, "-startlevel %d: the mission would not load\n", level);
		return;
	}
	if (level > Last_level) {
		con_printf(CON_URGENT, "-startlevel %d: the mission has %d levels\n", level, Last_level);
		return;
	}
	con_printf(CON_NORMAL, "-startlevel %d as %s\n", level, Players[Player_num].callsign);
	if (s_connected)
		vrxr_log("VR: -startlevel %d: loading the level", level);
	Difficulty_level = PlayerCfg.DefaultDifficulty;
	s_skip_briefing = 1;
	StartNewGame(level);
	s_skip_briefing = 0;
}
