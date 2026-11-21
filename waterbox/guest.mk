# guest.mk - core.wbx: the same engine, libraries and core sources as
# native.mk, built with miniBox's musl guest toolchain and linked at the guest
# base. Objects land in build/guest. core.wbx is checked by miniBox's
# check-wbx.sh (no thread-local storage, no %fs, no red zone) before it
# counts as built.
#
# Needs miniBox built WITH the C++ guest toolchain (Mesa is C++):
#   meson setup <miniBox>/build/meson-cpp <miniBox> -Dguest_cpp=true && ninja -C <miniBox>/build/meson-cpp
# and build/libs-guest (build-libs.sh -m <miniBox>) and build/mesa (setup-mesa.sh -m <miniBox>).
#
# Usage: make -f guest.mk -j16 [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
PFX := ../build/libs-guest
include sources.mk

B      := $(ROOT)/build/guest
MBUILD := $(MB)/build/meson-cpp
SR     := $(MBUILD)/guest-sysroot
CC     := gcc -specs $(SR)/lib/musl-gcc.specs
CXX    := g++ -specs $(SR)/lib/musl-gcc.specs

# miniBox's frozen guest flags, as source/guest/meson.build gives them to a guest
WBFLAGS := -fvisibility=hidden -mcmodel=large -mno-red-zone -mstack-protector-guard=global \
	-fno-stack-protector -fno-pic -fno-pie -fcf-protection=none -DNDEBUG -DCHIMERA_GUEST
MBINCS := -I$(MB)/extern/emulibc -I$(MB)/source/guest/include -I$(MB)/extern/jsmn
GLINC := -I$(ROOT)/build/mesa/include

OS_CFLAGS := $(WBFLAGS) -std=gnu99 -O2 -w $(NO_BUILTIN_MATH) $(OS_DEFS) $(OS_INCS)
CORE_CFLAGS := $(WBFLAGS) $(CORE_CFLAGS_COMMON) $(MBINCS) $(OS_INCS) $(GLINC) -I. -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(CC) | $(OS_CFLAGS) | $(CORE_CFLAGS))

OS_OBJS := $(patsubst $(OS)/%.c,$(B)/os/%.o,$(OS_SRCS)) $(B)/os/credits.o
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES)))
LIBS_A := $(foreach l,$(LIBNAMES),$(PFX)/lib/lib$(l).a)

all: $(B)/core.wbx

$(SR)/lib/libstdc++.a:
	@echo "miniBox's C++ guest toolchain is missing: $(SR)" >&2
	@false

$(B)/os/%.o: $(OS)/%.c $(B)/flags $(PATCH_STAMP) | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(OS_CFLAGS) -c -o $@ $<

$(B)/os/credits.o: $(GENDIR)/credits.c $(B)/flags
	@mkdir -p $(dir $@)
	$(CC) $(OS_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -c -o $@ $<

# the game's files, in the program as read-only data
$(B)/core/data.o: $(DATA_ZIP)
	@mkdir -p $(dir $@)
	cd $(dir $<) && ld -r -b binary -z noexecstack -o $(abspath $@) $(notdir $<)

# The guest kit's link recipe: the large code model's --no-relax, the weak
# pthread pulls libgcc_eh needs, cxxglue for the unwinder. Mesa
# (setup-mesa.sh): the archives in a group, the osmesa target's object beside
# them (the shared library it belongs to cannot be linked for a guest); Mesa
# declares some pthread entry points weak, which statically linked resolve to
# zero, so each is forced into the link.
MESA_G := $(ROOT)/build/mesa/build-guest2
MESA_G_TARGET = $(shell find $(MESA_G)/src/gallium/targets/osmesa -name target.c.o 2>/dev/null | head -1)
MESA_G_LIBS = $(shell find $(MESA_G) -name '*.a' 2>/dev/null | sort)

$(B)/core.wbx: $(CORE_OBJS) $(OS_OBJS) $(B)/core/data.o $(LIBS_A) sources.mk
	@[ -n "$(MESA_G_TARGET)" ] || { echo "no guest Mesa at $(MESA_G): run ./setup-mesa.sh -m <miniBox>" >&2; false; }
	$(CXX) -static -no-pie -Wl,--eh-frame-hdr -Wl,-O2 -Wl,--no-relax -Wl,--strip-debug -Wl,-z,stack-size=8388608 \
		-T $(MB)/source/guest/linkscript.T \
		-Wl,-u,pthread_mutexattr_init -Wl,-u,pthread_mutexattr_settype -Wl,-u,pthread_mutexattr_destroy \
		-Wl,-u,pthread_once -Wl,-u,pthread_cond_wait -Wl,-u,pthread_cond_broadcast -Wl,-u,pthread_key_create \
		-o $@.tmp $(CORE_OBJS) $(OS_OBJS) $(B)/core/data.o \
		-Wl,--start-group $(LIBS_A) -Wl,--end-group \
		$(MBUILD)/source/guest/cxxglue.c.o $(MBUILD)/source/guest/emulibc.c.o \
		$(MESA_G_TARGET) -Wl,--start-group $(MESA_G_LIBS) -Wl,--end-group $(WRAP_FLAGS) \
		-L$(SR)/lib -lstdc++ -lm -lgcc -lgcc_eh -lc
	sh $(MB)/source/guest/check-wbx.sh $@.tmp
	mv $@.tmp $@

engine: $(OS_OBJS)

clean:
	rm -rf $(B)

.PHONY: all clean engine
