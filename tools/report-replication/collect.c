/*
 ******************************************************************************
 Project:      OWA EPANET - Report Replication Test Harness
 Module:       collect.c
 Description:  Populates the RD_ReportData model using ONLY public toolkit
               API calls, while also driving the native simulation so the
               engine writes its own report for comparison.

               Every place where the public API cannot supply a value that
               the native report contains is logged in MISSING_API.md and
               marked with a "GAP:" comment here.
 ******************************************************************************
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "epanet2_2.h"
#include "report_data.h"

/* Conversion factors from user flow units to cfs (same constants the engine
   uses in src/types.h; pure units math, not model data) */
static const double FLOW_PER_CFS[] = {
    1.0,      /* EN_CFS  */
    448.831,  /* EN_GPM  */
    0.64632,  /* EN_MGD  */
    0.5382,   /* EN_IMGD */
    1.9837,   /* EN_AFD  */
    28.317,   /* EN_LPS  */
    1699.0,   /* EN_LPM  */
    2.4466,   /* EN_MLD  */
    101.94,   /* EN_CMH  */
    2446.6,   /* EN_CMD  */
    0.028317  /* EN_CMS  */
};
#define QZERO_CFS 1.e-6   /* zero-flow threshold used by the engine */
#define PI_CONST  3.141592654
#define RD_MISSING_SET (-1.e10)   /* the engine's MISSING link setting */
#define LPSperCFS 28.317
#define GPMperCFS 448.831

static const char *FIELD_NODE_NAMES[RD_NODE_FIELDS] =
    { "Elevation", "Demand", "Head", "Pressure", "Quality" };
static const char *FIELD_LINK_NAMES[RD_LINK_FIELDS] =
    { "Length", "Diameter", "Flow", "Velocity", "Headloss",
      "Quality", "State", "Setting", "Reaction", "F-Factor" };
static const char *FLOW_UNITS_TXT[] =
    { "cfs", "gpm", "mgd", "Imgd", "a-f/d",
      "L/s", "Lpm", "ML/d", "m3/h", "m3/d", "m3/s" };
static const char *PRESS_UNITS_TXT[] =
    { "PSI", "KPA", "METERS", "BAR", "FEET" };

#define CHECK(x) do { int _err = (x); if (_err > 100) { \
    snprintf(errmsg, errlen, "API error %d at %s:%d", _err, __FILE__, __LINE__); \
    return _err; } else if (_err > 0 && _err > warncode) warncode = _err; } while (0)

/* --------------------------------------------------------------------------
   Per-pump energy accumulators.  GAP: the engine accumulates these same
   quantities internally (Spump.Energy) but the toolkit offers no getter for
   the cumulative end-of-run values that the Energy Usage table prints, so
   they are re-integrated here from instantaneous EN_ENERGY / EN_PUMP_EFFIC
   readings at every hydraulic step (mirrors addenergy() in src/hydraul.c).
   ------------------------------------------------------------------------ */
typedef struct {
    int    linkIndex;
    double ecost;       /* pump-specific energy price (0 = use global)      */
    int    epat;        /* pump-specific price pattern index                 */
    double timeOnLine;  /* hrs                                               */
    double effic;       /* sum of e*dt                                       */
    double kwhPerFlow;  /* sum of (kw / cfs) * dt                            */
    double kwHrs;       /* sum of kw*dt                                      */
    double maxKwatts;
    double totalCost;
} EnergyAcc;

static double patternFactor(EN_Project ph, int pat, long n)
{
    int len = 0;
    double f = 1.0;
    if (pat <= 0) return 1.0;
    if (EN_getpatternlen(ph, pat, &len) > 0 || len <= 0) return 1.0;
    EN_getpatternvalue(ph, pat, (int)(n % (long)len) + 1, &f);
    return f;
}

static void accumulateEnergy(EN_Project ph, RD_ReportData *rd, EnergyAcc *acc,
                             double *emax, long t, long tstep,
                             double globalPrice, int globalPat,
                             long pstart, long pstep)
{
    int j;
    double dt, psum = 0.0;
    long n;

    /* mirrors addenergy() in src/hydraul.c */
    if (rd->duration == 0) dt = 1.0;
    else if (t < rd->duration) dt = (double)tstep / 3600.0;
    else dt = 0.0;
    if (dt == 0.0) return;
    n = (t + pstart) / pstep;

    for (j = 0; j < rd->nPumps; j++)
    {
        double p = 0.0, e = 0.0, q = 0.0, c, f;
        EN_getlinkvalue(ph, acc[j].linkIndex, EN_PUMP_EFFIC, &e);
        if (e == 0.0) continue;              /* pump closed this interval */
        EN_getlinkvalue(ph, acc[j].linkIndex, EN_ENERGY, &p);
        EN_getlinkvalue(ph, acc[j].linkIndex, EN_FLOW, &q);
        q = fabs(q) / FLOW_PER_CFS[rd->flowUnits];   /* back to cfs */
        if (q < QZERO_CFS) q = QZERO_CFS;

        c = (acc[j].ecost > 0.0) ? acc[j].ecost : globalPrice;
        f = (acc[j].epat > 0) ? patternFactor(ph, acc[j].epat, n)
                              : patternFactor(ph, globalPat, n);
        c *= f;

        psum += p;
        acc[j].timeOnLine += dt;
        acc[j].effic += e * dt;
        acc[j].kwhPerFlow += p / q * dt;
        acc[j].kwHrs += p * dt;
        if (p > acc[j].maxKwatts) acc[j].maxKwatts = p;
        acc[j].totalCost += c * p * dt;
    }
    if (psum > *emax) *emax = psum;
}

static void finalizeEnergy(RD_ReportData *rd, EnergyAcc *acc, double emax,
                           double demandChargeRate)
{
    /* mirrors savenergy() in src/output.c */
    int j;
    double hdur = (double)rd->duration / 3600.0;
    double csum = 0.0;

    for (j = 0; j < rd->nPumps; j++)
    {
        RD_PumpEnergy *pe = &rd->pumpEnergy[j];
        double t = acc[j].timeOnLine;
        double effic = acc[j].effic, kwhPerFlow = acc[j].kwhPerFlow;
        double kwHrs = acc[j].kwHrs, totalCost = acc[j].totalCost;

        if (hdur == 0.0)
        {
            totalCost *= 24.0;
        }
        else
        {
            if (t > 0.0)
            {
                effic /= t;
                kwhPerFlow /= t;
                kwHrs /= t;
            }
            t /= hdur;
            totalCost *= 24.0 / hdur;
        }
        pe->usageFactor = t * 100.0;
        pe->avgEffic = effic * 100.0;
        if (!rd->usUnits) kwhPerFlow *= (1000. / LPSperCFS / 3600.);
        else              kwhPerFlow *= (1.0e6 / GPMperCFS / 60.);
        pe->kwhPerFlow = kwhPerFlow;
        pe->avgKw = kwHrs;
        pe->peakKw = acc[j].maxKwatts;
        pe->costPerDay = totalCost;
        csum += totalCost;
    }
    rd->demandCharge = emax * demandChargeRate;
    rd->totalEnergyCost = csum + rd->demandCharge;
    rd->demandChargeRate = demandChargeRate;
}

