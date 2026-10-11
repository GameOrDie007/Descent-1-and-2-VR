/*
 * Descent 1 & 2 VR, Android: the game's own music.
 *
 * On PC the HMP songs go to Windows' MIDI synth.  Android has none, so here
 * the engine's hmp2mid turns a song into a standard MIDI file, TinySoundFont
 * (MIT, Bernhard Schelling) plays it with a General MIDI soundfont, and the
 * effects mixer in digi_audio.c adds the result to its output.
 *
 * The soundfont is descent.sf2: from the app's data folder if the player put
 * one there, otherwise the GeneralUser GS bank packed in the APK.
 *
 * Every entry point except androidmusic_mix runs on the game thread and
 * takes the audio lock; androidmusic_mix runs inside the audio callback,
 * which already holds it.
 *
 * Written against tsf.h/tml.h and the engine's hmp.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <android/log.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"
#define TML_IMPLEMENTATION
#include "tml.h"

#include "hmp.h"
#include "u_mem.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "DescentVR", __VA_ARGS__)

static tsf *s_tsf;
static int s_tried, s_rate = 44100;
static tml_message *s_song, *s_msg;
static double s_ms;
static int s_loop, s_paused, s_playing;
static float s_gain = 1.0f;

void androidmusic_set_rate(int rate)
{
	s_rate = rate;
	if (s_tsf)
		tsf_set_output(s_tsf, TSF_STEREO_INTERLEAVED, s_rate, 0.0f);
}

static void *read_all(const char *path, int *len)
{
	SDL_RWops *rw = SDL_RWFromFile(path, "rb");
	void *buf;
	int n;
	if (!rw)
		return NULL;
	n = SDL_RWseek(rw, 0, RW_SEEK_END);
	SDL_RWseek(rw, 0, RW_SEEK_SET);
	buf = n > 0 ? malloc(n) : NULL;
	if (buf && SDL_RWread(rw, buf, 1, n) != n) {
		free(buf);
		buf = NULL;
	}
	SDL_RWclose(rw);
	*len = n;
	return buf;
}

static int load_font(void)
{
	char path[600];
	const char *home = getenv("HOME");
	void *buf = NULL;
	int len = 0;
	if (s_tsf || s_tried)
		return s_tsf != NULL;
	s_tried = 1;
	if (home && *home) {
		snprintf(path, sizeof(path), "%s/descent.sf2", home);
		buf = read_all(path, &len);
	}
	if (buf)
		LOG("Music: soundfont %s (%d bytes)", path, len);
	else if ((buf = read_all("descent.sf2", &len)) != NULL)	/* the APK's assets */
		LOG("Music: soundfont from the app (%d bytes)", len);
	if (!buf) {
		LOG("Music: no soundfont; no music");
		return 0;
	}
	s_tsf = tsf_load_memory(buf, len);
	free(buf);
	if (!s_tsf) {
		LOG("Music: the soundfont would not load");
		return 0;
	}
	tsf_set_output(s_tsf, TSF_STEREO_INTERLEAVED, s_rate, 0.0f);
	tsf_set_max_voices(s_tsf, 128);	/* allocated now, never in the callback */
	return 1;
}

static void reset_channels(void)
{
	int c;
	tsf_reset(s_tsf);
	for (c = 0; c < 16; c++)
		tsf_channel_set_presetnumber(s_tsf, c, 0, c == 9);
}

void androidmusic_stop(void)
{
	SDL_LockAudio();
	s_playing = 0;
	s_msg = NULL;
	if (s_tsf)
		tsf_reset(s_tsf);
	if (s_song)
		tml_free(s_song);
	s_song = NULL;
	SDL_UnlockAudio();
}

int androidmusic_play(char *hmp_name, int loop)
{
	unsigned char *mid = NULL;
	unsigned int mid_len = 0;
	tml_message *song;

	androidmusic_stop();
	if (!load_font())
		return 0;
	hmp2mid(hmp_name, &mid, &mid_len);
	if (!mid || !mid_len) {
		LOG("Music: could not read %s", hmp_name);
		if (mid)
			d_free(mid);
		return 0;
	}
	song = tml_load_memory(mid, (int)mid_len);
	d_free(mid);
	if (!song) {
		LOG("Music: %s did not convert", hmp_name);
		return 0;
	}
	SDL_LockAudio();
	s_song = s_msg = song;
	s_ms = 0.0;
	s_loop = loop;
	s_paused = 0;
	reset_channels();
	s_playing = 1;
	SDL_UnlockAudio();
	LOG("Music: playing %s%s", hmp_name, loop ? " (looped)" : "");
	return 1;
}

void androidmusic_pause(void)
{
	SDL_LockAudio();
	s_paused = 1;
	if (s_tsf)
		tsf_note_off_all(s_tsf);
	SDL_UnlockAudio();
}

void androidmusic_resume(void)
{
	SDL_LockAudio();
	s_paused = 0;
	SDL_UnlockAudio();
}

void androidmusic_pause_resume(void)
{
	if (s_paused)
		androidmusic_resume();
	else
		androidmusic_pause();
}

/* The game's music volume, 0 to 8. */
void androidmusic_set_volume(int volume)
{
	SDL_LockAudio();
	s_gain = volume / 8.0f * 0.5f;	/* the soundfont runs hot: half, under the effects */
	SDL_UnlockAudio();
}

static void apply(const tml_message *m)
{
	switch (m->type) {
	case TML_PROGRAM_CHANGE:
		tsf_channel_set_presetnumber(s_tsf, m->channel, m->program, m->channel == 9);
		break;
	case TML_NOTE_ON:
		tsf_channel_note_on(s_tsf, m->channel, m->key, m->velocity / 127.0f);
		break;
	case TML_NOTE_OFF:
		tsf_channel_note_off(s_tsf, m->channel, m->key);
		break;
	case TML_PITCH_BEND:
		tsf_channel_set_pitchwheel(s_tsf, m->channel, m->pitch_bend);
		break;
	case TML_CONTROL_CHANGE:
		tsf_channel_midi_control(s_tsf, m->channel, m->control, m->control_value);
		break;
	}
}

/* Adds `frames` stereo frames of music to acc (16-bit scale), inside the
 * audio callback. */
void androidmusic_mix(int *acc, int frames)
{
	static float buf[2 * 64];
	if (!s_tsf || !s_playing || s_paused || s_gain <= 0.0f)
		return;
	while (frames > 0) {
		const int n = frames > 64 ? 64 : frames;
		const float scale = 32767.0f * s_gain;
		int i;
		s_ms += n * 1000.0 / s_rate;
		while (s_msg && s_msg->time <= s_ms) {
			apply(s_msg);
			s_msg = s_msg->next;
		}
		if (!s_msg) {
			if (s_loop) {
				s_msg = s_song;
				s_ms = 0.0;
				reset_channels();
			} else
				s_playing = 0;
		}
		tsf_render_float(s_tsf, buf, n, 0);
		for (i = 0; i < 2 * n; i++)
			acc[i] += (int)(buf[i] * scale);
		acc += 2 * n;
		frames -= n;
	}
}
