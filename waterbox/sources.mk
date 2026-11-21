# sources.mk - what native.mk and guest.mk both build: the same files with the
# same defines, so the native reference and the sandboxed core are the same
# program. Included, not run.
#
# The libraries (Allegro, PhysicsFS, SurgeScript, libpng, zlib, FreeType, Ogg
# and Vorbis) are built by build-libs.sh into build/libs-native and
# build/libs-guest. The engine is compiled here, file by file, as the engine's
# own CMake lists them (src/misc/cmake/srcs.cmake).

ROOT   := ..
OS     := $(ROOT)/extern/opensurge
MB     ?= $(or $(MINIBOX_DIR),$(HOME)/chimera/extern/chimera-common-minibox)
GENDIR := $(ROOT)/build/gen

# the engine's sources, from the engine's own list (the platform ones are not in it)
OS_LIST := $(shell awk '/^set\(GAME_SRCS/{f=1;next} /^\)/{f=0} f' $(OS)/src/misc/cmake/srcs.cmake | grep -o 'src/[^ ]*\.c' | grep -v 'misc/credits.c' | grep -v '^src/main.c')
OS_SRCS := $(addprefix $(OS)/,$(OS_LIST)) $(OS)/src/third_party/fast_draw.c

# As the engine's CMake configures it on Linux, with:
#   WANT_FASTDRAW      the engine's fast drawing routines
#   no WANT_PLAYMOD    no module-file loader (it needs a file chooser)
#   no BETTER_GAMEPAD  no gamepad database: the core's pad is the game's keyboard
#   GAME_DATADIR       where the game's files are in the machine's own disk
#   GAME_USERDIRNAME   its user folder's name
# and no build date: a build is the same program whenever it is made.
OS_DEFS := -DALLEGRO_EXCLUDE_GLX -DWANT_FASTDRAW=1 '-DGAME_USERDIRNAME="opensurge"' '-DGAME_DATADIR="/os/share"' -DCHIMERA_CORE=1
OS_INCS = -I$(OS)/src -I$(PFX)/include -I$(PFX)/include/freetype2 -I$(ROOT)/build/mesa/include

# the transcendental functions, which detmath.c answers in both builds (the
# engine's, SurgeScript's and the libraries': the link wraps every object):
# not builtins, so the compiler neither folds them nor fuses a sin and a cos
MATH_FUNCS := sin cos tan acos asin atan atan2 exp log pow exp2 log2 log10 sinf cosf tanf acosf asinf atanf atan2f \
	expf logf powf exp2f log2f log10f
NO_BUILTIN_MATH := $(foreach f,$(MATH_FUNCS) sincos sincosf,-fno-builtin-$(f))

# ---- the core
CORE_C_NAMES := osfiles seams detmath wbx-entry osurge-driver game-state
CORE_HDRS := osurge-driver.h $(wildcard allegro-driver/*.c)
CORE_CFLAGS_COMMON := -std=gnu11 -O2 $(NO_BUILTIN_MATH) -DCHIMERA_CORE=1

# ---- the calls the core answers itself (a link wrap each, in both builds)
WRAPS := fopen fopen64 open open64 close read write pread pread64 lseek lseek64 fstat fstat64 stat stat64 lstat lstat64 \
	access mkdir rmdir unlink remove rename opendir readdir readdir64 closedir realpath getcwd readlink fsync ftruncate \
	getenv qsort glDrawArrays glDrawElements exit time localtime localtime_r gettimeofday clock_gettime rand srand random srandom getpwuid getuid \
	$(MATH_FUNCS)
WRAP_FLAGS := $(foreach w,$(WRAPS),-Wl,--wrap=$(w))

# the patch series goes onto the submodules before anything of the engine builds
PATCH_STAMP := $(ROOT)/build/patches.stamp
$(PATCH_STAMP): $(wildcard $(ROOT)/patches/*/*.patch) apply-patches.sh
	sh apply-patches.sh
	@mkdir -p $(dir $@)
	@touch $@

# the engine's credits screen reads the licence table of the game's files
$(GENDIR)/credits.c: $(OS)/src/misc/credits.c.in $(OS)/src/misc/copyright_data.csv
	@mkdir -p $(dir $@)
	python3 gen-credits.py $(OS)/src/misc/credits.c.in $(OS)/src/misc/copyright_data.csv $@

# the game's files, one zip, in the program as data
DATA_ZIP := $(ROOT)/build/gen/surge-data.zip
$(DATA_ZIP): make-data-zip.py
	@mkdir -p $(dir $@)
	python3 make-data-zip.py $(OS) $@

# Every object depends on the flags it was built with: a change to them
# rebuilds it.
define flags_stamp
$(shell mkdir -p $(1); printf '%s\n' '$(2)' | cmp -s - $(1)/flags || printf '%s\n' '$(2)' > $(1)/flags)
endef

# the libraries' archives, in link order (Allegro's add-ons first)
LIBNAMES := allegro_acodec-static allegro_audio-static allegro_font-static allegro_ttf-static allegro_image-static \
	allegro_primitives-static allegro_physfs-static allegro_memfile-static allegro-static surgescript-static \
	physfs vorbisfile vorbis ogg freetype png16 z