/* --------------------------------------------------------------------------
   Result sampling at reporting times
   ------------------------------------------------------------------------ */
static int growPeriods(RD_ReportData *rd)
{
    if (rd->nPeriods < rd->nAllocPeriods) return 0;
    {
        int newAlloc = rd->nAllocPeriods == 0 ? 32 : rd->nAllocPeriods * 2;
        RD_NodeResult **nr = realloc(rd->nodeRes, newAlloc * sizeof(*nr));
        RD_LinkResult **lr = realloc(rd->linkRes, newAlloc * sizeof(*lr));
        if (!nr || !lr) return 101;
        rd->nodeRes = nr;
        rd->linkRes = lr;
        rd->nAllocPeriods = newAlloc;
    }
    return 0;
}

static int samplePeriod(EN_Project ph, RD_ReportData *rd)
{
    int i, err;
    RD_NodeResult *nr;
    RD_LinkResult *lr;

    if ((err = growPeriods(rd)) > 0) return err;
    nr = calloc(rd->nNodes, sizeof(RD_NodeResult));
    lr = calloc(rd->nLinks, sizeof(RD_LinkResult));
    if (!nr || !lr) { free(nr); free(lr); return 101; }

    for (i = 0; i < rd->nNodes; i++)
    {
        double d = 0, h = 0, p = 0, q = 0;
        EN_getnodevalue(ph, i + 1, EN_DEMAND, &d);
        EN_getnodevalue(ph, i + 1, EN_HEAD, &h);
        EN_getnodevalue(ph, i + 1, EN_PRESSURE, &p);
        EN_getnodevalue(ph, i + 1, EN_QUALITY, &q);
        /* stored as float: the engine stores REAL4 in its output file and
           the native tables print those single-precision values          */
        nr[i].demand = (float)d;
        nr[i].head = (float)h;
        nr[i].pressure = (float)p;
        nr[i].quality = (float)q;
    }
    for (i = 0; i < rd->nLinks; i++)
    {
        double f = 0, v = 0, hl = 0, q = 0, s = 0, st = 0;
        EN_getlinkvalue(ph, i + 1, EN_FLOW, &f);
        EN_getlinkvalue(ph, i + 1, EN_VELOCITY, &v);
        EN_getlinkvalue(ph, i + 1, EN_HEADLOSS, &hl);
        EN_getlinkvalue(ph, i + 1, EN_LINKQUAL, &q);
        EN_getlinkvalue(ph, i + 1, EN_SETTING, &s);
        EN_getlinkvalue(ph, i + 1, EN_STATUS, &st);
        lr[i].flow = (float)f;
        lr[i].velocity = (float)v;
        /* The native table reports unit headloss (per 1000 length units)
           for pipes; EN_HEADLOSS returns total headloss, so convert using
           the pipe's length.  GAP: no direct "unit headloss" property.   */
        if (rd->linkType[i] <= EN_PIPE && rd->linkLen[i] > 0.0)
            lr[i].headloss = (float)(1000.0 * hl / rd->linkLen[i]);
        else
            lr[i].headloss = (float)hl;
        lr[i].quality = (float)q;
        /* map API status (0 closed,1 open,2 active) to the engine's
           internal codes used by the report's status column             */
        lr[i].status = (float)(st == 0.0 ? 2 : (st == 2.0 ? 4 : 3));
        lr[i].setting = (float)s;
    }
    rd->nodeRes[rd->nPeriods] = nr;
    rd->linkRes[rd->nPeriods] = lr;
    rd->nPeriods++;
    return 0;
}

/* Post-process the sampled series into a single statistic period, mirroring
   savetimestat() in src/output.c (float arithmetic to match REAL4 stats). */
static void applyTimeStatistics(RD_ReportData *rd)
{
    int nvals = rd->nPeriods;
    int i, p;
    if (rd->tstatFlag == EN_SERIES || nvals <= 1) return;

#define STAT_LOOP(TYPE, COUNT, FIELD)                                       \
    for (i = 0; i < (COUNT); i++)                                           \
    {                                                                       \
        float sum = 0.0f, mn = 0.0f, mx = 0.0f, xx;                         \
        for (p = 0; p < nvals; p++)                                         \
        {                                                                   \
            xx = rd->TYPE##Res[p][i].FIELD;                                 \
            if (p == 0) { mn = mx = xx; }                                   \
            if (xx < mn) mn = xx;                                           \
            if (xx > mx) mx = xx;                                           \
            sum += xx;                                                      \
        }                                                                   \
        switch (rd->tstatFlag)                                              \
        {                                                                   \
            case EN_AVERAGE: xx = sum / (float)nvals; break;                \
            case EN_MINIMUM: xx = mn; break;                                \
            case EN_MAXIMUM: xx = mx; break;                                \
            default:         xx = mx - mn; break;                           \
        }                                                                   \
        rd->TYPE##Res[0][i].FIELD = xx;                                     \
    }

    /* mirror savetimestat()'s special cases: link flows are averaged as
       absolute values; link status is first collapsed to open(1)/closed(0)
       and the statistic mapped back to a status code                     */
    for (p = 0; p < nvals; p++)
        for (i = 0; i < rd->nLinks; i++)
        {
            rd->linkRes[p][i].flow = (float)fabs(rd->linkRes[p][i].flow);
            rd->linkRes[p][i].status =
                (rd->linkRes[p][i].status >= 3.0f) ? 1.0f : 0.0f;
        }

    STAT_LOOP(node, rd->nNodes, demand)
    STAT_LOOP(node, rd->nNodes, head)
    STAT_LOOP(node, rd->nNodes, pressure)
    STAT_LOOP(node, rd->nNodes, quality)
    STAT_LOOP(link, rd->nLinks, flow)
    STAT_LOOP(link, rd->nLinks, velocity)
    STAT_LOOP(link, rd->nLinks, headloss)
    STAT_LOOP(link, rd->nLinks, quality)
    STAT_LOOP(link, rd->nLinks, status)
    STAT_LOOP(link, rd->nLinks, setting)
#undef STAT_LOOP

    for (i = 0; i < rd->nLinks; i++)
        rd->linkRes[0][i].status =
            (rd->linkRes[0][i].status < 0.5f) ? 2.0f : 3.0f;

    for (p = 1; p < nvals; p++)
    {
        free(rd->nodeRes[p]);
        free(rd->linkRes[p]);
    }
    rd->nPeriods = 1;
}

/* --------------------------------------------------------------------------
   Static data collection
   ------------------------------------------------------------------------ */
