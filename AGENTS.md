# AGENTS.md - Open Surge Engine core for Chimera

This repository builds the Open Surge Engine (the C game engine of Surge the
Rabbit, on Allegro 5 and SurgeScript) as a core for Chimera
(https://github.com/ToolAssisted-run/chimera). It is a GAME core
(`"kind": "game"`). It produces one file, `opensurge.chimeraCore`. The package
carries Surge the Rabbit's files (their licences allow it: see
`waterbox/package-licenses.json`); a project may bring a game of its own as a
zip.

## Layout

- `extern/opensurge`: the engine and Surge the Rabbit's files. `extern/surgescript`,
  `extern/allegro5`, `extern/physfs`, `extern/zlib`, `extern/libpng`,
  `extern/freetype`, `extern/ogg`, `extern/vorbis`: the libraries. All are
  submodules at pinned commits. Never edited in place.
- `patches/opensurge/`, `patches/allegro5/`: numbered patches, applied by
  `waterbox/apply-patches.sh`. No patch is sent upstream.
- `waterbox/allegro-driver/`: Allegro's system, display, keyboard and audio
  drivers for this core (`osmesa_driver.c`, `osmesa_audio.c`). A patch puts them
  in Allegro's build; the files are here.
- `waterbox/sources.mk`, `native.mk`, `guest.mk`: the engine's sources and the
  two builds; `build-libs.sh` builds the libraries and Allegro for both;
  `setup-mesa.sh` builds Mesa's softpipe (the OpenGL) for both.
- `waterbox/osurge-driver.c`: the machine (the engine's loop in steps, the
  picture, the sound, the controls). `osfiles.c`: every file the engine opens
  (the game's zip, the user folder in memory). `seams.c`: the clock, the random
  numbers, the draw calls, the native dialogs. `game-state.c`: the memory
  domain and the properties. `wbx-entry.c`: the guest's exports. `detmath.c`:
  the math functions.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`,
  `package-licenses.json`, `languages/`: the declarations.
- `waterbox/run-gate.sh`: the core's tests. `waterbox/tests/`: the route,
  the zips the tests make, and the frontend tests.
- `build/`: every output. Ignored by git.

## Commands, in order

```
git submodule update --init
cd waterbox
./setup-mesa.sh -m <miniBox> && ./setup-mesa.sh -n     # about 15 minutes each
./run-gate.sh -m <miniBox>
./tests/run-frontend.sh --chimera-root <chimera> -m <miniBox>
./build-package.sh -m <miniBox>
```

`docs/BUILDING.md` has the requirements and the contract tests.

## Rules

- Upstream is changed only by a numbered patch.
- Native and sandbox must stay the same machine: anything that reads a clock,
  a thread, a host folder, the environment or the C library's math or random
  numbers is answered by the core.
- Not drawing a step (`SetRenderingEnabled(0)`) must leave the machine as it
  is: the gate's `undrawn` and `turbo` tests hold it.
- Every new test is broken on purpose once and seen to fail.
- No game of anyone's but Surge the Rabbit's in the repository or the package.
- Plain language and ASCII in documents. Commits: `type(scope): what`, a short
  body with the numbers, explicit paths staged.
