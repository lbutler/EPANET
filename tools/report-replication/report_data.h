/*
 ******************************************************************************
 Project:      OWA EPANET - Report Replication Test Harness
 Module:       report_data.h
 Description:  A renderer-independent data model holding every piece of
               information that appears in EPANET's native text report.

               The model is populated in collect.c using ONLY public toolkit
               API calls (see MISSING_API.md for the places where that is not
               yet possible and a documented workaround is used instead).

               Renderers consume this model to produce a report in any
               format.  render_text.c reproduces the classic .rpt text
               format 1:1 so it can be diffed against the engine's native
               report; an HTML/JSON renderer could consume the same struct.
 ******************************************************************************
*/

#ifndef REPORT_DATA_H
#define REPORT_DATA_H

#include <stdio.h>

#define RD_MAXID    64
#define RD_MAXLINE  512

/* Status report levels (mirrors EN_StatusReport) */
enum { RD_STATUS_NO = 0, RD_STATUS_YES = 1, RD_STATUS_FULL = 2 };

/* Node report variable slots (mirror the engine's ELEV..QUALITY fields) */
enum { RD_NF_ELEV = 0, RD_NF_DEMAND, RD_NF_HEAD, RD_NF_PRESSURE, RD_NF_QUALITY,
       RD_NODE_FIELDS };

/* Link report variable slots (mirror the engine's LENGTH..FRICTION fields) */
enum { RD_LF_LENGTH = 0, RD_LF_DIAM, RD_LF_FLOW, RD_LF_VELOCITY, RD_LF_HEADLOSS,
       RD_LF_QUALITY, RD_LF_STATUS, RD_LF_SETTING, RD_LF_REACTION, RD_LF_FRICTION,
       RD_LINK_FIELDS };

/* Metadata for one reportable variable column */
typedef struct {
    char   name[RD_MAXID];   /* column heading, e.g. "Pressure"              */
    char   units[RD_MAXID];  /* units heading, e.g. "psi"                    */
    int    enabled;        /* include column in report tables                */
    int    precision;      /* decimal places                                 */
    double lowLimit;       /* only report items with value below this limit  */
    double highLimit;      /* only report items with value above this limit  */
    int    hasLowLimit;
    int    hasHighLimit;
} RD_Field;

/* One node's results at one reporting period (already in report units).
   Stored as float because the engine stores REAL4 in its output file and
   prints those single-precision values in the report tables.               */
typedef struct {
    float demand;
    float head;
    float pressure;
    float quality;
} RD_NodeResult;

/* One link's results at one reporting period (already in report units).
   reaction & friction have NO public API - see MISSING_API.md; they are
   only needed when those non-default report columns are enabled.           */
typedef struct {
    float flow;
    float velocity;
    float headloss;    /* per 1000 length units for pipes, head units else  */
    float quality;
    float status;      /* engine status code: 2=closed 3=open 4=active      */
    float setting;
} RD_LinkResult;

/* Cumulative end-of-run energy usage for one pump (report units) */
typedef struct {
    char   linkId[RD_MAXID];
    double usageFactor;   /* % of time online                                */
    double avgEffic;      /* %                                               */
    double kwhPerFlow;    /* kWh/MGal (US) or kWh/m3 (SI)                    */
    double avgKw;
    double peakKw;
    double costPerDay;
} RD_PumpEnergy;

/* A warning code raised by EN_runH at a given simulation time */
typedef struct {
    long  time;           /* seconds                                         */
    int   code;           /* EN_runH return code (1..6)                      */
} RD_Warning;

/* ---- hydraulic status report (STATUS YES / FULL) --------------------- */

/* Engine-internal link/tank status codes (StatusType in src/types.h), used
   verbatim by the status report's text (StatTxt in src/enumstxt.h).       */
enum { RD_XHEAD = 0, RD_TEMPCLOSED, RD_CLOSED, RD_OPEN, RD_ACTIVE, RD_XFLOW,
       RD_XFCV, RD_XPRESSURE, RD_FILLING, RD_EMPTYING, RD_OVERFLOWING };

/* One line of the hydraulic status report, kept as structured data rather
   than text so any renderer can present it its own way.                   */
