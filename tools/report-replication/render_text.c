/*
 ******************************************************************************
 Project:      OWA EPANET - Report Replication Test Harness
 Module:       render_text.c
 Description:  Renders the RD_ReportData model in EPANET's classic .rpt text
               format, byte-for-byte identical to the engine's own report
               (src/report.c) apart from wall-clock timestamps.

               This is one renderer over the model; the same data could be
               rendered as HTML, JSON, etc.
 ******************************************************************************
*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "report_data.h"

/* ---- text constants copied verbatim from src/text.h -------------------- */
#define LOGO1 "******************************************************************"
#define LOGO2 "*                           E P A N E T                          *"
#define LOGO3 "*                   Hydraulic and Water Quality                  *"
#define LOGO4 "*                   Analysis for Pipe Networks                   *"
#define LOGO5 "*                          Version %d.%d.%02d                        *"
#define LOGO6 "******************************************************************"

static const char *HLOSS_FORM_TXT[] =
    { "Hazen-Williams", "Darcy-Weisbach", "Chezy-Manning" };
static const char *DEMAND_MODEL_TXT[] = { "DDA", "PDA" };
static const char *NODE_TYPE_TXT[] = { "Junction", "Reservoir", "Tank" };
static const char *LINK_TYPE_TXT[] =
    { "CV", "Pipe", "Pump", "PRV", "PSV", "PBV", "FCV", "TCV", "GPV", "PCV" };
/* engine internal status codes -> report text (StatTxt in src/enumstxt.h) */
static const char *STAT_TXT[] =
    { "closed because cannot deliver head", "temporarily closed",
      "closed", "open", "active", "open but exceeds maximum flow",
      "open but cannot deliver flow", "open but cannot deliver pressure",
      "filling", "emptying", "overflowing" };
static const char *TSTAT_TXT[] =
    { "none", "AVERAGE", "MINIMUM", "MAXIMUM", "RANGE" };

/* ---- writeline()/paging emulation --------------------------------------*/
typedef struct {
    FILE *f;
    long lineNum;
    long pageNum;
    long pageSize;
    const char *title0;
} Sink;

static void wline(Sink *sk, const char *s)
{
    /* mirrors writeline() in src/report.c (Rptflag is always 1 here) */
    if (sk->lineNum == sk->pageSize)
    {
        sk->pageNum++;
        fprintf(sk->f, "\n\f\n  Page %-ld    %60.60s\n", sk->pageNum,
                sk->title0);
        sk->lineNum = 3;
    }
    fprintf(sk->f, "\n  %s", s);
    sk->lineNum++;
}

static void headerBreak(Sink *sk)
{
    /* mirrors the top of writeheader() in src/report.c: move to the next
       page when fewer than 11 lines remain on the current one           */
    if (sk->lineNum + 11 > sk->pageSize)
    {
        while (sk->lineNum < sk->pageSize) wline(sk, " ");
    }
    wline(sk, " ");
}

static void wfill(Sink *sk, char ch, int n)
{
    /* mirrors fillstr() in src/report.c: writes n+1 copies of ch */
    char s[RD_MAXLINE];
    int i;
    for (i = 0; i <= n && i < RD_MAXLINE - 1; i++) s[i] = ch;
    s[i] = '\0';
    wline(sk, s);
}

static char *clockTime(char *buf, long seconds)
{
    long h = seconds / 3600;
    long m = seconds % 3600 / 60;
    long s = seconds - 3600 * h - 60 * m;
    sprintf(buf, "%01d:%02d:%02d", (int)h, (int)m, (int)s);
    return buf;
}

/* ---- report sections ---------------------------------------------------*/
static void renderLogo(const RD_ReportData *rd, Sink *sk)
{
    char s[RD_MAXLINE];
    time_t timer;

    time(&timer);
    sk->pageNum = 1;
    sk->lineNum = 2;
    fprintf(sk->f, "  Page 1                                    ");
    fprintf(sk->f, "%s", ctime(&timer));
    wline(sk, LOGO1);
    wline(sk, LOGO2);
    wline(sk, LOGO3);
    wline(sk, LOGO4);
    sprintf(s, LOGO5, rd->versionMajor, rd->versionMinor, rd->versionPatch);
    wline(sk, s);
    wline(sk, LOGO6);
    wline(sk, "");
}

