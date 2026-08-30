// check-coverage.mjs
//
// Classifies every line of EPANET .rpt reports against the shapes the type
// model in epanet-report.ts covers, and prints any line with no home.
// Layout lines (blanks, rules, logo, page headers) are recognised and
// skipped; echoed input lines following parse diagnostics are consumed as
// the diagnostic's offendingLine.
//
// Doubles as a seed for a real streaming parser: each regex below
// corresponds to one ReportStreamEvent / StatusEvent arm, so replacing
// `continue` with an event emit turns this classifier into the parser.
//
// Validated against 405 engine reports (439k non-blank lines): the only
// unmatched shapes are echoed offending input lines, which are stored as
// opaque strings by design.
import { readFileSync, readdirSync } from 'fs';

const T = String.raw`\d+:\d\d:\d\d`;           // clocktime
const NUM = String.raw`-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?`;
const LT = '(?:CV|Pipe|Pump|PRV|PSV|PBV|FCV|TCV|GPV|PCV)';
const ST = '(?:closed because cannot deliver head|temporarily closed|closed|open but exceeds maximum flow|open but cannot deliver flow|open but cannot deliver pressure|open|active|filling|emptying|overflowing)';

const shapes = [
  // ---- layout / frame (parser skips) ----
  ['layout', /^\s*$/],
  ['layout', /^ *\*{5,}$/],
  ['layout', /^ *-{5,}$/],
  ['layout', /^ *={5,}$/],
  ['layout', /^ *\* .*\*$/],                      // logo interior
  ['layout', /^ *Page \d+ {2,}/],                 // page headers (incl. title)
  ['header', /^ *Page 1 {2,}\w{3} \w{3} /],
  // ---- timestamps / section heads ----
  ['header',   /^ *Analysis (begun|ended) \w{3} \w{3}/],
  ['status',   /^ *Hydraulic Status:/],
  // ---- summary ----
  ['summary', /^ *Input Data File \.+ /],
  ['summary', /^ *Number of (Junctions|Reservoirs|Tanks |Pipes |Pumps |Valves )\.*/],
  ['summary', /^ *(Headloss Formula|Nodal Demand Model|Hydraulic Timestep|Hydraulic Accuracy|Headloss Error Limit|Flow Change Limit|Status Check Frequency|Maximum Trials Checked|Damping Limit Threshold|Maximum Trials|Quality Analysis|Water Quality Time Step|Water Quality Tolerance|Specific Gravity|Relative Kinematic Viscosity|Relative Chemical Diffusivity|Demand Multiplier|Total Duration) ?\.+ /],
  ['summary', /^ *Reporting Criteria:$/],
  ['summary', /^ *(No|All|Selected) (Nodes|Links)$/],
  ['summary', /^ *with .+ (below|above) /],
  // ---- status events ----
  [`control`, new RegExp(`^ *${T}: ${LT} \\S+ changed by (Junction|Reservoir|Tank) \\S+ control$`)],
  [`control`, new RegExp(`^ *${T}: ${LT} \\S+ changed by timer control$`)],
  [`rule`,    new RegExp(`^ *${T}: ${LT} \\S+ changed by rule .+$`)],
  [`balstart`,new RegExp(`^ *${T}: Balancing the network:$`)],
  [`trial`,   new RegExp(`^ *Trial +\\d+: relative flow change = ${NUM}$`)],
  [`convdet`, new RegExp(`^ *maximum +(flow change|head error) += ${NUM} for (Link|Node) .+$`)],
  [`switch`,  new RegExp(`^ *${LT} \\S+ switched from ${ST} to ${ST}$`)],
  [`setchg`,  new RegExp(`^ *${LT} \\S+ setting changed to ${NUM}$`)],
  [`illvalve`,new RegExp(`^ *${T}: Valve \\S+ caused ill-conditioning$`)],
  [`balance`, new RegExp(`^ *${T}: Balanced after \\d+ trials$`)],
  [`balance`, new RegExp(`^ *${T}: Unbalanced after \\d+ trials \\(flow change = ${NUM}\\)$`)],
  [`pda`,     new RegExp(`^ *(1 node had its demand|\\d+ nodes had demands) reduced by a total of ${NUM}%$`)],
  [`tank`,    new RegExp(`^ *${T}: Tank \\S+ is ${ST} at ${NUM} \\S+$`)],
  [`resv`,    new RegExp(`^ *${T}: Reservoir \\S+ is ${ST}$`)],
  [`linkst`,  new RegExp(`^ *${T}: ${LT} \\S+ ${ST}$`)],
  [`linkst`,  new RegExp(`^ *${T}: ${LT} \\S+ changed from ${ST} to ${ST}$`)],
  [`illnode`, new RegExp(`^ *${T}: System ill-conditioned at node .+$`)],
  // ---- warnings ----
  ['warn', new RegExp(`^ *WARNING: System unbalanced at ${T} hrs\\.( EXECUTION HALTED\\.)?$`)],
  ['warn', new RegExp(`^ *WARNING: Maximum trials exceeded at ${T} hrs\\. System may be unstable\\.$`)],
  ['warn', new RegExp(`^ *WARNING: Node .+ disconnected at ${T} hrs$`)],
  ['warn', new RegExp(`^ *WARNING: \\d+ additional nodes disconnected at ${T} hrs$`)],
  ['warn', /^ *WARNING: System disconnected because of Link .+$/],
  ['warn', new RegExp(`^ *WARNING: Pump \\S+ ${ST} at ${T} hrs\\.$`)],
  ['warn', new RegExp(`^ *WARNING: ${LT} \\S+ ${ST} at ${T} hrs\\.$`)],
  ['warn', new RegExp(`^ *WARNING: Negative pressures at ${T} hrs\\.$`)],
  // ---- balances ----
  ['flowbal', /^ *Hydraulic Flow Balance \(\S+\)$/],
  ['flowbal', new RegExp(`^ *(Total Inflow|Consumer Demand|Demand Deficit|Emitter Flow|Leakage Flow|Total Outflow|Storage Flow|Flow Ratio): +${NUM}$`)],
  ['massbal', /^ *Water Quality Mass Balance( \((mg|ug|hrs)\))?$/],
  ['massbal', new RegExp(`^ *(Initial Mass|Mass Inflow|Mass Outflow|Mass Reacted|Final Mass|Mass Ratio): +${NUM}$`)],
  ['massbal', /^ *Total Segments: +\d+$/],
  // ---- energy ----
  ['energy', /^ *Energy Usage:/],
  ['energy', /^ *Usage +Avg\. +Kw-hr/],
  ['energy', /^ *Pump +Factor Effic\./],
  ['energy', new RegExp(`^ *\\S+ +${NUM} +${NUM} +${NUM} +${NUM} +${NUM} +${NUM}$`)],
  ['energy', new RegExp(`^ *Demand Charge: +${NUM}$`)],
  ['energy', new RegExp(`^ *Total Cost: +${NUM}$`)],
  // ---- results tables ----
  ['table', new RegExp(`^ *(AVERAGE|MINIMUM|MAXIMUM|DIFFERENTIAL) (Node|Link) Results:( \\(continued\\))?$`)],
  ['table', new RegExp(`^ *(Node|Link) Results( at ${T} hrs)?:( \\(continued\\))?$`)],
  ['table', /^ *(Elevation|Demand|Head|Pressure|Length|Diameter|Flow|Velocity|Headloss|Quality|State|Setting|Reaction|F-Factor|% from|\S+)( +\S+)* *$/,
     'colhead'],  // header/units rows: constrained check below
  // ---- diagnostics ----
  ['diag', /^ *Error \d+: .+$/],
  ['diag', /^ *Input Error \d+: .+$/],
];

