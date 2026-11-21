/* Allegro 5 "system + display" driver with no window system and no threads.
 *
 * The OpenGL display draws through Mesa's OSMesa into a buffer in memory, and
 * everything else a computer gives a program is the core's to say:
 *   - time is a counter the core advances by hand (al_osmesa_advance_time),
 *     one step of the game at a time; al_rest moves it too;
 *   - the keyboard is a list of keys the core sets before each step
 *     (al_osmesa_set_key); the mouse and the joystick are there but never
 *     report anything, because the game insists on both being installed;
 *   - no thread is started here and there is no event pump.
 *
 * Added to Allegro 5.2.9.1 (patches/allegro5) as src/osmesa/osmesa_driver.c.
 * Written from the prototype of the Allegro feasibility study. */
#include <GL/gl.h>
#include "allegro5/allegro.h"
#include "allegro5/internal/aintern.h"
#include "allegro5/internal/aintern_bitmap.h"
#include "allegro5/internal/aintern_display.h"
#include "allegro5/internal/aintern_joystick.h"
#include "allegro5/internal/aintern_keyboard.h"
#include "allegro5/internal/aintern_mouse.h"
#include "allegro5/internal/aintern_opengl.h"
#include "allegro5/internal/aintern_system.h"
#include "allegro5/internal/aintern_vector.h"
#include <GL/osmesa.h>
#include <string.h>

ALLEGRO_DEBUG_CHANNEL("osmesa")

typedef struct {
   ALLEGRO_DISPLAY display;
   OSMesaContext ctx;
   unsigned char *pixels;       /* RGBA, bottom row first, w*h*4 */
} ALLEGRO_DISPLAY_OSM;

static ALLEGRO_SYSTEM_INTERFACE *sys_vt;
static ALLEGRO_DISPLAY_INTERFACE *dpy_vt;
static double virtual_time;     /* seconds; moved only by the core and al_rest */

/* The extension loader asks for glXGetProcAddress (Allegro is built without GLX). */
void *glXGetProcAddress(const unsigned char *name)
{ return (void *)OSMesaGetProcAddress((const char *)name); }
void *glXGetProcAddressARB(const unsigned char *name)
{ return (void *)OSMesaGetProcAddress((const char *)name); }

/* For the core: the colour buffer of a display, and its size. */
const unsigned char *al_osmesa_pixels(ALLEGRO_DISPLAY *d)
{ return d ? ((ALLEGRO_DISPLAY_OSM *)d)->pixels : NULL; }

/* ------------------------------------------------------------- display */

static ALLEGRO_DISPLAY *osm_create_display(int w, int h)
{
   ALLEGRO_DISPLAY_OSM *o = al_calloc(1, sizeof *o);
   ALLEGRO_DISPLAY *d = &o->display;
   ALLEGRO_SYSTEM *system = al_get_system_driver();
   ALLEGRO_DISPLAY **add;

   d->w = w; d->h = h;
   d->flags = al_get_new_display_flags() | ALLEGRO_OPENGL;
   d->refresh_rate = 60;
   d->backbuffer_format = ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE;

   o->pixels = al_calloc(1, (size_t)w * h * 4);
   o->ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
   if (!o->ctx || !OSMesaMakeCurrent(o->ctx, o->pixels, GL_UNSIGNED_BYTE, w, h)) {
      ALLEGRO_ERROR("OSMesa context failed\n");
      al_free(o->pixels); al_free(o);
      return NULL;
   }
   OSMesaPixelStore(OSMESA_Y_UP, 1);

   add = _al_vector_alloc_back(&system->displays);
   *add = d;
   _al_event_source_init(&d->es);
   d->vt = dpy_vt;
   d->extra_settings.settings[ALLEGRO_COMPATIBLE_DISPLAY] = true;

   d->ogl_extras = al_calloc(1, sizeof *d->ogl_extras);
   _al_ogl_manage_extensions(d);
   _al_ogl_set_extensions(d->ogl_extras->extension_api);
   _al_ogl_setup_gl(d);

   const int v = d->ogl_extras->ogl_info.version;
   d->extra_settings.settings[ALLEGRO_OPENGL_MAJOR_VERSION] = (v >> 24) & 0xFF;
   d->extra_settings.settings[ALLEGRO_OPENGL_MINOR_VERSION] = (v >> 16) & 0xFF;
   return d;
}