static void renderLimits(const RD_ReportData *rd, Sink *sk,
                         const RD_Field *fields, int j1, int j2)
{
    char s[RD_MAXLINE];
    int j;
    for (j = j1; j <= j2; j++)
    {
        if (fields[j].hasLowLimit)
        {
            sprintf(s, "       with %s below %-.2f %s", fields[j].name,
                    fields[j].lowLimit, fields[j].units);
            wline(sk, s);
        }
        if (fields[j].hasHighLimit)
        {
            sprintf(s, "       with %s above %-.2f %s", fields[j].name,
                    fields[j].highLimit, fields[j].units);
            wline(sk, s);
        }
    }
}

static void renderSummary(const RD_ReportData *rd, Sink *sk)
{
    /* mirrors writesummary() in src/report.c */
    char s[RD_MAXLINE + 80];
    int i;

    for (i = 0; i < 3; i++)
    {
        if (strlen(rd->title[i]) > 0)
        {
            sprintf(s, "%-.70s", rd->title[i]);
            wline(sk, s);
        }
    }
    wline(sk, " ");
    sprintf(s, "    Input Data File ................... %s", rd->inpFname);
    wline(sk, s);
    sprintf(s, "    Number of Junctions................ %-d", rd->nJuncs);
    wline(sk, s);
    sprintf(s, "    Number of Reservoirs............... %-d", rd->nReservoirs);
    wline(sk, s);
    sprintf(s, "    Number of Tanks ................... %-d", rd->nTanks);
    wline(sk, s);
    sprintf(s, "    Number of Pipes ................... %-d", rd->nPipes);
    wline(sk, s);
    sprintf(s, "    Number of Pumps ................... %-d", rd->nPumps);
    wline(sk, s);
    sprintf(s, "    Number of Valves .................. %-d", rd->nValves);
    wline(sk, s);
    sprintf(s, "    Headloss Formula .................. %s",
            HLOSS_FORM_TXT[rd->headlossForm]);
    wline(sk, s);
    sprintf(s, "    Nodal Demand Model ................ %s",
            DEMAND_MODEL_TXT[rd->demandModel]);
    wline(sk, s);
    /* the engine reports times in minutes when the hydraulic step is
       shorter than half an hour (initunits() in src/input1.c)           */
    {
        double tcf = (rd->hydStep < 1800) ? 60.0 : 3600.0;
        const char *tunits = (rd->hydStep < 1800) ? "min" : "hrs";
        sprintf(s, "    Hydraulic Timestep ................ %-.2f %s",
                (double)rd->hydStep / tcf, tunits);
        wline(sk, s);
    }
    sprintf(s, "    Hydraulic Accuracy ................ %-.6f", rd->accuracy);
    wline(sk, s);

    if (rd->headErrorLimit > 0.0)
    {
        sprintf(s, "    Headloss Error Limit .............. %-.6f %s",
                rd->headErrorLimit, rd->nodeField[RD_NF_HEAD].units);
        wline(sk, s);
    }
    if (rd->flowChangeLimit > 0.0)
    {
        sprintf(s, "    Flow Change Limit ................. %-.6f %s",
                rd->flowChangeLimit, rd->linkField[RD_LF_FLOW].units);
        wline(sk, s);
    }

    sprintf(s, "    Status Check Frequency ............ %-d", rd->checkFreq);
    wline(sk, s);
    sprintf(s, "    Maximum Trials Checked ............ %-d", rd->maxCheck);
    wline(sk, s);
    sprintf(s, "    Damping Limit Threshold ........... %-.6f", rd->dampLimit);
    wline(sk, s);
    sprintf(s, "    Maximum Trials .................... %-d", rd->maxIter);
    wline(sk, s);

    if (rd->qualType == 0 || rd->duration == 0)
        sprintf(s, "    Quality Analysis .................. None");
    else if (rd->qualType == 1)
        sprintf(s, "    Quality Analysis .................. %s", rd->chemName);
    else if (rd->qualType == 3)
        sprintf(s, "    Quality Analysis .................. Trace From Node %s",
                rd->traceNode);
    else
        sprintf(s, "    Quality Analysis .................. Age");
    wline(sk, s);
    if (rd->qualType != 0 && rd->duration > 0)
    {
        sprintf(s, "    Water Quality Time Step ........... %-.2f min",
                (float)rd->qualStep / 60.0);
        wline(sk, s);
        sprintf(s, "    Water Quality Tolerance ........... %-.2f %s",
                rd->qualTolerance, rd->nodeField[RD_NF_QUALITY].units);
        wline(sk, s);
    }

    sprintf(s, "    Specific Gravity .................. %-.2f", rd->spGrav);
    wline(sk, s);
    sprintf(s, "    Relative Kinematic Viscosity ...... %-.2f",
            rd->relViscosity);
    wline(sk, s);
    sprintf(s, "    Relative Chemical Diffusivity ..... %-.2f",
            rd->relDiffusivity);
    wline(sk, s);
    sprintf(s, "    Demand Multiplier ................. %-.2f", rd->demandMult);
    wline(sk, s);
    sprintf(s, "    Total Duration .................... %-.2f %s",
            (double)rd->duration / ((rd->hydStep < 1800) ? 60.0 : 3600.0),
            (rd->hydStep < 1800) ? "min" : "hrs");
    wline(sk, s);

    /* reporting criteria (Rptflag is always 1 for a file report); these
       reflect the settings at EN_open time - see summaryNodeRptFlag      */
    wline(sk, "    Reporting Criteria:");
    if (rd->summaryNodeRptFlag == 0) wline(sk, "       No Nodes");
    if (rd->summaryNodeRptFlag == 1) wline(sk, "       All Nodes");
    if (rd->summaryNodeRptFlag == 2) wline(sk, "       Selected Nodes");
    renderLimits(rd, sk, rd->nodeField, RD_NF_DEMAND, RD_NF_QUALITY);
    if (rd->summaryLinkRptFlag == 0) wline(sk, "       No Links");
    if (rd->summaryLinkRptFlag == 1) wline(sk, "       All Links");
    if (rd->summaryLinkRptFlag == 2) wline(sk, "       Selected Links");
    renderLimits(rd, sk, rd->linkField, RD_LF_DIAM, RD_LF_HEADLOSS);
    wline(sk, " ");
}