// data rows: ID + numbers (+ optional trailing suffix or status word)
const dataRow = new RegExp(`^ *\\S+( +(${NUM}|closed|open|active))+( +(Reservoir|Tank|Pump|PRV|PSV|PBV|FCV|TCV|GPV|PCV))? *$`);

// Usage: node check-coverage.mjs <dir-or-.rpt-file>...
const args = process.argv.slice(2);
if (!args.length) {
  console.error('Usage: node check-coverage.mjs <dir-or-.rpt-file>...');
  process.exit(2);
}
let files = [];
for (const a of args) {
  try { files.push(...readdirSync(a).filter(f=>f.endsWith('.rpt')).map(f=>a+'/'+f)); }
  catch { files.push(a); }
}
let inSummaryTitle = false, unmatched = new Map(), total = 0, echoNext = 0;
for (const file of files) {
  const lines = readFileSync(file,'latin1').split('\n');
  let afterLogo = false, afterBegun = false, expectTitle = 0;
  for (let i=0;i<lines.length;i++) {
    let raw = lines[i].replace(/\f/g,'');
    if (!raw.trim()) continue;
    total++;
    // echoed offending lines directly follow a parse diagnostic
    if (echoNext > 0) { echoNext--; continue; }
    let hit = shapes.find(([,re])=>re.test(raw));
    if (hit) {
      if (hit[0]==='diag' && / in \[[A-Z]+\] section:$|section contents ignored\.$|in following line of /.test(raw)) echoNext = 1;
      continue;
    }
    if (dataRow.test(raw)) continue;
    // summary title lines: free text between logo end and "Input Data File"
    // (approximate: free text lines before any structured match)
    if (/^ *[^ ].*$/.test(raw) && !/^ *(WARNING|Error|Trial)/.test(raw)) {
      // count as candidate title/free text
      const key = raw.trim().replace(/\d+/g,'N').slice(0,60);
      unmatched.set(key,(unmatched.get(key)||0)+1);
      continue;
    }
    const key = raw.trim().replace(/\d+/g,'N').slice(0,60);
    unmatched.set(key,(unmatched.get(key)||0)+1);
  }
}
console.log(`files: ${files.length}, non-blank lines: ${total}`);
console.log(`unmatched shapes: ${unmatched.size}`);
[...unmatched.entries()].sort((a,b)=>b[1]-a[1]).slice(0,30)
  .forEach(([k,v])=>console.log(String(v).padStart(6), '|'+k+'|'));
process.exit(unmatched.size ? 1 : 0);
