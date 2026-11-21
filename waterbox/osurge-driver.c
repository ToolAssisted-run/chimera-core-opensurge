/* osurge-driver.c - the Open Surge Engine as a machine that is stepped.
 *
 * The engine is the game's own (src/main.c's loop, split into pieces by the
 * patch: engine_init, engine_mainloop_begin, engine_step, engine_mainloop_end,
 * engine_release), on Allegro 5 with the drivers of allegro-driver/: no
 * window, no sound device, no threads, a clock the core moves by hand, a
 * keyboard the core sets.
 *
 *   - A step is one tick of the engine's own 60 Hz timer: the core sets the
 *     keys that are held (they become key events when they change), moves
 *     Allegro's clock by 1/60 of a second (which makes the timer tick), and
 *     lets the engine handle what is waiting and draw the picture. The
 *     machine's clock is the number of steps: the start time setting plus
 *     the steps, at 60 a second, which seams.c answers the C library's clock
 *     functions from.
 *   - The controls are the keys of the engine's default input map (the
 *     arrows, Space, Left Ctrl, Enter, Escape, the Shift keys); the pad of
 *     a project is those keys.
 *   - The picture is what the engine draws into its window, through Mesa's
 *     softpipe (OSMesa) compiled into the core, read back after the step.
 *   - The sound is the engine's own mixer, run for exactly one sixtieth of a
 *     second (735 frames of 44100 Hz) each step, after the music streams were
 *     given their next pieces to play.
 */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <allegro5/allegro.h>

#include "core/commandline.h"
#include "core/engine.h"
#include "osurge-driver.h"

/* allegro-driver/osmesa_driver.c and osmesa_audio.c */
void al_osmesa_set_key(int keycode, bool down);
void al_osmesa_advance_time(double dt);
const unsigned char *al_osmesa_pixels(ALLEGRO_DISPLAY *d);
unsigned al_osmesa_render_audio(short *out, unsigned frames);

#define TEXT_ARG "\x1f"

struct driver
{
	struct osurge_settings settings;
	commandline_t cmd;
	int init_done;
	int dead;               /* the engine said it cannot go on, or left */
	char fatal[1024];

	uint8_t held[OSG_BTN_COUNT];
	uint64_t frames;

	int16_t audio[OSG_AUDIO_FRAMES * 2];
	uint32_t video[OSG_VIDEO_MAX_WIDTH * OSG_VIDEO_MAX_HEIGHT];
	int video_w, video_h;

	char argv_buf[2048];
	char *argv[32];
	int argc;
};

static struct driver g;

/* seams.c: when set, the engine's draw calls do nothing (the picture is not looked at) */
extern int osurge_skip_drawing;
static jmp_buf g_jmp;
static int g_jmp_set;

static const int k_keys[OSG_BTN_COUNT] = {
	ALLEGRO_KEY_UP, ALLEGRO_KEY_DOWN, ALLEGRO_KEY_LEFT, ALLEGRO_KEY_RIGHT,
	ALLEGRO_KEY_SPACE, ALLEGRO_KEY_LCTRL, ALLEGRO_KEY_ENTER, ALLEGRO_KEY_ESCAPE,
	ALLEGRO_KEY_LSHIFT, ALLEGRO_KEY_RSHIFT,
};

/* ---------------------------------------------------------------- time */

long long osurgedrv_time_seconds(void) { return g.settings.start_time + (long long)(g.frames / 60); }
long long osurgedrv_time_micros(void) { return g.settings.start_time * 1000000ll + (long long)(g.frames * 1000000ull / 60); }

/* ----------------------------------------------------------- the end */

/* the engine's fatal_error ends with exit(): the core takes over there, and
 * the machine is over with the reason the engine gave */
void osurgedrv_fatal_message(const char *message)
{
	snprintf(g.fatal, sizeof g.fatal, "%s", message);
}

void __wrap_exit(int status)
{
	g.dead = 1;
	if (!g.fatal[0]) snprintf(g.fatal, sizeof g.fatal, "the engine ended with status %d", status);
	if (g_jmp_set)
	{
		g_jmp_set = 0;
		longjmp(g_jmp, 1);
	}
	for (;;) abort();
}

/* ---------------------------------------------------------------- init */