static void renderTimestamp(Sink *sk, const char *fmt)
{
    char s[RD_MAXLINE];
    time_t timer;
    time(&timer);
    sprintf(s, fmt, ctime(&timer));
    wline(sk, s);
}

static void renderEnergy(const RD_ReportData *rd, Sink *sk)
{
    /* mirrors writeenergy() + writeheader(ENERHDR) in src/report.c */
    char s[RD_MAXLINE];
    int j;

    if (rd->nPumps == 0) return;
    wline(sk, " ");
    headerBreak(sk);
    wline(sk, "Energy Usage:");
    wfill(sk, '-', 63);
    wline(sk, "           Usage   Avg.     Kw-hr      Avg.      Peak      Cost");
    sprintf(s, "Pump      Factor Effic.     %s        Kw        Kw      /day",
            rd->usUnits ? "/Mgal" : "  /m3");
    wline(sk, s);
    wfill(sk, '-', 63);

    for (j = 0; j < rd->nPumps; j++)
    {
        const RD_PumpEnergy *pe = &rd->pumpEnergy[j];
        if (sk->lineNum == sk->pageSize)
        {
            /* continuation header, mirrors writeheader(ENERHDR, 1) */
            headerBreak(sk);
            wline(sk, "Energy Usage: (continued)");
            wfill(sk, '-', 63);
            wline(sk,
              "           Usage   Avg.     Kw-hr      Avg.      Peak      Cost");
            sprintf(s,
              "Pump      Factor Effic.     %s        Kw        Kw      /day",
              rd->usUnits ? "/Mgal" : "  /m3");
            wline(sk, s);
            wfill(sk, '-', 63);
        }
        sprintf(s, "%-8s  %6.2f %6.2f %9.2f %9.2f %9.2f %9.2f",
                pe->linkId, pe->usageFactor, pe->avgEffic, pe->kwhPerFlow,
                pe->avgKw, pe->peakKw, pe->costPerDay);
        wline(sk, s);
    }

    wfill(sk, '-', 63);
    /* mirror the engine's double application of the demand-charge rate
       (savenergy() already scaled Emax by the rate; writeenergy()
       multiplies by it again) - see MISSING_API.md "Engine bugs found"  */
    {
        double reportedCharge = rd->demandCharge * rd->demandChargeRate;
        double csum = rd->totalEnergyCost - rd->demandCharge;
        sprintf(s, "%38s Demand Charge: %9.2f", "", reportedCharge);
        wline(sk, s);
        sprintf(s, "%38s Total Cost:    %9.2f", "", csum + reportedCharge);
        wline(sk, s);
    }
    wline(sk, " ");
}