static void osm_destroy_display(ALLEGRO_DISPLAY *d)
{
   ALLEGRO_DISPLAY_OSM *o = (void *)d;
   ALLEGRO_SYSTEM *system = al_get_system_driver();
   ALLEGRO_OGL_EXTRAS *ogl = d->ogl_extras;
   while (d->bitmaps._size > 0) {
      ALLEGRO_BITMAP **b = _al_vector_ref_back(&d->bitmaps);
      _al_convert_to_memory_bitmap(*b);
   }
   _al_ogl_unmanage_extensions(d);
   if (ogl->backbuffer) { _al_ogl_destroy_backbuffer(ogl->backbuffer); ogl->backbuffer = NULL; }
   _al_vector_free(&d->bitmaps);
   al_free(d->ogl_extras);
   al_free(d->vertex_cache);
   _al_event_source_free(&d->es);
   _al_vector_find_and_delete(&system->displays, &d);
   OSMesaDestroyContext(o->ctx);
   al_free(o->pixels);
   al_free(o);
}

static bool osm_set_current_display(ALLEGRO_DISPLAY *d)
{
   ALLEGRO_DISPLAY_OSM *o = (void *)d;
   return OSMesaMakeCurrent(o->ctx, o->pixels, GL_UNSIGNED_BYTE, d->w, d->h);
}
static void osm_unset_current_display(ALLEGRO_DISPLAY *d) { (void)d; }
static void osm_flip_display(ALLEGRO_DISPLAY *d) { (void)d; glFinish(); }
static void osm_update_display_region(ALLEGRO_DISPLAY *d, int x, int y, int w, int h)
{ (void)x; (void)y; (void)w; (void)h; osm_flip_display(d); }
static bool osm_is_compatible_bitmap(ALLEGRO_DISPLAY *d, ALLEGRO_BITMAP *b)
{ (void)d; (void)b; return true; }
static bool osm_false(ALLEGRO_DISPLAY *d) { (void)d; return false; }

/* the picture's size is the program's to change: a new buffer, the context
 * pointed at it, and Allegro told (the backbuffer, the viewport) */
static bool osm_resize_display(ALLEGRO_DISPLAY *d, int w, int h)
{
   ALLEGRO_DISPLAY_OSM *o = (void *)d;
   unsigned char *np = al_calloc(1, (size_t)w * h * 4);
   if (!np) return false;
   al_free(o->pixels);
   o->pixels = np;
   d->w = w;
   d->h = h;
   if (!OSMesaMakeCurrent(o->ctx, o->pixels, GL_UNSIGNED_BYTE, w, h)) return false;
   _al_ogl_setup_gl(d);
   return true;
}
/* no pointer to show or hide: all of it says "done" */
static bool osm_cursor_show(ALLEGRO_DISPLAY *d) { (void)d; return true; }
static bool osm_cursor_set(ALLEGRO_DISPLAY *d, ALLEGRO_MOUSE_CURSOR *c) { (void)d; (void)c; return true; }
static bool osm_cursor_set_system(ALLEGRO_DISPLAY *d, ALLEGRO_SYSTEM_MOUSE_CURSOR id) { (void)d; (void)id; return true; }

