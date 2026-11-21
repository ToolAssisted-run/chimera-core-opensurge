#!/usr/bin/env bash
# Builds the libraries the engine stands on, static, twice from the same
# sources with the same options: once for the miniBox guest (build/libs-guest)
# and once for the host (build/libs-native, the reference the tests compare
# the guest with).
#
#   zlib, libpng      pictures (Allegro's image add-on)
#   ogg, vorbis       the music and the sound effects
#   freetype          the game's default font is a TrueType file
#   physfs            the game's files
#   surgescript       the game's scripting language
#   allegro           the game library: core, primitives, image, audio,
#                     acodec, font, ttf, memfile, physfs; no window system
#
# Allegro is built without a window system, an audio device or a clock of its
# own: waterbox/allegro/ holds the system, display, keyboard, joystick and
# audio drivers the core puts in their place (patches/allegro5/).
#
# Usage: waterbox/build-libs.sh [-n] [-m <miniBox dir>] [-j N]
#   -n   the host flavour (no miniBox needed)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
minibox="${MINIBOX_DIR:-}"
jobs="$(nproc)"
native=0
while getopts "nm:j:" opt; do
	case "$opt" in
		n) native=1 ;;
		m) minibox="$OPTARG" ;;
		j) jobs="$OPTARG" ;;
		*) exit 2 ;;
	esac
done

if [ "$native" = 1 ]; then
	flavour=native
else
	flavour=guest
	[ -n "$minibox" ] || { echo "libs: the guest flavour needs -m <miniBox dir> (or MINIBOX_DIR)" >&2; exit 1; }
	minibox="$(cd "$minibox" && pwd)"
	sr="$minibox/build/meson-cpp/guest-sysroot"
	[ -f "$sr/lib/musl-gcc.specs" ] || { echo "libs: the guest sysroot is not built at $sr" >&2; exit 1; }
fi

# the patches to Allegro (and the engine) go on before anything builds
sh "$here/apply-patches.sh"

prefix="$root/build/libs-$flavour"
work="$root/build/libs-work-$flavour"
mesa="$root/build/mesa"
mkdir -p "$prefix" "$work"
if [ -f "$prefix/.done" ]; then
	echo "libs: already built in $prefix"
	exit 0
fi

# Mesa's headers only, in a folder of their own, so Allegro's OpenGL code
# builds against the OSMesa the core links and never against the host's.
gl="$work/gl-include"
mkdir -p "$gl/GL" "$gl/KHR"
[ -f "$mesa/include/GL/osmesa.h" ] || { echo "libs: build Mesa first (setup-mesa.sh)" >&2; exit 1; }
cp "$mesa/include/GL/"{gl.h,glext.h,osmesa.h} "$gl/GL/"
# Allegro includes GLU's header and uses nothing of it
echo "/* nothing of GLU is used */" > "$gl/GL/glu.h"
# Allegro's CMake wants OpenGL libraries to exist; a static build links none,
# so empty files stand in for them (and the host's are never looked at)
for l in libGL.so libGLU.so libOpenGL.so; do : > "$gl/$l"; done
cp "$mesa/include/KHR/khrplatform.h" "$gl/KHR/"

if [ "$native" = 1 ]; then
	cc=gcc
	wbflags=""
	toolchain=""
else
	mkdir -p "$work/bin"
	cat > "$work/bin/gw-cc" <<EOF
#!/bin/sh
exec gcc "\$@" -specs $sr/lib/musl-gcc.specs
EOF
	chmod +x "$work/bin/gw-cc"
	cc="$work/bin/gw-cc"
	# miniBox's frozen guest flags (source/guest/meson.build)
	wbflags="-fvisibility=hidden -mcmodel=large -mno-red-zone -mstack-protector-guard=global -fno-stack-protector -fno-pic -fno-pie -fcf-protection=none -DNDEBUG -DCHIMERA_GUEST"
fi
cflags="-O2 -ffp-contract=off -fno-builtin-sin -fno-builtin-cos -fno-builtin-tan -fno-builtin-atan2 -fno-builtin-sqrt $wbflags"

# every library: static, no tests, no tools, no option that asks the machine
# what it has
cm() {
	local name="$1" src="$2"; shift 2
	echo "libs: $name ($flavour)"
	rm -rf "$work/$name"
	CC="$cc" cmake -S "$src" -B "$work/$name" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
		-DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=OFF \
		-DCMAKE_C_FLAGS="$cflags" -DCMAKE_C_FLAGS_RELEASE="" \
		-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY "$@" > "$work/$name.log" 2>&1 || { tail -20 "$work/$name.log" >&2; exit 1; }
	nice -n 19 cmake --build "$work/$name" -j "$jobs" >> "$work/$name.log" 2>&1 || { tail -30 "$work/$name.log" >&2; exit 1; }
	cmake --install "$work/$name" >> "$work/$name.log" 2>&1
}

