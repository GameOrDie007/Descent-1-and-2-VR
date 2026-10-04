/* Part of Descent 1 & 2 VR, a derivative of DXX-Redux.
 * Distributed under the Parallax and D1X-Rebirth licenses in COPYING.txt:
 * non-commercial use only, and the source of any modified version must be
 * freely and publicly available. */

/* vr_options.c - the VR settings and the VR Options pages, for both games.
 *
 * A row on the pause menu and first on the game's Options; a short first
 * page; one page per topic, each setting on exactly one page; a Controls
 * page with one map per controller family, the shipped layout as the
 * default, one button one job, and Reset to defaults.  Built on the engine's
 * own newmenu pages.
 *
 * The settings live in vr.cfg beside the game, one key=value per line under
 * names that never change; a key that is missing keeps its default, so a
 * player's controls never move on an update. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pstypes.h"
#include "maths.h"
#include "vecmat.h"
#include "window.h"
#include "event.h"
#include "newmenu.h"
#include "console.h"
#include "gr.h"
#include "config.h"
#include "playsave.h"
#include "ogl_init.h"

#include "vr_descent.h"
#include "vr_xr.h"

vrd_settings VrSet;

static const char *const k_cfg = "vr.cfg";
/* The shipped button layout's number; 2 = the Frame's D-pad in Descent. */
#define VRD_BIND_LAYOUT 2

/* Stored names: never change these, or players' settings are lost. */
static const char *const k_action_key[VRA_COUNT] = {
	"none", "slideup", "slidedown", "nextprimary", "nextsecondary", "flare",
	"bomb", "rearview", "automap", "recenter", "afterburner", "headlight",
	"energyshield", "bankleft", "bankright"
};
static const char *const k_action_name[VRA_COUNT] = {
	"(none)", "Slide up", "Slide down", "Next primary", "Next secondary",
	"Fire flare", "Drop bomb", "Rear view", "Automap", "Recenter", "Afterburner",
	"Headlight", "Energy to shield", "Bank left", "Bank right"
};
static const char *const k_button_key[VRB_COUNT] = { "A", "B", "X", "Y", "MoveClick", "LookClick",
	"DpadUp", "DpadDown", "DpadLeft", "DpadRight", "LShoulder", "RShoulder" };
static const char *const k_family_key[VRD_FAMILIES] = { "Touch", "Index", "Frame" };

int vrd_action_allowed(int action)
{
	if (action < 0 || action >= VRA_COUNT)
		return 0;
#ifndef VRD_DESCENT2
	if (action == VRA_AFTERBURNER || action == VRA_HEADLIGHT || action == VRA_ENERGY_SHIELD)
		return 0;	/* Descent 2's ship systems */
#endif
	return 1;
}

const char *vrd_action_name(int action)
{
	return (action >= 0 && action < VRA_COUNT) ? k_action_name[action] : "?";
}

int vrd_family(void)
{
	switch (vrxr_controller_family()) {
	case VRXR_FAMILY_INDEX: return VRD_FAMILY_INDEX;
	case VRXR_FAMILY_FRAME: return VRD_FAMILY_FRAME;
	default:                return VRD_FAMILY_TOUCH;
	}
}

int vrd_family_buttons(int family)
{
	return family == VRD_FAMILY_FRAME ? VRB_COUNT : VRB_TOUCH_COUNT;
}

/* The button's printed name, as the player's hands hold it now: with the
 * Leftorium the weapon hand is the left, so on Touch "A" is pressed with X. */
