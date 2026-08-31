# EPANET report parser (epanet-js profile)

A small, dependency-free TypeScript library that turns an EPANET `.rpt`
report into structured data — plus a byte-exact regenerator that proves
the capture is lossless.

epanet-js writes the input file, so the report settings are pinned to
one **fixed profile**:

```
[REPORT]   Status FULL / Summary NO / Page 0 / Messages YES /
           Nodes NONE / Links NONE / Energy NO
[TIMES]    Statistic NONE
```

Node/link results reach epanet-js through the API and output file, not
the report.  The report's job is everything the API *cannot* tell you
(see `../report-replication/MISSING_API.md`): what happened inside the
hydraulic loop — control and rule actions, link status switching,
per-trial convergence, warnings — plus the closing balances and any
input diagnostics.  Under the profile a report is exactly:

```
Page-1 stamp + logo banner           -> ReportHeader
[validation / parse diagnostics]     -> ReportDiagnostic[]
Analysis begun <ctime>               -> analysisBegun
Hydraulic Status: stream             -> HydraulicTimeStep[] (STATUS FULL)
Hydraulic Flow Balance               -> FlowBalance
[Water Quality Mass Balance]         -> MassBalance   (quality runs)
Analysis ended <ctime>               -> analysisEnded
[terminal error lines]               -> ReportDiagnostic[]
```

## Files

| file | what |
| --- | --- |
| `epanet-report.ts` | the data model: `EpanetReport`, `StatusEvent`, `ReportStreamEvent`, … (each type cites the `FMTnn`/`WARNnn` macro in `src/text.h` it stores) |
| `parse.ts` | `ReportParser` (streaming tokenizer), `ReportReducer`, `parseReport()` |
| `regenerate.ts` | `regenerateReport()` — model back to byte-exact classic text |
| `roundtrip.mjs` | parse → regenerate → byte-diff over a corpus of native reports |
| `make-profile-corpus.sh` | rewrites any set of `.inp` files to the profile and runs the engine to produce native reports |
| `selftest.mjs` | symmetry check for status-line shapes no test network triggers, plus byte-at-a-time streaming equivalence |
| `fixtures/synthetic.rpt` | the self-test's source-derived fixture (ill-conditioning grammar) |

## Usage

```ts
import { parseReport } from './parse.js';
import { regenerateReport } from './regenerate.js';

const { report, unrecognized } = parseReport(text);
report.statusLog[0].events;   // what the solver did at t=0
report.flowBalance?.flowRatio;
report.complete;              // saw "Analysis ended"

// streaming: feed chunks as they arrive (any chunking, mid-line is fine)
import { ReportParser, ReportReducer } from './parse.js';
const reducer = new ReportReducer();
const parser = new ReportParser((ev) => {
  reducer.handle(ev);          // or drive a live UI from ev directly
});
parser.write(chunk);
parser.end();

// lossless-capture proof / classic rendering
const text2 = regenerateReport(report);  // === text for profile reports
```

Build with `npm run build` (plain `tsc`, no dependencies); everything
compiles under `--strict`.

## Verification

`roundtrip.mjs` demands the regenerated text equal the native report
**byte for byte — no masking**: wall-clock stamps are stored raw, so
even the `Analysis begun/ended` lines must survive.  Any field the
parser drops, mangles, mis-times or mis-orders shows up as a byte
difference; any line it fails to classify is reported as `unrecognized`.

Current result over the profile corpus — 189 networks: the 56-network
OWA regression corpus + the three shipped examples + purpose-built
probes (halted unbalanced runs, PDA demand reductions, rule actions,
timer/clocktime controls, chem/age/trace quality, overflowing tanks,
abnormal pumps/valves, disconnections, and 13 networks with validation,
parse, rule and unlinked-node failures):

```
round trip: 189 reports, 189 byte-identical, 0 differ, 0 with unrecognized lines
```

To reproduce:

```
npm run build
./make-profile-corpus.sh /tmp/profile-corpus ../../build/bin/runepanet \
    ../../example-networks <corpus-dirs...>
node roundtrip.mjs /tmp/profile-corpus
node selftest.mjs /tmp/profile-corpus/Net3.rpt
```

Three status-line shapes appear in no engine-runnable network we could
construct, so `selftest.mjs` pins them from the C source instead
(regenerate → parse → regenerate must be byte-stable and the model
deep-equal): FMT61 `Valve <id> caused ill-conditioning`, FMT62
`System ill-conditioned at node <id>` (including its
no-trailing-blank warning grammar from `writehyderr()`), and FMT56
`setting changed to` — which is dead code in 2.3-dev (every
`writestatchange()` call site is guarded by an actual status change)
but kept defensively.

## Parsing notes (hard-won specifics)

- **Line frame.**  Every content line is `\n` + two spaces
  (`writeline()` in `src/report.c`); strip exactly those two, never
  `trim()` — status lines carry meaningful indentation, and echoed
  input lines must survive verbatim.
- **Blank lines are derived, not stored.**  They come in three widths,
  each with a known producer: 0-width from a `\n` embedded in a format
  string (FMT64 `Balancing the network:`, FMT68 head-error detail,
  ctime stamps, input2.c's echoed line), 2-space from `writeline("")`
  (end of logo), 3-space from `writeline(" ")` (status-header break,
  end of every step block, after a step's warnings).  The regenerator
  re-derives all of them; the ill-conditioned failure path
  (`writehyderr()` → `disconnected()`) is the one spot warnings get
  no trailing blank.
- **Times.**  Step times are `h:mm:ss`, hour unpadded and unbounded,
  right-aligned in 10 columns; stored as seconds.  Wall-clock stamps
  are raw `ctime()` text.
- **Timeless lines attach to the current step** — trials, convergence
  details, the intra-step `switched from` lines, and WARN03c.
- **Warnings carry their own timestamp in the text** and always trail
  their step (`writehydstat()` runs before `writehydwarn()`); they are
  gated by MESSAGES, not STATUS.
- **Rule actions are stamped with the *next* period's time** (emitted
  from inside `EN_nextH`), so they open that step's block — grouping by
  printed time reproduces the report exactly.
- **Diagnostics come in pairs.**  `input2.c` parse errors and
  `rules.c` rule errors are a message line plus an echo of the
  offending input line; the input2 echo keeps its `\n` (a 0-width blank
  follows), the rule echo does not.  The line-too-long variant (214)
  has no `Error NNN:` prefix.  A rule error is re-reported by the
  generic handler, so the same clause yields two pairs.  The full line
  text is stored verbatim in `ReportDiagnostic.text`, so regeneration
  never depends on the error-message catalogue.
- **Number formats.**  Convergence values are `%.4f` / `%-.6f`; tank
  levels and settings `%-.2f`; flow-balance terms `%12.3f`; mass terms
  `%12.5e` (C prints two exponent digits — JS `toExponential` needs
  padding); mass ratio `%-.5f`.  C prints `-0.00` for negative zero;
  JS `toFixed` drops the sign, so the formatter restores it (and
  `parseFloat('-0.00')` preserving `-0` is what makes the round trip
  hold).
- **Trailing newline.**  A completed report ends with the `\n` from the
  `Analysis ended` ctime; a failed one ends unterminated after its last
  error line.

## Relation to `../report-replication`

That harness replicates the report *from the toolkit API* (in C) to
find API gaps; this library replicates it *from the report text* (in
TypeScript) so epanet-js can present the same information dynamically
today, before any of `PROPOSED_API.md` lands.  Both are validated the
same way: byte-exact reproduction over the same network corpus.
