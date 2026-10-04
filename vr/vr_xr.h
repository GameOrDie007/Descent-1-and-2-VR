/* Part of Descent 1 & 2 VR, a derivative of DXX-Redux.
 * Distributed under the Parallax and D1X-Rebirth licenses in COPYING.txt:
 * non-commercial use only, and the source of any modified version must be
 * freely and publicly available. */

/* vr_xr.h - the OpenXR layer's interface.
 *
 * The layer itself is GameOrDieXR.dll, Game Or Die's own closed library
 * (all rights reserved); this interface is published with the port.
 *
 * The layer knows nothing about Descent.  It owns the instance, session,
 * swapchains, the frame loop, the reference space, the panel quad and the
 * controllers; the game side (vr/vr_descent.c) owns where the player
 * is, what a button means, and where menus and the HUD go.
 *
 * Rules built into the shape of these calls:
 *   - every frame begun by vrxr_frame_begin() is ended by vrxr_frame_end(),
 *     whatever happens in between: call it unconditionally;
 *   - everything runs on the thread that owns the GL context;
 *   - vrxr_shutdown() before the GL context goes, and before exit;
 *   - the pose submitted is the pose the eye was drawn from. */

#ifndef VR_XR_H
#define VR_XR_H

#include "vr_handmap.h"

/* Pinned packing: see vr_handmap.h.  The engine compiles everything packed. */
#pragma pack(push, 8)

