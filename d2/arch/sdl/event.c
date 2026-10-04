/*
 *
 * SDL Event related stuff
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "event.h"
#include "key.h"
#include "mouse.h"
#include "window.h"
#ifdef USE_VR
#include "vr_descent.h"
#endif
#include "timer.h"
#include "config.h"
#include "args.h"

#include "joy.h"

extern void key_handler(SDL_KeyboardEvent *event);
extern void mouse_button_handler(SDL_MouseButtonEvent *mbe);
extern void mouse_motion_handler(SDL_MouseMotionEvent *mme);
extern void mouse_cursor_autohide();

static int initialised=0;

void event_poll()
{
	SDL_Event event;
	int clean_uniframe=1;
	window *wind = window_get_front();
	int idle = 1;
	
	// If the front window changes, exit this loop, otherwise unintended behavior can occur
	// like pressing 'Return' really fast at 'Difficulty Level' causing multiple games to be started
	while ((wind == window_get_front()) && SDL_PollEvent(&event))
	{
		switch(event.type) {
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				if (clean_uniframe)
					memset(unicode_frame_buffer,'\0',sizeof(unsigned char)*KEY_BUFFER_SIZE);
				clean_uniframe=0;
				key_handler((SDL_KeyboardEvent *)&event);
				idle = 0;
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				if (GameArg.CtlNoMouse)
					break;
				mouse_button_handler((SDL_MouseButtonEvent *)&event);
				idle = 0;
				break;
			case SDL_MOUSEMOTION:
				if (GameArg.CtlNoMouse)
					break;
				mouse_motion_handler((SDL_MouseMotionEvent *)&event);
				idle = 0;
				break;
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
				if (GameArg.CtlNoJoystick)
					break;
				joy_button_handler((SDL_JoyButtonEvent *)&event);
				idle = 0;
				break;
			case SDL_JOYAXISMOTION:
				if (GameArg.CtlNoJoystick)
					break;
				if (joy_axisbutton_handler((SDL_JoyAxisEvent *)&event))
					idle = 0;
				if (joy_axis_handler((SDL_JoyAxisEvent *)&event))
					idle = 0;
				break;
			case SDL_JOYHATMOTION:
				if (GameArg.CtlNoJoystick)
					break;
				joy_hat_handler((SDL_JoyHatEvent *)&event);
				idle = 0;
				break;
			case SDL_JOYBALLMOTION:
				break;
			case SDL_QUIT: {
				d_event qevent = { EVENT_QUIT };
				call_default_handler(&qevent);
				idle = 0;
			} break;
		}
	}

	// Send the idle event if there were no other events
	if (idle)
	{
		d_event ievent;
		
		ievent.type = EVENT_IDLE;
		event_send(&ievent);
	}
	else
		event_reset_idle_seconds();
	
	mouse_cursor_autohide();
}

void event_flush()
{
	SDL_Event event;
	
	while (SDL_PollEvent(&event));
}

int event_init()
{
	// We should now be active and responding to events.
	initialised = 1;

	return 0;
}

int (*default_handler)(d_event *event) = NULL;

void set_default_handler(int (*handler)(d_event *event))
{
	default_handler = handler;
}

int call_default_handler(d_event *event)
{
	if (default_handler)
		return (*default_handler)(event);
	
	return 0;
}

void event_send(d_event *event)
{
	window *wind;
	int handled = 0;

	for (wind = window_get_front(); wind != NULL && !handled; wind = window_get_prev(wind))
		if (window_is_visible(wind))
		{
			handled = window_send_event(wind, event);

			if (!window_exists(wind)) // break away if necessary: window_send_event() could have closed wind by now
				break;
			if (window_is_modal(wind))
				break;
		}
	
	if (!handled)
		call_default_handler(event);
}

// Process the first event in queue, sending to the appropriate handler
// This is the new object-oriented system
// Uses the old system for now, but this may change
#define DRAW_ALL		0
#define DRAW_GAME		1	// only the game's own window (the view, in the eyes)
#define DRAW_ABOVE_GAME	2	// only the windows over it (menus, on the panel)

static void draw_windows(int which)
{
	d_event event;
	window *wind;
	int above_game = 0;

	event.type = EVENT_WINDOW_DRAW;	// then draw all visible windows
	wind = window_get_first();
	while (wind != NULL)
	{
		window *prev = window_get_prev(wind);
		int send = 1;
#ifdef USE_VR
		if (which != DRAW_ALL)
		{
			const int is_game = vrd_is_game_window(wind);
			send = which == DRAW_GAME ? is_game : above_game;
			if (is_game)
				above_game = 1;
		}
#endif
		if (send && window_is_visible(wind))
		{
#ifdef USE_VR
			vrd_window_draw_begin(wind);	// on the panel: monitor-sized windows scale up
#endif
			window_send_event(wind, &event);
#ifdef USE_VR
			if (window_exists(wind))
				vrd_window_draw_end(wind);
#endif
		}
		if (!window_exists(wind))
		{
			if (!prev) // well there isn't a previous window ...
				break; // ... just bail out - we've done everything for this frame we can.
			wind = window_get_next(prev); // the current window seemed to be closed. so take the next one from the previous which should be able to point to the one after the current closed
		}
		else
			wind = window_get_next(wind);
	}
}

void event_process(void)
{
	window *wind = window_get_front();

	timer_update();

	event_poll();	// send input events first
#ifdef USE_VR
	vrd_input_frame();	// the VR controllers, as control states and keys
#endif

	// Doing this prevents problems when a draw event can create a newmenu,
	// such as some network menus when they report a problem
	if (window_get_front() != wind)
		return;
	
#ifdef USE_VR
	// In the headset the game's view is drawn once per eye (the game moves on
	// the first eye only, game.c) and everything flat goes on the panel: the
	// windows over the game, or else the HUD.  Every frame begun is ended.
	vrd_frame_begin();
	switch (vrd_frame_kind())
	{
		case VRD_EYES:
		{
			int eye, drawn = 0;
			for (eye = 0; eye < 2; eye++)
				if (vrd_eye_begin(eye))
				{
					draw_windows(DRAW_GAME);
					vrd_eye_end(eye);
					drawn++;
				}
			if (!drawn)
				draw_windows(DRAW_GAME);	// no eye this frame: the game still moves
			if (vrd_menu_above_game())
			{
				if (vrd_panel_begin(VRD_PANEL_MENU))
				{
					draw_windows(DRAW_ABOVE_GAME);
					vrd_panel_overlay();
					vrd_panel_end();
				}
				else
					draw_windows(DRAW_ABOVE_GAME);
			}
			else if (vrd_panel_begin(VRD_PANEL_HUD))
			{
				vrd_draw_hud();
				vrd_panel_end();
			}
			break;
		}
		case VRD_PANEL:
			if (vrd_panel_begin(VRD_PANEL_MENU))
			{
				draw_windows(DRAW_ALL);
				vrd_panel_overlay();
				vrd_panel_end();
			}
			else
				draw_windows(DRAW_ALL);
			break;
		default:
			draw_windows(DRAW_ALL);
			break;
	}
	vrd_frame_end();
#else
	draw_windows(DRAW_ALL);
#endif

	gr_flip();
}

void event_toggle_focus(int activate_focus)
{
	if (activate_focus && GameCfg.Grabinput)
		SDL_WM_GrabInput(SDL_GRAB_ON);
	else
		SDL_WM_GrabInput(SDL_GRAB_OFF);
	mouse_toggle_cursor(!activate_focus);
}

static fix64 last_event = 0;

void event_reset_idle_seconds()
{
	last_event = timer_query();
}

fix event_get_idle_seconds()
{
	return (timer_query() - last_event)/F1_0;
}

