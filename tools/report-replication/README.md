# Report replication test harness (`repgen`)

Reproduces EPANET's native `.rpt` report **1:1 using only the public toolkit
API**, to find where the API falls short of the report's content.  The gaps
found so far are logged in [MISSING_API.md](MISSING_API.md).

## How it works

```
repgen <input.inp> <native.rpt> <replica.rpt> [--status no|yes|full]
                                              [--set "<REPORT command>"]...
```

One process runs one simulation and writes two reports:

1. **native.rpt** - written by the engine itself (`EN_open` writes the logo
   and summary, `EN_openH`/`EN_closeQ` the begun/ended stamps, warnings
   appear during the solve, and `EN_report` appends the energy table and the
   node/link result tables).
2. **replica.rpt** - generated from an `RD_ReportData` model populated
   purely by `EN_*` getter calls made while driving the same simulation
   step-by-step.

The design separates **data collection** from **rendering** so the same
model can later feed other output formats:

| File            | Role |
|-----------------|------|
| `report_data.h` | renderer-independent data model: everything the report contains |
| `collect.c`     | fills the model via the toolkit API while running the simulation (energy re-integration, result sampling at report times, statistics post-processing, warning reconstruction) |
| `inp_sniff.c`   | documented workaround for API gap #1: re-parses `[REPORT]` settings the API cannot read back |
| `render_text.c` | renders the model in the classic `.rpt` text format, byte-for-byte (an HTML/JSON renderer would be a sibling of this file) |
| `main.c`        | CLI |

`--set` commands (e.g. `--set "NODES ALL"`) are applied to the native engine
via `EN_setreport` *and* mirrored into the replica model, so both reports
reflect them - useful to force the result tables and energy table on for
networks whose INP files don't request them.

`--status yes|full` is plumbed through for the next pass (replicating the
hydraulic status report); the replica does not reproduce those sections yet
and says so.

## Comparing across a corpus

```
tools/report-replication/compare-reports.sh <repgen> <outdir> <inp dir>...

# e.g., after a CMake build:
tools/report-replication/compare-reports.sh \
    build/tools/report-replication/repgen out example-networks

# force full tables everywhere:
REPGEN_ARGS='--status no --set "NODES ALL" --set "LINKS ALL" --set "ENERGY YES"' \
tools/report-replication/compare-reports.sh ...
```

The script masks the wall-clock timestamp lines (`Page 1` datestamp,
`Analysis begun/ended`), diffs each pair, and classifies every network as
*identical*, *warning-gap only* (all remaining differences are `WARNING`
lines the API cannot reproduce - MISSING_API.md #3/#4), *mismatched*, or
*errored*.

## Current results (first pass, STATUS NO)

Against the 3 bundled example networks plus the 56 networks of
[epanet-example-networks](https://github.com/OpenWaterAnalytics/epanet-example-networks)
(`epanet-tests`, `msx-examples`), in both as-is and forced-full-tables
modes, **all 59 networks replicate byte-for-byte**:

```
 identical:          59
 warning-gap only:   0
 mismatched:         0
 errored:            0
```

A third sweep over a 63-network variant corpus (`make-variants.sh`: every
network's `SUMMARY` forced on so the summary block is exercised everywhere,
plus `PAGE 55` pagination, AGE quality, and priced-energy variants) passes
63/63 the same way.

Covered and verified byte-identical: logo/banner, input summary (all option
lines, minutes-vs-hours time units, quality variants incl. TRACE headers,
reporting criteria), energy usage table (re-integrated to the cent, incl.
prices, price patterns, pump efficiency curves and demand charges), node
and link result tables at every reporting period (incl. `STATISTIC AVERAGE`
post-processing, tank/reservoir/valve/pump row suffixes, closed/open/active
status text), pagination with page headers, and the warning lines
(WARN01/02/04/05/06 - WARN05 only via an undocumented API quirk; see
MISSING_API.md #3).

The replication succeeding does not mean the API is complete - it means
every gap in MISSING_API.md now has either a workaround that duplicates
engine internals or relies on undocumented behavior.  The log is the list
of what the API should add to make this possible cleanly.