ALLEGRO_DISPLAY_INTERFACE *_al_osmesa_display_driver(void)
{
   if (dpy_vt) return dpy_vt;
   dpy_vt = al_calloc(1, sizeof *dpy_vt);
   dpy_vt->id = AL_ID('O', 'S', 'M', '1');
   dpy_vt->create_display = osm_create_display;
   dpy_vt->destroy_display = osm_destroy_display;
   dpy_vt->set_current_display = osm_set_current_display;
   dpy_vt->unset_current_display = osm_unset_current_display;
   dpy_vt->flip_display = osm_flip_display;
   dpy_vt->update_display_region = osm_update_display_region;
   dpy_vt->acknowledge_resize = osm_false;
   dpy_vt->resize_display = osm_resize_display;
   dpy_vt->show_mouse_cursor = osm_cursor_show;
   dpy_vt->hide_mouse_cursor = osm_cursor_show;
   dpy_vt->set_mouse_cursor = osm_cursor_set;
   dpy_vt->set_system_mouse_cursor = osm_cursor_set_system;
   dpy_vt->create_bitmap = _al_ogl_create_bitmap;
   dpy_vt->set_target_bitmap = _al_ogl_set_target_bitmap;
   dpy_vt->get_backbuffer = _al_ogl_get_backbuffer;
   dpy_vt->is_compatible_bitmap = osm_is_compatible_bitmap;
   dpy_vt->update_render_state = _al_ogl_update_render_state;
   _al_ogl_add_drawing_functions(dpy_vt);
   return dpy_vt;
}

/* ------------------------------------------------------------ keyboard */

typedef struct { ALLEGRO_KEYBOARD kb; ALLEGRO_KEYBOARD_STATE state; } OSM_KEYBOARD;
static OSM_KEYBOARD *keyboard;

static bool osm_kbd_init(void)
{
   keyboard = al_calloc(1, sizeof *keyboard);
   _al_event_source_init(&keyboard->kb.es);
   return true;
}
static void osm_kbd_exit(void)
{
   if (keyboard) { _al_event_source_free(&keyboard->kb.es); al_free(keyboard); keyboard = NULL; }
}
static ALLEGRO_KEYBOARD *osm_kbd_get(void) { return keyboard ? &keyboard->kb : NULL; }
static bool osm_kbd_leds(int leds) { (void)leds; return false; }
static const char *osm_kbd_name(int keycode)
{ return keycode >= 0 && keycode < ALLEGRO_KEY_MAX ? _al_keyboard_common_names[keycode] : NULL; }
static void osm_kbd_state(ALLEGRO_KEYBOARD_STATE *ret)
{ if (keyboard) *ret = keyboard->state; else memset(ret, 0, sizeof *ret); }
static void osm_kbd_clear(void)
{ if (keyboard) memset(&keyboard->state, 0, sizeof keyboard->state); }

static ALLEGRO_KEYBOARD_DRIVER osm_kbd_drv = {
   .keydrv_id = AL_ID('O', 'S', 'M', 'K'),
   .keydrv_name = "OSMesa keyboard", .keydrv_desc = "OSMesa keyboard", .keydrv_ascii_name = "OSMesa keyboard",
   .init_keyboard = osm_kbd_init, .exit_keyboard = osm_kbd_exit, .get_keyboard = osm_kbd_get,
   .set_keyboard_leds = osm_kbd_leds, .keycode_to_name = osm_kbd_name,
   .get_keyboard_state = osm_kbd_state, .clear_keyboard_state = osm_kbd_clear,
};
static ALLEGRO_KEYBOARD_DRIVER *osm_kbd(void) { return &osm_kbd_drv; }

/* The core says what is held: an event on each change, as a window
 * system's keyboard would give it. */
void al_osmesa_set_key(int keycode, bool down)
{
   if (!keyboard || keycode <= 0 || keycode >= ALLEGRO_KEY_MAX) return;
   bool was = _AL_KEYBOARD_STATE_KEY_DOWN(keyboard->state, keycode);
   if (was == down) return;
   ALLEGRO_EVENT ev;
   memset(&ev, 0, sizeof ev);
   _al_event_source_lock(&keyboard->kb.es);
   if (down) _AL_KEYBOARD_STATE_SET_KEY_DOWN(keyboard->state, keycode);
   else _AL_KEYBOARD_STATE_CLEAR_KEY_DOWN(keyboard->state, keycode);
   ev.keyboard.type = down ? ALLEGRO_EVENT_KEY_DOWN : ALLEGRO_EVENT_KEY_UP;
   ev.keyboard.timestamp = virtual_time;
   ev.keyboard.display = NULL;
   ev.keyboard.keycode = keycode;
   _al_event_source_emit_event(&keyboard->kb.es, &ev);
   _al_event_source_unlock(&keyboard->kb.es);
}

