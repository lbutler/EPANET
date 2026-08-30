#!/bin/bash
# ----------------------------------------------------------------------------
# make-variants.sh - generates INP variants that exercise report sections the
# stock test corpora never enable, so compare-reports.sh can sweep them:
#
#   sum_<name>.inp    every network with [REPORT] SUMMARY forced to Yes
#                     (54 of the 56 corpus networks disable the summary)
#   page_Net1.inp     PAGE 55 to exercise pagination + page headers
#   age_Net2.inp      QUALITY AGE analysis
#   price_Net1.inp    global energy price + price pattern + demand charge
#   pprice_Net1.inp   pump-specific price/pattern + pump efficiency curve
#   timer_Net1.inp    TIMER and CLOCKTIME simple controls (STATUS YES lines)
#   pda_Net3.inp      pressure-driven analysis (PDA demand-reduction lines)
#
# Usage:
#   make-variants.sh <output-dir> <inp-file-or-dir>...
#
# Net1/Net2-based variants are only generated when files named Net1.inp /
# Net2.inp are found among the inputs (e.g. EPANET's example-networks dir).
# ----------------------------------------------------------------------------
set -eu

if [ $# -lt 2 ]; then
    echo "Usage: $0 <output-dir> <inp-file-or-dir>..." >&2
    exit 2
fi
OUTDIR=$1
shift
mkdir -p "$OUTDIR"

INPS=()
for arg in "$@"; do
    if [ -d "$arg" ]; then
        while IFS= read -r f; do INPS+=("$f"); done \
            < <(find "$arg" -name "*.inp" | sort)
    else
        INPS+=("$arg")
    fi
done

NET1=""
NET2=""
NET3=""
for f in "${INPS[@]}"; do
    n=$(basename "$f")
    sed -E 's/^([ \t]*[Ss]ummary[ \t]+)[Nn]o/\1Yes/' "$f" > "$OUTDIR/sum_$n"
    [ "$n" = "Net1.inp" ] && NET1=$f
    [ "$n" = "Net2.inp" ] && NET2=$f
    [ "$n" = "Net3.inp" ] && NET3=$f
done

if [ -n "$NET1" ]; then
    sed -E 's/^([ \t]*Page[ \t]+)0/\155/' "$NET1" > "$OUTDIR/page_Net1.inp"

    awk 'BEGIN{IGNORECASE=1} /^\[/{sec=$0}
         sec ~ /\[ENERGY\]/ && /Global Price/ {
             print " Global Price        0.11";
             print " Global Pattern      1"; next }
         sec ~ /\[ENERGY\]/ && /Demand Charge/ {
             print " Demand Charge       12.5"; next }
         {print}' "$NET1" > "$OUTDIR/price_Net1.inp"

    awk 'BEGIN{IGNORECASE=1} /^\[/{sec=$0}
         sec ~ /\[ENERGY\]/ && /Global Price/ {
             print " Global Price        0.11";
             print " Pump 9 Price        0.29";
             print " Pump 9 Pattern      1";
             print " Pump 9 Effic        E1"; next }
         sec ~ /\[ENERGY\]/ && /Demand Charge/ {
             print " Demand Charge       3.75"; next }
         /^\[CURVES\]/ {
             print;
             print ";efficiency curve";
             print " E1  500  60";
             print " E1  1000 75";
             print " E1  1500 80";
             print " E1  2000 70"; next }
         {print}' "$NET1" > "$OUTDIR/pprice_Net1.inp"
fi

if [ -n "$NET1" ]; then
    awk '/^\[CONTROLS\]/{ print;
             print " LINK 9 CLOSED AT TIME 5";
             print " LINK 9 OPEN AT CLOCKTIME 3 AM"; next }
         {print}' "$NET1" > "$OUTDIR/timer_Net1.inp"
fi

if [ -n "$NET3" ]; then
    awk '/^\[OPTIONS\]/{ print;
             print " Demand Model        PDA";
             print " Minimum Pressure    0";
             print " Required Pressure   30";
             print " Pressure Exponent   0.5"; next }
         {print}' "$NET3" > "$OUTDIR/pda_Net3.inp"
fi

if [ -n "$NET2" ]; then
    awk 'BEGIN{IGNORECASE=1} /^\[/{sec=$0}
         sec ~ /OPTIONS/ && /^[ \t]*Quality/ {
             print " Quality             Age"; next }
         { sub(/^([ \t]*)Summary([ \t]+)No/, " Summary             Yes");
           print }' "$NET2" > "$OUTDIR/age_Net2.inp"
fi

echo "wrote $(ls "$OUTDIR" | wc -l) variant INP files to $OUTDIR"
