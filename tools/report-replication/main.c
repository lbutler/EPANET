/*
 ******************************************************************************
 Project:      OWA EPANET - Report Replication Test Harness
 Module:       main.c
 Description:  Command line program that runs an EPANET simulation and
               writes TWO reports:

                 1. the native report produced by the engine itself
                 2. a replica generated purely from public toolkit API data

               Diffing the two (ignoring wall-clock timestamps) proves - or
               disproves - that the toolkit API exposes everything the
               native report contains.  Gaps found this way are logged in
               MISSING_API.md.

               Usage:
                 repgen <input.inp> <native.rpt> <replica.rpt> [options]

               Options:
                 --status no|yes|full   status report level (default: leave
                                        the INP file's setting untouched)
                 --set "<command>"      extra [REPORT]-style command, applied
                                        to the native engine via EN_setreport
                                        and mirrored by the replica (e.g.
                                        --set "NODES ALL" --set "ENERGY YES")
 ******************************************************************************
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "report_data.h"

#define MAX_SET_CMDS 32

static void usage(const char *prog)
{
    printf(
        "\nUsage:\n"
        "  %s <input.inp> <native.rpt> <replica.rpt> [options]\n\n"
        "Options:\n"
        "  --status no|yes|full   status report level override\n"
        "  --set \"<command>\"      extra [REPORT] command for both reports\n",
        prog);
}

int main(int argc, char *argv[])
{
    RD_ReportData rd;
    const char *inpFile, *nativeRpt, *replicaRpt;
    const char *setCmds[MAX_SET_CMDS];
    int nSetCmds = 0;
    int statusLevel = -1;   /* -1 = keep the INP file's setting */
    int i, err;
    char errmsg[256] = "";
    FILE *f;

    if (argc < 4)
    {
        usage(argv[0]);
        return 2;
    }
    inpFile = argv[1];
    nativeRpt = argv[2];
    replicaRpt = argv[3];

    for (i = 4; i < argc; i++)
    {
        if (strcmp(argv[i], "--status") == 0 && i + 1 < argc)
        {
            i++;
            if (strcmp(argv[i], "no") == 0)        statusLevel = RD_STATUS_NO;
            else if (strcmp(argv[i], "yes") == 0)  statusLevel = RD_STATUS_YES;
            else if (strcmp(argv[i], "full") == 0) statusLevel = RD_STATUS_FULL;
            else
            {
                fprintf(stderr, "repgen: bad --status value '%s'\n", argv[i]);
                return 2;
            }
        }
        else if (strcmp(argv[i], "--set") == 0 && i + 1 < argc)
        {
            if (nSetCmds >= MAX_SET_CMDS)
            {
                fprintf(stderr, "repgen: too many --set commands\n");
                return 2;
            }
            setCmds[nSetCmds++] = argv[++i];
        }
        else
        {
            fprintf(stderr, "repgen: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (statusLevel == RD_STATUS_FULL)
    {
        fprintf(stderr,
            "repgen: note: STATUS FULL requested - the replica reproduces "
            "the STATUS YES content but not the extra per-trial solver "
            "trace; expect differences.\n");
    }

    err = rd_collect(&rd, inpFile, nativeRpt, statusLevel,
                     setCmds, nSetCmds, errmsg, sizeof(errmsg));
    if (err > 100)
    {
        fprintf(stderr, "repgen: %s (%s)\n", errmsg, inpFile);
        rd_free(&rd);
        return 1;
    }

    f = fopen(replicaRpt, "wt");
    if (!f)
    {
        fprintf(stderr, "repgen: cannot open %s for writing\n", replicaRpt);
        rd_free(&rd);
        return 1;
    }
    rd_render_text(&rd, f);
    fclose(f);
    rd_free(&rd);

    printf("repgen: wrote %s (native) and %s (replica)\n",
           nativeRpt, replicaRpt);
    return 0;
}