/* -------------------------------------------------------------- mouse */

typedef struct { ALLEGRO_MOUSE m; } OSM_MOUSE;
static OSM_MOUSE *mouse;
static bool osm_mouse_init(void)
{ mouse = al_calloc(1, sizeof *mouse); _al_event_source_init(&mouse->m.es); return true; }
static void osm_mouse_exit(void)
{ if (mouse) { _al_event_source_free(&mouse->m.es); al_free(mouse); mouse = NULL; } }
static ALLEGRO_MOUSE *osm_mouse_get(void) { return mouse ? &mouse->m : NULL; }
static unsigned int osm_mouse_buttons(void) { return 3; }
static unsigned int osm_mouse_axes(void) { return 2; }
static bool osm_mouse_xy(ALLEGRO_DISPLAY *d, int x, int y) { (void)d; (void)x; (void)y; return false; }
static bool osm_mouse_axis(int w, int v) { (void)w; (void)v; return false; }
static void osm_mouse_state(ALLEGRO_MOUSE_STATE *s) { memset(s, 0, sizeof *s); }
static ALLEGRO_MOUSE_DRIVER osm_mouse_drv = {
   .msedrv_id = AL_ID('O', 'S', 'M', 'M'),
   .msedrv_name = "OSMesa mouse", .msedrv_desc = "OSMesa mouse", .msedrv_ascii_name = "OSMesa mouse",
   .init_mouse = osm_mouse_init, .exit_mouse = osm_mouse_exit, .get_mouse = osm_mouse_get,
   .get_mouse_num_buttons = osm_mouse_buttons, .get_mouse_num_axes = osm_mouse_axes,
   .set_mouse_xy = osm_mouse_xy, .set_mouse_axis = osm_mouse_axis, .get_mouse_state = osm_mouse_state,
};
static ALLEGRO_MOUSE_DRIVER *osm_mouse_drv_get(void) { return &osm_mouse_drv; }

/* ----------------------------------------------------------- joystick */

/* installed, with none connected */
static bool osm_joy_init(void) { return true; }
static void osm_joy_exit(void) {}
static bool osm_joy_reconf(void) { return false; }
static int osm_joy_num(void) { return 0; }
static ALLEGRO_JOYSTICK *osm_joy_get(int n) { (void)n; return NULL; }
static void osm_joy_release(ALLEGRO_JOYSTICK *j) { (void)j; }
static void osm_joy_state(ALLEGRO_JOYSTICK *j, ALLEGRO_JOYSTICK_STATE *s) { (void)j; memset(s, 0, sizeof *s); }
static const char *osm_joy_name(ALLEGRO_JOYSTICK *j) { (void)j; return ""; }
static bool osm_joy_active(ALLEGRO_JOYSTICK *j) { (void)j; return false; }
static ALLEGRO_JOYSTICK_DRIVER osm_joy_drv = {
   .joydrv_id = AL_ID('O', 'S', 'M', 'J'),
   .joydrv_name = "OSMesa joystick", .joydrv_desc = "none", .joydrv_ascii_name = "none",
   .init_joystick = osm_joy_init, .exit_joystick = osm_joy_exit, .reconfigure_joysticks = osm_joy_reconf,
   .num_joysticks = osm_joy_num, .get_joystick = osm_joy_get, .release_joystick = osm_joy_release,
   .get_joystick_state = osm_joy_state, .get_name = osm_joy_name, .get_active = osm_joy_active,
};
static ALLEGRO_JOYSTICK_DRIVER *osm_joy(void) { return &osm_joy_drv; }