x="$root/extern"
cm zlib "$x/zlib" -DZLIB_BUILD_EXAMPLES=OFF -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_TESTING=OFF
rm -f "$prefix/lib/libz.so"* # only the archive
cm libpng "$x/libpng" -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF \
	-DPNG_HARDWARE_OPTIMIZATIONS=OFF -DPNG_INTEL_SSE=off -DPNG_ARM_NEON=off -DZLIB_ROOT="$prefix" \
	-DZLIB_LIBRARY="$prefix/lib/libz.a" -DZLIB_INCLUDE_DIR="$prefix/include"
cm ogg "$x/ogg" -DINSTALL_DOCS=OFF -DBUILD_TESTING=OFF
cm vorbis "$x/vorbis" -DBUILD_TESTING=OFF -DINSTALL_DOCS=OFF -DOGG_ROOT="$prefix" \
	-DOGG_LIBRARY="$prefix/lib/libogg.a" -DOGG_INCLUDE_DIR="$prefix/include"
cm freetype "$x/freetype" -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON \
	-DFT_DISABLE_BROTLI=ON -DFT_REQUIRE_ZLIB=OFF
cm physfs "$x/physfs" -DPHYSFS_BUILD_SHARED=OFF -DPHYSFS_BUILD_STATIC=ON -DPHYSFS_BUILD_TEST=OFF -DPHYSFS_BUILD_DOCS=OFF \
	-DPHYSFS_ARCHIVE_ZIP=ON -DPHYSFS_ARCHIVE_7Z=OFF -DPHYSFS_ARCHIVE_GRP=OFF -DPHYSFS_ARCHIVE_WAD=OFF \
	-DPHYSFS_ARCHIVE_HOG=OFF -DPHYSFS_ARCHIVE_MVL=OFF -DPHYSFS_ARCHIVE_QPAK=OFF -DPHYSFS_ARCHIVE_SLB=OFF \
	-DPHYSFS_ARCHIVE_ISO9660=OFF -DPHYSFS_ARCHIVE_VDF=OFF
cm surgescript "$x/surgescript" -DWANT_SHARED=OFF -DWANT_STATIC=ON -DWANT_EXECUTABLE=OFF -DWANT_EXECUTABLE_MULTITHREAD=OFF

# Allegro: the drivers of waterbox/allegro-driver replace the window system,
# the audio device and the clock. The function checks the guest cannot answer
# are given, the same to both builds, so the two are one configuration.
cm allegro "$x/allegro5" -DSHARED=OFF -DWANT_MONOLITH=OFF -DWANT_X11=OFF -DWANT_OPENGL=ON -DWANT_OPENGLES=OFF \
	-DWANT_AUDIO=ON -DWANT_ALSA=OFF -DWANT_OSS=OFF -DWANT_PULSEAUDIO=OFF -DWANT_OPENAL=OFF -DWANT_JACK=OFF \
	-DWANT_ACODEC=ON -DWANT_FLAC=OFF -DWANT_OPUS=OFF -DWANT_MODAUDIO=OFF -DWANT_OGG_VORBIS=ON -DWANT_ACODEC_DYNAMIC_LOAD=OFF \
	-DWANT_IMAGE=ON -DWANT_IMAGE_PNG=ON -DWANT_IMAGE_JPG=OFF -DWANT_IMAGE_WEBP=OFF -DWANT_IMAGE_FREEIMAGE=OFF \
	-DWANT_NATIVE_IMAGE_LOADER=OFF -DWANT_FONT=ON -DWANT_TTF=ON -DWANT_PHYSFS=ON -DWANT_MEMFILE=ON -DWANT_PRIMITIVES=ON \
	-DWANT_COLOR=OFF -DWANT_NATIVE_DIALOG=OFF -DWANT_VIDEO=OFF -DWANT_DEMO=OFF -DWANT_EXAMPLES=OFF -DWANT_TESTS=OFF \
	-DWANT_DOCS=OFF -DWANT_POPUP_EXAMPLES=OFF -DWANT_ALLOW_SSE=OFF \
	-DALLEGRO_HAVE_STRERROR_S=0 -DALLEGRO_HAVE_GETEXECNAME=0 -DALLEGRO_HAVE_FTELLI64=0 -DALLEGRO_HAVE_FSEEKI64=0 \
	-DCHIMERA_OSMESA_DRIVER="$here/allegro-driver/osmesa_driver.c" -DCHIMERA_OSMESA_AUDIO="$here/allegro-driver/osmesa_audio.c" \
	-DOPENGL_INCLUDE_DIR="$gl" -DOPENGL_GLU_INCLUDE_DIR="$gl" -DOPENGL_gl_LIBRARY="$gl/libGL.so" \
	-DOPENGL_glu_LIBRARY="$gl/libGLU.so" -DOPENGL_opengl_LIBRARY="$gl/libOpenGL.so" \
	-DPHYSFS_INCLUDE_DIR="$prefix/include" -DPHYSFS_LIBRARY="$prefix/lib/libphysfs.a" \
	-DCMAKE_C_FLAGS="$cflags -DALLEGRO_EXCLUDE_GLX -I$gl -I$x/allegro5/addons/audio"
# the native dialogs are not built (there is no window); the game only needs the header, and the core answers the calls
cp "$x/allegro5/addons/native_dialog/allegro5/allegro_native_dialog.h" "$prefix/include/allegro5/"
touch "$prefix/.done"
