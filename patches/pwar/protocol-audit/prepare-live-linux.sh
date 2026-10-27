#!/bin/sh
# Prepare isolated, unconnected PipeWire nodes. No link to physical outputs.
set -eu
work=${1:?qualification directory required}
peer=${2:?Windows IPv4 required}
test -x "$work/build/linux/pwar_cli"
test ! -e "$work/live-running" || { echo 'Existing live run marker: inspect processes first' >&2; exit 1; }
touch "$work/live-running"
python3 - "$work/tone.wav" <<'PY'
import math, struct, sys, wave
with wave.open(sys.argv[1], 'wb') as output:
    output.setparams((1, 2, 48000, 0, 'NONE', 'not compressed'))
    output.writeframes(b''.join(struct.pack('<h', int(3276 * math.sin(2 * math.pi * 440 * i / 48000))) for i in range(48000 * 30)))
PY
nohup "$work/build/linux/pwar_cli" --ip "$peer" --buffer_size 128 > "$work/live-start.log" 2>&1 < /dev/null &
echo $! > "$work/live-pwar.pid"
nohup pw-cat -r --target 0 --rate 48000 --channels 2 --format f32 --latency 128 \
    -P '{"node.name":"timox-qualification-record"}' "$work/return.wav" > "$work/record.log" 2>&1 < /dev/null &
echo $! > "$work/live-record.pid"
nohup pw-cat -p --target 0 --latency 128 -P '{"node.name":"timox-qualification-source"}' \
    "$work/tone.wav" > "$work/play.log" 2>&1 < /dev/null &
echo $! > "$work/live-source.pid"
sleep 1
pw-link -io | grep -E 'pwar:|timox-qualification'
