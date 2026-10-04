/* Part of Descent 1 & 2 VR, a derivative of DXX-Redux.
 * Distributed under the Parallax and D1X-Rebirth licenses in COPYING.txt:
 * non-commercial use only, and the source of any modified version must be
 * freely and publicly available. */

/* vr_descent.h - Descent's side of VR (both games): where the eyes are in the mine, when
 * the engine draws them, the panel for everything flat, and the controllers.
 *
 * Every function here is safe to call with VR off: each then does nothing and
 * reports "no eye", so the stock paths run exactly as before. */

#ifndef VR_DESCENT_H
#define VR_DESCENT_H

#ifdef USE_VR

#include "vecmat.h"

struct window;

/* After the arguments are read: -vr connects to the headset (no graphics
 * needed).  No headset = one log line and the game carries on flat. */
void vrd_startup(void);
int vrd_enabled(void);
/* The session is bound to the current GL context: never rebuild it. */
int vrd_holds_context(void);

/* The frame, from event_process(): begin before the windows are drawn, end
 * before gr_flip(), always both, whatever happens between. */
#define VRD_FLAT	0	/* draw as stock, to the monitor */
#define VRD_EYES	1	/* the game's view in the eyes; menus or HUD on the panel */
#define VRD_PANEL	2	/* no game view: everything on the panel */
void vrd_frame_begin(void);
void vrd_frame_end(void);
int vrd_frame_kind(void);
int vrd_eye_begin(int eye);		/* 0 = skip this eye */
void vrd_eye_end(int eye);

/* The panel: a flat quad in the cockpit. */
#define VRD_PANEL_MENU	0
#define VRD_PANEL_HUD	1
int vrd_panel_begin(int what);		/* 0 = no panel this frame */
void vrd_panel_end(void);
int vrd_hud_pass(void);
/* Drawing on the panel: blend alpha separately (OGL_BLENDFUNC in ogl.c, gr.c). */
int vrd_panel_alpha(void);
void vrd_draw_hud(void);

/* Which windows go where: the game's in the eyes, anything over it on the panel. */
int vrd_is_game_window(struct window *wind);
int vrd_menu_above_game(void);

/* While an eye is being drawn. */
int vrd_eye_active(void);
/* Drawing into an eye (any view, the small missile view too): sprites
 * face the eye, upright to the ship (ogl.c g3_draw_bitmap_full). */
int vrd_in_eye_pass(void);
int vrd_repeat_eye(void);		/* the second eye: the game has already moved this frame */
#ifdef VRD_DESCENT2
/* Around render_frame(): window 0 is the eye's view, others are the
 * cockpit's small extra views, drawn flat inside the eye. */
void vrd_view_begin(int window_num);
void vrd_view_end(int window_num);
#endif
/* The eye's position and orientation in the mine, from the viewer's (base_pos,
 * base_orient) and the head; and the zoom that goes with the eye's frustum. */
void vrd_eye_view(vms_vector *eye_pos, vms_matrix *eye_orient,
	const vms_vector *base_pos, const vms_matrix *base_orient);
fix vrd_eye_zoom(void);
/* g3_set_view_matrix for cameras that set their own: the eye in VR. */
void vrd_set_view(vms_vector *pos, vms_matrix *orient, fix zoom);
/* The world projection for this eye, at the given near plane. */
int vrd_eye_frustum(double znear, double *l, double *r, double *b, double *t);

/* The crosshair: vrd_owns_reticle() = a VR pass is drawing, so the stock
 * centre is wrong; vrd_reticle() = where in this eye, or 0 = not here. */
int vrd_owns_reticle(void);
int vrd_reticle(int *x, int *y);
/* Sound: the viewer's orientation turned by the head (digiobj.c). */
vms_matrix *vrd_listener_orient(vms_matrix *ship);
/* The screen size the crosshair is scaled for in an eye: by angle. */
void vrd_reticle_screen(int *w, int *h);

/* The controllers: once a frame after the events (buttons, menu keys), and
 * from the control reader (the sticks and grips, added to its axes). */
void vrd_input_frame(void);
void vrd_add_axes(int speed_factor);
int automap_in_front(void);	/* automap.c: the map is open with nothing over it */

/* The headset paces the loop; the engine's own frame cap must stand aside. */
int vrd_paced(void);
/* Lay something out between frames at the panel's size, as it will be
 * drawn (briefing pages).  Returns 1 when it changed the screen; hand that
 * to vrd_layout_end. */
int vrd_layout_begin(void);
void vrd_layout_end(int pushed);

/* -startlevel N: straight into level N of the loaded pilot, no briefing. */
int vrd_start_level(void);
void vrd_start_level_now(void);
int vrd_skip_briefing(void);
/* -vrkit: every weapon and item at each level start (gamecntl.c, per game). */
void vrd_give_test_kit(void);

/* After the main loop, while the GL context still exists. */
void vrd_shutdown(void);
/* After the shutdown: start the other game if Switch Game asked for it. */
void vrd_after_exit(void);

