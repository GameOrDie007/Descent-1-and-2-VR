/* Part of Descent 1 & 2 VR, a derivative of DXX-Redux.
 * Distributed under the Parallax and D1X-Rebirth licenses in COPYING.txt:
 * non-commercial use only, and the source of any modified version must be
 * freely and publicly available.
 *
 * A pure mapping with no engine or VR-library dependency. */

#ifndef VR_HANDMAP_H
#define VR_HANDMAP_H

/* GameOrDieXR is Game Or Die's own closed library (GameOrDieXR.dll); these
 * headers are its interface, published with the port. */
#ifndef VRXR_API
#if defined(VRXR_BUILD)
#define VRXR_API __declspec(dllexport)
#elif defined(VRXR_STATIC)
#define VRXR_API
#else
#define VRXR_API __declspec(dllimport)
#endif
#endif


/* The engine's pstypes.h switches to #pragma pack(1) and never switches back,
 * so pin this header's packing: the layer (no engine headers) and the game
 * glue (engine headers first) must agree on these structs' layout. */
#pragma pack(push, 8)

/* The Leftorium and Swap sticks, as one pure function.
 *
 * Every OpenXR action this port binds sits on exactly one physical control,
 * so the controllers are read by the PHYSICAL hand first and mapped to what
 * the game wants here, afterwards.  The bindings themselves never change,
 * and both options are nothing but this mapping, so a self-test can run
 * every case of it without the game, a headset or a runtime.
 *
 *  - The Leftorium (left-handed) moves the weapon hand's controls to the left
 *    controller: primary fire on the left trigger, and the face buttons trade
 *    places (X/Y slide, A/B cycle weapons).
 *  - The sticks are NOT part of it.  The left-handed player it was made for
 *    keeps moving on the left stick and turning on the right, so the sticks
 *    (axes and their clicks together) have their own row, Swap sticks, which
 *    works with or without the Leftorium.
 *  - The grips raise and lower the ship (left grip up, right grip down)
 *    whatever the hands do: up is a direction, not a hand.
 *  - The menu button exists on the left controller only, so it opens the menu
 *    in every combination. */

typedef struct vr_phys_input {
	float lstick_x, lstick_y, rstick_x, rstick_y;
	float lgrip, rgrip, ltrigger, rtrigger;
	int a, b, x, y;				/* held */
	int a_edge, b_edge, x_edge, y_edge;	/* went down this frame */
	int lclick, lclick_edge, rclick, rclick_edge;
	int menu;				/* left-only menu button, held */
	/* Steam Frame only (0 on other controllers): its right hand's X and Y,
	 * the left hand's D-pad, and both shoulder buttons, held. */
	int frame_x, frame_y, dpad_up, dpad_down, dpad_left, dpad_right, lshoulder, rshoulder;
} vr_phys_input;

typedef struct vr_hand_map {
	float move_x, move_y, look_x, look_y, roll;
	int fire_primary, fire_secondary;	/* held */
	int slide_up, slide_down;		/* held */
	int cycle_primary_edge, cycle_secondary_edge;
	int recenter_edge;	/* the move stick's click: recenter, or photo mode over a menu */
	int pause;		/* the look stick's click, or the menu button: held */
	int primary_hand;	/* physical hand that fires primary: 0 left, 1 right */
} vr_hand_map;

/* The mapping itself is in GameOrDieXR (vr_handmap.c, closed). */
VRXR_API void vr_map_hands(const vr_phys_input *p, int leftorium, int swap_sticks,
	vr_hand_map *m);

#pragma pack(pop)

#endif