static void build_argv(void)
{
	size_t used = 0;
	g.argc = 0;
	const char *a[32];
	int n = 0;
	char res[8], quality[8];
	snprintf(res, sizeof res, "%d", g.settings.resolution);
	snprintf(quality, sizeof quality, "%s", g.settings.quality == 0 ? "low" : g.settings.quality == 2 ? "high" : "medium");
	a[n++] = "opensurge";
	a[n++] = "--windowed";
	a[n++] = "--resolution"; a[n++] = res;
	a[n++] = "--quality"; a[n++] = quality;
	if (g.settings.verbose) a[n++] = "--verbose";
	if (osfiles_has_game()) { a[n++] = "--game-folder"; a[n++] = "/os/game"; }
	if (g.settings.language[0]) { a[n++] = "--language"; a[n++] = g.settings.language; }
	if (g.settings.start_level[0]) { a[n++] = "--level"; a[n++] = g.settings.start_level; }
	else if (g.settings.quest[0]) { a[n++] = "--quest"; a[n++] = g.settings.quest; }
	for (int i = 0; i < n && g.argc < 31; i++)
	{
		size_t l = strlen(a[i]) + 1;
		if (used + l > sizeof g.argv_buf) break;
		memcpy(g.argv_buf + used, a[i], l);
		g.argv[g.argc++] = g.argv_buf + used;
		used += l;
	}
	g.argv[g.argc] = NULL;
}

int osurgedrv_init(char *err, int errsize)
{
	memset(&g, 0, sizeof g);
	osurge_settings_read(&g.settings);
	if (g.settings.resolution < 1 || g.settings.resolution > 4)
	{
		snprintf(err, (size_t)errsize, "error.setting_resolution_is_it" TEXT_ARG "%d" TEXT_ARG "4", g.settings.resolution);
		return 0;
	}
	if (!osfiles_load(&g.settings, err, errsize)) return 0;
	build_argv();

	if (setjmp(g_jmp) != 0)
	{
		snprintf(err, (size_t)errsize, "error.engine_did_not_start" TEXT_ARG "%s", g.fatal);
		return 0;
	}
	g_jmp_set = 1;
	g.cmd = commandline_parse(g.argc, g.argv);
	engine_init(&g.cmd);
	engine_mainloop_begin();
	g_jmp_set = 0;
	g.init_done = 1;
	osurgedrv_state_publish();
	return 1;
}

/* ---------------------------------------------------------------- steps */

void osurgedrv_set_button(int index, int down)
{
	if (index >= 0 && index < OSG_BTN_COUNT) g.held[index] = down ? 1 : 0;
}

static void read_picture(void)
{
	ALLEGRO_DISPLAY *d = al_get_current_display();
	const unsigned char *px = d ? al_osmesa_pixels(d) : NULL;
	if (!px) return;
	int w = al_get_display_width(d), h = al_get_display_height(d);
	if (w > OSG_VIDEO_MAX_WIDTH) w = OSG_VIDEO_MAX_WIDTH;
	if (h > OSG_VIDEO_MAX_HEIGHT) h = OSG_VIDEO_MAX_HEIGHT;
	int full = al_get_display_width(d);
	/* OSMesa's buffer is RGBA with its bottom row first */
	for (int y = 0; y < h; y++)
	{
		const unsigned char *s = px + (size_t)(al_get_display_height(d) - 1 - y) * full * 4;
		uint32_t *o = g.video + (size_t)y * w;
		for (int x = 0; x < w; x++, s += 4)
			o[x] = 0xFF000000u | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | (uint32_t)s[2];
	}
	g.video_w = w;
	g.video_h = h;
}

void osurgedrv_frame(int render)
{
	osurgedrv_state_apply();
	if (!g.dead)
	{
		if (setjmp(g_jmp) == 0)
		{
			g_jmp_set = 1;
			for (int i = 0; i < OSG_BTN_COUNT; i++) al_osmesa_set_key(k_keys[i], g.held[i] != 0);
			osurge_skip_drawing = !render;
			al_osmesa_advance_time(1.0 / 60.0);
			if (!engine_step())
			{
				/* the game's loop is over: it quits, or it starts again */
				engine_mainloop_end();
				engine_release();
				if (engine_must_restart(&g.cmd))
				{
					engine_init(&g.cmd);
					engine_mainloop_begin();
				}
				else
					g.dead = 1;
			}
			g_jmp_set = 0;
		}
	}
	osurge_skip_drawing = 0;
	g.frames++;
	if (g.dead) memset(g.audio, 0, sizeof g.audio);
	else al_osmesa_render_audio(g.audio, OSG_AUDIO_FRAMES);
	if (render && !g.dead) read_picture();
	osurgedrv_state_publish();
}

const uint32_t *osurgedrv_video(int *w, int *h)
{
	*w = g.video_w ? g.video_w : 426;
	*h = g.video_h ? g.video_h : 240;
	return g.video;
}

const int16_t *osurgedrv_audio(int *samples)
{
	*samples = OSG_AUDIO_FRAMES;
	return g.audio;
}

uint64_t osurgedrv_frames(void) { return g.frames; }
