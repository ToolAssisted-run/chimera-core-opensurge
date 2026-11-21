/* osurge-driver.h - the Open Surge Engine (Surge the Rabbit's game engine,
 * made with Allegro and SurgeScript) as a machine that is stepped.
 *
 * The same driver is compiled for the miniBox guest and for the native
 * reference (run-native); wbx-entry.c puts the guest ABI on top of it.
 *
 * Wire format (waterbox.config "input.buttons", same order): the game's
 * controls as its default keyboard map has them - the four directions, the
 * two action buttons, Start, Back and the two shoulder buttons. A step reads
 * them as the keys the engine's default map names (osurge-driver.c).
 */
#ifndef OSURGE_DRIVER_H
#define OSURGE_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum OsurgeButton
{
	OSG_BTN_UP,
	OSG_BTN_DOWN,
	OSG_BTN_LEFT,
	OSG_BTN_RIGHT,
	OSG_BTN_FIRE1,   /* jump, confirm */
	OSG_BTN_FIRE2,   /* cancel */
	OSG_BTN_START,   /* fire3: start, pause */
	OSG_BTN_BACK,    /* fire4: back, quit */
	OSG_BTN_LEFTSHOULDER,
	OSG_BTN_RIGHTSHOULDER,
	OSG_BTN_COUNT
};

/* the picture: the engine's window at its largest resolution (4x of 426x240) */
#define OSG_VIDEO_MAX_WIDTH 1704
#define OSG_VIDEO_MAX_HEIGHT 960

/* the engine's step is 1/60 of a second; sound is 44100 Hz stereo, 16 bits:
 * 735 frames of it a step, exactly */
#define OSG_STEP_NUM 60
#define OSG_STEP_DEN 1
#define OSG_AUDIO_RATE 44100
#define OSG_AUDIO_FRAMES 735

/* the project's settings, read by osurge-driver.c and osfiles.c */
struct osurge_settings
{
	long long start_time;  /* what time() answers at the first step: seconds since 1970 (UTC) */
	int resolution;        /* 1 to 4: the picture is 426x240 times this */
	char language[32];     /* a language file of the game ("english"), "" for the game's own choice */
	char start_level[128]; /* a level to start in ("levels/x.lev"), "" for the game's menu */
	char quest[128];       /* a quest to start in, "" for none */
	int quality;           /* 0 low, 1 medium, 2 high: the engine's video quality */
	int verbose;           /* the engine's log goes to standard error (for finding out what it does) */
};
void osurge_settings_read(struct osurge_settings *s);

/* 0 on failure, with the reason in err */
int osurgedrv_init(char *err, int errsize);
void osurgedrv_set_button(int index, int down);
/* runs the engine through its next step */
void osurgedrv_frame(int render);
const uint32_t *osurgedrv_video(int *w, int *h);
const int16_t *osurgedrv_audio(int *samples);   /* stereo frames, left then right */
uint64_t osurgedrv_frames(void);

/* the seams (seams.c) ask these */
long long osurgedrv_time_seconds(void);   /* time(): the start time plus the machine's own seconds */
long long osurgedrv_time_micros(void);    /* gettimeofday() and clock_gettime(): the same, in microseconds */

/* game-state: the property block, the table */
int osurgedrv_domain_count(void);
const char *osurgedrv_domain_name(int i);
uint8_t *osurgedrv_domain_ptr(int i);
int64_t osurgedrv_domain_size(int i);
int osurgedrv_domain_writable(int i);
const char *osurgedrv_game_properties(void);
void osurgedrv_state_publish(void);   /* the engine's variables into the block, after a step */
void osurgedrv_state_apply(void);     /* the block into the engine, before a step */

/* osfiles.c: the project's files, as the game sees its disk */
/* 0 on failure, with the reason in err */
int osfiles_load(const struct osurge_settings *s, char *err, int errsize);
/* 1 when the project brought a game of its own (the engine then reads /os/game) */
int osfiles_has_game(void);
/* the files the game wrote, those that hold anything (Export Save Data) */
int osfiles_saves_count(void);
const char *osfiles_saves_name(int index);
int64_t osfiles_saves_size(int index);
const uint8_t *osfiles_saves_data(int index);

#ifdef __cplusplus
}
#endif

#endif
