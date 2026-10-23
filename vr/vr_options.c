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
	"energyshield", "bankleft", "bankright", "changeview", "slideleft", "slideright"
};
static const char *const k_action_name[VRA_COUNT] = {
	"(none)", "Slide up", "Slide down", "Next primary", "Next secondary",
	"Fire flare", "Drop bomb", "Rear view", "Automap", "Recenter", "Afterburner",
	"Headlight", "Energy to shield", "Bank left", "Bank right", "Change view",
	"Slide left", "Slide right"
};
static const char *const k_button_key[VRB_COUNT] = { "A", "B", "X", "Y", "MoveClick", "LookClick",
	"DpadUp", "DpadDown", "DpadLeft", "DpadRight", "LShoulder", "RShoulder", "LGrip", "RGrip" };
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
	return family == VRD_FAMILY_FRAME ? VRB_COUNT : VRB_TOUCH_COUNT + 2;
}

/* The Controls page's rows: the face buttons and stick clicks, the grips,
 * then the Frame's D-pad and shoulder buttons. */
int vrd_family_button(int family, int row)
{
	(void)family;
	if (row < VRB_TOUCH_COUNT)
		return row;
	if (row == VRB_TOUCH_COUNT)
		return VRB_LGRIP;
	if (row == VRB_TOUCH_COUNT + 1)
		return VRB_RGRIP;
	return row - 2;
}

/* The button's printed name, as the player's hands hold it now: with the
 * Leftorium the weapon hand is the left, so on Touch "A" is pressed with X. */
