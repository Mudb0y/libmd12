#!/usr/bin/env bash
# Runs every netlist here through ngspice in batch mode and writes the results
# to tests/data/spice_<name>.csv. AC files: freq_hz,mag_db,phase_deg (phase
# wrapped to -180..180). env: time_s and node voltages; env_dc: forced C111
# voltage and the resulting pin 13 voltage at three RT6 settings.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
out=$(cd "$here/../.." && pwd)/tests/data
mkdir -p "$out"

if command -v ngspice >/dev/null 2>&1; then
    ngspice=(ngspice)
else
    ngspice=(nix shell nixpkgs#ngspice -c ngspice)
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

nets=(aa fixed3 fixed4 fixed5 fixed6 dynout env mix preamp)
expect=(aa aa_sp12r8 fixed3 fixed4 fixed5 fixed6 dynout env env_dc mix
    preamp_0db preamp_40db preamp_20db_a1b1 preamp_20db_a0b0)

echo "run.sh: simulating ${#nets[@]} netlists into $out"

for n in "${nets[@]}"; do
    if ! (cd "$work" && "${ngspice[@]}" -b "$here/$n.cir" >"$work/$n.log" 2>&1) ||
        grep -qiE '^ *error' "$work/$n.log"; then
        echo "run.sh: error: ngspice failed on $n.cir, log follows" >&2
        tail -n 30 "$work/$n.log" >&2
        exit 1
    fi
done

for d in "${expect[@]}"; do
    if [[ ! -s "$work/$d.dat" ]]; then
        echo "run.sh: error: no output $d.dat was produced" >&2
        exit 1
    fi
    csv="$out/spice_$d.csv"
    awk 'BEGIN { OFS = "," }
        NR == 1 {
            if ($1 == "frequency") $1 = "freq_hz"
            else if ($1 == "time") $1 = "time_s"
            else $1 = "cap_v"
        }
        { $1 = $1; print }' "$work/$d.dat" >"$csv"
    echo "run.sh: wrote $csv ($(($(wc -l <"$csv") - 1)) rows)"
done