static int collectStatic(EN_Project ph, RD_ReportData *rd,
                         char *errmsg, int errlen)
{
    int i, n, warncode = 0;
    int version;
    double v;

    EN_getversion(&version);
    rd->versionMajor = version / 10000;
    rd->versionMinor = (version % 10000) / 100;
    rd->versionPatch = version % 100;

    CHECK(EN_gettitle(ph, rd->title[0], rd->title[1], rd->title[2]));

    CHECK(EN_getcount(ph, EN_NODECOUNT, &rd->nNodes));
    CHECK(EN_getcount(ph, EN_LINKCOUNT, &rd->nLinks));
    CHECK(EN_getflowunits(ph, &rd->flowUnits));
    rd->usUnits = (rd->flowUnits <= EN_AFD);
    CHECK(EN_getoption(ph, EN_PRESS_UNITS, &v)); rd->pressUnits = (int)v;

    CHECK(EN_getoption(ph, EN_HEADLOSSFORM, &v)); rd->headlossForm = (int)v;
    {
        double pmin, preq, pexp;
        CHECK(EN_getdemandmodel(ph, &rd->demandModel, &pmin, &preq, &pexp));
    }
    CHECK(EN_gettimeparam(ph, EN_HYDSTEP, &rd->hydStep));
    CHECK(EN_gettimeparam(ph, EN_QUALSTEP, &rd->qualStep));
    CHECK(EN_gettimeparam(ph, EN_DURATION, &rd->duration));
    CHECK(EN_gettimeparam(ph, EN_REPORTSTART, &rd->rptStart));
    CHECK(EN_gettimeparam(ph, EN_REPORTSTEP, &rd->rptStep));
    {
        long stat;
        CHECK(EN_gettimeparam(ph, EN_STATISTIC, &stat));
        rd->tstatFlag = (int)stat;
    }
    CHECK(EN_getoption(ph, EN_ACCURACY, &rd->accuracy));
    CHECK(EN_getoption(ph, EN_HEADERROR, &rd->headErrorLimit));
    CHECK(EN_getoption(ph, EN_FLOWCHANGE, &rd->flowChangeLimit));
    CHECK(EN_getoption(ph, EN_CHECKFREQ, &v)); rd->checkFreq = (int)v;
    CHECK(EN_getoption(ph, EN_MAXCHECK, &v)); rd->maxCheck = (int)v;
    CHECK(EN_getoption(ph, EN_DAMPLIMIT, &rd->dampLimit));
    CHECK(EN_getoption(ph, EN_TRIALS, &v)); rd->maxIter = (int)v;
    CHECK(EN_getoption(ph, EN_SP_GRAVITY, &rd->spGrav));
    CHECK(EN_getoption(ph, EN_SP_VISCOS, &rd->relViscosity));
    CHECK(EN_getoption(ph, EN_SP_DIFFUS, &rd->relDiffusivity));
    CHECK(EN_getoption(ph, EN_DEMANDMULT, &rd->demandMult));
    CHECK(EN_getoption(ph, EN_TOLERANCE, &rd->qualTolerance));

    {
        int traceNode = 0;
        CHECK(EN_getqualinfo(ph, &rd->qualType, rd->chemName, rd->chemUnits,
                             &traceNode));
        if (rd->qualType == EN_TRACE && traceNode > 0)
            CHECK(EN_getnodeid(ph, traceNode, rd->traceNode));
        /* GAP: for TRACE analyses EN_getqualinfo returns the generic pair
           "TRACE"/"% from", but the report's quality column is titled
           "% from" over the trace node's ID (input3.c sets ChemName =
           "% from", ChemUnits = <trace node>); reconstruct that here     */
        if (rd->qualType == EN_TRACE)
        {
            snprintf(rd->chemName, sizeof(rd->chemName), "%s", "% from");
            snprintf(rd->chemUnits, sizeof(rd->chemUnits), "%s",
                     rd->traceNode);
        }
    }

    /* status level IS readable through the API */
    CHECK(EN_getoption(ph, EN_STATUS_REPORT, &v));
    rd->statusLevel = (int)v;

    /* elements */
    rd->nodeId = calloc(rd->nNodes, sizeof(*rd->nodeId));
    rd->nodeType = calloc(rd->nNodes, sizeof(int));
    rd->nodeElev = calloc(rd->nNodes, sizeof(float));
    rd->nodeRpt = calloc(rd->nNodes, 1);
    rd->linkId = calloc(rd->nLinks, sizeof(*rd->linkId));
    rd->linkType = calloc(rd->nLinks, sizeof(int));
    rd->linkLen = calloc(rd->nLinks, sizeof(float));
    rd->linkDiam = calloc(rd->nLinks, sizeof(float));
    rd->linkRpt = calloc(rd->nLinks, 1);
    if (!rd->nodeId || !rd->nodeType || !rd->nodeElev || !rd->nodeRpt ||
        !rd->linkId || !rd->linkType || !rd->linkLen || !rd->linkDiam ||
        !rd->linkRpt)
    {
        snprintf(errmsg, errlen, "out of memory");
        return 101;
    }

    for (i = 1; i <= rd->nNodes; i++)
    {
        CHECK(EN_getnodeid(ph, i, rd->nodeId[i - 1]));
        CHECK(EN_getnodetype(ph, i, &rd->nodeType[i - 1]));
        CHECK(EN_getnodevalue(ph, i, EN_ELEVATION, &v));
        rd->nodeElev[i - 1] = (float)v;
        switch (rd->nodeType[i - 1])
        {
            case EN_JUNCTION:  rd->nJuncs++; break;
            case EN_RESERVOIR: rd->nReservoirs++; break;
            default:           rd->nTanks++; break;
        }
    }
    for (i = 1; i <= rd->nLinks; i++)
    {
        CHECK(EN_getlinkid(ph, i, rd->linkId[i - 1]));
        CHECK(EN_getlinktype(ph, i, &rd->linkType[i - 1]));
        CHECK(EN_getlinkvalue(ph, i, EN_LENGTH, &v));
        rd->linkLen[i - 1] = (float)v;
        CHECK(EN_getlinkvalue(ph, i, EN_DIAMETER, &v));
        rd->linkDiam[i - 1] = (float)v;
        switch (rd->linkType[i - 1])
        {
            case EN_CVPIPE:
            case EN_PIPE: rd->nPipes++; break;
            case EN_PUMP: rd->nPumps++; break;
            default:      rd->nValves++; break;
        }
    }

    /* report variable fields: defaults mirror initreport()/initunits() in
       src/input1.c.  GAP: [REPORT] PRECISION / field enabling / BELOW /
       ABOVE settings are not readable through the API; defaults assumed
       (the INP sniffer warns when a network customizes them).            */
    for (i = 0; i < RD_NODE_FIELDS; i++)
    {
        RD_Field *fl = &rd->nodeField[i];
        snprintf(fl->name, sizeof(fl->name), "%s", FIELD_NODE_NAMES[i]);
        fl->enabled = (i >= RD_NF_DEMAND);
        fl->precision = 2;
    }
    if (rd->qualType == EN_NONE) rd->nodeField[RD_NF_QUALITY].enabled = 0;
    for (i = 0; i < RD_LINK_FIELDS; i++)
    {
        RD_Field *fl = &rd->linkField[i];
        snprintf(fl->name, sizeof(fl->name), "%s", FIELD_LINK_NAMES[i]);
        fl->enabled = (i >= RD_LF_FLOW && i <= RD_LF_HEADLOSS);
        fl->precision = (i == RD_LF_FRICTION) ? 3 : 2;
    }

    n = rd->flowUnits;
    if (rd->usUnits)
    {
        strcpy(rd->nodeField[RD_NF_ELEV].units, "ft");
        strcpy(rd->nodeField[RD_NF_HEAD].units, "ft");
        strcpy(rd->linkField[RD_LF_LENGTH].units, "ft");
        strcpy(rd->linkField[RD_LF_DIAM].units, "in");
        strcpy(rd->linkField[RD_LF_VELOCITY].units, "fps");
        strcpy(rd->linkField[RD_LF_HEADLOSS].units, "/1000ft");
    }
    else
    {
        strcpy(rd->nodeField[RD_NF_ELEV].units, "m");
        strcpy(rd->nodeField[RD_NF_HEAD].units, "m");
        strcpy(rd->linkField[RD_LF_LENGTH].units, "m");
        strcpy(rd->linkField[RD_LF_DIAM].units, "mm");
        strcpy(rd->linkField[RD_LF_VELOCITY].units, "m/s");
        strcpy(rd->linkField[RD_LF_HEADLOSS].units, "/1000m");
    }
    snprintf(rd->nodeField[RD_NF_DEMAND].units,
             sizeof(rd->nodeField[RD_NF_DEMAND].units), "%s",
             FLOW_UNITS_TXT[n]);
    snprintf(rd->linkField[RD_LF_FLOW].units,
             sizeof(rd->linkField[RD_LF_FLOW].units), "%s",
             FLOW_UNITS_TXT[n]);
    snprintf(rd->nodeField[RD_NF_PRESSURE].units,
             sizeof(rd->nodeField[RD_NF_PRESSURE].units), "%s",
             PRESS_UNITS_TXT[rd->pressUnits]);
    /* the summary's quality-tolerance units and the node table's quality
       column FIELD units follow initunits() in src/input1.c: chem units
       for CHEM, "hrs" for AGE, "% from" for TRACE (the table column
       HEADER separately shows chemName over chemUnits)                   */
    switch (rd->qualType)
    {
        case EN_CHEM:
            snprintf(rd->nodeField[RD_NF_QUALITY].units,
                     sizeof(rd->nodeField[RD_NF_QUALITY].units), "%s",
                     rd->chemUnits);
            break;
        case EN_AGE:
            strcpy(rd->nodeField[RD_NF_QUALITY].units, "hrs");
            break;
        case EN_TRACE:
            strcpy(rd->nodeField[RD_NF_QUALITY].units, "% from");
            break;
        default:
            rd->nodeField[RD_NF_QUALITY].units[0] = '\0';
            break;
    }
    /* Field[LINKQUAL].Units is never assigned by the engine (stays "") */
    rd->linkField[RD_LF_QUALITY].units[0] = '\0';

    (void)warncode;
    return 0;
}

