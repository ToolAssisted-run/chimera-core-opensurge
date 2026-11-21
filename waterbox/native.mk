# native.mk - the native reference: the same engine, libraries and core
# sources as guest.mk, built for the host, plus the two harnesses (run-native
# drives the exports directly; run-wbx drives core.wbx through the miniBox
# host exactly as the frontend does). Objects land in build/native.
#
# Needs build/libs-native (build-libs.sh -n) and build/mesa (setup-mesa.sh -n).
#
# Usage: make -f native.mk -j16 [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
PFX := ../build/libs-native
include sources.mk

B := $(ROOT)/build/native
MBINCS := -Inative-shim -I$(MB)/source/guest/include -I$(MB)/extern/jsmn
GLINC := -I$(ROOT)/build/mesa/include

OS_CFLAGS := -std=gnu99 -O2 -w $(NO_BUILTIN_MATH) $(OS_DEFS) $(OS_INCS)
CORE_CFLAGS := $(CORE_CFLAGS_COMMON) $(MBINCS) $(OS_INCS) $(GLINC) -I. -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(OS_CFLAGS) | $(CORE_CFLAGS))

OS_OBJS := $(patsubst $(OS)/%.c,$(B)/os/%.o,$(OS_SRCS)) $(B)/os/credits.o
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES)))
LIBS_A := $(foreach l,$(LIBNAMES),$(PFX)/lib/lib$(l).a)

engine: $(OS_OBJS)

all: $(B)/run-native $(B)/run-wbx

$(B)/os/%.o: $(OS)/%.c $(B)/flags $(PATCH_STAMP)
	@mkdir -p $(dir $@)
	gcc $(OS_CFLAGS) -c -o $@ $<

$(B)/os/credits.o: $(GENDIR)/credits.c $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(OS_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/run-native.o: run-native.c gate-harness.h osurge-driver.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 -Wall -DGATE_NATIVE -I. -c -o $@ $<

# the game's files, as the guest has them: in the program
$(B)/core/data.o: $(DATA_ZIP)
	@mkdir -p $(dir $@)
	cd $(dir $<) && ld -r -b binary -z noexecstack -o $(abspath $@) $(notdir $<)

# Mesa (setup-mesa.sh -n): its archives refer to each other both ways, so
# they are linked in a group, with the osmesa target's own object beside them
MESA_N := $(ROOT)/build/mesa/build-native
MESA_N_TARGET = $(shell find $(MESA_N)/src/gallium/targets/osmesa -name target.c.o 2>/dev/null | head -1)
MESA_N_LIBS = $(shell find $(MESA_N) -name '*.a' 2>/dev/null | sort)

$(B)/run-native: $(CORE_OBJS) $(OS_OBJS) $(B)/core/data.o $(B)/core/run-native.o $(LIBS_A) sources.mk
	@[ -n "$(MESA_N_TARGET)" ] || { echo "no host Mesa at $(MESA_N): run ./setup-mesa.sh -n" >&2; false; }
	g++ -o $@ $(CORE_OBJS) $(OS_OBJS) $(B)/core/data.o $(B)/core/run-native.o \
		-Wl,--start-group $(LIBS_A) -Wl,--end-group $(MESA_N_TARGET) -Wl,--start-group $(MESA_N_LIBS) -Wl,--end-group \
		$(WRAP_FLAGS) -lm -lpthread

# run-wbx links the miniBox host library
MBHOST := $(MB)/build/meson-linux/source/host
$(B)/run-wbx: run-wbx.c gate-harness.h osurge-driver.h $(B)/flags
	gcc -O2 -Wall -I. -I$(MB)/source/host -o $@ run-wbx.c $(MBHOST)/libminiboxhost.so -Wl,-rpath,$(MBHOST)

clean:
	rm -rf $(B)

.PHONY: all clean engine
