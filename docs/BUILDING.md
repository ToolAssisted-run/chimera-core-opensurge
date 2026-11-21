# Building the Open Surge Engine core

## What you need

A Linux machine (Ubuntu 24.04 is what CI uses), with:

- gcc and g++, make, cmake, meson and ninja, python3 with `mako` and
  `packaging` (Mesa's build needs them), zip, curl, git.
- A Chimera checkout, built (`meson setup build/meson-linux ...`; see Chimera's
  README), for the contract tests and `chimera-run`.
- miniBox, the sandbox's host library and its C and C++ guest toolchains. It is
  a submodule of Chimera (`extern/chimera-common-minibox`):

      mb=<chimera>/extern/chimera-common-minibox
      meson setup $mb/build/meson-linux $mb && meson compile -C $mb/build/meson-linux
      meson setup $mb/build/meson-cpp $mb -Dguest_cpp=true && meson compile -C $mb/build/meson-cpp

Allegro, PhysicsFS, SurgeScript, FreeType, libpng, zlib, libogg and libvorbis are
built here from the submodules; no system copy of them is used. The first
build takes about 25 minutes (most of it is Mesa) and the repository's own
build files are the only thing built again after that.

## Sources

    git submodule update --init
    # the engine, SurgeScript, Allegro, PhysicsFS, zlib, libpng, FreeType, libogg, libvorbis

The submodules are never edited in place. `patches/<submodule>/` holds the
numbered patches, which `waterbox/apply-patches.sh` puts on (every build step
that needs them does it). The series is checked as a whole: a tree that is
partly patched is refused, with the files named.

## Build steps

All of them are in `waterbox/`; each takes `-m <miniBox dir>`.

| Step | What it does |
|---|---|
| `setup-mesa.sh -m <mb>` and `setup-mesa.sh -n` | Mesa 24.0.9 (softpipe behind OSMesa, no LLVM), for the guest and for the host, into `build/mesa`. About 15 minutes each. |
| `build-libs.sh -m <mb>` and `build-libs.sh -n` | The libraries and Allegro, static, into `build/libs-guest` and `build/libs-native`. About a minute each. |
| `make -f guest.mk -j16 MB=<mb>` | `build/guest/core.wbx`, the program for the sandbox. |
| `make -f native.mk -j16 MB=<mb>` | `build/native/run-native` and `run-wbx`: the same program for the host, and the two programs that drive a machine for the tests. |
| `build-package.sh -m <mb> [-r <chimera>]` | `build/package/opensurge.chimeraCore`, or into `<chimera>/build/Cores`. |

## The tests

    waterbox/run-gate.sh -m <mb>                          # the core's own tests (about N minutes)
    waterbox/tests/run-frontend.sh --chimera-root <chimera> -m <mb>   # the package through chimera-run and Chimera's window
    <chimera>/build/meson-linux/source/ui/test-packages build/package   # Chimera's contract tests

`docs/PLAN.md` says what each test compares and how long it takes.

## What CI does

`.github/workflows/chimera.yml` checks Chimera out at `main` with submodules,
builds miniBox, runs `run-gate.sh`, runs the contract tests against the
package, uploads it as `opensurge-<sha>` and calls Chimera's publish workflow.
The game's files are in the engine's submodule, so no test needs a download.

## Troubleshooting

- `Mesa has not been built`: run `setup-mesa.sh` first (the guest flavour and the host one).
- A change to `waterbox/allegro-driver/` is built by `build-libs.sh`: remove
  `build/libs-<flavour>/.done` and run it again.
- `check-wbx` says the image uses `%fs`: something was compiled without the guest flags.