/* --------------------------------------------------------------------------
   Hydraulic status report (STATUS YES / FULL)

   The engine writes these lines from inside the solver:
     runhyd()  -> controls()      : FMT54/55 control actions  (BEFORE the solve)
               -> writehydstat()  : FMT58/59, FMT69a/b, FMT50/51, FMT52/53
               -> writehydwarn()  : WARN01..WARN06
     nexthyd() -> writeflowbalance() at the end of the run
   A stepwise API caller has no observation point between controls() and the
   solve, so control actions are PREDICTED from the previous step's cached
   state (mirroring controls() in src/hydraul.c); everything else is probed
   after EN_runH.  See MISSING_API.md.
   ------------------------------------------------------------------------ */

/* Per-run state carried between steps by the status report */
typedef struct {
    double  hcf;            /* internal ft -> user head units (1 or 0.3048)  */
    double  qcf;            /* user flow units per cfs                       */
    long    startTime;      /* EN_STARTTIME, for time-of-day controls        */
    int    *oldLinkStatus;  /* [nLinks] mirrors hyd->OldStatus for links     */
    int    *curLinkStatus;  /* [nLinks] mirrors hyd->LinkStatus during
                               control evaluation within one step          */
    int    *oldTankStatus;  /* [nNodes] mirrors hyd->OldStatus for tanks     */
    double *prevHead;       /* [nNodes] head after the previous solve        */
    double *prevDemand;     /* [nNodes] demand after the previous solve      */
    double *prevSetting;    /* [nLinks] setting after the previous solve     */
    /* flow balance accumulators (flow units x seconds) */
    double  fbInflow, fbOutflow, fbConsumer, fbEmitter, fbLeakage,
            fbDeficit, fbStorage;
    /* components of the solve just completed, awaiting a time weight */
    double  pendInflow, pendOutflow, pendConsumer, pendEmitter, pendLeakage,
            pendDeficit, pendStorage;
} RD_RunState;

static RD_StatusEvent *newEvent(RD_ReportData *rd, int type, long t)
{
    RD_StatusEvent *ev;
    if (rd->nEvents >= rd->nAllocEvents)
    {
        int n = rd->nAllocEvents ? rd->nAllocEvents * 2 : 64;
        RD_StatusEvent *e = realloc(rd->events, n * sizeof(RD_StatusEvent));
        if (!e) return NULL;
        rd->events = e;
        rd->nAllocEvents = n;
    }
    ev = &rd->events[rd->nEvents++];
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    ev->time = t;
    return ev;
}

/* Recover a link's RAW internal status code (StatusType), which is what the
   status report prints.  EN_getlinkvalue(EN_STATUS) collapses everything to
   closed/open/active, so it cannot be used.  EN_PUMP_STATE returns the raw
   hyd->LinkStatus for non-pump links (src/epanet.c refines it only for
   pumps); for a pump the refinement replaces a raw OPEN with XFLOW/XHEAD,
   so an open pump is mapped back to OPEN.  See MISSING_API.md gap #3.     */
static int rawLinkStatus(EN_Project ph, int linkIndex, int linkType)
{
    double v = 0.0, st = 0.0;
    EN_getlinkvalue(ph, linkIndex, EN_PUMP_STATE, &v);
    if (linkType == EN_PUMP)
    {
        EN_getlinkvalue(ph, linkIndex, EN_STATUS, &st);
        if (st >= 1.0) return RD_OPEN;
    }
    return (int)v;
}

/* Cache the state that the next step's control evaluation and status
   comparison need (mirrors what the engine keeps in hyd->...).           */
static void cacheState(EN_Project ph, RD_ReportData *rd, RD_RunState *rs)
{
    int i;
    for (i = 0; i < rd->nNodes; i++)
    {
        EN_getnodevalue(ph, i + 1, EN_HEAD, &rs->prevHead[i]);
        EN_getnodevalue(ph, i + 1, EN_DEMAND, &rs->prevDemand[i]);
    }
    for (i = 0; i < rd->nLinks; i++)
        EN_getlinkvalue(ph, i + 1, EN_SETTING, &rs->prevSetting[i]);
}

/* Seed the status memories the way inithyd() does (src/hydraul.c:95-160):
   tanks start at TEMPCLOSED, links at their post-initialization status.  */