const char *vrd_button_name(int family, int button)
{
	const int left = VrSet.leftorium, swap = VrSet.swap_sticks;
	if (button == VRB_LGRIP)
		return "Left grip";
	if (button == VRB_RGRIP)
		return "Right grip";
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
	/* The grips raise and lower the ship, analog; A and B bank, which few
	 * players need often. */
	b[VRB_LGRIP] = VRA_SLIDE_UP;
	b[VRB_RGRIP] = VRA_SLIDE_DOWN;
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
	VrSet.aim_head = 0;
	VrSet.rumble = 1;
	VrSet.ship_bob = 1;		/* as Parallax made it */
	VrSet.swivel = 0;		/* a comfort choice, not the default */
	VrSet.invert_look = 0;
	VrSet.cockpit = 0;		/* a comfort choice, not the default */
	VrSet.classic_follow = 1;
	VrSet.classic_size = 3;		/* 100%: the picture fills the view */
	VrSet.classic_height = 4;
	VrSet.third_person = 0;
	VrSet.book_float = 0;		/* the Game Tome in the off hand, like a book */
	VrSet.head_steer = 0;		/* options; the sticks fly by default */
	VrSet.stick_layout = 0;		/* the sticks as shipped; FPV is a choice */
	VrSet.motion_fly = 0;
	VrSet.world_size = 100;		/* the mine as made: one mine unit a metre */
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
		else if (!strcmp(key, "VRAimHead")) VrSet.aim_head = atoi(val) != 0;
		else if (!strcmp(key, "VRStickLayout")) VrSet.stick_layout = clampi(atoi(val), 0, 1);
		else if (!strcmp(key, "VRRumble")) VrSet.rumble = atoi(val) != 0;
		else if (!strcmp(key, "VRShipBob")) VrSet.ship_bob = atoi(val) != 0;
		else if (!strcmp(key, "VRSwivel")) VrSet.swivel = atoi(val) != 0;
		else if (!strcmp(key, "VRInvertLook")) VrSet.invert_look = atoi(val) != 0;
		else if (!strcmp(key, "VRCockpit")) VrSet.cockpit = clampi(atoi(val), 0, 2);
		else if (!strcmp(key, "VRClassicFollow")) VrSet.classic_follow = atoi(val) != 0;
		else if (!strcmp(key, "VRClassicSize")) VrSet.classic_size = clampi(atoi(val), 0, 6);
		else if (!strcmp(key, "VRClassicHeight")) VrSet.classic_height = clampi(atoi(val), 0, 8);
		else if (!strcmp(key, "VRThirdPerson")) VrSet.third_person = clampi(atoi(val), 0, 2);
		else if (!strcmp(key, "VRBookFloat")) VrSet.book_float = atoi(val) != 0;
		else if (!strcmp(key, "VRHeadSteering")) VrSet.head_steer = atoi(val) != 0;
		else if (!strcmp(key, "VRMotionFlying")) VrSet.motion_fly = atoi(val) != 0;
		else if (!strcmp(key, "VRWorldSize")) VrSet.world_size = clampi(atoi(val), 50, 200);
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
	fprintf(fp, "VRAimHead=%d\n", VrSet.aim_head);
	fprintf(fp, "VRStickLayout=%d\n", VrSet.stick_layout);
	fprintf(fp, "VRRumble=%d\n", VrSet.rumble);
	fprintf(fp, "VRShipBob=%d\n", VrSet.ship_bob);
	fprintf(fp, "VRSwivel=%d\n", VrSet.swivel);
	fprintf(fp, "VRInvertLook=%d\n", VrSet.invert_look);
	fprintf(fp, "VRCockpit=%d\n", VrSet.cockpit);
	fprintf(fp, "VRClassicFollow=%d\n", VrSet.classic_follow);
	fprintf(fp, "VRClassicSize=%d\n", VrSet.classic_size);
	fprintf(fp, "VRClassicHeight=%d\n", VrSet.classic_height);
	fprintf(fp, "VRThirdPerson=%d\n", VrSet.third_person);
	fprintf(fp, "VRBookFloat=%d\n", VrSet.book_float);
	fprintf(fp, "VRHeadSteering=%d\n", VrSet.head_steer);
	fprintf(fp, "VRMotionFlying=%d\n", VrSet.motion_fly);
	fprintf(fp, "VRWorldSize=%d\n", VrSet.world_size);
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
#define HP_INVERT	2
#define HP_AIM_HAND	5
#define HP_AIM_HEAD	6
#define HP_AIM_SHIP	7
#define HP_RUMBLE	9
#define HP_BOB		10
#define HP_SWIVEL	11
#define HP_FOLLOW	12
#define HP_CSIZE	13
#define HP_CHEIGHT	14
#define HP_BOOK_FLOAT	15
static newmenu_item s_hand_items[16];

static void hand_page_read(void)
{
	VrSet.leftorium = s_hand_items[HP_LEFT].value;
	VrSet.swap_sticks = s_hand_items[HP_SWAP].value;
	VrSet.invert_look = s_hand_items[HP_INVERT].value;
	VrSet.aim_controller = s_hand_items[HP_AIM_HAND].value || s_hand_items[HP_AIM_HEAD].value;
	VrSet.aim_head = s_hand_items[HP_AIM_HEAD].value;
	VrSet.rumble = s_hand_items[HP_RUMBLE].value;
	VrSet.ship_bob = s_hand_items[HP_BOB].value;
	VrSet.swivel = s_hand_items[HP_SWIVEL].value;
	VrSet.classic_follow = s_hand_items[HP_FOLLOW].value;
	VrSet.classic_size = s_hand_items[HP_CSIZE].value;
	VrSet.classic_height = s_hand_items[HP_CHEIGHT].value;
	VrSet.book_float = s_hand_items[HP_BOOK_FLOAT].value;
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
	m[2].type = NM_TYPE_CHECK; m[2].text = "Invert look (up and down)"; m[2].value = VrSet.invert_look;
	m[3].type = NM_TYPE_TEXT;  m[3].text = "";
	m[4].type = NM_TYPE_TEXT;  m[4].text = "Aim with:";
	m[5].type = NM_TYPE_RADIO; m[5].text = "Your controller"; m[5].group = 0; m[5].value = VrSet.aim_controller && !VrSet.aim_head;
	m[6].type = NM_TYPE_RADIO; m[6].text = "Your head"; m[6].group = 0; m[6].value = VrSet.aim_controller && VrSet.aim_head;
	m[7].type = NM_TYPE_RADIO; m[7].text = "The ship's nose"; m[7].group = 0; m[7].value = !VrSet.aim_controller;
	m[8].type = NM_TYPE_TEXT;  m[8].text = "";
	m[9].type = NM_TYPE_CHECK; m[9].text = "Controller vibration"; m[9].value = VrSet.rumble;
	m[10].type = NM_TYPE_CHECK; m[10].text = "Ship bob"; m[10].value = VrSet.ship_bob;
	m[11].type = NM_TYPE_CHECK; m[11].text = "Swivel chair"; m[11].value = VrSet.swivel;
	m[12].type = NM_TYPE_CHECK; m[12].text = "Classic cockpit follows your head"; m[12].value = VrSet.classic_follow;
	m[13].type = NM_TYPE_SLIDER; m[13].text = "Classic cockpit size"; m[13].value = VrSet.classic_size;
	m[13].min_value = 0; m[13].max_value = 6;
	m[14].type = NM_TYPE_SLIDER; m[14].text = "Classic cockpit height"; m[14].value = VrSet.classic_height;
	m[14].min_value = 0; m[14].max_value = 8;
	m[15].type = NM_TYPE_CHECK; m[15].text = "Float the Game Tome"; m[15].value = VrSet.book_float;
	newmenu_do("Hands and Aim", NULL, 16, m, hand_page_cb, NULL);
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

/* Flying and World Size: the ways to fly besides the sticks, and the mine's
 * scale against the player. */
#define FL_HEAD		0
#define FL_MOTION	1
#define FL_WORLD	3
static newmenu_item s_fly_items[10];
static char s_world_text[32];
#define WORLD_AT(v)	(50 + 25 * (v))	/* 50-200 percent */

static void flying_page_read(void)
{
	VrSet.head_steer = s_fly_items[FL_HEAD].value;
	VrSet.motion_fly = s_fly_items[FL_MOTION].value;
	VrSet.world_size = WORLD_AT(s_fly_items[FL_WORLD].value);
	snprintf(s_world_text, sizeof(s_world_text), "World size: %d%%", VrSet.world_size);
}

static int flying_page_cb(newmenu *menu, d_event *event, void *userdata)
{
	(void)menu; (void)userdata;
	if (event->type == EVENT_NEWMENU_CHANGED)
		flying_page_read();	/* World size changes live, so it can be judged in place */
	return 0;
}

static void flying_page(void)
{
	newmenu_item *m = s_fly_items;
	memset(s_fly_items, 0, sizeof(s_fly_items));
	m[FL_HEAD].type = NM_TYPE_CHECK; m[FL_HEAD].text = "Head steering"; m[FL_HEAD].value = VrSet.head_steer;
	m[FL_MOTION].type = NM_TYPE_CHECK; m[FL_MOTION].text = "Motion flying (grips)"; m[FL_MOTION].value = VrSet.motion_fly;
	m[2].type = NM_TYPE_TEXT; m[2].text = "";
	snprintf(s_world_text, sizeof(s_world_text), "World size: %d%%", clampi(VrSet.world_size, 50, 200));
	slider(&m[FL_WORLD], s_world_text, (clampi(VrSet.world_size, 50, 200) - 50 + 12) / 25, 6);
	m[4].type = NM_TYPE_TEXT; m[4].text = "";
	m[5].type = NM_TYPE_TEXT; m[5].text = "Head steering: fly where you look.";
	m[6].type = NM_TYPE_TEXT; m[6].text = "Motion flying: left grip, move it";
	m[7].type = NM_TYPE_TEXT; m[7].text = "to slide; right grip, twist to bank.";
	m[8].type = NM_TYPE_TEXT; m[8].text = "World size: 100% is as made;";
	m[9].type = NM_TYPE_TEXT; m[9].text = "lower: a smaller mine, more depth.";
	newmenu_do("Flying", NULL, 10, m, flying_page_cb, NULL);	/* titles: 18 characters at most */
	flying_page_read();
}

/* Controls: one row per mappable button; choosing a row moves it to the next
 * action no other button has.  To swap two, set one to (none) first. */
static int s_ctl_rows;
#define CP_RESET	(s_ctl_rows + 1)
#define CP_LAYOUT	(s_ctl_rows + 2)
static newmenu_item s_ctl_items[VRB_COUNT + 9];	/* rows, a gap, Reset, Stick layout, a gap, three lines of text */
static char s_layout_text[48];
static char s_ctl_text[VRB_COUNT][64];
static int s_ctl_family;

static void ctl_rows(void)
{
	int r;
	for (r = 0; r < s_ctl_rows; r++) {
		const int b = vrd_family_button(s_ctl_family, r);
		snprintf(s_ctl_text[r], sizeof(s_ctl_text[r]), "%s: %s",
			vrd_button_name(s_ctl_family, b), vrd_action_name(VrSet.bind[s_ctl_family][b]));
	}
	snprintf(s_layout_text, sizeof(s_layout_text), "Stick layout: %s", VrSet.stick_layout ? "FPV (drone)" : "Standard");
	if (s_ctl_items[CP_LAYOUT + 2].type == NM_TYPE_TEXT) {
		s_ctl_items[CP_LAYOUT + 2].text = VrSet.stick_layout ? "FPV: left stick up, down, turn;" : "Fixed: triggers fire, sticks fly,";
		s_ctl_items[CP_LAYOUT + 3].text = VrSet.stick_layout ? "right stick pitch, bank; grips" : "Menu pauses, both grips held";
		s_ctl_items[CP_LAYOUT + 4].text = VrSet.stick_layout ? "forward, back. Both held: recenter." : "recenter. Keyboard: Home recenters.";
	}
}

/* FPV takes the sticks' strafing away: A and B slide instead of banking
 * (the right stick banks), and back again, when they are still as shipped. */
static void ctl_layout_toggle(void)
{
	int f;
	VrSet.stick_layout = !VrSet.stick_layout;
	for (f = 0; f < VRD_FAMILIES; f++) {
		int *b = VrSet.bind[f];
		if (VrSet.stick_layout && b[VRB_A] == VRA_BANK_LEFT && b[VRB_B] == VRA_BANK_RIGHT) {
			b[VRB_A] = VRA_SLIDE_LEFT;
			b[VRB_B] = VRA_SLIDE_RIGHT;
		} else if (!VrSet.stick_layout && b[VRB_A] == VRA_SLIDE_LEFT && b[VRB_B] == VRA_SLIDE_RIGHT) {
			b[VRB_A] = VRA_BANK_LEFT;
			b[VRB_B] = VRA_BANK_RIGHT;
		}
	}
	vrxr_log("VR: stick layout %s", VrSet.stick_layout ? "FPV" : "standard");
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
		for (b = 0; b < VRB_COUNT; b++)
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
			ctl_cycle(vrd_family_button(s_ctl_family, c));
		else if (c == CP_RESET)
			vrd_bind_defaults(s_ctl_family);
		else if (c == CP_LAYOUT)
			ctl_layout_toggle();
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
	m[n].type = NM_TYPE_MENU; m[n].text = s_layout_text; n++;
	m[n].type = NM_TYPE_TEXT; m[n].text = ""; n++;
	m[n].type = NM_TYPE_TEXT; n++;
	m[n].type = NM_TYPE_TEXT; n++;
	m[n].type = NM_TYPE_TEXT; n++;
	ctl_rows();	/* the layout row and the fixed lines */
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
	VrSet.aim_head = 0;
#ifdef OGL
	GameCfg.TexFilt = classic ? 0 : (ogl_maxanisotropy > 1.0f ? 3 : 2);
#endif
	PlayerCfg.AlphaEffects = !classic;
	PlayerCfg.DynLightColor = !classic;
	if (VrSet.cockpit)
		VrSet.cockpit = classic ? 2 : 1;	/* each style its own cockpit */
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

/* The first page: Recenter, the Game Tome, Play style, the topic pages,
 * Switch Game. */
#define FP_RECENTER	0
#define FP_MANUAL	1
#define FP_VIEW		2
#define FP_STYLE	4
#define FP_HANDS	5
#define FP_FLYING	6
#define FP_SCREEN	7
#define FP_CONTROLS	8
#define FP_SWITCH	10
static newmenu_item s_first_items[11];
static char s_switch_text[48];
static char s_style_text[48];
static char s_manual_text[48];
static char s_view_text[48];

const char *vrd_view_name(void)
{
	if (VrSet.third_person)
		return VrSet.third_person == 2 ? "Behind, far" : "Behind, near";
	return VrSet.cockpit == 2 ? "Classic cockpit" : VrSet.cockpit == 1 ? "Modern cockpit" : "No cockpit";
}

/* No cockpit -> Modern -> Classic -> behind, near -> behind, far -> no cockpit. */
void vrd_cycle_view(void)
{
	const int v = VrSet.third_person ? 2 + VrSet.third_person : VrSet.cockpit;
	const int n = (v + 1) % 5;
	VrSet.third_person = n >= 3 ? n - 2 : 0;
	if (n < 3)
		VrSet.cockpit = n;
}

static const char *view_name(void)
{
	return vrd_view_name();
}

static int first_page_cb(newmenu *menu, d_event *event, void *userdata)
{
	(void)userdata;
	if (event->type != EVENT_NEWMENU_SELECTED)
		return 0;
	switch (newmenu_get_citem(menu)) {
	case FP_RECENTER: vrd_recenter(); return 1;
	case FP_MANUAL:
		if (!vrd_book_open()) {
			/* No book yet: say so on the row itself (a message box is laid out
			 * for the monitor and runs off the panel). */
			snprintf(s_manual_text, sizeof(s_manual_text), "Game Tome: run Setup again");
			vrxr_log("VR: Game Tome chosen, but there is no tome\\tome.txt beside the game");
		}
		return 1;
	case FP_VIEW:
		vrd_cycle_view();
		snprintf(s_view_text, sizeof(s_view_text), "View: %s", view_name());
		vrxr_log("VR: view: %s", view_name());
		return 1;
	case FP_STYLE:
		vrd_play_style_menu();
		snprintf(s_style_text, sizeof(s_style_text), "Play style: %s...",
			VrSet.play_style == 1 ? "Classic" : "Modern");
		return 1;
	case FP_HANDS:    hand_page(); return 1;
	case FP_FLYING:   flying_page(); return 1;
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
	snprintf(s_manual_text, sizeof(s_manual_text), "Game Tome");
	m[1].type = NM_TYPE_MENU; m[1].text = s_manual_text;
	snprintf(s_view_text, sizeof(s_view_text), "View: %s", view_name());
	m[2].type = NM_TYPE_MENU; m[2].text = s_view_text;
	m[3].type = NM_TYPE_TEXT; m[3].text = "";
	snprintf(s_style_text, sizeof(s_style_text), "Play style: %s...",
		VrSet.play_style == 1 ? "Classic" : "Modern");
	m[4].type = NM_TYPE_MENU; m[4].text = s_style_text;
	m[5].type = NM_TYPE_MENU; m[5].text = "Hands and Aim...";
	m[6].type = NM_TYPE_MENU; m[6].text = "Flying and World Size...";
	m[7].type = NM_TYPE_MENU; m[7].text = "HUD and Observer View...";
	m[8].type = NM_TYPE_MENU; m[8].text = "Controls...";
	m[9].type = NM_TYPE_TEXT; m[9].text = "";
	m[10].type = NM_TYPE_MENU; m[10].text = s_switch_text;
	/* Switch Game only when the other game is installed. */
	newmenu_do("VR Options", NULL, vrd_switch_available() ? 11 : 9, m, first_page_cb, NULL);
	vrd_settings_save();
	vrxr_log("VR: VR Options closed; settings saved (Leftorium %d, swap sticks %d, aim %s, vibration %d, ship bob %d, swivel chair %d, stick layout %s, head steering %d, motion flying %d, world size %d%%, invert look %d, cockpit %d (classic: follow %d, size %d, height %d), view %s, observer view %s, manual %s)",
		VrSet.leftorium, VrSet.swap_sticks, !VrSet.aim_controller ? "ship" : VrSet.aim_head ? "head" : "controller",
		VrSet.rumble, VrSet.ship_bob, VrSet.swivel, VrSet.stick_layout ? "FPV" : "standard", VrSet.head_steer, VrSet.motion_fly, VrSet.world_size, VrSet.invert_look, VrSet.cockpit, VrSet.classic_follow, VrSet.classic_size, VrSet.classic_height, view_name(), VrSet.desktop_view ? "steady" : "left eye",
		VrSet.book_float ? "floating" : "in hand");
}