/* -------------------------------------------------------------- system */

static ALLEGRO_SYSTEM *osm_initialize(int flags)
{
   ALLEGRO_SYSTEM *s = al_calloc(1, sizeof *s);
   (void)flags;
   s->vt = sys_vt;
   _al_vector_init(&s->displays, sizeof(ALLEGRO_DISPLAY *));
   return s;
}
static void osm_shutdown_system(void)
{
   ALLEGRO_SYSTEM *s = al_get_system_driver();
   while (_al_vector_size(&s->displays) > 0) {
      ALLEGRO_DISPLAY **dp = _al_vector_ref(&s->displays, 0);
      al_destroy_display(*dp);
   }
   _al_vector_free(&s->displays);
   al_free(s);
}
static ALLEGRO_DISPLAY_INTERFACE *osm_get_display_driver(void) { return _al_osmesa_display_driver(); }
static int osm_num_adapters(void) { return 1; }
static bool osm_monitor_info(int a, ALLEGRO_MONITOR_INFO *i)
{ (void)a; i->x1 = 0; i->y1 = 0; i->x2 = 1920; i->y2 = 1080; return true; }
static int osm_dpi(int a) { (void)a; return 96; }
static ALLEGRO_PATH *osm_get_path(int id)
{
   ALLEGRO_PATH *p = al_create_path_for_directory("/");
   if (id == ALLEGRO_EXENAME_PATH) al_set_path_filename(p, "app");
   return p;
}
static ALLEGRO_MOUSE_CURSOR *osm_create_cursor(ALLEGRO_BITMAP *b, int x, int y)
{ (void)b; (void)x; (void)y; return al_calloc(1, 8); }
static void osm_destroy_cursor(ALLEGRO_MOUSE_CURSOR *c) { al_free(c); }
static void osm_heartbeat(void) {}
static double osm_get_time(void) { return virtual_time; }
static void osm_rest(double s) { virtual_time += s; }

/* The program's clock. One call moves time forward and delivers the timer
 * ticks that fall in the step (the work Allegro's timer thread does). */
double _al_timer_thread_handle_tick(double interval);
void al_osmesa_advance_time(double dt)
{
   virtual_time += dt;
   _al_timer_thread_handle_tick(dt);
}
/* The clock itself, for a savestate's sake the core keeps it in its own memory. */
double al_osmesa_get_time(void) { return virtual_time; }
void al_osmesa_set_time(double t) { virtual_time = t; }
static void osm_init_timeout(ALLEGRO_TIMEOUT *t, double s)
{ double *deadline = (double *)t; *deadline = virtual_time + s; }

ALLEGRO_SYSTEM_INTERFACE *_al_system_osmesa_driver(void)
{
   if (sys_vt) return sys_vt;
   sys_vt = al_calloc(1, sizeof *sys_vt);
   sys_vt->id = AL_ID('O', 'S', 'M', 'S');
   sys_vt->initialize = osm_initialize;
   sys_vt->get_display_driver = osm_get_display_driver;
   sys_vt->get_keyboard_driver = osm_kbd;
   sys_vt->get_mouse_driver = osm_mouse_drv_get;
   sys_vt->get_joystick_driver = osm_joy;
   sys_vt->shutdown_system = osm_shutdown_system;
   sys_vt->get_num_video_adapters = osm_num_adapters;
   sys_vt->get_monitor_info = osm_monitor_info;
   sys_vt->get_monitor_dpi = osm_dpi;
   sys_vt->get_path = osm_get_path;
   sys_vt->heartbeat = osm_heartbeat;
   sys_vt->create_mouse_cursor = osm_create_cursor;
   sys_vt->destroy_mouse_cursor = osm_destroy_cursor;
   sys_vt->get_time = osm_get_time;
   sys_vt->rest = osm_rest;
   sys_vt->init_timeout = osm_init_timeout;
   return sys_vt;
}