const char *vrd_button_name(int family, int button)
{
	const int left = VrSet.leftorium, swap = VrSet.swap_sticks;
	if (button == VRB_MOVECLICK)
		return swap ? "R stick click" : "L stick click";
	if (family == VRD_FAMILY_FRAME) {
		/* The Frame's face buttons are all on the right hand: they stay put
		 * with the Leftorium. */
		switch (button) {
		case VRB_A: return "A";
		case VRB_B: return "B";
		case VRB_X: return "X";
		case VRB_Y: return "Y";
		case VRB_LOOKCLICK: return swap ? "L stick click" : "R stick click";
		case VRB_DPAD_UP: return "D-pad up";
		case VRB_DPAD_DOWN: return "D-pad down";
		case VRB_DPAD_LEFT: return "D-pad left";
		case VRB_DPAD_RIGHT: return "D-pad right";
		case VRB_LSHOULDER: return "L shoulder";
		default: return "R shoulder";
		}
	}
	if (button == VRB_LOOKCLICK)
		return swap ? "Left stick click" : "Right stick click";
	if (family == VRD_FAMILY_INDEX) {
		switch (button) {
		case VRB_A: return left ? "Left A" : "Right A";
		case VRB_B: return left ? "Left B" : "Right B";
		case VRB_X: return left ? "Right A" : "Left A";
		default:    return left ? "Right B" : "Left B";
		}
	}
	switch (button) {
	case VRB_A: return left ? "X" : "A";
	case VRB_B: return left ? "Y" : "B";
	case VRB_X: return left ? "A" : "X";
	default:    return left ? "B" : "Y";
	}
}

/* The layout that shipped: A/B slide, X/Y cycle weapons; the move stick's
 * click is Descent 2's afterburner (Descent 1: a flare), the look stick's
 * click drops a bomb. */
void vrd_bind_defaults(int family)
{
	int *b = VrSet.bind[family];
	int i;
	for (i = 0; i < VRB_COUNT; i++)
		b[i] = VRA_NONE;
	/* The grips raise and lower the ship (fixed); A and B bank, which few
	 * players need often. */
	b[VRB_A] = VRA_BANK_LEFT;
	b[VRB_B] = VRA_BANK_RIGHT;
	b[VRB_X] = VRA_NEXT_PRIMARY;
	b[VRB_Y] = VRA_NEXT_SECONDARY;
#ifdef VRD_DESCENT2
	b[VRB_MOVECLICK] = VRA_AFTERBURNER;
#else
	b[VRB_MOVECLICK] = VRA_FLARE;
#endif
	b[VRB_LOOKCLICK] = VRA_BOMB;
	if (family == VRD_FAMILY_FRAME) {
		/* The Frame's D-pad and shoulders take the systems Touch has no room
		 * for. */
		b[VRB_DPAD_UP] = VRA_REAR_VIEW;
		b[VRB_DPAD_DOWN] = VRA_AUTOMAP;
		b[VRB_LSHOULDER] = VRA_FLARE;
#ifdef VRD_DESCENT2
		b[VRB_DPAD_LEFT] = VRA_HEADLIGHT;
		b[VRB_DPAD_RIGHT] = VRA_ENERGY_SHIELD;
#else
		/* Descent has none of those systems: the D-pad gives the flare and
		 * the bomb a second home away from the stick clicks, which are easy
		 * to press by accident in a fight. */
		b[VRB_DPAD_LEFT] = VRA_FLARE;
		b[VRB_DPAD_RIGHT] = VRA_BOMB;
#endif
	}
}

