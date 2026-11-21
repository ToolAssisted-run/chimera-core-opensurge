/* game-state.c - the game's own variables as a memory domain, "Game State"
 * (chimera docs/game-cores.md): a packed block the core fills from the game
 * after every step and copies back into it before the next, with the table
 * that names each field. A poke is a write into the block, applied when the
 * next step begins; a freeze is the same write made before every step.
 *
 * The values are the engine's own: the score, the lives and the collectibles
 * (the rings) of the game, the clock of the level, and the active player's
 * position and speeds, as floats in pixels. A value is written back only when
 * it was changed in the block, so the engine's numbers are never converted
 * and put back.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "entities/player.h"
#include "scenes/level.h"
#include "osurge-driver.h"

struct state
{
	uint64_t steps;
	uint64_t clock;       /* the machine's date: seconds since 1970 (UTC), the start time plus the steps */
	int32_t in_level, lives, score, collectibles;
	float level_time, pad0;
	float x, y, xsp, ysp, gsp, pad1;
	char level[128];
};

static struct state g_state;
static struct state g_shadow;   /* what the block held when the game last wrote it */

static char g_json[4096];
static int g_json_len;

static void add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void add(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (g_json_len < (int)sizeof g_json - 1)
		g_json_len += vsnprintf(g_json + g_json_len, sizeof g_json - (size_t)g_json_len, fmt, ap);
	va_end(ap);
}

static void prop(const char *name, size_t offset, const char *type, int writable, const char *group, const char *description)
{
	add("%s{\"name\": \"%s\", \"domain\": \"Game State\", \"offset\": %zu, \"type\": \"%s\"%s, \"group\": \"%s\", \"description\": \"%s\"}",
	    g_json_len > 20 ? ", " : "", name, offset, type, writable ? "" : ", \"writable\": false", group, description);
}

static void build_table(void)
{
	g_json_len = 0;
	add("{\"properties\": [");
	prop("Steps", offsetof(struct state, steps), "u64", 0, "Game", "property.steps");
	prop("Clock", offsetof(struct state, clock), "u64", 0, "Game", "property.clock");
	prop("In Level", offsetof(struct state, in_level), "s32", 0, "Game", "property.in_level");
	prop("Lives", offsetof(struct state, lives), "s32", 1, "Game", "property.lives");
	prop("Score", offsetof(struct state, score), "s32", 1, "Game", "property.score");
	prop("Collectibles", offsetof(struct state, collectibles), "s32", 1, "Game", "property.collectibles");
	prop("Level Time", offsetof(struct state, level_time), "f32", 0, "Level", "property.level_time");
	add(", {\"name\": \"Level File\", \"domain\": \"Game State\", \"offset\": %zu, \"type\": \"string\", \"length\": 128, "
	    "\"writable\": false, \"group\": \"Level\", \"description\": \"property.level_file\"}",
	    offsetof(struct state, level));
	prop("Player.X", offsetof(struct state, x), "f32", 1, "Player", "property.player_x");
	prop("Player.Y", offsetof(struct state, y), "f32", 1, "Player", "property.player_y");
	prop("Player.X Speed", offsetof(struct state, xsp), "f32", 1, "Player", "property.player_xsp");
	prop("Player.Y Speed", offsetof(struct state, ysp), "f32", 1, "Player", "property.player_ysp");
	prop("Player.Ground Speed", offsetof(struct state, gsp), "f32", 1, "Player", "property.player_gsp");
	add("]}");
}

/* the engine's variables into the block, after a step */
void osurgedrv_state_publish(void)
{
	if (!g_json_len) build_table();   /* at Init, so that no tool's question changes the machine */
	struct state *s = &g_state;
	player_t *p = level_player();
	s->steps = osurgedrv_frames();
	s->clock = (uint64_t)osurgedrv_time_seconds();
	s->in_level = p != NULL;
	s->lives = player_get_lives();
	s->score = player_get_score();
	s->collectibles = player_get_collectibles();
	s->level_time = p ? level_time() : 0.0f;
	if (p)
	{
		v2d_t pos = player_position(p);
		s->x = pos.x;
		s->y = pos.y;
		s->xsp = player_xsp(p);
		s->ysp = player_ysp(p);
		s->gsp = player_gsp(p);
	}
	else
		s->x = s->y = s->xsp = s->ysp = s->gsp = 0.0f;
	memset(s->level, 0, sizeof s->level);
	if (p) snprintf(s->level, sizeof s->level, "%s", level_file());
	g_shadow = g_state;
}

/* what was written into the block since the game wrote it, written back */
void osurgedrv_state_apply(void)
{
	const struct state *s = &g_state;
	const struct state *o = &g_shadow;
	player_t *p = level_player();
	if (s->lives != o->lives) player_set_lives(s->lives);
	if (s->score != o->score) player_set_score(s->score);
	if (s->collectibles != o->collectibles) player_set_collectibles(s->collectibles);
	if (p)
	{
		if (s->x != o->x || s->y != o->y)
		{
			v2d_t pos = { s->x, s->y };
			player_set_position(p, pos);
		}
		if (s->xsp != o->xsp) player_set_xsp(p, s->xsp);
		if (s->ysp != o->ysp) player_set_ysp(p, s->ysp);
		if (s->gsp != o->gsp) player_set_gsp(p, s->gsp);
	}
}

int osurgedrv_domain_count(void) { return 1; }
const char *osurgedrv_domain_name(int i) { return i == 0 ? "Game State" : ""; }
uint8_t *osurgedrv_domain_ptr(int i) { return i == 0 ? (uint8_t *)&g_state : NULL; }
int64_t osurgedrv_domain_size(int i) { return i == 0 ? (int64_t)sizeof g_state : 0; }
int osurgedrv_domain_writable(int i) { return i == 0; }
const char *osurgedrv_game_properties(void) { return g_json; }