enum {
    RD_EV_CONTROL = 0,  /* FMT54/55  simple control changed a link         */
    RD_EV_RULE,         /* FMT63     rule changed a link                   */
    RD_EV_BALANCE,      /* FMT58/59  balanced / unbalanced after N trials  */
    RD_EV_DEFICIENT,    /* FMT69a/b  PDA demand reduction                  */
    RD_EV_TANK,         /* FMT50/51  tank / reservoir status transition    */
    RD_EV_LINK,         /* FMT52/53  link status transition                */
    RD_EV_WARNING,      /* WARN01..WARN06                                  */
    RD_EV_BLANK         /* the " " line that closes each status block      */
};

/* Warning kinds, so a renderer can format them without parsing text */
enum {
    RD_WARN_UNBALANCED = 1,  /* WARN01 */
    RD_WARN_UNSTABLE,        /* WARN02 */
    RD_WARN_PUMP,            /* WARN04 */
    RD_WARN_VALVE,           /* WARN05 */
    RD_WARN_NEGPRESSURE      /* WARN06 */
};

typedef struct {
    int    type;          /* RD_EV_*                                       */
    long   time;          /* simulation time, seconds                      */
    int    index;         /* link index (0-based) for link/control events  */
    int    nodeIndex;     /* controlling / affected node (0-based)         */
    int    oldStatus;     /* engine status code before the change          */
    int    newStatus;     /* engine status code after the change           */
    int    isTimerControl;/* control event: timer/time-of-day rather than
                             a node-level control                          */
    int    iters;         /* balance event: trials taken                   */
    double relerr;        /* balance event: relative flow change           */
    int    balanced;      /* balance event: converged within accuracy      */
    double level;         /* tank event: water level in head units         */
    int    count;         /* deficient nodes, or warning element index     */
    double reduction;     /* PDA demand reduction, percent                 */
    int    warnKind;      /* RD_WARN_*                                     */
    int    warnStatus;    /* pump/valve status code behind a warning       */
    char   text[RD_MAXID];/* rule ID                                       */
} RD_StatusEvent;

/* End-of-run hydraulic flow balance block (flow units) */
typedef struct {
    int    valid;
    double totalInflow, consumerDemand, deficitDemand, emitterDemand;
    double leakageDemand, totalOutflow, storageDemand, ratio;
} RD_FlowBalance;

/* End-of-run water quality mass balance block.
   GAP: only `ratio` is obtainable through the public API - see
   MISSING_API.md; the mass terms live inside the quality solver.          */
typedef struct {
    int    valid;
    int    haveMasses;    /* 0 = mass terms unavailable through the API    */
    double initial, inflow, outflow, reacted, final, ratio;
    int    segCount;
} RD_MassBalance;

