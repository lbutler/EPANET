#!/bin/bash
# ----------------------------------------------------------------------------
# line-coverage.sh - reports WHICH KINDS of report lines the replica does not
# reproduce, over a whole corpus.
#
# compare-reports.sh answers "does this network match?".  This answers the
# complementary question "across everything we ran, what line shapes has the
# replica never been able to produce?" - the empirical counterpart to the
# source-level audit in MISSING_API.md.
#
# Usage:
#   line-coverage.sh <compare-reports output dir>...
#
# Reads the .diff files left behind by compare-reports.sh, normalises numbers
# and identifiers out of each native-only line, and tallies the distinct
# shapes.  Anything listed here is a line the engine wrote and the replica
# did not.  Replica-only shapes are reported separately: those would be bugs
# (the replica inventing output), and the list should always be empty.
# ----------------------------------------------------------------------------
set -u

if [ $# -lt 1 ]; then
    echo "Usage: $0 <compare-reports output dir>..." >&2
    exit 2
fi

normalise() {
    sed -E "s/^.  *//
            s/[0-9]+\.[0-9]+([eE][-+]?[0-9]+)?/N/g
            s/\b[0-9]+\b/N/g
            s/^N:N:N: /<time>: /
            s/for (Link|Node) .*/for \1 <id>/
            s/^(Trial +N:).*/\1 relative flow change = N/
            s/(Pump|Pipe|CV|PRV|PSV|PBV|FCV|TCV|GPV|PCV) [^ ]+ (switched|setting)/\1 <id> \2/"
}

ndiff=0
for d in "$@"; do
    n=$(find "$d" -name "*.diff" 2>/dev/null | wc -l)
    ndiff=$((ndiff + n))
done

echo "=============================================================="
echo " report line coverage over $# dir(s), $ndiff differing network(s)"
echo "=============================================================="
echo
echo "LINE SHAPES THE ENGINE WROTE AND THE REPLICA DID NOT:"
echo
for d in "$@"; do cat "$d"/*.diff 2>/dev/null; done \
    | grep '^-' | grep -v '^---' | normalise | grep -vE '^-?$' \
    | sort | uniq -c | sort -rn \
    | awk '{ n=$1; $1=""; sub(/^ /,""); printf "  %8d  %s\n", n, $0 }'

echo
echo "LINE SHAPES THE REPLICA INVENTED (should be empty):"
echo
out=$(for d in "$@"; do cat "$d"/*.diff 2>/dev/null; done \
    | grep '^+' | grep -v '^+++' | normalise | grep -vE '^\+?$' \
    | sort | uniq -c | sort -rn \
    | awk '{ n=$1; $1=""; sub(/^ /,""); printf "  %8d  %s\n", n, $0 }')
if [ -z "$out" ]; then echo "  (none)"; else echo "$out"; fi