#ifdef __cplusplus
extern "C" {
#endif

/* One eye, for this frame: the target is bound when vrxr_eye_begin() returns. */
typedef struct vrxr_eye {
	int width, height;			/* the eye image, pixels */
	float tan_l, tan_r, tan_d, tan_u;	/* frustum tangents, ordered: l < r, d < u */
	float orient[4];			/* x, y, z, w, in tracking space (right-handed, Y up, -Z ahead) */
	float pos[3];				/* metres, tracking space */
} vrxr_eye;

/* One line into vr_log.txt next to the executable, unbuffered, so it survives
 * a crash.  The previous run's is kept as vr_log.prev.txt (vrxr_log_start). */
VRXR_API void vrxr_log_start(const char *what);
VRXR_API void vrxr_log(const char *fmt, ...);

/* Instance, headset and eye size, with no graphics needed, so the game can learn
 * the eye size before it opens its window.  0 = no VR this run; the reason is
 * logged and vrxr_failure() returns it.  The game then carries on flat. */
VRXR_API int vrxr_connect(const char *app_name);
VRXR_API const char *vrxr_failure(void);
VRXR_API void vrxr_eye_size(int *w, int *h);

/* The session, swapchains and panel: the GL context must exist and be current
 * on this thread.  0 = no VR this run (logged). */
VRXR_API int vrxr_bring_up(void);
/* Draw the eyes multisampled (before vrxr_bring_up; 0 or 1 = off). */
VRXR_API void vrxr_set_msaa(int samples);

/* The GL context is no longer the one the session was bound to (an engine
 * that rebuilds its context on a video mode change).  The session cannot
 * follow: shut down and carry on flat. */
VRXR_API int vrxr_context_changed(void);

/* A session exists and the runtime has asked for frames. */
VRXR_API int vrxr_active(void);
/* A frame has begun and not yet ended. */
VRXR_API int vrxr_frame_is_open(void);

/* Wait for the runtime, begin a frame, read the controllers and locate the
 * eyes.  1 = draw the eyes this frame; 0 = do not (vrxr_frame_end still). */
VRXR_API int vrxr_frame_begin(void);
VRXR_API int vrxr_eye_begin(int eye, vrxr_eye *out);	/* 0 = skip this eye */
VRXR_API void vrxr_eye_end(int eye);

/* The panel: a flat quad in the tracking space, for menus and the HUD.  Its
 * texture is panel_w x panel_h; the pose is where it hangs, in metres. */
VRXR_API void vrxr_panel_size(int *w, int *h);
VRXR_API int vrxr_panel_begin(void);			/* binds the panel texture; 0 = no panel */
VRXR_API void vrxr_panel_end(void);
/* A picture for the panel (the port's splash): 8-bit RGB or RGBA rows, top
 * row first, uploaded once; NULL frees it.  vrxr_panel_picture draws it over
 * the open panel, fitted with clear bars, before the panel is released (so
 * the observer view shows it too). */
VRXR_API int vrxr_set_panel_picture(const unsigned char *pixels, int w, int h, int channels);
VRXR_API void vrxr_panel_picture(void);
VRXR_API void vrxr_panel_place(const float orient[4], const float pos[3],
	float width_m, float height_m);

VRXR_API void vrxr_frame_end(void);			/* ALWAYS, after every vrxr_frame_begin */

/* Where the frames' time went since the last call (which resets it). */
typedef struct vrxr_frame_stats {
	int frames, late, gpu_frames;
	double period_ms;			/* the headset's frame time (11.1 = 90 Hz) */
	double cpu_ms_sum, cpu_ms_max;		/* xrWaitFrame returned -> xrEndFrame */
	double gpu_ms_sum, gpu_ms_max;		/* GPU between those two points */
	double outside_ms_sum, outside_ms_max;	/* xrEndFrame -> next xrWaitFrame */
	double wait_ms_sum;			/* inside xrWaitFrame */
} vrxr_frame_stats;
VRXR_API void vrxr_take_frame_stats(vrxr_frame_stats *out);

/* The head this frame (between the eyes), tracking space. */
VRXR_API int vrxr_head(float orient[4], float pos[3]);
/* Take the head's current facing and position as the origin (yaw only). */
VRXR_API void vrxr_recenter(void);

/* This frame's controllers by PHYSICAL hand, with edges; see vr_handmap.h
 * for what the game makes of them.  0 = no controller input this frame. */
VRXR_API int vrxr_read_physical(vr_phys_input *out);
VRXR_API void vrxr_haptic(int physical_hand, float amplitude, float seconds);	/* 0 left, 1 right */

/* A controller's aim pose (its pointing ray: -Z ahead) this frame, tracking
 * space, by physical hand.  0 = not tracked. */
VRXR_API int vrxr_hand_aim(int physical_hand, float orient[4], float pos[3]);

/* The controllers in the player's hands, known once the runtime says. */
#define VRXR_FAMILY_UNKNOWN	0
#define VRXR_FAMILY_TOUCH	1	/* Meta Quest Touch */
#define VRXR_FAMILY_INDEX	2	/* Valve Index */
#define VRXR_FAMILY_OTHER	3
#define VRXR_FAMILY_FRAME	4	/* Valve Steam Frame (its own profile) */
VRXR_API int vrxr_controller_family(void);

/* The folder this executable is in; start a program detached (Switch Game). */
VRXR_API int vrxr_exe_dir(char *buf, int size);
VRXR_API int vrxr_start_program(const char *path);

/* The desktop window: show this eye in it (0 or 1), the panel
 * (VRXR_MIRROR_PANEL) or nothing (-1), fitted with bars. */
#define VRXR_MIRROR_PANEL 2
VRXR_API void vrxr_set_mirror(int eye, int window_w, int window_h);
/* The desktop shows the steady spectator view of that eye (1) or the raw eye (0). */
VRXR_API void vrxr_set_mirror_steady(int on);
/* Stop the desktop swap waiting for the monitor: the headset paces the game.
 * Needs the GL context current. */
VRXR_API void vrxr_desktop_vsync_off(void);
/* The observer view: the desktop window fills its
 * monitor, borderless, and Alt+Enter makes it an ordinary window.  Done by
 * restyling and moving the window the GL context belongs to, never by a new
 * video mode, so the context the session holds is kept.  Needs the GL
 * context current; returns 1 when the window now fills the monitor. */
VRXR_API int vrxr_desktop_fill_monitor(int on);
VRXR_API int vrxr_desktop_fills_monitor(void);
/* Before any window exists: let this process see real pixels on a scaled
 * display, so a monitor-filling spectator view is sharp, not stretched. */
VRXR_API void vrxr_dpi_aware(void);

VRXR_API void vrxr_shutdown(void);

#ifdef __cplusplus
}
#endif

#pragma pack(pop)

#endif