static void nodeTableHeader(const RD_ReportData *rd, Sink *sk, long htime,
                            int contin)
{
    char s[RD_MAXLINE], s2[RD_MAXLINE], s3[RD_MAXLINE], atime[16];
    int i, n;

    /* mirrors writeheader(NODEHDR) in src/report.c */
    headerBreak(sk);
    if (rd->tstatFlag == 4)      /* RANGE */
        sprintf(s, "%s Node Results:", "DIFFERENTIAL");
    else if (rd->tstatFlag != 0)
        sprintf(s, "%s Node Results:", TSTAT_TXT[rd->tstatFlag]);
    else if (rd->duration == 0)
        sprintf(s, "Node Results:");
    else
        sprintf(s, "Node Results at %s hrs:", clockTime(atime, htime));
    if (contin) strcat(s, " (continued)");
    wline(sk, s);

    n = 15;
    sprintf(s2, "%15s", "");
    sprintf(s3, "%-15s", "Node");
    for (i = RD_NF_ELEV; i < RD_NF_QUALITY; i++)
    {
        if (rd->nodeField[i].enabled)
        {
            n += 10;
            sprintf(s, "%10s", rd->nodeField[i].name);
            strcat(s2, s);
            sprintf(s, "%10s", rd->nodeField[i].units);
            strcat(s3, s);
        }
    }
    if (rd->nodeField[RD_NF_QUALITY].enabled)
    {
        n += 10;
        sprintf(s, "%10s", rd->chemName);
        strcat(s2, s);
        sprintf(s, "%10s", rd->chemUnits);
        strcat(s3, s);
    }
    wfill(sk, '-', n);
    wline(sk, s2);
    wline(sk, s3);
    wfill(sk, '-', n);
}

static int checkLimits(const RD_Field *fields, const double *y, int j1, int j2)
{
    /* mirrors checklimits() in src/report.c */
    int j;
    for (j = j1; j <= j2; j++)
    {
        if (fields[j].hasLowLimit && y[j] > fields[j].lowLimit) return 0;
        if (fields[j].hasHighLimit && y[j] < fields[j].highLimit) return 0;
    }
    return 1;
}

static void catValue(char *s, double y, int precision)
{
    char s1[32];
    if (fabs(y) > 1.e6) sprintf(s1, "%10.2e", y);
    else sprintf(s1, "%10.*f", precision, y);
    strcat(s, s1);
}

static void renderNodeTable(const RD_ReportData *rd, Sink *sk, int period,
                            long htime)
{
    char s[RD_MAXLINE];
    double y[RD_NODE_FIELDS];
    int i, j;

    nodeTableHeader(rd, sk, htime, 0);

    for (i = 0; i < rd->nNodes; i++)
    {
        const RD_NodeResult *nr = &rd->nodeRes[period][i];
        y[RD_NF_ELEV] = rd->nodeElev[i];
        y[RD_NF_DEMAND] = nr->demand;
        y[RD_NF_HEAD] = nr->head;
        y[RD_NF_PRESSURE] = nr->pressure;
        y[RD_NF_QUALITY] = nr->quality;

        if ((rd->nodeRptFlag == 1 || rd->nodeRpt[i]) &&
            checkLimits(rd->nodeField, y, RD_NF_ELEV, RD_NF_QUALITY))
        {
            if (sk->lineNum == sk->pageSize)
                nodeTableHeader(rd, sk, htime, 1);
            sprintf(s, "%-15s", rd->nodeId[i]);
            for (j = RD_NF_ELEV; j <= RD_NF_QUALITY; j++)
            {
                if (rd->nodeField[j].enabled)
                    catValue(s, y[j], rd->nodeField[j].precision);
            }
            if (rd->nodeType[i] != 0)
            {
                strcat(s, "  ");
                strcat(s, NODE_TYPE_TXT[rd->nodeType[i]]);
            }
            wline(sk, s);
        }
    }
    wline(sk, " ");
}

static void linkTableHeader(const RD_ReportData *rd, Sink *sk, long htime,
                            int contin)
{
    char s[RD_MAXLINE], s2[RD_MAXLINE], s3[RD_MAXLINE], atime[16];
    int i, n;

    headerBreak(sk);
    if (rd->tstatFlag == 4)
        sprintf(s, "%s Link Results:", "DIFFERENTIAL");
    else if (rd->tstatFlag != 0)
        sprintf(s, "%s Link Results:", TSTAT_TXT[rd->tstatFlag]);
    else if (rd->duration == 0)
        sprintf(s, "Link Results:");
    else
        sprintf(s, "Link Results at %s hrs:", clockTime(atime, htime));
    if (contin) strcat(s, " (continued)");
    wline(sk, s);

    n = 15;
    sprintf(s2, "%15s", "");
    sprintf(s3, "%-15s", "Link");
    for (i = RD_LF_LENGTH; i <= RD_LF_FRICTION; i++)
    {
        if (rd->linkField[i].enabled)
        {
            n += 10;
            sprintf(s, "%10s", rd->linkField[i].name);
            strcat(s2, s);
            sprintf(s, "%10s", rd->linkField[i].units);
            strcat(s3, s);
        }
    }
    wfill(sk, '-', n);
    wline(sk, s2);
    wline(sk, s3);
    wfill(sk, '-', n);
}

