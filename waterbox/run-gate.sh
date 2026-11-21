#!/bin/bash
# run-gate.sh - the Open Surge Engine core's tests: the native reference and
# core.wbx are the same machine, playing Surge the Rabbit's first level (the
# game's own files are in the core, so nothing is needed from anyone).
#
# Every test says what it compared. The tests:
#   equivalence  run-native and run-wbx on a route of 3000 steps through the
#                first level: every step's sound, length and lag, the game's
#                state, and the picture of every 50th step
#   rerecord     the sandbox saved and loaded before every step = without
#   session      saved at a step, a new host, loaded, finished = without
#   pictures     native and sandbox on the first 300 steps with every step drawn
#   undrawn      a run with most steps not drawn is the same machine as one
#                with every step drawn (sound and state; only the stale
#                pictures differ)
#   turbo        the first half not drawn: the rest the same
#   settings     startTime is the engine's clock and seed (the same time the
#                same game, another time another one; the time zone is not
#                read); resolution 2 draws a picture twice the size
#   game         a game of the project's own, given as a zip with the engine's
#                files in a folder of it, plays the same as the built-in one
#   saves        a file the project starts with is in Export Save Data
#   refusals     a game zip that is not a game says why
#   sound        the route's sound is not silence
#   clock        the guest has no time() of its own
#   teeth        the comparison sees a one-step difference
#
# usage: run-gate.sh [-m <miniBox dir>] [-n <steps of the route>]
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}"
steps=3000
while getopts "m:n:" opt; do
	case "$opt" in
		m) mb="$OPTARG" ;;
		n) steps="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
mb="$(cd "$mb" && pwd)"

echo "== build (miniBox $mb)"
mkdir -p "$root/build"
# the OpenGL both builds draw with: Mesa, built once into build/mesa
bash "$here/setup-mesa.sh" -m "$mb" > "$root/build/gate-mesa.log" 2>&1 && bash "$here/setup-mesa.sh" -n >> "$root/build/gate-mesa.log" 2>&1 || {
	tail -20 "$root/build/gate-mesa.log"; echo "the Mesa build failed"; exit 1; }
# the libraries, then the engine, for the host and for the sandbox
bash "$here/build-libs.sh" -n > "$root/build/gate-libs.log" 2>&1 && bash "$here/build-libs.sh" -m "$mb" >> "$root/build/gate-libs.log" 2>&1 || {
	tail -20 "$root/build/gate-libs.log"; echo "the libraries failed to build"; exit 1; }
make -C "$here" -f native.mk MB="$mb" -j"$(nproc)" > "$root/build/gate-native.log" 2>&1 || {
	tail -20 "$root/build/gate-native.log"; echo "native build failed"; exit 1; }
make -C "$here" -f guest.mk MB="$mb" -j"$(nproc)" > "$root/build/gate-guest.log" 2>&1 || {
	tail -20 "$root/build/gate-guest.log"; echo "guest build failed"; exit 1; }
native="$root/build/native/run-native"
wbx="$root/build/native/run-wbx"
core="$root/build/guest/core.wbx"

work="$root/build/gate"
rm -rf "$work"
mkdir -p "$work"
fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# the digests only (the sandbox's own log lines go to the same stdout)
digests() { grep -E '^[A-Za-z]+(\[[^]]*\])?=' "$1" || true; }
same() { # same <test> <a> <b> <what>
	digests "$2" > "$2.d"
	digests "$3" > "$3.d"
	if cmp -s "$2.d" "$3.d"; then
		pass "$1: $4"
	else
		fail "$1: $4"; diff "$2.d" "$3.d" | head -20
	fi
}
value() { digests "$1" | sed -n "s/^$2=//p"; }
# the digests that are the machine's (not the pictures): the sound, the
# lengths and lag, the clock and the game's state
machine() { digests "$1" | grep -E '^(audioHash|stepsHash|lagFrames|clock|domain)'; }

level='{"startLevel": "levels/sunshine-1.lev"}'
mkproj() { # mkproj <dir> [<settings JSON>]: the settings a project hands the core
	mkdir -p "$work/$1"
	printf '%s' "${2:-$level}" > "$work/$1/settings"
}

