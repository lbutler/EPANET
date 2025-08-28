/*
 * fireflow_sensitivity.c
 * 
 * Sensitivity analysis of fire flow to constraint parameters
 * Shows how fire flow changes with different pressure/velocity thresholds
 * 
 * Usage: ./fireflow_sensitivity <network.inp> <hydrant_id>
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../include/epanet2.h"
#include "../include/epanet2_2.h"
#include "../include/epanet2_fireflow.h"

void printBar(double value, double maxValue, int width) {
    int filled = (int)((value / maxValue) * width);
    printf("[");
    for (int i = 0; i < width; i++) {
        if (i < filled) printf("█");
        else printf(" ");
    }
    printf("]");
}

int main(int argc, char *argv[])
{
    EN_Project ph;
    int err, hydrantIdx;
    double flow;
    int critNode, critLink;
    char critID[256];
    
    if (argc != 3) {
        printf("Usage: %s <network.inp> <hydrant_id>\n", argv[0]);
        return 1;
    }
    
    // Initialize
    EN_createproject(&ph);
    EN_open(ph, argv[1], "", "");
    EN_getnodeindex(ph, argv[2], &hydrantIdx);
    EN_openH(ph);
    
    // Enable fire flow
    EN_setfireflow(ph, 1);
    EN_setfireflowhydrant(ph, hydrantIdx);
    
    printf("Fire Flow Sensitivity Analysis\n");
    printf("Network: %s\n", argv[1]);
    printf("Hydrant: %s\n\n", argv[2]);
    
    // Test 1: Vary pressure threshold
    printf("=== PRESSURE THRESHOLD SENSITIVITY ===\n");
    printf("(Fixed velocity threshold: 10 ft/s)\n\n");
    
    double pressures[] = {10, 15, 20, 25, 30, 35, 40};
    int numP = sizeof(pressures) / sizeof(double);
    double maxFlowP = 0;
    double flowsP[10];
    
    // First pass to find max
    for (int i = 0; i < numP; i++) {
        EN_setfireflowparams(ph, pressures[i], 10.0, 5.0);
        EN_runfireflow(ph, EN_FF_DESIGN, &flowsP[i], &critNode, &critLink);
        if (flowsP[i] > maxFlowP) maxFlowP = flowsP[i];
    }
    
    // Second pass to display
    printf("Pressure  Flow (gpm)  Critical        Graph\n");
    printf("--------  ----------  -------------   -------------------------\n");
    
    for (int i = 0; i < numP; i++) {
        EN_setfireflowparams(ph, pressures[i], 10.0, 5.0);
        EN_runfireflow(ph, EN_FF_DESIGN, &flow, &critNode, &critLink);
        
        strcpy(critID, "");
        if (critNode > 0) {
            EN_getnodeid(ph, critNode, critID);
        } else if (critLink > 0) {
            EN_getlinkid(ph, critLink, critID);
        }
        
        printf("%5.0f psi %8.1f   %-12s   ", pressures[i], flow, critID);
        printBar(flow, maxFlowP, 25);
        printf(" %.0f%%\n", (flow / maxFlowP) * 100);
    }
    
    // Test 2: Vary velocity threshold
    printf("\n=== VELOCITY THRESHOLD SENSITIVITY ===\n");
    printf("(Fixed pressure threshold: 20 psi)\n\n");
    
    double velocities[] = {5, 6, 7, 8, 9, 10, 12, 15, 20};
    int numV = sizeof(velocities) / sizeof(double);
    double maxFlowV = 0;
    double flowsV[10];
    
    // First pass to find max
    for (int i = 0; i < numV; i++) {
        EN_setfireflowparams(ph, 20.0, velocities[i], 5.0);
        EN_runfireflow(ph, EN_FF_DESIGN, &flowsV[i], &critNode, &critLink);
        if (flowsV[i] > maxFlowV) maxFlowV = flowsV[i];
    }
    
    // Second pass to display
    printf("Velocity  Flow (gpm)  Critical        Graph\n");
    printf("--------  ----------  -------------   -------------------------\n");
    
    for (int i = 0; i < numV; i++) {
        EN_setfireflowparams(ph, 20.0, velocities[i], 5.0);
        EN_runfireflow(ph, EN_FF_DESIGN, &flow, &critNode, &critLink);
        
        strcpy(critID, "");
        if (critNode > 0) {
            EN_getnodeid(ph, critNode, critID);
        } else if (critLink > 0) {
            EN_getlinkid(ph, critLink, critID);
        }
        
        printf("%5.0f ft/s %7.1f   %-12s   ", velocities[i], flow, critID);
        printBar(flow, maxFlowV, 25);
        printf(" %.0f%%\n", (flow / maxFlowV) * 100);
    }
    
    // Test 3: 2D sensitivity matrix
    printf("\n=== 2D SENSITIVITY MATRIX ===\n");
    printf("Fire flow (gpm) for different constraint combinations\n\n");
    
    double matrixP[] = {15, 20, 25, 30};
    double matrixV[] = {6, 8, 10, 12};
    int nP = 4, nV = 4;
    
    // Header
    printf("         ");
    for (int v = 0; v < nV; v++) {
        printf("%7.0f ft/s ", matrixV[v]);
    }
    printf("\n");
    
    // Data rows
    for (int p = 0; p < nP; p++) {
        printf("%5.0f psi ", matrixP[p]);
        for (int v = 0; v < nV; v++) {
            EN_setfireflowparams(ph, matrixP[p], matrixV[v], 5.0);
            EN_runfireflow(ph, EN_FF_DESIGN, &flow, NULL, NULL);
            printf("%10.1f ", flow);
        }
        printf("\n");
    }
    
    // Test 4: Convergence analysis
    printf("\n=== CONVERGENCE ANALYSIS ===\n");
    printf("Effect of tolerance on results\n\n");
    
    double tolerances[] = {50, 20, 10, 5, 2, 1, 0.5};
    int numT = sizeof(tolerances) / sizeof(double);
    
    printf("Tolerance  Flow (gpm)  Iterations  Time Ratio\n");
    printf("---------  ----------  ----------  ----------\n");
    
    int baseIter = 0;
    for (int i = 0; i < numT; i++) {
        EN_setfireflowparams(ph, 20.0, 10.0, tolerances[i]);
        EN_runfireflow(ph, EN_FF_DESIGN, &flow, NULL, NULL);
        
        int conv, iter;
        double rc;
        EN_getfireflowstatus(ph, &conv, &iter, &rc);
        
        if (i == 0) baseIter = iter;
        double ratio = (double)iter / baseIter;
        
        printf("%7.1f gpm %8.1f %10d %10.2fx\n", 
               tolerances[i], flow, iter, ratio);
    }
    
    printf("\n=== ANALYSIS COMPLETE ===\n");
    
    // Clean up
    EN_closeH(ph);
    EN_close(ph);
    EN_deleteproject(ph);
    
    return 0;
}