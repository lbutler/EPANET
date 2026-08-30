#!/bin/bash
# ----------------------------------------------------------------------------
# compare-reports.sh - runs repgen over a set of EPANET networks and diffs
# the engine's native report against the pure-toolkit-API replica.
#
# Usage:
#   compare-reports.sh <repgen-binary> <output-dir> <inp-file-or-dir>...
#
# Environment:
#   REPGEN_ARGS   extra arguments passed to repgen for every network
#                 (default: --status no)
#
# Wall-clock timestamps (the "Page 1" date stamp and the Analysis begun/ended
# lines) are masked before diffing - those are expected to differ.
#
# Exit status: 0 if every network matched, 1 otherwise.
# ----------------------------------------------------------------------------
set -u

if [ $# -lt 3 ]; then
    echo "Usage: $0 <repgen-binary> <output-dir> <inp-file-or-dir>..." >&2
    exit 2
fi

REPGEN=$1
OUTDIR=$2
shift 2
ARGS=${REPGEN_ARGS---status no}

mkdir -p "$OUTDIR"

# collect INP files
INPS=()
for arg in "$@"; do
    if [ -d "$arg" ]; then
        while IFS= read -r f; do INPS+=("$f"); done \
            < <(find "$arg" -name "*.inp" | sort)
    else
        INPS+=("$arg")
    fi
done

mask() {
    # mask wall-clock timestamps and normalize the volatile parts
    sed -E \
        -e 's/^(  Page 1[[:space:]]+).*/\1<DATE>/' \
        -e 's/^(  Analysis (begun|ended) ).*/\1<DATE>/' \
        "$1"
}

pass=0; fail=0; gap=0; error=0
declare -a FAILED GAPPED ERRORED

for inp in "${INPS[@]}"; do
    name=$(basename "$inp" .inp)
    nat="$OUTDIR/$name.native.rpt"
    rep="$OUTDIR/$name.replica.rpt"
    log="$OUTDIR/$name.log"

    eval '"$REPGEN" "$inp" "$nat" "$rep"' "$ARGS" > "$log" 2>&1
    rc=$?
    if [ $rc -ge 2 ] || [ ! -s "$nat" ]; then
        error=$((error+1)); ERRORED+=("$name(rc=$rc)")
        continue
    fi

    mask "$nat" > "$nat.masked"
    mask "$rep" > "$rep.masked"

    if diff -u "$nat.masked" "$rep.masked" > "$OUTDIR/$name.diff" 2>&1; then
        pass=$((pass+1))
        rm -f "$OUTDIR/$name.diff"
    else
        # Classify. A diff is a documented API gap - not a bug - when every
        # native-only line is one the toolkit provably cannot supply, and the
        # replica adds nothing of its own. The unreachable kinds are:
        #   WARNING: ...                      disconnected-node warnings (#4)
        #   Initial Mass / Mass Inflow / ...  quality mass balance      (#11)
        #   <t>: Balancing the network:       FMT64  solver trace       (#12)
        #   Trial  N: relative flow change    FMT65  solver trace       (#12)
        #   maximum  flow change/head error   FMT66/67/68               (#12)
        #   <type> <id> switched from A to B  FMT57  intra-iteration    (#12)
        #   <type> <id> setting changed to N  FMT56  intra-iteration    (#12)
        #   <t>: Valve <id> caused ill-cond.  FMT61                     (#12)
        if awk '
            /^-/ && !/^---/ {
                line=substr($0,2)
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
                if (line == "") next
                if (line ~ /^WARNING:/) next
                if (line ~ /^(Initial Mass|Mass Inflow|Mass Outflow|Mass Reacted|Final Mass|Total Segments):/) next
                if (line ~ /^[0-9]+:[0-9][0-9]:[0-9][0-9]: Balancing the network:$/) next
                if (line ~ /^Trial +[0-9]+: relative flow change =/) next
                if (line ~ /^maximum +(flow change|head error) +=.*for (Link|Node) /) next
                if (line ~ / switched from .* to /) next
                if (line ~ / setting changed to /) next
                if (line ~ /caused ill-conditioning$/) next
                exit 1
            }
            /^\+/ && !/^\+\+\+/ {
                line=substr($0,2)
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
                if (line != "") exit 1
            }' "$OUTDIR/$name.diff"; then
            gap=$((gap+1)); GAPPED+=("$name")
        else
            fail=$((fail+1)); FAILED+=("$name")
        fi
    fi
done

echo "=============================================================="
echo " repgen report comparison  (args: $ARGS)"
echo "=============================================================="
echo " identical:          $pass"
echo " api-gap only:       $gap   (only lines the API cannot supply)"
echo " mismatched:         $fail"
echo " errored:            $error"
[ $gap -gt 0 ]   && printf ' api-gap:     %s\n' "${GAPPED[*]}"
[ $fail -gt 0 ]  && printf ' mismatched:  %s\n' "${FAILED[*]}"
[ $error -gt 0 ] && printf ' errored:     %s\n' "${ERRORED[*]}"
echo " details: $OUTDIR/<name>.diff"

[ $fail -eq 0 ] && [ $error -eq 0 ]
