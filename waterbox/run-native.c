#define _GNU_SOURCE
/* run-native.c - the native reference for the equivalence gate.
 *
 * Links the SAME driver, engine, SDL and Vorbis objects the guest build uses
 * (emulibc degraded to calloc by native-shim/) and drives the exports
 * directly. The work dir holds what the sandbox would see mounted: the game's
 * data file (Data.rsdk, or the name a "slots" file gives) and the "settings"
 * JSON.
 *
 * usage: run-native <workdir> [gate-harness options]
 */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "gate-harness.h"

/* OSG_PROF=<file>: a sample of the running program's stack 200 times a
 * second, written as addresses (for addr2line) when it ends: a way to see
 * where the time goes without a profiler. */
#include <execinfo.h>
#include <link.h>
#include <signal.h>
#include <sys/time.h>
#define PROF_MAX 40000
char *__real_getenv(const char *name);
static void *g_prof[PROF_MAX][8];
static int g_prof_n;
static const char *g_prof_path;
static void prof_tick(int s) { (void)s; if (g_prof_n < PROF_MAX) backtrace(g_prof[g_prof_n++], 8); }
static int prof_base(struct dl_phdr_info *i, size_t sz, void *d) { (void)sz; *(unsigned long *)d = i->dlpi_addr; return 1; }
static void prof_dump(void)
{
	unsigned long base = 0;
	dl_iterate_phdr(prof_base, &base);
	FILE *f = gate_fopen(g_prof_path, "w");
	if (!f) return;
	for (int i = 0; i < g_prof_n; i++)
	{
		for (int k = 0; k < 8 && g_prof[i][k]; k++) fprintf(f, "%lx ", (unsigned long)g_prof[i][k] - base);
		fprintf(f, "\n");
	}
	fclose(f);
}
__attribute__((constructor)) static void prof_start(void)
{
	g_prof_path = __real_getenv("OSG_PROF");
	if (!g_prof_path) return;
	struct itimerval t = { { 0, 5000 }, { 0, 5000 } };
	signal(SIGPROF, prof_tick);
	setitimer(ITIMER_PROF, &t, NULL);
	atexit(prof_dump);
}

extern int Init(void);
extern const char *GetLoadError(void);
extern void SetButton(int32_t index, int32_t state);
extern void FrameAdvance(uint64_t packed);
extern uint32_t *GetVideoBgra(void);
extern int GetVideoWidth(void);
extern int GetVideoHeight(void);
extern int16_t *GetAudio(void);
extern int GetAudioSampleCount(void);
extern int InputWasRead(void);
extern void SetRenderingEnabled(int on);
extern int GetVsyncNumerator(void);
extern int GetVsyncDenominator(void);
extern int GetMemoryDomainCount(void);
extern const char *GetMemoryDomainName(int i);
extern uint8_t *GetMemoryDomainPtr(int i);
extern int64_t GetMemoryDomainSize(int i);
extern uint64_t GetCycleCount(void);
extern const char *GetGameProperties(void);
extern int32_t GetSaveDataFileCount(void);
extern const char *GetSaveDataFileName(int32_t i);
extern int64_t GetSaveDataFileSize(int32_t i);
extern const uint8_t *GetSaveDataFileBuffer(int32_t i);

static void frame(void) { FrameAdvance(0); }
static const uint32_t *video(int *w, int *h)
{
	*w = GetVideoWidth();
	*h = GetVideoHeight();
	return GetVideoBgra();
}
static const int16_t *audio(int *n)
{
	*n = GetAudioSampleCount();
	return GetAudio();
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: run-native <workdir> [options]\n");
		return 2;
	}
	if (chdir(argv[1]) != 0)
	{
		perror(argv[1]);
		return 1;
	}
	static struct gate_opts o;
	if (!gate_parse_opts(argc, argv, 2, &o))
		return 2;

	struct gate_core c = {
		.init = Init,
		.load_error = GetLoadError,
		.set_button = SetButton,
		.frame = frame,
		.video = video,
		.audio = audio,
		.input_was_read = InputWasRead,
		.domain_count = GetMemoryDomainCount,
		.domain_name = GetMemoryDomainName,
		.domain_ptr = GetMemoryDomainPtr,
		.domain_size = GetMemoryDomainSize,
		.savedata_count = GetSaveDataFileCount,
		.savedata_name = GetSaveDataFileName,
		.savedata_size = GetSaveDataFileSize,
		.savedata_buffer = GetSaveDataFileBuffer,
		.vsync_numerator = GetVsyncNumerator,
		.vsync_denominator = GetVsyncDenominator,
		.pre_frame = NULL,
		.set_rendering = SetRenderingEnabled,
		.clock = GetCycleCount,
		.game_properties = GetGameProperties,
	};
	return gate_run(&c, &o);
}