static void seedStatus(EN_Project ph, RD_ReportData *rd, RD_RunState *rs)
{
    int i;
    for (i = 0; i < rd->nNodes; i++)
        rs->oldTankStatus[i] = RD_TEMPCLOSED;
    for (i = 0; i < rd->nLinks; i++)
        rs->oldLinkStatus[i] = rawLinkStatus(ph, i + 1, rd->linkType[i]);
    cacheState(ph, rd, rs);
}

/* tankvolume() from src/hydraul.c, in internal units, rebuilt from the API */
static double tankVolume(EN_Project ph, RD_ReportData *rd, RD_RunState *rs,
                         int nodeIndex, double headInternal)
{
    double vcurve = 0.0, elev = 0.0, minvol = 0.0, minlevel = 0.0, diam = 0.0;
    double vcf = rs->hcf * rs->hcf * rs->hcf;   /* Ucf[VOLUME] */

    EN_getnodevalue(ph, nodeIndex, EN_VOLCURVE, &vcurve);
    EN_getnodevalue(ph, nodeIndex, EN_ELEVATION, &elev);

    if ((int)vcurve == 0)
    {
        double area;
        EN_getnodevalue(ph, nodeIndex, EN_MINVOLUME, &minvol);
        EN_getnodevalue(ph, nodeIndex, EN_MINLEVEL, &minlevel);
        EN_getnodevalue(ph, nodeIndex, EN_TANKDIAM, &diam);
        area = PI_CONST * (diam / rs->hcf) * (diam / rs->hcf) / 4.0;
        return minvol / vcf +
               (headInternal - (elev / rs->hcf + minlevel / rs->hcf)) * area;
    }
    else
    {
        /* interpolate the volume curve on the water level in user units */
        int npts = 0, k;
        double y = (headInternal - elev / rs->hcf) * rs->hcf;
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0, v;
        EN_getcurvelen(ph, (int)vcurve, &npts);
        if (npts <= 0) return 0.0;
        EN_getcurvevalue(ph, (int)vcurve, 1, &x0, &y0);
        if (y <= x0) return y0 / vcf;
        for (k = 2; k <= npts; k++)
        {
            EN_getcurvevalue(ph, (int)vcurve, k, &x1, &y1);
            if (y <= x1)
            {
                if (x1 == x0) v = y1;
                else v = y0 + (y - x0) / (x1 - x0) * (y1 - y0);
                return v / vcf;
            }
            x0 = x1; y0 = y1;
        }
        return y1 / vcf;
    }
}

/* Predict the simple-control actions the engine took at the start of this
   step, mirroring controls() in src/hydraul.c using the cached previous
   state.  Emits the FMT54/FMT55 lines.                                    */
static void probeControls(EN_Project ph, RD_ReportData *rd, RD_RunState *rs,
                          long t)
{
    int nControls = 0, i;

    EN_getcount(ph, EN_CONTROLCOUNT, &nControls);
    if (nControls <= 0) return;
    /* the engine evaluates controls against link state that earlier
       controls in the same step may already have changed              */
    memcpy(rs->curLinkStatus, rs->oldLinkStatus, rd->nLinks * sizeof(int));
    for (i = 1; i <= nControls; i++)
    {
        int type = 0, link = 0, node = 0, enabled = 1, reset = 0, ctlStatus;
        double setting = 0.0, level = 0.0, ctlSetting;
        int s1, s2;
        double k1, k2;

        if (EN_getcontrolenabled(ph, i, &enabled) == 0 && !enabled) continue;
        if (EN_getcontrol(ph, i, &type, &link, &setting, &node, &level) > 0)
            continue;
        if (link <= 0) continue;

        /* control status / setting as controldata() stored them */
        if (setting == EN_SET_OPEN)        { ctlStatus = RD_OPEN;   ctlSetting = RD_MISSING_SET; }
        else if (setting == EN_SET_CLOSED) { ctlStatus = RD_CLOSED; ctlSetting = RD_MISSING_SET; }
        else
        {
            ctlSetting = setting;
            if (rd->linkType[link - 1] == EN_PUMP ||
                rd->linkType[link - 1] == EN_PIPE)
                ctlStatus = (setting == 0.0) ? RD_CLOSED : RD_OPEN;
            else ctlStatus = RD_ACTIVE;
        }

        /* tank-level control: the engine only tests nodes above the last
           junction, so junction (pressure) controls never fire here       */
        if (node > 0 && rd->nodeType[node - 1] != EN_JUNCTION)
        {
            double h = rs->prevHead[node - 1] / rs->hcf;
            double vplus = fabs(rs->prevDemand[node - 1]) / rs->qcf;
            double elev = 0.0, grade, v1, v2;
            EN_getnodevalue(ph, node, EN_ELEVATION, &elev);
            grade = elev / rs->hcf + level / rs->hcf;
            v1 = tankVolume(ph, rd, rs, node, h);
            v2 = tankVolume(ph, rd, rs, node, grade);
            if (type == EN_LOWLEVEL && v1 <= v2 + vplus) reset = 1;
            if (type == EN_HILEVEL  && v1 >= v2 - vplus) reset = 1;
        }
        if (type == EN_TIMER && (long)level == t) reset = 1;
        if (type == EN_TIMEOFDAY &&
            (t + rs->startTime) % 86400L == (long)level) reset = 1;
        if (!reset) continue;

        s1 = (rs->curLinkStatus[link - 1] <= RD_CLOSED) ? RD_CLOSED : RD_OPEN;
        s2 = ctlStatus;
        k1 = rs->prevSetting[link - 1];
        k2 = (rd->linkType[link - 1] > EN_PIPE) ? ctlSetting : k1;
        if (s1 != s2 || k1 != k2)
        {
            RD_StatusEvent *ev = newEvent(rd, RD_EV_CONTROL, t);
            if (!ev) return;
            ev->index = link - 1;
            ev->nodeIndex = node - 1;
            ev->isTimerControl = (type == EN_TIMER || type == EN_TIMEOFDAY);
            /* the engine applies the change now; the cached state must
               follow so a later control in the same step sees it        */
            rs->curLinkStatus[link - 1] = s2;
            rs->prevSetting[link - 1] = k2;
        }
    }
}