python3 "$here/tests/make-route.py" "$steps" "$work/route.txt"
python3 "$here/tests/make-zips.py" "$root/extern/opensurge" "$work/zips"
mkproj main "$level"
draw=50
echo "== the route ($steps steps through Sunshine Paradise 1, every ${draw}th drawn)"
"$native" "$work/main" --frames "$steps" --movie "$work/route.txt" --draw-every $draw > "$work/n.txt"
"$wbx" "$core" "$work/main" --frames "$steps" --movie "$work/route.txt" --draw-every $draw > "$work/w.txt" 2> "$work/w.err"
same equivalence "$work/n.txt" "$work/w.txt" "native = sandbox, $steps steps (lag $(value "$work/n.txt" lagFrames))"
"$wbx" "$core" "$work/main" --frames "$steps" --movie "$work/route.txt" --draw-every $draw --rerecord > "$work/r.txt" 2> "$work/r.err"
same rerecord "$work/r.txt" "$work/w.txt" "save+load before every step = straight ($(sed -n 's/stateBytes=//p' "$work/r.err") state bytes)"
half=$((steps / 2))
"$wbx" "$core" "$work/main" --frames "$steps" --movie "$work/route.txt" --draw-every $draw --session-at $half > "$work/s.txt" 2> "$work/s.err"
same session "$work/s.txt" "$work/w.txt" "saved at step $half, loaded into a new host = straight"

echo "== pictures and undrawn steps"
pic=300
"$native" "$work/main" --frames $pic --movie "$work/route.txt" --draw-every 1 > "$work/p.n"
"$wbx" "$core" "$work/main" --frames $pic --movie "$work/route.txt" --draw-every 1 > "$work/p.w" 2>/dev/null
same pictures "$work/p.n" "$work/p.w" "native = sandbox, $pic steps, every one drawn"
"$native" "$work/main" --frames $pic --movie "$work/route.txt" --draw-every 100 > "$work/u.n"
if [ "$(machine "$work/u.n")" = "$(machine "$work/p.n")" ]; then
	pass "undrawn: $pic steps with every 100th drawn leave the sound and the game's state as drawing every step does"
else
	fail "undrawn: not drawing a step changes the machine"; diff <(machine "$work/u.n") <(machine "$work/p.n") | head
fi
"$native" "$work/main" --frames $pic --movie "$work/route.txt" --turbo > "$work/t.n"
if [ "$(machine "$work/t.n")" = "$(machine "$work/p.n")" ] && [ "$(value "$work/t.n" tailVideoHash)" = "$(value "$work/p.n" tailVideoHash)" ]; then
	pass "turbo: the first half not drawn - the second half's pictures, the sound and the state are the same"
else
	fail "turbo"; diff <(digests "$work/t.n") <(digests "$work/p.n") | head
fi

echo "== settings"
timed() { # timed <dir> <startTime>
	mkproj "$1" "{\"startLevel\": \"levels/sunshine-1.lev\", \"startTime\": $2}"
	"$native" "$work/$1" --frames 700 --movie "$work/route.txt" --draw-every 100 > "$work/$1/n"
	"$wbx" "$core" "$work/$1" --frames 700 --movie "$work/route.txt" --draw-every 100 > "$work/$1/w" 2>/dev/null
}
timed t1 1704067200
timed t1b 1704067200
timed t2 1717243200
if [ "$(machine "$work/t1/n")" = "$(machine "$work/t1b/n")" ] && [ "$(machine "$work/t1/n")" != "$(machine "$work/t2/n")" ] &&
   [ "$(machine "$work/t2/n")" = "$(machine "$work/t2/w")" ]; then
	pass "settings: startTime is the engine's clock and seed: the same time plays one game, another time another; both builds agree"
else
	fail "settings: startTime"; machine "$work/t1/n"; machine "$work/t2/n"
fi
mkproj tz "$level"
TZ=Asia/Tokyo "$native" "$work/tz" --frames 700 --movie "$work/route.txt" --draw-every 100 > "$work/tz/n"
if [ "$(machine "$work/tz/n")" = "$(machine "$work/t1/n")" ]; then
	pass "settings: the host's time zone is not read (700 steps in Asia/Tokyo and in UTC are the same)"
else
	fail "settings: the time zone changed the machine"
fi
size() { python3 -c "import sys; d = open(sys.argv[1], 'rb').read(18); print('%dx%d' % (d[12] | d[13] << 8, d[14] | d[15] << 8))" "$1" 2>/dev/null || echo none; }
mkproj big '{"startLevel": "levels/sunshine-1.lev", "resolution": 2}'
"$native" "$work/big" --frames 12 --screenshot 11:"$work/big/s.tga" > "$work/big/n" 2>&1 || true
"$wbx" "$core" "$work/big" --frames 12 > "$work/big/w" 2>&1 || true
"$native" "$work/main" --frames 12 --screenshot 11:"$work/main/s.tga" > /dev/null 2>&1 || true
if [ "$(size "$work/main/s.tga")" = 426x240 ] && [ "$(size "$work/big/s.tga")" = 852x480 ]; then
	same settings "$work/big/w" "$work/big/n" "resolution 1 draws 426x240 and 2 draws 852x480; native = sandbox"
else
	fail "settings: resolution drew $(size "$work/main/s.tga") and $(size "$work/big/s.tga")"
fi

