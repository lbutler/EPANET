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

static void addWarnLine(RD_ReportData *rd, const char *line)
{
    char **lines = realloc(rd->warnLines,
                           (rd->nWarnLines + 1) * sizeof(char *));
    if (!lines) return;
    rd->warnLines = lines;
    rd->warnLines[rd->nWarnLines] = strdup(line);
    if (rd->warnLines[rd->nWarnLines]) rd->nWarnLines++;
}

static char *clockTimeStr(char *buf, long seconds)
{
    long h = seconds / 3600;
    long m = seconds % 3600 / 60;
    long s = seconds - 3600 * h - 60 * m;
    sprintf(buf, "%01d:%02d:%02d", (int)h, (int)m, (int)s);
    return buf;
}

/* Best-effort reconstruction of the WARNING lines writehydwarn() (in
   src/report.c) writes to the report after a warned hydraulic step, by
   probing solver state through the API.  Checks run in the same order as
   the engine's.  What CANNOT be reconstructed (see MISSING_API.md):
     - WARN05: a valve stuck in an abnormal state (XFCV/XPRESSURE) - the
       API collapses valve status to closed/open/active;
     - WARN03a/b/c: disconnected-node details - no connectivity API.      */
static void probeWarnings(EN_Project ph, RD_ReportData *rd,
                          const EnergyAcc *acc, long t, int unbalancedOpt)
{
    double iters = 0, relerr = 0, v;
    char atime[16], line[256];
    int i, fired = 0;

    clockTimeStr(atime, t);
    EN_getstatistic(ph, EN_ITERATIONS, &iters);
    EN_getstatistic(ph, EN_RELATIVEERROR, &relerr);

    /* WARN02: converged but only after exceeding the trial limit */
    if (iters > rd->maxIter && relerr <= rd->accuracy)
    {
        sprintf(line, "WARNING: Maximum trials exceeded at %s hrs. "
                      "System may be unstable.", atime);
        addWarnLine(rd, line);
        fired = 1;
    }

    /* WARN06: negative pressures at demand nodes (DDA only; engine tests
       NodeHead < Elev && NodeDemand > 0, i.e. pressure < 0)              */
    if (rd->demandModel == 0)
    {
        int deficient = 0;
        for (i = 0; i < rd->nNodes; i++)
        {
            double press = 0, dem = 0;
            if (rd->nodeType[i] != 0) continue;
            EN_getnodevalue(ph, i + 1, EN_PRESSURE, &press);
            EN_getnodevalue(ph, i + 1, EN_DEMAND, &dem);
            if (press < 0.0 && dem > 0.0) { deficient = 1; break; }
        }
        if (deficient)
        {
            sprintf(line, "WARNING: Negative pressures at %s hrs.", atime);
            addWarnLine(rd, line);
            fired = 1;
        }
    }

    /* WARN05: valves stuck in an abnormal state (XFCV/XPRESSURE).
       EN_getlinkvalue(EN_STATUS) collapses these to "active", but
       EN_PUMP_STATE - undocumented for non-pumps - returns the raw
       internal status code (src/epanet.c), exposing XFCV(6)/XPRESSURE(7).
       An official EN_VALVE_STATE would be cleaner - see MISSING_API.md.  */
    for (i = 0; i < rd->nLinks; i++)
    {
        static const char *LINK_TXT[] =
            { "CV", "Pipe", "Pump", "PRV", "PSV", "PBV", "FCV", "TCV",
              "GPV", "PCV" };
        if (rd->linkType[i] < 3) continue;   /* valves only */
        EN_getlinkvalue(ph, i + 1, EN_PUMP_STATE, &v);
        if (v >= 6.0)
        {
            sprintf(line, "WARNING: %s %s %s at %s hrs.",
                    LINK_TXT[rd->linkType[i]], rd->linkId[i],
                    v == 6.0 ? "open but cannot deliver flow"
                             : "open but cannot deliver pressure",
                    atime);
            addWarnLine(rd, line);
            fired = 1;
        }
    }

    /* WARN04: pumps that cannot deliver head (XHEAD) or exceed their
       maximum flow (XFLOW), exposed through EN_PUMP_STATE               */
    for (i = 0; i < rd->nPumps; i++)
    {
        int link = acc[i].linkIndex;
        EN_getlinkvalue(ph, link, EN_PUMP_STATE, &v);
        if (v == 0.0 || v == 5.0)
        {
            sprintf(line, "WARNING: Pump %s %s at %s hrs.",
                    rd->linkId[link - 1],
                    v == 0.0 ? "closed because cannot deliver head"
                             : "open but exceeds maximum flow",
                    atime);
            addWarnLine(rd, line);
            fired = 1;
        }
    }

    /* WARN01: failed to converge within the trial limit */
    if (iters > rd->maxIter && relerr > rd->accuracy)
    {
        sprintf(line, "WARNING: System unbalanced at %s hrs.%s", atime,
                unbalancedOpt == -1 ? " EXECUTION HALTED." : "");
        addWarnLine(rd, line);
        fired = 1;
    }

    /* WARN03a/b/c (disconnected nodes) would be written here - GAP       */

    if (fired) addWarnLine(rd, " ");
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

    memset(rd, 0, sizeof(*rd));
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
    do
    {
        err = EN_runH(ph, &t);
        if (err > 100) goto fail;
        if (err > 0 && err < 100)
        {
            recordWarning(rd, t, err);
            probeWarnings(ph, rd, acc, t, unbalancedOpt);
        }
        err = EN_nextH(ph, &tstep);
        if (err > 100) goto fail;
        accumulateEnergy(ph, rd, acc, &emax, t, tstep,
                         globalPrice, globalPat, pstart, pstep);
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
    (void)warncode;
    return rd->warnflag;

fail:
    snprintf(errmsg, errlen, "API error %d during simulation", err);
    EN_close(ph);
    EN_deleteproject(ph);
    free(acc);
    return err;
}

void rd_free(RD_ReportData *rd)
{
    int p;
    for (p = 0; p < rd->nWarnLines; p++) free(rd->warnLines[p]);
    free(rd->warnLines);
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