static void renderLinkTable(const RD_ReportData *rd, Sink *sk, int period,
                            long htime)
{
    char s[RD_MAXLINE], s1[32];
    double y[RD_LINK_FIELDS];
    int i, j;

    linkTableHeader(rd, sk, htime, 0);

    for (i = 0; i < rd->nLinks; i++)
    {
        const RD_LinkResult *lr = &rd->linkRes[period][i];
        y[RD_LF_LENGTH] = rd->linkLen[i];
        y[RD_LF_DIAM] = rd->linkDiam[i];
        y[RD_LF_FLOW] = lr->flow;
        y[RD_LF_VELOCITY] = lr->velocity;
        y[RD_LF_HEADLOSS] = lr->headloss;
        y[RD_LF_QUALITY] = lr->quality;
        y[RD_LF_STATUS] = lr->status;
        y[RD_LF_SETTING] = lr->setting;
        y[RD_LF_REACTION] = 0.0;   /* GAP: no API for reaction rate */
        y[RD_LF_FRICTION] = 0.0;   /* GAP: no API for friction factor */

        if ((rd->linkRptFlag == 1 || rd->linkRpt[i]) &&
            checkLimits(rd->linkField, y, RD_LF_DIAM, RD_LF_FRICTION))
        {
            if (sk->lineNum == sk->pageSize)
                linkTableHeader(rd, sk, htime, 1);
            sprintf(s, "%-15s", rd->linkId[i]);
            for (j = RD_LF_LENGTH; j <= RD_LF_FRICTION; j++)
            {
                if (!rd->linkField[j].enabled) continue;
                if (j == RD_LF_STATUS)
                {
                    int k;
                    if (y[j] <= 2.0) k = 2;       /* closed */
                    else if (y[j] == 4.0) k = 4;  /* active */
                    else k = 3;                   /* open   */
                    sprintf(s1, "%10s", STAT_TXT[k]);
                    strcat(s, s1);
                }
                else catValue(s, y[j], rd->linkField[j].precision);
            }
            if (rd->linkType[i] > 1)   /* > PIPE */
            {
                strcat(s, "  ");
                strcat(s, LINK_TYPE_TXT[rd->linkType[i]]);
            }
            wline(sk, s);
        }
    }
    wline(sk, " ");
}

static void renderResults(const RD_ReportData *rd, Sink *sk)
{
    /* mirrors writeresults() in src/report.c */
    int np, nnv = 0, nlv = 0, j;
    long htime = rd->rptStart;

    if (!rd->nodeRptFlag && !rd->linkRptFlag) return;
    for (j = 0; j < RD_NODE_FIELDS; j++) nnv += rd->nodeField[j].enabled;
    for (j = 0; j < RD_LINK_FIELDS; j++) nlv += rd->linkField[j].enabled;
    if (nnv == 0 && nlv == 0) return;

    for (np = 0; np < rd->nPeriods; np++)
    {
        if (nnv > 0 && rd->nodeRptFlag > 0) renderNodeTable(rd, sk, np, htime);
        if (nlv > 0 && rd->linkRptFlag > 0) renderLinkTable(rd, sk, np, htime);
        htime += rd->rptStep;
    }
}

int rd_render_text(const RD_ReportData *rd, FILE *f)
{
    Sink sk;
    sk.f = f;
    sk.lineNum = 0;
    sk.pageNum = 0;
    sk.pageSize = rd->pageSize;
    sk.title0 = rd->title[0];

    renderLogo(rd, &sk);
    if (rd->summaryFlag) renderSummary(rd, &sk);
    renderTimestamp(&sk, "Analysis begun %s");

    /* Hydraulic status report (STATUS YES/FULL) would be rendered here.
       Not implemented yet - the first pass covers STATUS NO only.  See
       MISSING_API.md for the API gaps that block replicating it, and
       this spot as the future extension point.                          */

    /* Warning lines reconstructed by probing the API after each warned
       hydraulic step (see probeWarnings() in collect.c).  GAP: abnormal
       valve states (WARN05) and disconnected-node details (WARN03) are
       not reconstructable and will be missing here.                     */
    {
        int i;
        for (i = 0; i < rd->nWarnLines; i++) wline(&sk, rd->warnLines[i]);
    }

    renderTimestamp(&sk, "Analysis ended %s");

    if (rd->energyFlag) renderEnergy(rd, &sk);
    renderResults(rd, &sk);
    return 0;
}
