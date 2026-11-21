/* seams.c - what the engine reaches through the C library, answered by the
 * core.
 *
 * Linked with -Wl,--wrap=<name> for each __wrap_ below, in both builds
 * (sources.mk). The files are osfiles.c's; the clock and the random numbers
 * are here:
 *
 *   - time: the engine and SurgeScript ask the clock for the date, for
 *     their random seeds and for how long a script ran. The machine's clock
 *     is the project's start time setting plus the steps taken, at 60 a
 *     second (osurge-driver.c); gettimeofday, clock_gettime and time all
 *     answer it, and localtime reads it as UTC, in both builds, whatever the
 *     host's time zone.
 *   - random numbers: rand and random are one sequence of our own, the same
 *     in both builds (glibc's and musl's differ), seeded by what the engine
 *     seeds them with.
 *   - the native dialogs: there is no window, so no dialog; the engine asks
 *     and is told there is none.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <GL/gl.h>

#include "osurge-driver.h"

/* ------------------------------------------------------------- drawing */

/* Not drawn: when nobody will look at the picture (the frontend's fast
 * forward, a seek, a movie played without a window), the engine still runs
 * its whole step, its own drawing included, but the draw calls of Allegro's
 * OpenGL driver do nothing. Software OpenGL is the slowest thing in this
 * machine, and what it draws is read by nothing in the game (the tests
 * check that a run with a stretch not drawn is the same machine). */
int osurge_skip_drawing;

void __real_glDrawArrays(GLenum mode, GLint first, GLsizei count);
void __real_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void __wrap_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	if (!osurge_skip_drawing) __real_glDrawArrays(mode, first, count);
}
void __wrap_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
	if (!osurge_skip_drawing) __real_glDrawElements(mode, count, type, indices);
}

/* ----------------------------------------------------------------- time */

time_t __wrap_time(time_t *t)
{
	const time_t v = (time_t)osurgedrv_time_seconds();
	if (t) *t = v;
	return v;
}

int __wrap_gettimeofday(struct timeval *tv, void *tz)
{
	const long long us = osurgedrv_time_micros();
	(void)tz;
	if (tv)
	{
		tv->tv_sec = (time_t)(us / 1000000);
		tv->tv_usec = (suseconds_t)(us % 1000000);
	}
	return 0;
}

int __wrap_clock_gettime(clockid_t clock, struct timespec *ts)
{
	const long long us = osurgedrv_time_micros();
	(void)clock;
	if (ts)
	{
		ts->tv_sec = (time_t)(us / 1000000);
		ts->tv_nsec = (long)(us % 1000000) * 1000;
	}
	return 0;
}

struct tm *__wrap_localtime(const time_t *t) { return gmtime(t); }
struct tm *__wrap_localtime_r(const time_t *t, struct tm *out) { return gmtime_r(t, out); }

/* --------------------------------------------------------------- random */

/* a 64-bit linear congruential generator, its high bits: musl's rand(),
 * written out so that both builds have it */
static uint64_t g_rand = 1;

void __wrap_srand(unsigned seed) { g_rand = seed - 1ull; }
void __wrap_srandom(unsigned seed) { g_rand = seed - 1ull; }
int __wrap_rand(void)
{
	g_rand = 6364136223846793005ull * g_rand + 1;
	return (int)(g_rand >> 33);
}
long __wrap_random(void)
{
	g_rand = 6364136223846793005ull * g_rand + 1;
	return (long)(g_rand >> 33);
}

/* ----------------------------------------------------------------- sort */

/* qsort: glibc's and musl's put equal elements in a different order (one is
 * a merge sort, the other not stable at all), and a sort of things that tie
 * - the engine's draw order, SurgeScript's tables - would then differ
 * between the two builds. This one is stable, and the same in both. */
static void merge_sort(char *a, char *tmp, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	if (n < 2) return;
	size_t h = n / 2;
	merge_sort(a, tmp, h, size, cmp);
	merge_sort(a + h * size, tmp, n - h, size, cmp);
	size_t i = 0, j = h, k = 0;
	while (i < h && j < n)
	{
		if (cmp(a + j * size, a + i * size) < 0) { memcpy(tmp + k * size, a + j * size, size); j++; }
		else { memcpy(tmp + k * size, a + i * size, size); i++; }
		k++;
	}
	while (i < h) { memcpy(tmp + k * size, a + i * size, size); i++; k++; }
	while (j < n) { memcpy(tmp + k * size, a + j * size, size); j++; k++; }
	memcpy(a, tmp, n * size);
}

void __wrap_qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	if (n < 2 || size == 0) return;
	char *tmp = (char *)malloc(n * size);
	if (!tmp) abort();
	merge_sort((char *)base, tmp, n, size, cmp);
	free(tmp);
}

/* ------------------------------------------------------- native dialogs */

/* declared as the add-on's header does, without including it (the add-on
 * is not built) */
struct ALLEGRO_DISPLAY;
struct ALLEGRO_FILECHOOSER;
struct ALLEGRO_TEXTLOG;

int al_init_native_dialog_addon(void) { return 1; }
int al_is_native_dialog_addon_initialized(void) { return 1; }
void al_shutdown_native_dialog_addon(void) {}
struct ALLEGRO_FILECHOOSER *al_create_native_file_dialog(const char *initial_path, const char *title, const char *patterns, int mode)
{
	(void)initial_path; (void)title; (void)patterns; (void)mode;
	return NULL;
}
int al_show_native_file_dialog(struct ALLEGRO_DISPLAY *display, struct ALLEGRO_FILECHOOSER *dialog)
{
	(void)display; (void)dialog;
	return 0;
}
void al_destroy_native_file_dialog(struct ALLEGRO_FILECHOOSER *dialog) { (void)dialog; }
int al_show_native_message_box(struct ALLEGRO_DISPLAY *display, const char *title, const char *heading, const char *text,
                               const char *buttons, int flags)
{
	(void)display; (void)title; (void)heading; (void)text; (void)buttons; (void)flags;
	return 0;
}
size_t al_get_native_file_dialog_count(const struct ALLEGRO_FILECHOOSER *dialog) { (void)dialog; return 0; }
const char *al_get_native_file_dialog_path(const struct ALLEGRO_FILECHOOSER *dialog, size_t i) { (void)dialog; (void)i; return NULL; }
struct ALLEGRO_TEXTLOG *al_open_native_text_log(const char *title, int flags) { (void)title; (void)flags; return NULL; }
void al_close_native_text_log(struct ALLEGRO_TEXTLOG *textlog) { (void)textlog; }
void al_append_native_text_log(struct ALLEGRO_TEXTLOG *textlog, const char *format, ...) { (void)textlog; (void)format; }