void vrd_settings_defaults(void)
{
	int f;
	memset(&VrSet, 0, sizeof(VrSet));
	/* Release defaults: Modern aim, vibration, the steady observer view. */
	VrSet.leftorium = 0;
	VrSet.swap_sticks = 0;
	VrSet.aim_controller = 1;	/* Modern; Play style asks before the first new game */
	VrSet.rumble = 1;
	VrSet.ship_bob = 1;		/* as Parallax made it */
	VrSet.hud_size = 160;
	VrSet.hud_dist = 150;
	VrSet.hud_height = -5;
	VrSet.menu_size = 200;
	VrSet.menu_dist = 150;
	VrSet.desktop_view = 1;	/* the steady observer view */
	VrSet.desktop_windowed = 0;
	VrSet.show_advanced = 0;
	VrSet.play_style = -1;
	for (f = 0; f < VRD_FAMILIES; f++)
		vrd_bind_defaults(f);
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

void vrd_settings_load(void)
{
	FILE *fp;
	char line[160], key[80], val[80];
	int layout = 1, f, b;

	vrd_settings_defaults();
	fp = fopen(k_cfg, "r");
	if (!fp)
		return;
	while (fgets(line, sizeof(line), fp)) {
		int a;
		if (sscanf(line, " %79[^= ] = %79s", key, val) != 2)
			continue;
		if (!strcmp(key, "VRLeftorium")) VrSet.leftorium = atoi(val) != 0;
		else if (!strcmp(key, "VRSwapSticks")) VrSet.swap_sticks = atoi(val) != 0;
		else if (!strcmp(key, "VRAimController")) VrSet.aim_controller = atoi(val) != 0;
		else if (!strcmp(key, "VRRumble")) VrSet.rumble = atoi(val) != 0;
		else if (!strcmp(key, "VRShipBob")) VrSet.ship_bob = atoi(val) != 0;
		else if (!strcmp(key, "VRHudSize")) VrSet.hud_size = clampi(atoi(val), 40, 400);
		else if (!strcmp(key, "VRHudDistance")) VrSet.hud_dist = clampi(atoi(val), 40, 400);
		else if (!strcmp(key, "VRHudHeight")) VrSet.hud_height = clampi(atoi(val), -100, 100);
		else if (!strcmp(key, "VRMenuSize")) VrSet.menu_size = clampi(atoi(val), 40, 400);
		else if (!strcmp(key, "VRMenuDistance")) VrSet.menu_dist = clampi(atoi(val), 40, 400);
		else if (!strcmp(key, "VRDesktopView")) VrSet.desktop_view = clampi(atoi(val), 0, 1);
		else if (!strcmp(key, "VRDesktopWindowed")) VrSet.desktop_windowed = clampi(atoi(val), 0, 1);
		else if (!strcmp(key, "VRShowAdvanced")) VrSet.show_advanced = clampi(atoi(val), 0, 1);
		else if (!strcmp(key, "VRPlayStyle")) VrSet.play_style = clampi(atoi(val), -1, 1);
		else if (!strcmp(key, "VRBindLayout")) layout = atoi(val);
		else if (!strncmp(key, "VRBind", 6)) {
			/* VRBind<Family><Button>=<action> */
			for (f = 0; f < VRD_FAMILIES; f++) {
				const size_t fl = strlen(k_family_key[f]);
				if (strncmp(key + 6, k_family_key[f], fl))
					continue;
				for (b = 0; b < VRB_COUNT; b++) {
					if (strcmp(key + 6 + fl, k_button_key[b]))
						continue;
					for (a = 0; a < VRA_COUNT; a++)
						if (!strcmp(val, k_action_key[a]) && vrd_action_allowed(a))
							VrSet.bind[f][b] = a;
				}
			}
		}
	}
	fclose(fp);

	/* A vr.cfg from before layout 2 saved every button, the empty ones as
	 * "none", so new defaults never reached them: the buttons still empty
	 * take the shipped layout's job once (the Frame's D-pad in Descent). */
	if (layout < VRD_BIND_LAYOUT)
		for (f = 0; f < VRD_FAMILIES; f++) {
			int saved[VRB_COUNT];
			memcpy(saved, VrSet.bind[f], sizeof(saved));
			vrd_bind_defaults(f);
			for (b = 0; b < VRB_COUNT; b++)
				if (saved[b] != VRA_NONE)
					VrSet.bind[f][b] = saved[b];
		}
}

void vrd_settings_save(void)
{
	FILE *fp = fopen(k_cfg, "w");
	int f, b;
	if (!fp) {
		vrxr_log("VR: could not write %s", k_cfg);
		return;
	}
	fprintf(fp, "VRLeftorium=%d\n", VrSet.leftorium);
	fprintf(fp, "VRSwapSticks=%d\n", VrSet.swap_sticks);
	fprintf(fp, "VRAimController=%d\n", VrSet.aim_controller);
	fprintf(fp, "VRRumble=%d\n", VrSet.rumble);
	fprintf(fp, "VRShipBob=%d\n", VrSet.ship_bob);
	fprintf(fp, "VRHudSize=%d\n", VrSet.hud_size);
	fprintf(fp, "VRHudDistance=%d\n", VrSet.hud_dist);
	fprintf(fp, "VRHudHeight=%d\n", VrSet.hud_height);
	fprintf(fp, "VRMenuSize=%d\n", VrSet.menu_size);
	fprintf(fp, "VRMenuDistance=%d\n", VrSet.menu_dist);
	fprintf(fp, "VRDesktopView=%d\n", VrSet.desktop_view);
	fprintf(fp, "VRDesktopWindowed=%d\n", VrSet.desktop_windowed);
	fprintf(fp, "VRShowAdvanced=%d\n", VrSet.show_advanced);
	fprintf(fp, "VRPlayStyle=%d\n", VrSet.play_style);
	fprintf(fp, "VRBindLayout=%d\n", VRD_BIND_LAYOUT);
	for (f = 0; f < VRD_FAMILIES; f++)
		for (b = 0; b < VRB_COUNT; b++)
			fprintf(fp, "VRBind%s%s=%s\n", k_family_key[f], k_button_key[b],
				k_action_key[VrSet.bind[f][b]]);
	fclose(fp);
}

/* --- the pages ------------------------------------------------------------------ */

/* Hands and Aim: which hand, which stick flies (a ship turns with the stick,
 * so there is no turn mode to choose), what aims, vibration. */
#define HP_LEFT		0
#define HP_SWAP		1
#define HP_AIM_HAND	4
#define HP_AIM_SHIP	5
#define HP_RUMBLE	7
#define HP_BOB		8
static newmenu_item s_hand_items[9];

static void hand_page_read(void)
{
	VrSet.leftorium = s_hand_items[HP_LEFT].value;
	VrSet.swap_sticks = s_hand_items[HP_SWAP].value;
	VrSet.aim_controller = s_hand_items[HP_AIM_HAND].value;
	VrSet.rumble = s_hand_items[HP_RUMBLE].value;
	VrSet.ship_bob = s_hand_items[HP_BOB].value;
}

int vrd_ship_bob(void)
{
	return !vrd_enabled() || VrSet.ship_bob;	/* flat, the stock game */
}

static int hand_page_cb(newmenu *menu, d_event *event, void *userdata)
{
	(void)menu; (void)userdata;
	if (event->type == EVENT_NEWMENU_CHANGED)
		hand_page_read();
	return 0;
}

static void hand_page(void)
{
	newmenu_item *m = s_hand_items;
	memset(s_hand_items, 0, sizeof(s_hand_items));
	m[0].type = NM_TYPE_CHECK; m[0].text = "Leftorium (left-handed)"; m[0].value = VrSet.leftorium;
	m[1].type = NM_TYPE_CHECK; m[1].text = "Fly with the right stick"; m[1].value = VrSet.swap_sticks;
	m[2].type = NM_TYPE_TEXT;  m[2].text = "";
	m[3].type = NM_TYPE_TEXT;  m[3].text = "Aim with:";
	m[4].type = NM_TYPE_RADIO; m[4].text = "Your controller"; m[4].group = 0; m[4].value = VrSet.aim_controller;
	m[5].type = NM_TYPE_RADIO; m[5].text = "The ship's nose"; m[5].group = 0; m[5].value = !VrSet.aim_controller;
	m[6].type = NM_TYPE_TEXT;  m[6].text = "";
	m[7].type = NM_TYPE_CHECK; m[7].text = "Controller vibration"; m[7].value = VrSet.rumble;
	m[8].type = NM_TYPE_CHECK; m[8].text = "Ship bob"; m[8].value = VrSet.ship_bob;
	newmenu_do("Hands and Aim", NULL, 9, m, hand_page_cb, NULL);
	hand_page_read();
}

/* HUD, Menus and Observer View: the HUD panel, the menu panel, the desktop window. */
#define SP_HUD_SIZE	0
#define SP_HUD_DIST	1
#define SP_HUD_HEIGHT	2
#define SP_MENU_SIZE	4
#define SP_MENU_DIST	5
#define SP_DESK_EYE	8
#define SP_DESK_STEADY	9
static newmenu_item s_screen_items[10];

/* Each slider's steps, in centimetres.  Short sliders: a menu adds its widest
 * slider to every row, and long ones push the page off the panel. */
#define HUD_SIZE_AT(v)	(80 + 20 * (v))	/* 80-220 */
#define HUD_DIST_AT(v)	(50 + 20 * (v))	/* 50-210 */
#define HUD_HEIGHT_AT(v)	(-45 + 10 * (v))	/* -45-55 */
#define MENU_SIZE_AT(v)	(120 + 20 * (v))	/* 120-260 */
#define MENU_DIST_AT(v)	(70 + 20 * (v))	/* 70-230 */
static int s_screen_was[SP_DESK_STEADY + 1];

/* A setting changes only when its slider was moved, so a size saved between
 * the steps keeps its exact value. */
static void screen_page_read(void)
{
	const newmenu_item *m = s_screen_items;
	if (m[SP_HUD_SIZE].value != s_screen_was[SP_HUD_SIZE]) VrSet.hud_size = HUD_SIZE_AT(m[SP_HUD_SIZE].value);
	if (m[SP_HUD_DIST].value != s_screen_was[SP_HUD_DIST]) VrSet.hud_dist = HUD_DIST_AT(m[SP_HUD_DIST].value);
	if (m[SP_HUD_HEIGHT].value != s_screen_was[SP_HUD_HEIGHT]) VrSet.hud_height = HUD_HEIGHT_AT(m[SP_HUD_HEIGHT].value);
	if (m[SP_MENU_SIZE].value != s_screen_was[SP_MENU_SIZE]) VrSet.menu_size = MENU_SIZE_AT(m[SP_MENU_SIZE].value);
	if (m[SP_MENU_DIST].value != s_screen_was[SP_MENU_DIST]) VrSet.menu_dist = MENU_DIST_AT(m[SP_MENU_DIST].value);
	{
		int i;
		for (i = 0; i <= SP_DESK_STEADY; i++)
			s_screen_was[i] = m[i].value;
	}
	VrSet.desktop_view = m[SP_DESK_STEADY].value ? 1 : 0;
}

static int slider_at(int cm, int base, int step, int max)
{
	const int v = (cm - base + step / 2) / step;
	return v < 0 ? 0 : (v > max ? max : v);
}

static int screen_page_cb(newmenu *menu, d_event *event, void *userdata)
{
	(void)menu; (void)userdata;
	if (event->type == EVENT_NEWMENU_CHANGED)
		screen_page_read();	/* sizes change live, so they can be judged in place */
	return 0;
}

static void slider(newmenu_item *m, const char *text, int value, int max)
{
	m->type = NM_TYPE_SLIDER;
	m->text = (char *)text;
	m->min_value = 0;
	m->max_value = max;
	m->value = clampi(value, 0, max);
}

static void screen_page(void)
{
	newmenu_item *m = s_screen_items;
	memset(s_screen_items, 0, sizeof(s_screen_items));
	slider(&m[SP_HUD_SIZE], "HUD size", slider_at(VrSet.hud_size, 80, 20, 7), 7);
	slider(&m[SP_HUD_DIST], "HUD distance", slider_at(VrSet.hud_dist, 50, 20, 8), 8);
	slider(&m[SP_HUD_HEIGHT], "HUD height", slider_at(VrSet.hud_height, -45, 10, 10), 10);
	m[3].type = NM_TYPE_TEXT; m[3].text = "";
	slider(&m[SP_MENU_SIZE], "Menu size", slider_at(VrSet.menu_size, 120, 20, 7), 7);
	slider(&m[SP_MENU_DIST], "Menu distance", slider_at(VrSet.menu_dist, 70, 20, 8), 8);
	m[6].type = NM_TYPE_TEXT; m[6].text = "";
	m[7].type = NM_TYPE_TEXT; m[7].text = "Observer view (monitor):";
	m[SP_DESK_EYE].type = NM_TYPE_RADIO; m[SP_DESK_EYE].text = "Left eye";
	m[SP_DESK_EYE].group = 0; m[SP_DESK_EYE].value = VrSet.desktop_view == 0;
	m[SP_DESK_STEADY].type = NM_TYPE_RADIO; m[SP_DESK_STEADY].text = "Steady (for streams)";
	m[SP_DESK_STEADY].group = 0; m[SP_DESK_STEADY].value = VrSet.desktop_view == 1;
	{
		int i;
		for (i = 0; i <= SP_DESK_STEADY; i++)
			s_screen_was[i] = m[i].value;
	}
	newmenu_do("HUD and Observer", NULL, 10, m, screen_page_cb, NULL);	/* titles: 18 characters at most, the title font is large */
	screen_page_read();
}

/* Controls: one row per mappable button; choosing a row moves it to the next
 * action no other button has.  To swap two, set one to (none) first. */
static int s_ctl_rows;
#define CP_RESET	(s_ctl_rows + 1)
static newmenu_item s_ctl_items[VRB_COUNT + 8];	/* rows, a gap, Reset, a gap, three lines of text */
static char s_ctl_text[VRB_COUNT][64];
static int s_ctl_family;

static void ctl_rows(void)
{
	int b;
	for (b = 0; b < s_ctl_rows; b++)
		snprintf(s_ctl_text[b], sizeof(s_ctl_text[b]), "%s: %s",
			vrd_button_name(s_ctl_family, b), vrd_action_name(VrSet.bind[s_ctl_family][b]));
}

static void ctl_cycle(int button)
{
	int *bind = VrSet.bind[s_ctl_family];
	int a = bind[button], tries;
	for (tries = 0; tries < VRA_COUNT; tries++) {
		int b, taken = 0;
		a = (a + 1) % VRA_COUNT;
		if (!vrd_action_allowed(a))
			continue;
		for (b = 0; b < s_ctl_rows; b++)
			if (b != button && a != VRA_NONE && bind[b] == a)
				taken = 1;	/* a button never does two jobs: skip it */
		if (!taken)
			break;
	}
	bind[button] = a;
}

static int ctl_page_cb(newmenu *menu, d_event *event, void *userdata)
{
	(void)userdata;
	if (event->type == EVENT_NEWMENU_SELECTED) {
		const int c = newmenu_get_citem(menu);
		if (c >= 0 && c < s_ctl_rows)
			ctl_cycle(c);
		else if (c == CP_RESET)
			vrd_bind_defaults(s_ctl_family);
		ctl_rows();
		return 1;	/* stay on the page */
	}
	return 0;
}

static void ctl_page(void)
{
	newmenu_item *m = s_ctl_items;
	int b, n = 0;
	s_ctl_family = vrd_family();
	s_ctl_rows = vrd_family_buttons(s_ctl_family);
	ctl_rows();
	memset(s_ctl_items, 0, sizeof(s_ctl_items));
	for (b = 0; b < s_ctl_rows; b++) {
		m[n].type = NM_TYPE_MENU;
		m[n].text = s_ctl_text[b];
		n++;
	}
	m[n].type = NM_TYPE_TEXT; m[n].text = ""; n++;
	m[n].type = NM_TYPE_MENU; m[n].text = "Reset to defaults"; n++;
	m[n].type = NM_TYPE_TEXT; m[n].text = ""; n++;
	m[n].type = NM_TYPE_TEXT; m[n].text = "Fixed: triggers fire,"; n++;
	m[n].type = NM_TYPE_TEXT; m[n].text = "sticks fly, left grip raises,"; n++;
	m[n].type = NM_TYPE_TEXT; m[n].text = "right grip lowers, Menu pauses,"; n++;
	m[n].type = NM_TYPE_TEXT; m[n].text = "both grips held recenter."; n++;
	newmenu_do("Controls", s_ctl_family == VRD_FAMILY_FRAME ? "Steam Frame controllers" :
		s_ctl_family == VRD_FAMILY_INDEX ? "Valve Index controllers" :
		(VrSet.leftorium ? "Quest Touch (Leftorium)" : "Quest Touch"),
		n, m, ctl_page_cb, NULL);
}

/* Play style: two ways to fly, chosen once and changed here any time. */
static void play_style_apply(int classic)
{
	VrSet.play_style = classic ? 1 : 0;
	VrSet.aim_controller = !classic;
#ifdef OGL
	GameCfg.TexFilt = classic ? 0 : (ogl_maxanisotropy > 1.0f ? 3 : 2);
#endif
	PlayerCfg.AlphaEffects = !classic;
	PlayerCfg.DynLightColor = !classic;
	vrd_settings_save();
	write_player_file();
	/* The new filtering reaches textures already loaded (the GL context is
	 * kept under VR; this only rebuilds the textures). */
	gr_set_attributes();
	gr_set_mode(Game_screen_mode);
	vrxr_log("VR: play style %s", classic ? "Classic" : "Modern");
}

void vrd_play_style_menu(void)
{
	/* A page like the others: a message box lays itself out for the monitor
	 * and runs off the panel. */
	newmenu_item m[10];
	int r;
	if (!vrd_enabled())
		return;
	memset(m, 0, sizeof(m));
	m[0].type = NM_TYPE_MENU; m[0].text = "Modern";
	m[1].type = NM_TYPE_TEXT; m[1].text = "Aim with your controller:";
	m[2].type = NM_TYPE_TEXT; m[2].text = "shots go where your hand points";
	m[3].type = NM_TYPE_TEXT; m[3].text = "Smooth textures, colored light";
	m[4].type = NM_TYPE_TEXT; m[4].text = "";
	m[5].type = NM_TYPE_MENU; m[5].text = "Classic";
	m[6].type = NM_TYPE_TEXT; m[6].text = "As Parallax made it: shots go";
	m[7].type = NM_TYPE_TEXT; m[7].text = "where the ship points, pixel look";
	m[8].type = NM_TYPE_TEXT; m[8].text = "";
	m[9].type = NM_TYPE_TEXT; m[9].text = "Change it in VR Options";
	r = newmenu_do1(NULL, "How do you want to fly?", 10, m, NULL, NULL,
		VrSet.play_style == 1 ? 5 : 0);
	if (r == 0 || r == 5)
		play_style_apply(r == 5);
	else if (VrSet.play_style < 0)
		play_style_apply(0);	/* backed out the first time: Modern */
}

void vrd_play_style_once(void)
{
	if (vrd_enabled() && VrSet.play_style < 0)
		vrd_play_style_menu();
}

/* The first page: Recenter, Play style, the topic pages, Switch Game. */
#define FP_RECENTER	0
#define FP_STYLE	2
#define FP_HANDS	3
#define FP_SCREEN	4
#define FP_CONTROLS	5
#define FP_SWITCH	7
static newmenu_item s_first_items[8];
static char s_switch_text[48];
static char s_style_text[48];

static int first_page_cb(newmenu *menu, d_event *event, void *userdata)
{
	(void)userdata;
	if (event->type != EVENT_NEWMENU_SELECTED)
		return 0;
	switch (newmenu_get_citem(menu)) {
	case FP_RECENTER: vrd_recenter(); return 1;
	case FP_STYLE:
		vrd_play_style_menu();
		snprintf(s_style_text, sizeof(s_style_text), "Play style: %s...",
			VrSet.play_style == 1 ? "Classic" : "Modern");
		return 1;
	case FP_HANDS:    hand_page(); return 1;
	case FP_SCREEN:   screen_page(); return 1;
	case FP_CONTROLS: ctl_page(); return 1;
	case FP_SWITCH:   vrd_switch_game(); return 0;	/* closes the page */
	default:          return 1;
	}
}

void vrd_options_menu(void)
{
	newmenu_item *m = s_first_items;
	if (!vrd_enabled())
		return;
	snprintf(s_switch_text, sizeof(s_switch_text), "Switch to %s", vrd_other_game_name());
	memset(s_first_items, 0, sizeof(s_first_items));
	m[0].type = NM_TYPE_MENU; m[0].text = "Recenter";
	m[1].type = NM_TYPE_TEXT; m[1].text = "";
	snprintf(s_style_text, sizeof(s_style_text), "Play style: %s...",
		VrSet.play_style == 1 ? "Classic" : "Modern");
	m[2].type = NM_TYPE_MENU; m[2].text = s_style_text;
	m[3].type = NM_TYPE_MENU; m[3].text = "Hands and Aim...";
	m[4].type = NM_TYPE_MENU; m[4].text = "HUD and Observer View...";
	m[5].type = NM_TYPE_MENU; m[5].text = "Controls...";
	m[6].type = NM_TYPE_TEXT; m[6].text = "";
	m[7].type = NM_TYPE_MENU; m[7].text = s_switch_text;
	/* Switch Game only when the other game is installed. */
	newmenu_do("VR Options", NULL, vrd_switch_available() ? 8 : 6, m, first_page_cb, NULL);
	vrd_settings_save();
	vrxr_log("VR: VR Options closed; settings saved (Leftorium %d, swap sticks %d, aim %s, vibration %d, ship bob %d, observer view %s)",
		VrSet.leftorium, VrSet.swap_sticks, VrSet.aim_controller ? "controller" : "ship",
		VrSet.rumble, VrSet.ship_bob, VrSet.desktop_view ? "steady" : "left eye");
}