/* writehydstat() replica: emitted after the solve for the step at time t */
static void probeStatus(EN_Project ph, RD_ReportData *rd, RD_RunState *rs,
                        long t)
{
    double iters = 0.0, relerr = 0.0, v;
    int i;

    EN_getstatistic(ph, EN_ITERATIONS, &iters);
    EN_getstatistic(ph, EN_RELATIVEERROR, &relerr);

    if (iters > 0)
    {
        RD_StatusEvent *ev = newEvent(rd, RD_EV_BALANCE, t);
        if (ev)
        {
            ev->iters = (int)iters;
            ev->relerr = relerr;
            ev->balanced = (relerr <= rd->accuracy);
        }
        if (rd->demandModel == EN_PDA)
        {
            double deficient = 0.0, reduction = 0.0;
            EN_getstatistic(ph, EN_DEFICIENTNODES, &deficient);
            EN_getstatistic(ph, EN_DEMANDREDUCTION, &reduction);
            if (deficient > 0)
            {
                ev = newEvent(rd, RD_EV_DEFICIENT, t);
                if (ev)
                {
                    ev->count = (int)deficient;
                    ev->reduction = reduction;
                }
            }
        }
    }

    /* tank / reservoir status transitions */
    for (i = 0; i < rd->nNodes; i++)
    {
        int newstat;
        double demand = 0.0, head = 0.0, elev = 0.0, maxlevel = 0.0;
        if (rd->nodeType[i] == EN_JUNCTION) continue;
        EN_getnodevalue(ph, i + 1, EN_DEMAND, &demand);
        demand /= rs->qcf;                       /* back to internal cfs */
        EN_getnodevalue(ph, i + 1, EN_HEAD, &head);
        EN_getnodevalue(ph, i + 1, EN_ELEVATION, &elev);
        if (fabs(demand) < 0.001) newstat = RD_CLOSED;
        else if (demand < 0.0)    newstat = RD_EMPTYING;
        else
        {
            newstat = RD_FILLING;
            if (rd->nodeType[i] == EN_TANK)
            {
                EN_getnodevalue(ph, i + 1, EN_MAXLEVEL, &maxlevel);
                if (fabs(head - (elev + maxlevel)) / rs->hcf < 0.001)
                    newstat = RD_OVERFLOWING;
            }
        }
        if (newstat != rs->oldTankStatus[i])
        {
            RD_StatusEvent *ev = newEvent(rd, RD_EV_TANK, t);
            if (ev)
            {
                ev->nodeIndex = i;
                ev->newStatus = newstat;
                ev->level = head - elev;
            }
            rs->oldTankStatus[i] = newstat;
        }
    }

    /* link status transitions */
    for (i = 0; i < rd->nLinks; i++)
    {
        int newstat = rawLinkStatus(ph, i + 1, rd->linkType[i]);
        if (newstat != rs->oldLinkStatus[i])
        {
            RD_StatusEvent *ev = newEvent(rd, RD_EV_LINK, t);
            if (ev)
            {
                ev->index = i;
                ev->oldStatus = rs->oldLinkStatus[i];
                ev->newStatus = newstat;
            }
            rs->oldLinkStatus[i] = newstat;
        }
    }
    (void)v;
    newEvent(rd, RD_EV_BLANK, t);
}

/* updateflowbalance() from src/flowbalance.c.  The engine accumulates the
   flows of the solve just completed weighted by the NEXT time step, so the
   components are probed before EN_nextH and weighted afterwards.        */
static void probeFlowBalance(EN_Project ph, RD_ReportData *rd, RD_RunState *rs)
{
    int i;
    rs->pendInflow = rs->pendOutflow = rs->pendConsumer = 0.0;
    rs->pendEmitter = rs->pendLeakage = rs->pendDeficit = rs->pendStorage = 0.0;

    for (i = 0; i < rd->nNodes; i++)
    {
        double v = 0.0;
        if (rd->nodeType[i] != EN_JUNCTION) continue;
        EN_getnodevalue(ph, i + 1, EN_DEMANDFLOW, &v);
        if (v < 0.0) rs->pendInflow += -v;
        else { rs->pendConsumer += v; rs->pendOutflow += v; }
        EN_getnodevalue(ph, i + 1, EN_EMITTERFLOW, &v);
        rs->pendEmitter += v; rs->pendOutflow += v;
        EN_getnodevalue(ph, i + 1, EN_LEAKAGEFLOW, &v);
        rs->pendLeakage += v; rs->pendOutflow += v;
        if (rd->demandModel == EN_PDA)
        {
            double full = 0.0, delivered = 0.0;
            EN_getnodevalue(ph, i + 1, EN_FULLDEMAND, &full);
            if (full > 0.0)
            {
                EN_getnodevalue(ph, i + 1, EN_DEMANDFLOW, &delivered);
                if (full - delivered > 0.0) rs->pendDeficit += full - delivered;
            }
        }
    }
    for (i = 0; i < rd->nNodes; i++)
    {
        double v = 0.0;
        if (rd->nodeType[i] == EN_JUNCTION) continue;
        EN_getnodevalue(ph, i + 1, EN_DEMAND, &v);
        if (rd->nodeType[i] == EN_RESERVOIR)
        {
            if (v >= 0.0) rs->pendOutflow += v;
            else          rs->pendInflow += -v;
        }
        else rs->pendStorage += v;
    }
}

static void accumulateFlowBalance(RD_ReportData *rd, RD_RunState *rs,
                                  long t, long tstep)
{
    double dt;
    if (rd->duration == 0) dt = 1.0;
    else if (t < rd->duration) dt = (double)tstep;
    else return;

    rs->fbInflow   += rs->pendInflow * dt;
    rs->fbOutflow  += rs->pendOutflow * dt;
    rs->fbConsumer += rs->pendConsumer * dt;
    rs->fbEmitter  += rs->pendEmitter * dt;
    rs->fbLeakage  += rs->pendLeakage * dt;
    rs->fbDeficit  += rs->pendDeficit * dt;
    rs->fbStorage  += rs->pendStorage * dt;
}

/* endflowbalance() from src/flowbalance.c */
static void finalizeFlowBalance(RD_ReportData *rd, RD_RunState *rs, long tFinal)
{
    double seconds = (tFinal > 0) ? (double)tFinal : 1.0;
    double qin, qout, qstor, r;
    RD_FlowBalance *fb = &rd->flowBalance;

    fb->totalInflow    = rs->fbInflow / seconds;
    fb->totalOutflow   = rs->fbOutflow / seconds;
    fb->consumerDemand = rs->fbConsumer / seconds;
    fb->emitterDemand  = rs->fbEmitter / seconds;
    fb->leakageDemand  = rs->fbLeakage / seconds;
    fb->deficitDemand  = rs->fbDeficit / seconds;
    fb->storageDemand  = rs->fbStorage / seconds;

    qin = fb->totalInflow;
    qout = fb->totalOutflow;
    qstor = fb->storageDemand;
    if (qstor > 0.0) qout += qstor;
    else             qin -= qstor;
    if (qin == qout)     r = 1.0;
    else if (qin > 0.0)  r = qout / qin;
    else                 r = 0.0;
    fb->ratio = r;
    fb->valid = 1;
}

static void recordWarning(RD_ReportData *rd, long t, int code)
{
    RD_Warning *w = realloc(rd->warnings,
                            (rd->nWarnings + 1) * sizeof(RD_Warning));
    if (!w) return;
    rd->warnings = w;
    rd->warnings[rd->nWarnings].time = t;
    rd->warnings[rd->nWarnings].code = code;
    rd->nWarnings++;
    if (code > rd->warnflag) rd->warnflag = code;
}

/* Best-effort reconstruction of the WARNING lines writehydwarn() (in
   src/report.c) writes after a warned hydraulic step, by probing solver
   state through the API.  Checks run in the engine's order.  What CANNOT
   be reconstructed (see MISSING_API.md): WARN03a/b/c disconnected-node
   details, which need the engine's connectivity walk.                    */
