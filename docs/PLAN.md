# Plan and design log - the Open Surge Engine core

Dated entries. What was decided, why, and what was measured.

## 2026-11-21: the feasibility answers (chimera#628)

The request: a core for the Open Surge Engine (https://github.com/alemart/opensurge,
C, GPL-3.0), the engine of Surge the Rabbit. The owner decided to build it
(2026-11-21). The questions of docs/new-core.md, answered from the source:

- **May it be distributed, modified?** The engine is GPL-3.0; SurgeScript, the
  engine's scripting language (https://github.com/alemart/surgescript), is
  Apache-2.0. A core is the engine's source with patches, published as a binary
  with its licence texts: allowed. Surge the Rabbit's files are a separate
  matter, answered below ("The licence audit").
- **One thread on one clock?** Yes, with Allegro's help. The engine is a loop that
  waits for Allegro's events; the 60 Hz timer's tick is one step of the game
  (input, scripts, physics, then a picture). The core makes the clock: it moves
  Allegro's time by 1/60 of a second and the tick follows. The engine's own
  threads are two: the scripts are compiled in a thread at start (now inline)
  and a file chooser (now a call that says there is no dialog). Allegro's are
  the timer, the music feeder and the audio device's (all replaced).
- **Where does the picture come from?** The engine draws through Allegro's
  OpenGL driver, with GLSL shaders. There is no framebuffer to read. The
  picture is drawn by Mesa's softpipe (OSMesa) compiled into the core, the way
  the Retro Engine v4 core does it. Speed: see below.
- **Does it recompile?** No.
- **What must the user bring?** Nothing for Surge the Rabbit (its files are in
  the core); optionally another game's zip, and the saves of an earlier play.
- **What can a test run on a public runner?** Everything: the game's files are in
  the engine's repository, which is a submodule.

## The machine

One step of the machine is one step of the engine: one tick of its timer (60 a
second, exactly). What the frontend sees:

- **Allegro without a computer.** The libraries are built here, static, from
  the submodules, for the guest and for the host from the same sources and
  flags (`waterbox/build-libs.sh`). Allegro 5.2.9.1 carries a patch
  (`patches/allegro5`) that puts in drivers of this core's own
  (`waterbox/allegro-driver/`): a system and display driver whose OpenGL
  display is an OSMesa context on a memory buffer; a keyboard the core sets (a key
  down or up is an event when it changes, as a window system's would give it);
  a mouse and a joystick that are installed and report nothing (the engine
  refuses to start without them); a clock the core moves; an audio driver with
  no device and no thread. The music streams (Ogg Vorbis) are fed by Allegro
  in a thread of each stream's own; here they are read when the core asks for
  the step's sound.
- **The sound** is the engine's mixer (one voice, 44100 Hz, 16 bits, stereo),
  asked for exactly 735 frames each step.
- **The controls** are the engine's default keyboard map: the four arrows,
  Space (Fire 1, jump), Left Ctrl (Fire 2), Enter (Start), Escape (Back) and
  the two Shift keys (shoulders). The first gamepad is the same buttons
  (`default_keybinds.json`); the engine's own joystick code is not used.
- **The files** are a virtual disk (`waterbox/osfiles.c`): the C library's file
  calls are wrapped, in both builds. /os/share is Surge the Rabbit's files (a
  stored zip that is part of the program, read in place); /os/user is the
  engine's user folder (preferences, saves, logs), in guest memory, carried by
  savestates, with Game > Export Save Data from miniBox's kit; the project's
  `game` slot (a zip of another game's folder) replaces /os/share and is
  played with the engine's own `--game-folder`; the `savedata` slot puts
  files in /os/user before the first step.
- **Time and chance.** `time`, `gettimeofday` and `clock_gettime` answer the
  start time setting plus the steps taken (60 a second); `localtime` is UTC.
  `rand` and `random` are one generator of the core's own (glibc's and musl's
  differ), seeded by what the engine seeds them with, which is the start time.
  SurgeScript seeds its generator, and the perfect hash function it makes for
  its names, from the same clock. The transcendental functions are
  `detmath.c`'s, as in the other game cores.