typedef struct RD_ReportData {

    /* ---- program information -------------------------------------- */
    int    versionMajor, versionMinor, versionPatch;
    char   title[3][RD_MAXLINE];
    char   inpFname[RD_MAXLINE];

    /* ---- network size ---------------------------------------------- */
    int    nNodes, nLinks;
    int    nJuncs, nReservoirs, nTanks;
    int    nPipes, nPumps, nValves;

    /* ---- unit system ------------------------------------------------ */
    int    flowUnits;        /* EN_FlowUnits code                            */
    int    usUnits;          /* 1 = US customary, 0 = SI                     */
    int    pressUnits;       /* EN_PressUnits code                           */

    /* ---- hydraulic options ------------------------------------------ */
    int    headlossForm;     /* EN_HeadLossType code                         */
    int    demandModel;      /* EN_DemandModel code                          */
    long   hydStep;          /* seconds                                      */
    double accuracy;
    double headErrorLimit;   /* head units                                   */
    double flowChangeLimit;  /* flow units                                   */
    int    checkFreq, maxCheck;
    double dampLimit;
    int    maxIter;
    double spGrav, relViscosity, relDiffusivity, demandMult;
    long   duration;         /* seconds                                      */

    /* ---- water quality options -------------------------------------- */
    int    qualType;         /* EN_QualityType code                          */
    char   chemName[RD_MAXID], chemUnits[RD_MAXID], traceNode[RD_MAXID];
    long   qualStep;         /* seconds                                      */
    double qualTolerance;    /* quality units                                */

    /* ---- report settings -------------------------------------------- */
    int    statusLevel;      /* RD_STATUS_*; readable: EN_getoption(EN_STATUS_REPORT) */
    int    summaryFlag;      /* workaround: sniffed from INP [REPORT]        */
    int    energyFlag;       /* workaround: sniffed from INP [REPORT]        */
    int    messageFlag;      /* workaround: sniffed from INP [REPORT]        */
    int    nodeRptFlag;      /* 0=none 1=all 2=selected (sniffed)            */
    int    linkRptFlag;      /* 0=none 1=all 2=selected (sniffed)            */
    /* the engine writes the summary during EN_open, so its "Reporting
       Criteria" lines reflect the settings at open time even when report
       commands are issued afterwards; snapshot of those open-time flags:  */
    int    summaryNodeRptFlag;
    int    summaryLinkRptFlag;
    int    pageSize;         /* workaround: sniffed from INP [REPORT]        */
    int    tstatFlag;        /* EN_gettimeparam(EN_STATISTIC)                */
    RD_Field nodeField[RD_NODE_FIELDS];
    RD_Field linkField[RD_LINK_FIELDS];

    /* ---- static element data ---------------------------------------- */
    char  (*nodeId)[RD_MAXID];
    int    *nodeType;        /* EN_NodeType                                  */
    float  *nodeElev;        /* report units                                 */
    char   *nodeRpt;         /* 1 = individually selected for reporting      */
    char  (*linkId)[RD_MAXID];
    int    *linkType;        /* EN_LinkType                                  */
    float  *linkLen;         /* report units                                 */
    float  *linkDiam;        /* report units                                 */
    char   *linkRpt;

    /* ---- time series results ---------------------------------------- */
    long   rptStart, rptStep;   /* seconds                                   */
    int    nPeriods;            /* number of stored reporting periods        */
    int    nAllocPeriods;
    RD_NodeResult **nodeRes;    /* [period][node 0-based]                    */
    RD_LinkResult **linkRes;    /* [period][link 0-based]                    */

    /* ---- energy usage ------------------------------------------------ */
    RD_PumpEnergy *pumpEnergy;  /* [nPumps]                                  */
    double demandCharge;        /* CORRECT value: peak system kW x rate      */
    double totalEnergyCost;     /* CORRECT value: sum of costs + charge      */
    /* ENGINE QUIRK (see MISSING_API.md "Engine bugs found"): the native
       report multiplies the demand charge by the rate twice (savenergy()
       scales Emax by Dcost when saving the binary epilog, writeenergy()
       multiplies by Dcost again).  The text renderer mirrors that using
       this rate so the reports stay byte-identical; other renderers
       should use the correct values above.                                */
    double demandChargeRate;

    /* ---- solver warnings --------------------------------------------- */
    RD_Warning *warnings;
    int    nWarnings;
    int    warnflag;            /* worst warning code from the run           */

    /* Best-effort reconstruction of the WARNING lines the engine writes
       between "Analysis begun/ended".  Built in collect.c by probing the
       API after each warned hydraulic step, mirroring writehydwarn() in
       src/report.c.  GAP: abnormal valve states (WARN05) and disconnected
       node details (WARN03a/b/c) cannot be reconstructed - see
       MISSING_API.md.                                                     */
    char **warnLines;
    int    nWarnLines;

    /* ---- hydraulic status report (STATUS YES / FULL) ---------------- */
    RD_StatusEvent *events;
    int    nEvents;
    int    nAllocEvents;
    RD_FlowBalance flowBalance;
    RD_MassBalance massBalance;
} RD_ReportData;

/* collect.c */
int  rd_collect(RD_ReportData *rd, const char *inpFile, const char *rptFile,
                int statusLevel, const char **setCmds, int nSetCmds,
                char *errmsg, int errlen);
void rd_free(RD_ReportData *rd);

/* inp_sniff.c - GAP WORKAROUND (see MISSING_API.md):
   the toolkit has no getters for [REPORT] page/summary/messages/energy/
   nodes/links settings, so they are re-parsed from the INP file here.      */
int  rd_sniff_inp_report_settings(RD_ReportData *rd, const char *inpFile);
int  rd_apply_report_command(RD_ReportData *rd, const char *command);

/* render_text.c - renders the model in EPANET's native .rpt text format   */
int  rd_render_text(const RD_ReportData *rd, FILE *f);

#endif
