/* Part of Descent 1 & 2 VR, a derivative of DXX-Redux.
 * Distributed under the Parallax and D1X-Rebirth licenses in COPYING.txt:
 * non-commercial use only, and the source of any modified version must be
 * freely and publicly available. */

/* vr_winmain.c - the Windows entry point.
 *
 * The VR build is a Windows program, not a console one, so no console window
 * opens with the game or stays after it.  This does what SDL 1.2's console
 * entry does (hand SDL the module handle, then run the game's main) without
 * SDLmain's redirect of the output to files.  No engine
 * headers: they pack structures differently from windows.h. */

#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

extern int SDL_main(int argc, char *argv[]);		/* the game's main, renamed by SDL.h */
extern void __cdecl SDL_SetModuleHandle(void *hInst);	/* SDL_main.h */

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
	(void)prev; (void)cmdline; (void)show;
	/* A Windows program has no console behind stdout and stderr, and the C
	 * runtime takes a write to them as an invalid parameter and ends the
	 * process (0xC0000409 at the first printf).  Point them at NUL (the
	 * game's own gamelog.txt keeps the same text), and the engine's reopen
	 * of them on CON is left out of the VR build (inferno.c). */
	freopen("NUL", "w", stdout);
	freopen("NUL", "w", stderr);
	SDL_SetModuleHandle(inst);
	return SDL_main(__argc, __argv);
}
#endif
