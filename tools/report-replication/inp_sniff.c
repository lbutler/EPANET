/*
 ******************************************************************************
 Project:      OWA EPANET - Report Replication Test Harness
 Module:       inp_sniff.c
 Description:  GAP WORKAROUND (see MISSING_API.md).

               The native report's content is controlled by [REPORT] settings
               (SUMMARY, PAGE, ENERGY, MESSAGES, NODES, LINKS, STATUS, field
               options).  The toolkit can WRITE these via EN_setreport but
               offers no way to READ them back, except the status level
               (EN_getoption(EN_STATUS_REPORT)).  Until getters exist, this
               module re-parses the [REPORT] section of the INP file with the
               same semantics as reportdata() in src/input3.c.

               rd_apply_report_command() is also used to mirror report
               commands passed on the command line (which are applied to the
               native engine via EN_setreport).
 ******************************************************************************
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "report_data.h"

#define MAXTOKS 40

/* mimics match() in src/epanet.c: case-insensitive check that str begins
   with all of substr (ignoring leading whitespace) */
static int match(const char *str, const char *substr)
{
    int i, j;
    for (i = 0; str[i]; i++)
    {
        if (!isspace((unsigned char)str[i])) break;
    }
    for (j = 0; substr[j]; j++, i++)
    {
        if (!str[i] || tolower((unsigned char)str[i]) !=
                       tolower((unsigned char)substr[j]))
            return 0;
    }
    return 1;
}

static int gettokens(char *line, char **tok)
{
    int n = 0;
    char *s = line;
    /* strip comment */
    char *c = strchr(line, ';');
    if (c) *c = '\0';
    while (n < MAXTOKS)
    {
        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s) break;
        tok[n++] = s;
        while (*s && !isspace((unsigned char)*s)) s++;
        if (*s) *s++ = '\0';
    }
    return n;
}

static int findNode(RD_ReportData *rd, const char *id)
{
    int i;
    for (i = 0; i < rd->nNodes; i++)
        if (strcmp(rd->nodeId[i], id) == 0) return i;
    return -1;
}

static int findLink(RD_ReportData *rd, const char *id)
{
    int i;
    for (i = 0; i < rd->nLinks; i++)
        if (strcmp(rd->linkId[i], id) == 0) return i;
    return -1;
}

/* Parse a single [REPORT]-section command with the semantics of
   reportdata() in src/input3.c and apply it to the replica model.
   Returns 0 if understood, 1 if the directive is not supported by the
   replica yet (a warning is printed).                                     */
int rd_apply_report_command(RD_ReportData *rd, const char *command)
{
    char buf[RD_MAXLINE];
    char *tok[MAXTOKS];
    int ntoks, n, i;

    snprintf(buf, sizeof(buf), "%s", command);
    ntoks = gettokens(buf, tok);
    if (ntoks < 1) return 0;
    n = ntoks - 1;

    if (match(tok[0], "PAGE"))
    {
        rd->pageSize = atoi(tok[n]);
        return 0;
    }
    if (match(tok[0], "STATUS"))
    {
        /* readable via EN_getoption(EN_STATUS_REPORT); nothing to sniff */
        if (match(tok[n], "NO"))   rd->statusLevel = RD_STATUS_NO;
        if (match(tok[n], "YES"))  rd->statusLevel = RD_STATUS_YES;
        if (match(tok[n], "FULL")) rd->statusLevel = RD_STATUS_FULL;
        return 0;
    }
    if (match(tok[0], "SUMMARY"))
    {
        if (match(tok[n], "NO"))  rd->summaryFlag = 0;
        if (match(tok[n], "YES")) rd->summaryFlag = 1;
        return 0;
    }
    if (match(tok[0], "MESSAGES"))
    {
        if (match(tok[n], "NO"))  rd->messageFlag = 0;
        if (match(tok[n], "YES")) rd->messageFlag = 1;
        return 0;
    }
    if (match(tok[0], "ENERGY"))
    {
        if (match(tok[n], "NO"))  rd->energyFlag = 0;
        if (match(tok[n], "YES")) rd->energyFlag = 1;
        return 0;
    }
    if (match(tok[0], "NODE"))
    {
        if (match(tok[n], "NONE")) rd->nodeRptFlag = 0;
        else if (match(tok[n], "ALL")) rd->nodeRptFlag = 1;
        else
        {
            for (i = 1; i < ntoks; i++)
            {
                int idx = findNode(rd, tok[i]);
                if (idx >= 0) rd->nodeRpt[idx] = 1;
            }
            rd->nodeRptFlag = 2;
        }
        return 0;
    }
    if (match(tok[0], "LINK"))
    {
        if (match(tok[n], "NONE")) rd->linkRptFlag = 0;
        else if (match(tok[n], "ALL")) rd->linkRptFlag = 1;
        else
        {
            for (i = 1; i < ntoks; i++)
            {
                int idx = findLink(rd, tok[i]);
                if (idx >= 0) rd->linkRpt[idx] = 1;
            }
            rd->linkRptFlag = 2;
        }
        return 0;
    }

    /* field directives (ELEVATION YES, PRESSURE PRECISION 3, DEMAND BELOW x,
       FILE ...) are not needed by the current test corpus; flag them so a
       failing comparison can be traced here                               */
    fprintf(stderr,
        "repgen: warning: unsupported [REPORT] directive ignored by the "
        "replica: \"%s\"\n", command);
    return 1;
}

int rd_sniff_inp_report_settings(RD_ReportData *rd, const char *inpFile)
{
    FILE *f = fopen(inpFile, "rt");
    char line[RD_MAXLINE];
    int inReport = 0;
    /* status level was already read back through the API
       (EN_getoption(EN_STATUS_REPORT)) - don't let the sniffer clobber a
       level set with EN_setstatusreport after EN_open                     */
    int apiStatusLevel = rd->statusLevel;

    /* defaults per setdefaults()/initreport() in src/input1.c */
    rd->summaryFlag = 1;
    rd->messageFlag = 1;
    rd->energyFlag = 0;
    rd->nodeRptFlag = 0;
    rd->linkRptFlag = 0;
    rd->pageSize = 0;

    if (!f) return 302;
    while (fgets(line, sizeof(line), f))
    {
        char *s = line;
        while (*s && isspace((unsigned char)*s)) s++;
        if (*s == '[')
        {
            inReport = match(s, "[REPORT");
            continue;
        }
        if (!inReport) continue;
        if (*s == '\0' || *s == ';') continue;
        rd_apply_report_command(rd, s);
    }
    fclose(f);
    rd->statusLevel = apiStatusLevel;
    return 0;
}