echo "== a game of the project's own, and saves"
mkdir -p "$work/own"
cp "$work/zips/game.zip" "$work/own/game.zip"
printf '{"game": ["game.zip"]}' > "$work/own/slots"
printf '{}' > "$work/own/settings"
mkdir -p "$work/builtin"
printf '{}' > "$work/builtin/settings"
"$native" "$work/builtin" --frames 400 --draw-every 100 > "$work/builtin/n"
"$native" "$work/own" --frames 400 --draw-every 100 > "$work/own/n" 2>&1 || true
"$wbx" "$core" "$work/own" --frames 400 --draw-every 100 > "$work/own/w" 2>/dev/null || true
if [ "$(machine "$work/own/n")" = "$(machine "$work/builtin/n")" ] && [ "$(value "$work/own/n" videoHash)" = "$(value "$work/builtin/n" videoHash)" ]; then
	pass "game: Surge the Rabbit's files as a zip given to the project play the same as the ones in the core (400 steps, pictures and all)"
	same game "$work/own/w" "$work/own/n" "the game of the project's own: native = sandbox"
else
	fail "game: the zip's game is not the built-in one"; diff <(digests "$work/own/n") <(digests "$work/builtin/n") | head
fi
mkdir -p "$work/sv" "$work/sv-out/notes"
cp "$work/zips/saves.zip" "$work/sv/saves.zip"
printf '{"savedata": ["saves.zip"]}' > "$work/sv/slots"
printf '{}' > "$work/sv/settings"
"$native" "$work/sv" --frames 5 --savedata-out "$work/sv-out" > "$work/sv/n" 2>&1 || true
if [ "$(cat "$work/sv-out/notes/test.txt" 2>/dev/null)" = "a file of the user folder, put there by the project" ]; then
	pass "saves: a file the project starts with is in the user folder and comes out of Export Save Data as it went in"
else
	fail "saves: the file given in the savedata slot did not come back ($(tail -2 "$work/sv/n" | tr '\n' ' '))"
fi

echo "== refusals"
refuse() { # refuse <dir> <expected text> <what>
	out="$("$native" "$work/$1" --frames 1 2>&1 || true)"
	outw="$("$wbx" "$core" "$work/$1" --frames 1 2>&1 || true)"
	if echo "$out" | grep -q "loadError=.*$2" && echo "$outw" | grep -q "loadError=.*$2"; then
		pass "refusals: $3 - $(echo "$out" | sed -n 's/^loadError=//p')"
	else
		fail "refusals: $3"; echo "$out" | tail -2
	fi
}
mkdir -p "$work/bad"
cp "$work/zips/bad.zip" "$work/bad/bad.zip"
printf '{"game": ["bad.zip"]}' > "$work/bad/slots"
printf '{}' > "$work/bad/settings"
refuse bad "error.zip_could_not_be_read.bad.zip" "a game that is not a zip"

echo "== sound"
# the music and the effects are really in the sound (it is 44100 Hz stereo,
# 735 frames a step): the level's music plays from the first steps and the
# route's jumps and rings add effects, so a stretch of the run is far from silence
"$native" "$work/main" --frames 700 --movie "$work/route.txt" --draw-every 1000 --audio "$work/pcm" > /dev/null
snd="$(python3 - "$work/pcm" <<'PYEND'
import array, sys
a = array.array("h")
a.frombytes(open(sys.argv[1], "rb").read())
per = 735 * 2
loud = sum(1 for v in a[200 * per:700 * per] if abs(v) > 1000)
total = len(a[200 * per:700 * per])
print("%d %d" % (loud, total))
PYEND
)"
if [ "${snd% *}" -gt $(( ${snd#* } / 20 )) ]; then
	pass "sound: steps 200 to 700 of the route are not silence (${snd% *} of ${snd#* } samples are loud)"
else
	fail "sound: steps 200 to 700 are silent ($snd)"
fi

echo "== clock"
if nm "$core" | grep -qE ' (T|W|t) (time|__real_time)$'; then
	fail "clock: the guest links a time() of the C library's"
else
	pass "clock: time() is only the core's (the startTime setting plus the steps)"
fi

echo "== teeth"
# step 600 is in the run: Left instead of Right
awk '!/^#/ { n++ } n == 601 && !/^#/ { print "L"; next } { print }' "$work/route.txt" > "$work/teeth.movie"
"$native" "$work/main" --frames 700 --movie "$work/teeth.movie" --draw-every 100 > "$work/teeth.txt"
if [ "$(machine "$work/teeth.txt")" = "$(machine "$work/t1/n")" ]; then
	fail "teeth: a route one step different digests the same"
else
	pass "teeth: a route one step different digests differently"
fi

echo
if [ $fails -eq 0 ]; then
	echo "gate: all tests passed"
else
	echo "gate: $fails test(s) failed"
	exit 1
fi
