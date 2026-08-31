#!/bin/bash
# ----------------------------------------------------------------------------
# make-profile-corpus.sh - produces native EPANET reports under the fixed
# epanet-js report profile, for roundtrip.mjs to verify against.
#
# Every input network's [REPORT] section is replaced wholesale with:
#
#     Status FULL / Summary NO / Page 0 / Messages YES /
#     Nodes NONE / Links NONE / Energy NO
#
# and any [TIMES] Statistic option is forced to NONE, then the engine is
# run to produce the .rpt.  Networks that fail to run (bad inputs, halted
# runs) still write a report - that is the point of including them.
#
# Usage:
#   make-profile-corpus.sh <output-dir> <runepanet-binary> <inp-file-or-dir>...
#
# Files named sum_*.inp / page_*.inp are skipped (their variations are
# meaningless under the profile); duplicate basenames keep the first copy.
# ----------------------------------------------------------------------------
set -eu

if [ $# -lt 3 ]; then
    echo "Usage: $0 <output-dir> <runepanet-binary> <inp-file-or-dir>..." >&2
    exit 2
fi
OUTDIR=$1
RUNEPANET=$2
shift 2
mkdir -p "$OUTDIR"

profile_rewrite() {
    awk '
        BEGIN { inreport = 0; inserted = 0 }
        function profile() {
            print "[REPORT]"
            print " Status              FULL"
            print " Summary             NO"
            print " Page                0"
            print " Messages            YES"
            print " Nodes               NONE"
            print " Links               NONE"
            print " Energy              NO"
            print ""
            inserted = 1
        }
        /^\[/ {
            up = toupper($0)
            if (up ~ /^\[REPORT\]/) { inreport = 1; next }
            inreport = 0
            if (up ~ /^\[END\]/ && !inserted) profile()
            print; next
        }
        inreport { next }
        toupper($1) ~ /^STATISTIC/ { print " Statistic           NONE"; next }
        { print }
        END { if (!inserted) profile() }
    ' "$1" > "$2"
}

total=0; ran=0; failed=0
for arg in "$@"; do
    if [ -d "$arg" ]; then
        set +e
        FILES=$(find "$arg" -name '*.inp' | sort)
        set -e
    else
        FILES=$arg
    fi
    for f in $FILES; do
        base=$(basename "$f" .inp)
        case "$base" in sum_*|page_*) continue;; esac
        [ -e "$OUTDIR/$base.inp" ] && continue
        total=$((total + 1))
        profile_rewrite "$f" "$OUTDIR/$base.inp"
        if "$RUNEPANET" "$OUTDIR/$base.inp" "$OUTDIR/$base.rpt" \
            > /dev/null 2>&1; then
            ran=$((ran + 1))
        else
            failed=$((failed + 1))   # expected for the bad_ networks
        fi
    done
done
echo "profile corpus: $total networks, $ran ran clean, $failed returned errors"