/* --- buttons, settings and VR Options ------------------------------------------ */

/* Mappable buttons, by role: A/B are the weapon hand's face buttons, X/Y the
 * other hand's (they trade places with the Leftorium); the stick clicks follow
 * Swap sticks.  Triggers, grips, sticks and the menu button are fixed. */
#define VRB_A		0
#define VRB_B		1
#define VRB_X		2
#define VRB_Y		3
#define VRB_MOVECLICK	4
#define VRB_LOOKCLICK	5
/* Steam Frame only: */
#define VRB_DPAD_UP	6
#define VRB_DPAD_DOWN	7
#define VRB_DPAD_LEFT	8
#define VRB_DPAD_RIGHT	9
#define VRB_LSHOULDER	10
#define VRB_RSHOULDER	11
#define VRB_COUNT	12
#define VRB_TOUCH_COUNT	6	/* Touch and Index have the first six */

#define VRA_NONE		0
#define VRA_SLIDE_UP		1
#define VRA_SLIDE_DOWN		2
#define VRA_NEXT_PRIMARY	3
#define VRA_NEXT_SECONDARY	4
#define VRA_FLARE		5
#define VRA_BOMB		6
#define VRA_REAR_VIEW		7
#define VRA_AUTOMAP		8
#define VRA_RECENTER		9
#define VRA_AFTERBURNER		10	/* Descent 2 only */
#define VRA_HEADLIGHT		11	/* Descent 2 only */
#define VRA_ENERGY_SHIELD	12	/* Descent 2 only */
#define VRA_BANK_LEFT		13
#define VRA_BANK_RIGHT		14
#define VRA_COUNT		15

#define VRD_FAMILY_TOUCH	0
#define VRD_FAMILY_INDEX	1
#define VRD_FAMILY_FRAME	2
#define VRD_FAMILIES		3

/* The VR settings, saved in vr.cfg beside the game (vr_options.c). */
typedef struct vrd_settings {
	int leftorium, swap_sticks, aim_controller, rumble;
	int hud_size, hud_dist, hud_height;	/* centimetres; height + is up */
	int menu_size, menu_dist;		/* centimetres */
	int desktop_view;			/* 0 the left eye, 1 the steady view */
	int desktop_windowed;		/* 0 the view fills the monitor, 1 a window (Alt+Enter) */
	int show_advanced;		/* the game's Options shows its advanced rows */
	int play_style;			/* -1 not chosen yet, 0 Modern, 1 Classic */
	int ship_bob;			/* the ship's own gentle up-and-down drift */
	int bind[VRD_FAMILIES][VRB_COUNT];	/* VRA_* per button, per controller family */
} vrd_settings;
extern vrd_settings VrSet;

/* 1 = the ship drifts up and down as the stock game has it (controls.c). */
int vrd_ship_bob(void);

void vrd_settings_defaults(void);
void vrd_settings_load(void);
void vrd_settings_save(void);
/* Play style: Modern (aim with the controller, the enhanced look) or Classic
 * (shots where the ship points, the original look).  _once asks only if it
 * was never chosen (before the first new game). */
void vrd_play_style_menu(void);
void vrd_play_style_once(void);
/* Alt+Enter in the headset: the desktop window between filling the
 * monitor and a window, kept for next time.  Returns 1 when it fills. */
int vrd_toggle_desktop(void);
int vrd_desktop_fills(void);
void vrd_bind_defaults(int family);
int vrd_action_allowed(int action);		/* this game has it */
const char *vrd_action_name(int action);
const char *vrd_button_name(int family, int button);
int vrd_family(void);				/* VRD_FAMILY_* of the controllers in hand */
int vrd_family_buttons(int family);		/* how many VRB_* it has */

/* VR Options: the pause menu's row and Options' first row open it. */
void vrd_options_menu(void);
void vrd_recenter(void);
/* A line in the VR log from engine code (once, not every frame). */
void vrd_note(const char *what);
const char *vrd_other_game_name(void);		/* "Descent 2" from Descent 1, and back */
void vrd_switch_game(void);			/* close this game, start the other */
/* Switch Game is offered only when the other game is installed: Setup
 * writes its launcher beside this game's (VR\Descent VR.cmd and
 * VR\Descent 2 VR.cmd, one of them a forwarder to the other game's VR
 * folder). */
int vrd_switch_available(void);
int vrd_switch_pending(void);

/* Each shot's direction: the controller's aim with "Aim with: the
 * controller", else the ship's own (ship_fvec). */
vms_vector vrd_shot_dir(vms_vector ship_fvec);
/* A shot or missile left the ship (damage = its weapon's strength). */
void vrd_on_fire(fix damage, int secondary);

/* The panel pass draws each window over the game: windows laid out for the
 * monitor are scaled up to the panel around their draw. */
void vrd_window_draw_begin(struct window *wind);
void vrd_window_draw_end(struct window *wind);
/* After the windows on the panel: the on-screen keyboard over a text field. */
void vrd_panel_overlay(void);

#endif /* USE_VR */

#endif
