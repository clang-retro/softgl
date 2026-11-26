#!/bin/sh
# test-glide-dosbox.sh EXE - runs a DOS program of the Voodoo driver in
# DOSBox-X, which emulates a Voodoo Graphics card ("[voodoo] voodoo_card=software").
# The test passes when the output of the program has a "PASS" line.
#
#   DOSBOXX   the DOSBox-X program (default: dosbox-x)
#   TIMEOUT   seconds before DOSBox-X is stopped (default: 120)
#   SHOT      when set, a PNG file for a screenshot of the Voodoo output
#             (needs xvfb-run and ImageMagick); the program then runs
#             in a window and the test does not stop it
#
# Part of clang-retro softgl, a software implementation of OpenGL.
# SPDX-License-Identifier: AGPL-3.0-only
set -u
EXE=${1:?usage: test-glide-dosbox.sh EXE [ARGS]}
shift
DOSBOXX=${DOSBOXX:-dosbox-x}
TIMEOUT=${TIMEOUT:-120}
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
cp "$EXE" "$D/RUN.EXE" || exit 1
cat >"$D/run.conf" <<EOF
[sdl]
output=surface
[dosbox]
machine=svga_s3
memsize=64
[cpu]
cycles=max
core=normal
[voodoo]
voodoo_card=software
[mixer]
nosound=true
[speaker]
pcspeaker=false
[sblaster]
sbtype=none
[autoexec]
mount c "$D"
c:
RUN.EXE $* > OUT.TXT
exit
EOF
if [ -n "${SHOT:-}" ]; then
  # Take the screenshot while the program shows its frames.
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    \"$DOSBOXX\" -conf \"$D/run.conf\" -exit >/dev/null 2>&1 &
    sleep \${SHOT_DELAY:-8}
    import -window root \"$SHOT\"
    wait"
else
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout "$TIMEOUT" "$DOSBOXX" -conf "$D/run.conf" -exit >/dev/null 2>&1
fi
tr -d '\r' <"$D/OUT.TXT" 2>/dev/null
grep -q "^PASS" "$D/OUT.TXT" 2>/dev/null