static void probeWarnings(EN_Project ph, RD_ReportData *rd,
                          const EnergyAcc *acc, long t, int unbalancedOpt)
{
    double iters = 0, relerr = 0, v;
    int i, fired = 0;
    RD_StatusEvent *ev;

    EN_getstatistic(ph, EN_ITERATIONS, &iters);
    EN_getstatistic(ph, EN_RELATIVEERROR, &relerr);

    /* WARN02: converged, but only after exceeding the trial limit */
    if (iters > rd->maxIter && relerr <= rd->accuracy)
    {
        ev = newEvent(rd, RD_EV_WARNING, t);
        if (ev) ev->warnKind = RD_WARN_UNSTABLE;
        fired = 1;
    }

    /* WARN06: negative pressures at demand nodes (DDA only) */
    if (rd->demandModel == EN_DDA)
    {
        for (i = 0; i < rd->nNodes; i++)
        {
            double press = 0, dem = 0;
            if (rd->nodeType[i] != EN_JUNCTION) continue;
            EN_getnodevalue(ph, i + 1, EN_PRESSURE, &press);
            EN_getnodevalue(ph, i + 1, EN_DEMAND, &dem);
            if (press < 0.0 && dem > 0.0)
            {
                ev = newEvent(rd, RD_EV_WARNING, t);
                if (ev) ev->warnKind = RD_WARN_NEGPRESSURE;
                fired = 1;
                break;
            }
        }
    }

    /* WARN05: valves stuck in an abnormal state (XFCV / XPRESSURE).
       EN_getlinkvalue(EN_STATUS) collapses these to "active"; the raw
       internal code comes from EN_PUMP_STATE, which is undocumented for
       non-pump links - see MISSING_API.md gap #3.                        */
    for (i = 0; i < rd->nLinks; i++)
    {
        if (rd->linkType[i] < EN_PRV) continue;      /* valves only */
        EN_getlinkvalue(ph, i + 1, EN_PUMP_STATE, &v);
        if (v >= RD_XFCV)
        {
            ev = newEvent(rd, RD_EV_WARNING, t);
            if (ev)
            {
                ev->warnKind = RD_WARN_VALVE;
                ev->index = i;
                ev->warnStatus = (int)v;
            }
            fired = 1;
        }
    }

    /* WARN04: pumps that cannot deliver head (XHEAD) or exceed their
       maximum flow (XFLOW), as refined by EN_PUMP_STATE               */
    for (i = 0; i < rd->nPumps; i++)
    {
        int link = acc[i].linkIndex;
        EN_getlinkvalue(ph, link, EN_PUMP_STATE, &v);
        if (v == RD_XHEAD || v == RD_XFLOW)
        {
            ev = newEvent(rd, RD_EV_WARNING, t);
            if (ev)
            {
                ev->warnKind = RD_WARN_PUMP;
                ev->index = link - 1;
                ev->warnStatus = (int)v;
            }
            fired = 1;
        }
    }

    /* WARN01: failed to converge within the trial limit */
    if (iters > rd->maxIter && relerr > rd->accuracy)
    {
        ev = newEvent(rd, RD_EV_WARNING, t);
        if (ev)
        {
            ev->warnKind = RD_WARN_UNBALANCED;
            ev->count = (unbalancedOpt == -1);   /* "EXECUTION HALTED." */
        }
        fired = 1;
    }

    /* WARN03a/b/c (disconnected nodes) would be written here - GAP */

    if (fired) newEvent(rd, RD_EV_BLANK, t);
}

/* --------------------------------------------------------------------------
   Main entry: open project, run simulation (producing the native report),
   and collect all replica data along the way.
   ------------------------------------------------------------------------ */