- **No exit.** The engine ends a failed start with exit(); the core catches
  that (a longjmp), reports the engine's message as the load error, and the
  machine is over.

## Not drawing a step

The first speed measurement: software OpenGL is slow. Steady state in the first
level is about 0.37 seconds of CPU a step at 426x240 (2.6 steps a second), 78%
of which is softpipe running Allegro's fragment shader in its interpreter
(measured by sampling the native build's stack, `OSG_PROF`). Quality low is no
faster, and a 2x picture is four times the work.

Not drawing is far cheaper: a step whose picture nobody looks at
(`SetRenderingEnabled(0)`: the frontend's fast forward, a seek, a movie played
without a window) has the draw calls of Allegro's OpenGL driver
(`glDrawArrays`, `glDrawElements`) skipped, link wraps in `seams.c`, and costs
about 0.01 seconds of CPU: 600 steps with every 60th drawn take 7.6 seconds,
against 225 seconds all drawn. The engine still runs its whole step, drawing
code included, and the tests prove that this leaves the machine as it is:
300 steps drawn every step and drawn every 100th have the same sound and the
same game state, and so does a run whose first half is not drawn.

## The licence audit (Surge the Rabbit's files)

Every music file (22 of 22), sound effect (91 of 91) and picture (122 of 122)
is listed with its author and licence in the engine's own table,
`src/misc/copyright_data.csv`, and so are the fonts' pictures and the
translations; the `licenses/` folder has the texts. The licences are CC-BY-3.0
(about 157 files), CC-BY-4.0 (55), CC0-1.0 (39), CC-BY-SA-3.0 (ten music
tracks by Johan Brodd, and one picture), CC-BY-SA-4.0 (one picture), MIT (the
translations and a flag picture), Apache-2.0 (the Roboto fonts), OFL-1.1 (the
Noto fonts), and the Allegro Giftware licence (two logos). Nothing is blank,
unknown or "all rights reserved". The levels, sprite and script files
(about 400, the game's own text files) are the engine's, GPL-3.0, as the repository's licence says. One
folder-level blemish: `languages/lihuanian.lng` (a misspelt copy of the
Lithuanian file) is not in the table.

Decision: the package carries them. They are the engine's own sample game, a
free licence covers each file, and what each licence asks is done: the author
of every file is named in the engine's credits screen (the table is compiled
in) and in the package (`licenses.json` and the table itself as the last
licence text), and the licence texts are in `licenses/`. The CC-BY-SA files are
shipped unchanged, as separate files of a zip beside the program (an
aggregate): their share-alike terms apply to them, not to the engine.

## The tests

`waterbox/run-gate.sh` (see its header for the list) and
`waterbox/tests/run-frontend.sh`. The route is `tests/make-route.py`: 3000
steps through the first level, Sunshine Paradise 1, started with the
startLevel setting (the player runs right, jumps in bursts, looks up and ducks
now and then; it reaches the middle of the level, 28 rings, and stops at a wall
it does not jump).

## Results (2026-11-21)

Measured on a shared 256-thread machine (AMD engineering sample, load average
over 200, the jobs run at the lowest priority), so every time is the CPU time of
one process and a slow one.

Speed:

| What | Time |
|---|---|
| a step drawn, in a level, 426x240 | about 0.37 s (600 steps in 3 min 46 s) |
| a step not drawn | a few ms (3000 steps, every 50th drawn: 32 s native, 37 s sandbox) |
| Chimera's window, steps drawn | about 0.7 s a step (150 steps in 1 min 48 s) |

Where a drawn step's time goes (the native build, its stack sampled 200 times a
second for 230 steps in the first level): 78% of the samples are inside
softpipe's shader interpreter (`shade_quads`, `exec_run`,
`tgsi_exec_machine_run`); triangle setup, the vertex pipeline, Allegro and the
engine are the rest (not separated). So it is the number of pixels shaded: the
engine draws several screen-sized layers of background and bricks every step.
A 2x picture has four times the pixels (not measured).

