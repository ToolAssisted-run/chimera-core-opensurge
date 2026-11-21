/* Allegro 5 audio driver with no sound device and no thread.
 *
 * There is one voice (the game makes one: 44100 Hz, 16 bits, stereo). The
 * core asks for sound a step at a time (al_osmesa_render_audio): the driver
 * first lets the music streams read their next pieces from their files
 * (_al_kcm_pump_streams, which is what Allegro's feeder threads do), and then
 * has the voice's mixer produce exactly the number of frames asked for.
 *
 * Added to Allegro 5.2.9.1 (patches/allegro5) as addons/audio/osmesa_audio.c.
 * The model is addons/audio/sdl_audio.c. */
#define ALLEGRO_INTERNAL_UNSTABLE
#include "allegro5/allegro.h"
#include "allegro5/internal/aintern_audio.h"
#include <string.h>

ALLEGRO_DEBUG_CHANNEL("osmesa_audio")

void _al_kcm_pump_streams(void);

static ALLEGRO_VOICE *the_voice;
static bool playing;

static int osm_open(void) { return 0; }
static void osm_close(void) {}

static int osm_allocate_voice(ALLEGRO_VOICE *voice)
{
   if (the_voice) return 1;     /* one voice only */
   the_voice = voice;
   voice->extra = NULL;
   return 0;
}
static void osm_deallocate_voice(ALLEGRO_VOICE *voice)
{
   if (the_voice == voice) { the_voice = NULL; playing = false; }
}
static int osm_load_voice(ALLEGRO_VOICE *voice, const void *data)
{ (void)data; voice->attached_stream->pos = 0; return 0; }
static void osm_unload_voice(ALLEGRO_VOICE *voice) { (void)voice; }
static int osm_start_voice(ALLEGRO_VOICE *voice) { (void)voice; playing = true; return 0; }
static int osm_stop_voice(ALLEGRO_VOICE *voice) { (void)voice; playing = false; return 0; }
static bool osm_voice_is_playing(const ALLEGRO_VOICE *voice) { (void)voice; return playing; }
static unsigned int osm_get_voice_position(const ALLEGRO_VOICE *voice)
{ return voice->attached_stream->pos; }
static int osm_set_voice_position(ALLEGRO_VOICE *voice, unsigned int pos)
{ voice->attached_stream->pos = pos; return 0; }
static int osm_allocate_recorder(ALLEGRO_AUDIO_RECORDER *r) { (void)r; return 1; }
static void osm_deallocate_recorder(ALLEGRO_AUDIO_RECORDER *r) { (void)r; }

ALLEGRO_AUDIO_DRIVER _al_kcm_osmesa_driver = {
   "OSMesa",
   osm_open, osm_close,
   osm_allocate_voice, osm_deallocate_voice,
   osm_load_voice, osm_unload_voice,
   osm_start_voice, osm_stop_voice, osm_voice_is_playing,
   osm_get_voice_position, osm_set_voice_position,
   osm_allocate_recorder, osm_deallocate_recorder,
   NULL,
};

/* Fills out with `frames` stereo frames of 16 bits (left, right, left...):
 * silence where there is no voice or the voice is stopped. Returns the
 * number of frames written, always `frames`. */
unsigned al_osmesa_render_audio(short *out, unsigned frames)
{
   unsigned done = 0;
   _al_kcm_pump_streams();
   if (the_voice && playing && the_voice->attached_stream &&
       the_voice->depth == ALLEGRO_AUDIO_DEPTH_INT16 &&
       al_get_channel_count(the_voice->chan_conf) == 2) {
      while (done < frames) {
         unsigned n = frames - done;
         const void *data = _al_voice_update(the_voice, the_voice->mutex, &n);
         if (!data || n == 0) break;
         memcpy(out + (size_t)done * 2, data, (size_t)n * 4);
         done += n;
      }
   }
   if (done < frames) memset(out + (size_t)done * 2, 0, (size_t)(frames - done) * 4);
   _al_kcm_pump_streams();
   return frames;
}
