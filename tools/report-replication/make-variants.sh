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
#   bad_tank_Net1.inp   invalid tank levels        -> validateproject Error 225
#   bad_curve_Net1.inp  nonincreasing curve x      -> Errors 227 + 230
#   bad_pump_Net1.inp   pump with no curve/power   -> Error 226
#   bad_parse_Net1.inp  malformed input line       -> input2.c parse errors
#   bad_rule_Net1.inp   malformed rule clause      -> rules.c ruleerrmsg
#   bad_unlinked_Net1.inp junction with no links   -> unlinked() Errors 234/233
#   bad_hydfile_Net1.inp  HYDRAULICS USE <missing> -> silent Error 305
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

if [ -n "$NET1" ]; then
    # invalid tank levels: initial level pushed above the maximum
    sed -E 's/^( 2[ \t]+850[ \t]+)120/\1200/' "$NET1" > "$OUTDIR/bad_tank_Net1.inp"

    # a curve whose x-values do not increase
    awk '/^\[CURVES\]/{ print; print ";nonincreasing"; print " 1  100  200";
                         print " 1   50  250"; next } {print}' \
        "$NET1" > "$OUTDIR/bad_curve_Net1.inp"

    # pump with neither a head curve nor a power rating
    sed -E 's/^( 9[ \t]+9[ \t]+10[ \t]+)HEAD 1.*$/\1/' \
        "$NET1" > "$OUTDIR/bad_pump_Net1.inp"

    # a line the input parser cannot read
    awk '/^\[PATTERNS\]/{ print; print " EMPTYPAT"; next } {print}' \
        "$NET1" > "$OUTDIR/bad_parse_Net1.inp"

    # a missing saved-hydraulics file: the engine reports Error 305 in the
    # report, falls back to SCRATCH and returns success - no API signal
    awk '/^\[OPTIONS\]/{ print;
             print " Hydraulics          Use     /nonexistent/nope.hyd"; next }
         {print}' "$NET1" > "$OUTDIR/bad_hydfile_Net1.inp"

    # a junction no link connects to
    awk '/^\[JUNCTIONS\]/{ print; print " LONELY          \t700         \t0           \t;"; next }
         {print}' "$NET1" > "$OUTDIR/bad_unlinked_Net1.inp"

    # a rule clause the rule parser cannot read
    awk '/^\[RULES\]/{ print; print "RULE BAD1";
             print "IF TANK 2 LEVEL BOGUSOP 110";
             print "THEN PUMP 9 STATUS IS OPEN"; print ""; next } {print}' \
        "$NET1" > "$OUTDIR/bad_rule_Net1.inp"
fi

if [ -n "$NET2" ]; then
    awk 'BEGIN{IGNORECASE=1} /^\[/{sec=$0}
         sec ~ /OPTIONS/ && /^[ \t]*Quality/ {
             print " Quality             Age"; next }
         { sub(/^([ \t]*)Summary([ \t]+)No/, " Summary             Yes");
           print }' "$NET2" > "$OUTDIR/age_Net2.inp"
fi

echo "wrote $(ls "$OUTDIR" | wc -l) variant INP files to $OUTDIR"