`waterbox/run-gate.sh` (roughly a quarter of an hour here once everything is built), every leg passing:

| Leg | What it compared |
|---|---|
| equivalence | native and sandbox, 3000 steps of the route: the sound, the lengths, the lag, the game state, and the picture of every 50th step: identical |
| rerecord | the sandbox saved and loaded before every one of the 3000 steps (19.3 MB a state): identical to the straight run |
| session | saved at step 1500, a new host, loaded, finished: identical |
| pictures | native and sandbox, 300 steps with every step drawn: identical |
| undrawn | 300 steps drawn every 100th, against every step drawn: the same sound and game state |
| turbo | the first half not drawn: the second half's pictures, the sound and the state the same |
| settings | startTime: the same time the same game, another time another, both builds agree; the host's time zone (Asia/Tokyo against UTC) is not read; resolution 2 draws 852x480 |
| game | Surge the Rabbit's files as a zip in a folder, given to the `game` slot: the same 400 steps, pictures included, as the built-in copy; native = sandbox |
| saves | a file the project starts with is in Game > Export Save Data, byte for byte |
| refusals | a game zip that is not a zip is refused with the load error |
| sound | steps 200 to 700 of the route are 82% loud samples (the level's music, and effects) |
| clock | the guest links no C library `time()` |
| teeth | a route one step different digests differently |

`waterbox/tests/run-frontend.sh` (about ten minutes here), 8 of 8 passing: the package
builds (its SHA-1 was the same in two builds); `chimera-run` plays the route
from a movie made into a project and from a hand-written project, and both end
with the reference's Game State and last picture; Chimera's window, headless,
plays the first 120 steps to the reference's state, `game.list/get/set` work, the
core's own load error (a resolution of 9) is shown, the default bindings are the
package's, and nothing is written under Chimera's checkout. Chimera's contract
tests (`test-packages`) pass on the package.

Each new leg was broken on purpose once and seen to fail:

- undrawn: time not passing on a step that is not drawn changes the sound and the state.
- equivalence (and with it rerecord, session, pictures, which use the same
  comparison): a sandbox run of a route one step different digests differently.
- settings/startTime: ignoring the start time makes two start times one machine.
- settings/resolution, saves: failed in the first run for real (a display that
  could not be resized; an export that listed the engine's log), and passed
  when fixed.
- game: a game zip whose folder is not found (the engine says it is not a game directory).
- saves: not putting the project's files in the user folder.
- refusals: taking a zip that cannot be read.
- sound: silence is refused.
- clock: taking `time` out of the wrapped functions puts the C library's in the guest.

What broke only on a real run, for whoever ports the next Allegro game: the
engine refuses to start without a keyboard, a mouse and a joystick driver (the
core's are installed and empty); Allegro's display driver needs the mouse
cursor and `resize_display` entries (a missing one is an `ASSERT`, then a
crash); PhysicsFS asks `/proc` where the program is (the virtual disk says it is
`/os/bin/opensurge`); the C library's `qsort` and `rand` differ between glibc and
musl, so both are the core's own.

## What is not proved

- **Windows and macOS.** Nothing here was run on either. The package declares
  all three x86_64 hosts like the other game cores, because the guest is one
  file; the tests that find what fails only there (stacks, the thread pointer)
  have not been run for this core. aarch64 is a build of its own and was not
  built.
- **A real third-party game.** The `game` slot is tested with Surge the
  Rabbit's own files zipped in a folder, which plays like the built-in copy. A
  fan game was not tried.
- **A gamepad.** The default bindings are checked to be the ones Chimera adopts;
  no pad was connected.
- **Real time.** Drawing is slow (above): the core does not play in real time.
  Chimera's own window draws every step it shows (about 0.7 seconds a step in
  the test), so the window test plays 120 steps.
- **The menus.** The route starts in the first level (the startLevel setting).
  The intro and the title screen run (the first 400 steps are in the tests, not
  a route through the menus).
- **Translations.** The 25 language tables other than English are English
  copies.
- **The mouse and the touch screen.** The engine's mouse is installed and never
  moves; touch is not there. The game's menus need neither.