int rd_collect(RD_ReportData *rd, const char *inpFile, const char *rptFile,
               int statusLevel, const char **setCmds, int nSetCmds,
               char *errmsg, int errlen)
{
    EN_Project ph = NULL;
    int err = 0, i, warncode = 0;
    long t = 0, tstep = 0;
    EnergyAcc *acc = NULL;
    double emax = 0.0, globalPrice = 0.0, demandChargeRate = 0.0;
    int globalPat = 0;
    long pstart = 0, pstep = 3600;
    long nextRptTime;
    long enginePeriods = 0;
    int unbalancedOpt = 0;
    RD_RunState rs;

    memset(rd, 0, sizeof(*rd));
    memset(&rs, 0, sizeof(rs));
    snprintf(rd->inpFname, sizeof(rd->inpFname), "%s", inpFile);
    errmsg[0] = '\0';

    CHECK(EN_createproject(&ph));
    err = EN_open(ph, inpFile, rptFile, "");
    if (err > 100)
    {
        snprintf(errmsg, errlen, "EN_open failed with error %d", err);
        EN_deleteproject(ph);
        return err;
    }

    /* status level override requested on the command line: applied to the
       native engine; collectStatic() reads it back through the API        */
    if (statusLevel >= 0) CHECK(EN_setstatusreport(ph, statusLevel));

    /* extra [REPORT]-style commands: applied to the native engine via
       EN_setreport and mirrored into the replica model further below     */
    for (i = 0; i < nSetCmds; i++)
    {
        CHECK(EN_setreport(ph, setCmds[i]));
    }

    if ((err = collectStatic(ph, rd, errmsg, errlen)) > 100)
    {
        EN_deleteproject(ph);
        return err;
    }

    /* GAP WORKAROUND: [REPORT] summary/page/messages/energy/nodes/links
       settings have no API getters; re-parse them from the INP file.     */
    rd_sniff_inp_report_settings(rd, inpFile);

    /* the native summary was written during EN_open, so its "Reporting
       Criteria" lines reflect the INP settings, not later overrides      */
    rd->summaryNodeRptFlag = rd->nodeRptFlag;
    rd->summaryLinkRptFlag = rd->linkRptFlag;

    /* mirror CLI report commands into the replica settings model */
    for (i = 0; i < nSetCmds; i++) rd_apply_report_command(rd, setCmds[i]);
    if (statusLevel >= 0) rd->statusLevel = statusLevel;

    /* energy accounting inputs */
    EN_getoption(ph, EN_GLOBALPRICE, &globalPrice);
    {
        double v = 0;
        EN_getoption(ph, EN_GLOBALPATTERN, &v);
        globalPat = (int)v;
        EN_getoption(ph, EN_DEMANDCHARGE, &demandChargeRate);
    }
    EN_gettimeparam(ph, EN_PATTERNSTART, &pstart);
    EN_gettimeparam(ph, EN_PATTERNSTEP, &pstep);
    if (pstep <= 0) pstep = 3600;

    acc = calloc(rd->nPumps > 0 ? rd->nPumps : 1, sizeof(EnergyAcc));
    rd->pumpEnergy = calloc(rd->nPumps > 0 ? rd->nPumps : 1,
                            sizeof(RD_PumpEnergy));
    if (!acc || !rd->pumpEnergy)
    {
        free(acc);
        EN_deleteproject(ph);
        snprintf(errmsg, errlen, "out of memory");
        return 101;
    }
    {
        int j = 0;
        for (i = 1; i <= rd->nLinks; i++)
        {
            if (rd->linkType[i - 1] == EN_PUMP)
            {
                double v = 0;
                acc[j].linkIndex = i;
                EN_getlinkvalue(ph, i, EN_PUMP_ECOST, &acc[j].ecost);
                EN_getlinkvalue(ph, i, EN_PUMP_EPAT, &v);
                acc[j].epat = (int)v;
                snprintf(rd->pumpEnergy[j].linkId,
                         sizeof(rd->pumpEnergy[j].linkId), "%s",
                         rd->linkId[i - 1]);
                j++;
            }
        }
    }

    /* ---- hydraulic phase (writes "Analysis begun" + status/warnings to
            the native report) ------------------------------------------- */
    err = EN_openH(ph);
    if (err > 100) goto fail;
    err = EN_initH(ph, EN_SAVE);
    if (err > 100) goto fail;
    {
        double v = 0;
        EN_getoption(ph, EN_UNBALANCED, &v);
        unbalancedOpt = (int)v;
    }

    /* status-report state: the engine seeds its status memories in
       inithyd(), so mirror that right after EN_initH                     */
    rs.hcf = rd->usUnits ? 1.0 : 0.3048;
    rs.qcf = FLOW_PER_CFS[rd->flowUnits];
    EN_gettimeparam(ph, EN_STARTTIME, &rs.startTime);
    rs.oldLinkStatus = calloc(rd->nLinks > 0 ? rd->nLinks : 1, sizeof(int));
    rs.curLinkStatus = calloc(rd->nLinks > 0 ? rd->nLinks : 1, sizeof(int));
    rs.oldTankStatus = calloc(rd->nNodes, sizeof(int));
    rs.prevHead = calloc(rd->nNodes, sizeof(double));
    rs.prevDemand = calloc(rd->nNodes, sizeof(double));
    rs.prevSetting = calloc(rd->nLinks > 0 ? rd->nLinks : 1, sizeof(double));
    if (!rs.oldLinkStatus || !rs.curLinkStatus || !rs.oldTankStatus || !rs.prevHead ||
        !rs.prevDemand || !rs.prevSetting)
    {
        err = 101;
        snprintf(errmsg, errlen, "out of memory");
        goto fail;
    }
    seedStatus(ph, rd, &rs);

    do
    {
        err = EN_runH(ph, &t);
        if (err > 100) goto fail;

        /* control actions fire inside EN_runH BEFORE the solve, so they
           are predicted from the cached previous state (see
           probeControls) and must be recorded before the status lines   */
        if (rd->statusLevel > RD_STATUS_NO) probeControls(ph, rd, &rs, t);
        if (rd->statusLevel > RD_STATUS_NO) probeStatus(ph, rd, &rs, t);

        if (err > 0 && err < 100)
        {
            recordWarning(rd, t, err);
            probeWarnings(ph, rd, acc, t, unbalancedOpt);
        }
        probeFlowBalance(ph, rd, &rs);

        err = EN_nextH(ph, &tstep);
        if (err > 100) goto fail;
        /* EN_nextH advances tank levels, so the state the NEXT step's
           controls() will see is only complete now (see tanklevels() in
           src/hydraul.c)                                                */
        cacheState(ph, rd, &rs);
        accumulateEnergy(ph, rd, acc, &emax, t, tstep,
                         globalPrice, globalPat, pstart, pstep);
        accumulateFlowBalance(rd, &rs, t, tstep);
        if (tstep == 0) finalizeFlowBalance(rd, &rs, t);
    } while (tstep > 0);
    err = EN_closeH(ph);
    if (err > 100) goto fail;

    finalizeEnergy(rd, acc, emax, demandChargeRate);

    /* ---- water quality phase (samples results at reporting times; the
            closing call writes "Analysis ended" to the native report) --- */
    nextRptTime = rd->rptStart;
    err = EN_openQ(ph);
    if (err > 100) goto fail;
    err = EN_initQ(ph, EN_SAVE);
    if (err > 100) goto fail;
    do
    {
        err = EN_runQ(ph, &t);
        if (err > 100) goto fail;
        if (t >= nextRptTime)
        {
            if ((err = samplePeriod(ph, rd)) > 100) goto fail;
            nextRptTime += rd->rptStep;
        }
        err = EN_nextQ(ph, &tstep);
        if (err > 100) goto fail;
    } while (tstep > 0);
    /* the water quality mass balance ratio is the only part of that block
       the API exposes; the mass terms live inside the quality solver     */
    if (rd->qualType != EN_NONE && rd->statusLevel > RD_STATUS_NO)
    {
        double ratio = 0.0;
        EN_getstatistic(ph, EN_MASSBALANCE, &ratio);
        rd->massBalance.ratio = ratio;
        rd->massBalance.haveMasses = 0;   /* GAP - see MISSING_API.md */
        rd->massBalance.valid = 1;
    }

    err = EN_closeQ(ph);
    if (err > 100) goto fail;

    /* sanity check: engine's own period count should match our sampling */
    EN_gettimeparam(ph, EN_PERIODS, &enginePeriods);

    applyTimeStatistics(rd);

    if ((int)enginePeriods != rd->nPeriods)
    {
        fprintf(stderr,
            "repgen: warning: sampled %d reporting periods but engine "
            "reports %ld\n", rd->nPeriods, enginePeriods);
    }

    /* ---- native report (energy table + node/link tables) -------------- */
    err = EN_report(ph);
    if (err > 100) goto fail;

    EN_close(ph);
    EN_deleteproject(ph);
    free(acc);
    free(rs.oldLinkStatus); free(rs.curLinkStatus); free(rs.oldTankStatus);
    free(rs.prevHead); free(rs.prevDemand); free(rs.prevSetting);
    (void)warncode;
    return rd->warnflag;

fail:
    if (!errmsg[0])
        snprintf(errmsg, errlen, "API error %d during simulation", err);
    EN_close(ph);
    EN_deleteproject(ph);
    free(acc);
    free(rs.oldLinkStatus); free(rs.curLinkStatus); free(rs.oldTankStatus);
    free(rs.prevHead); free(rs.prevDemand); free(rs.prevSetting);
    return err;
}

void rd_free(RD_ReportData *rd)
{
    int p;
    for (p = 0; p < rd->nWarnLines; p++) free(rd->warnLines[p]);
    free(rd->warnLines);
    free(rd->events);
    for (p = 0; p < rd->nPeriods; p++)
    {
        if (rd->nodeRes) free(rd->nodeRes[p]);
        if (rd->linkRes) free(rd->linkRes[p]);
    }
    free(rd->nodeRes);
    free(rd->linkRes);
    free(rd->nodeId); free(rd->nodeType); free(rd->nodeElev); free(rd->nodeRpt);
    free(rd->linkId); free(rd->linkType); free(rd->linkLen); free(rd->linkDiam);
    free(rd->linkRpt);
    free(rd->pumpEnergy);
    free(rd->warnings);
    memset(rd, 0, sizeof(*rd));
}
