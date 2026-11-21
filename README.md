# chimera-core-opensurge

The [Open Surge Engine](https://github.com/alemart/opensurge) as a core for
[Chimera](https://github.com/ToolAssisted-run/chimera). It is a game core: it
plays Surge the Rabbit, the engine's own sample game, or any game made with the
engine, as a project, with movies and TAStudio like any other core.

The core is one file, `opensurge.chimeraCore`. Put it in Chimera's `Cores`
folder.

**Speed, plainly:** the game does not play in real time in Chimera's window.
The picture is drawn by a software OpenGL (Mesa's softpipe), and one drawn
frame costs about 0.37 seconds of CPU. In Chimera's window that was measured
at about 1.4 frames a second on a busy build machine. Frames that are not
shown (seeking, fast forward, `chimera-run`) cost a few milliseconds each.
Drawing on the graphics card is later work. Details are in "Speed" below.

## What it is

- The engine is upstream's C code, built from source with a few patches
  (`patches/`). It runs on [Allegro 5](https://liballeg.org/) and
  [SurgeScript](https://github.com/alemart/surgescript), the engine's
  scripting language.
- Allegro has no window, no sound device and no threads in this core. A
  display driver draws through Mesa's softpipe (a software OpenGL) into memory.
  An audio driver mixes the sound a step at a time. The clock is the number of
  steps.
- A movie row is one step of the engine: one tick of its 60 Hz timer.
- The package carries Surge the Rabbit's files (their authors' licences are in
  the package). A project may bring a game of its own, as a zip.

## What the project brings

Nothing is needed to play Surge the Rabbit. Optional files:

- **game**: a zip of another game's folder (the folder with its `surge.cfg`).
- **savedata**: a zip of the files the game wrote in an earlier play (Game >
  Export Save Data writes it).

## Controls

The controls are the engine's default keyboard map: the arrows, Space (Fire 1,
jump), Left Ctrl (Fire 2), Enter (Start), Escape (Back) and the Shift keys.
A first gamepad has its default bindings too.

## Speed

The picture is drawn by a software OpenGL (Mesa's softpipe), so a step that is
drawn is slow: about 0.37 seconds of one processor at 426x240 (2.6 steps a
second; a picture at 2x has four times the pixels). A step whose picture nobody
looks at is not drawn at all and costs a few milliseconds: 3000 steps of a run
through the first level, every 50th drawn, take about half a minute. Seeking,
fast forward, `chimera-run` and the tests draw only the steps that are
looked at. The numbers, and how they were measured, are in
[docs/PLAN.md](docs/PLAN.md). Playing in real time is not possible until the
picture is drawn on the graphics card, which is later work.

## Building

See [docs/BUILDING.md](docs/BUILDING.md). The design log is
[docs/PLAN.md](docs/PLAN.md); [AGENTS.md](AGENTS.md) is the same for somebody
who does not read prose.

## Licence

GPL-3.0-or-later for the engine and this core; the game files are under their
authors' licences. See [LICENSE](LICENSE).
