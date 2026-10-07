#!/usr/bin/env bash
# Renders IN.wav through the variations worth hearing, into OUTDIR:
# effect mode on the whole file, and sample mode on its first 2.5 seconds.
# Needs build/md12 (make cli).
set -euo pipefail
if [[ $# -ne 2 ]]; then
    echo "usage: listen.sh IN.wav OUTDIR" >&2
    exit 2
fi
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
md12=$here/../build/md12
in=$1
out=$2
mkdir -p "$out"
echo "listen.sh: rendering $in into $out"
run() { "$md12" "$@" >/dev/null; }

run effect "$in" "$out/effect output 8 raw.wav" -c 8
run effect "$in" "$out/effect output 6 filtered.wav" -c 6 -o ring
run effect "$in" "$out/effect output 3 filtered.wav" -c 3 -o ring
run effect "$in" "$out/effect output 1 filter fired every half second.wav" -c 1 -o ring -p 500
run effect "$in" "$out/effect output 8 raw 45 rpm trick at minus 5.wav" -c 8 -t -5
run effect "$in" "$out/effect output 3 filtered 45 rpm trick at minus 8.wav" -c 3 -o ring -t -8
run effect "$in" "$out/effect mix output 6 SP-12 measured.wav" -c 6 -o mix -m sp12m

for t in -8 -5 0 7; do
    run sample "$in" "$out/sample output 8 raw tune $t.wav" -c 8 -t "$t"
done
run sample "$in" "$out/sample output 1 filtered tune 0.wav" -c 1 -o ring
run sample "$in" "$out/sample output 1 filtered tune minus 5.wav" -c 1 -o ring -t -5
run sample "$in" "$out/sample output 1 filtered tune 0 SP-12 measured.wav" -c 1 -o tip -m sp12m
echo "listen.sh: wrote $(ls "$out" | wc -l) files"
