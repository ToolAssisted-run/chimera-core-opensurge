/* wbx-entry.c - the chimera guest ABI over osurge-driver.
 *
 * Compiles identically for the guest (miniBox emulibc) and for the native
 * reference (native-shim/emulibc.h), which is what makes the equivalence gate
 * a real proof: the same driver, the same exports, one in the sandbox and one
 * out of it.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emulibc.h>

#include "osurge-driver.h"

/* the ready-made windows (Export Save Data and the like): miniBox's kit answers
 * Chimera's requests for them (add-windows.py puts them in the package) */
#define WBX_WINDOWS_IMPLEMENTATION
#include <waterbox_windows.h>

static char g_load_error[1024];

/* buttons set one at a time (SetButton) and the packed mask of FrameAdvance:
 * a step sees their union */
static uint8_t g_set_buttons[OSG_BTN_COUNT];

/* Turbo: the picture is not read back. Only that - the engine draws its
 * screen whatever happens, because what it draws is part of the game. */
ECL_INVISIBLE int chimera_render_enabled = 1;

ECL_EXPORT const char *GetLoadError(void) { return g_load_error; }

ECL_EXPORT int Init(void)
{
	g_load_error[0] = '\0';
	return osurgedrv_init(g_load_error, (int)sizeof g_load_error);
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < OSG_BTN_COUNT) g_set_buttons[index] = state ? 1 : 0;
}

ECL_EXPORT int IsButtonActive(int32_t index) { return index >= 0 && index < OSG_BTN_COUNT; }

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	for (int i = 0; i < OSG_BTN_COUNT; i++)
		osurgedrv_set_button(i, g_set_buttons[i] | (int)((packed >> i) & 1));
	osurgedrv_frame(chimera_render_enabled);
}

/* Game > Export Save Data: the files the game wrote, those that hold
 * anything; the Save data slot takes them back */
ECL_EXPORT int32_t GetSaveDataFileCount(void) { return osfiles_saves_count(); }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) { return osfiles_saves_name(i); }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { return osfiles_saves_size(i); }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i) { return osfiles_saves_data(i); }

ECL_EXPORT void SetRenderingEnabled(int on) { chimera_render_enabled = on != 0; }

/* the live size: the engine's window (waterbox.config gives the capacity) */
ECL_EXPORT uint32_t *GetVideoBgra(void)
{
	int w, h;
	return (uint32_t *)osurgedrv_video(&w, &h);
}
ECL_EXPORT int GetVideoWidth(void)
{
	int w, h;
	osurgedrv_video(&w, &h);
	return w;
}
ECL_EXPORT int GetVideoHeight(void)
{
	int w, h;
	osurgedrv_video(&w, &h);
	return h;
}
/* square pixels: the picture's own shape */
ECL_EXPORT int GetDisplayAspectX(void)
{
	int w, h;
	osurgedrv_video(&w, &h);
	return w;
}
ECL_EXPORT int GetDisplayAspectY(void)
{
	int w, h;
	osurgedrv_video(&w, &h);
	return h;
}

ECL_EXPORT int16_t *GetAudio(void)
{
	int n;
	return (int16_t *)osurgedrv_audio(&n);
}
ECL_EXPORT int GetAudioSampleCount(void)
{
	int n;
	osurgedrv_audio(&n);
	return n;
}

ECL_EXPORT int GetVsyncNumerator(void) { return OSG_STEP_NUM; }
ECL_EXPORT int GetVsyncDenominator(void) { return OSG_STEP_DEN; }

/* memory domains: Game State (the property block) */
ECL_EXPORT int GetMemoryDomainCount(void) { return osurgedrv_domain_count(); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return osurgedrv_domain_name(i); }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return osurgedrv_domain_ptr(i); }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return osurgedrv_domain_size(i); }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return osurgedrv_domain_writable(i); }

/* chimera docs/game-cores.md: the property table */
ECL_EXPORT const char *GetGameProperties(void) { return osurgedrv_game_properties(); }

/* the machine's own clock, in steps, for harnesses that compare machines */
ECL_EXPORT uint64_t GetCycleCount(void) { return osurgedrv_frames(); }
ECL_EXPORT int InputWasRead(void) { return 1; } /* every step of this engine reads the keyboard */
