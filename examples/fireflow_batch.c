/*
 * fireflow_batch.c
 * 
 * Batch fire flow analysis for multiple hydrants
 * Demonstrates analyzing all junctions in a network
 * 
 * Usage: ./fireflow_batch <network.inp>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/epanet2.h"
#include "../include/epanet2_2.h"
#include "../include/epanet2_fireflow.h"

typedef struct {
    int index;
    char id[256];
    double fireFlow;
    int criticalNode;
    int criticalLink;
    int converged;
    double relCloseness;
} HydrantResult;

int compareResults(const void *a, const void *b) {
    HydrantResult *ha = (HydrantResult *)a;
    HydrantResult *hb = (HydrantResult *)b;
    // Sort by fire flow descending
    if (ha->fireFlow < hb->fireFlow) return 1;
    if (ha->fireFlow > hb->fireFlow) return -1;
    return 0;
}

int main(int argc, char *argv[])
{
    EN_Project ph;
    int err, numNodes, nodeType;
    HydrantResult *results;
    int numHydrants = 0;
    
    if (argc != 2) {
        printf("Usage: %s <network.inp>\n", argv[0]);
        return 1;
    }
    
    // Initialize EPANET
    EN_createproject(&ph);
    EN_open(ph, argv[1], "", "");
    EN_openH(ph);
    
    // Get number of nodes
    EN_getcount(ph, EN_NODECOUNT, &numNodes);
    results = calloc(numNodes, sizeof(HydrantResult));
    
    // Configure fire flow parameters
    EN_setfireflow(ph, 1);
    EN_setfireflowparams(ph, 20.0, 10.0, 5.0);
    
    printf("Analyzing fire flow for all junctions in %s\n", argv[1]);
    printf("Parameters: 20 psi min pressure, 10 ft/s max velocity\n");
    printf("This may take a few minutes...\n\n");
    
    // Analyze each junction
    for (int i = 1; i <= numNodes; i++) {
        // Check if it's a junction (not tank or reservoir)
        EN_getnodetype(ph, i, &nodeType);
        if (nodeType != EN_JUNCTION) continue;
        
        // Get node ID
        EN_getnodeid(ph, i, results[numHydrants].id);
        results[numHydrants].index = i;
        
        // Set as hydrant
        EN_setfireflowhydrant(ph, i);
        
        // Run analysis
        err = EN_runfireflow(ph, EN_FF_DESIGN, 
                            &results[numHydrants].fireFlow,
                            &results[numHydrants].criticalNode,
                            &results[numHydrants].criticalLink);
        
        if (err == 0 || err == 2) {
            // Get status
            int iter;
            EN_getfireflowstatus(ph, 
                                &results[numHydrants].converged,
                                &iter,
                                &results[numHydrants].relCloseness);
            
            printf(".");
            fflush(stdout);
            numHydrants++;
        }
    }
    
    printf("\n\nAnalysis complete. Processed %d hydrants.\n\n", numHydrants);
    
    // Sort results by fire flow
    qsort(results, numHydrants, sizeof(HydrantResult), compareResults);
    
    // Display summary table
    printf("%-12s %10s %10s %12s %s\n", 
           "Hydrant", "Flow (gpm)", "Converged", "Margin (%)", "Critical Element");
    printf("%-12s %10s %10s %12s %s\n",
           "-------", "----------", "---------", "-----------", "----------------");
    
    for (int i = 0; i < numHydrants && i < 20; i++) {  // Show top 20
        char criticalID[256] = "N/A";
        
        if (results[i].criticalNode > 0) {
            EN_getnodeid(ph, results[i].criticalNode, criticalID);
            strcat(criticalID, " (P)");  // Pressure
        } else if (results[i].criticalLink > 0) {
            EN_getlinkid(ph, results[i].criticalLink, criticalID);
            strcat(criticalID, " (V)");  // Velocity
        }
        
        printf("%-12s %10.1f %10s %11.2f%% %s\n",
               results[i].id,
               results[i].fireFlow,
               results[i].converged ? "Yes" : "No",
               results[i].relCloseness * 100,
               criticalID);
    }
    
    if (numHydrants > 20) {
        printf("\n(Showing top 20 of %d hydrants)\n", numHydrants);
    }
    
    // Find statistics
    double totalFlow = 0, minFlow = 1e9, maxFlow = 0;
    int numConverged = 0;
    
    for (int i = 0; i < numHydrants; i++) {
        totalFlow += results[i].fireFlow;
        if (results[i].fireFlow < minFlow) minFlow = results[i].fireFlow;
        if (results[i].fireFlow > maxFlow) maxFlow = results[i].fireFlow;
        if (results[i].converged) numConverged++;
    }
    
    printf("\n=== STATISTICS ===\n");
    printf("Total Hydrants: %d\n", numHydrants);
    printf("Converged: %d (%.1f%%)\n", 
           numConverged, 100.0 * numConverged / numHydrants);
    printf("Maximum Flow: %.1f gpm\n", maxFlow);
    printf("Minimum Flow: %.1f gpm\n", minFlow);
    printf("Average Flow: %.1f gpm\n", totalFlow / numHydrants);
    
    // Clean up
    free(results);
    EN_closeH(ph);
    EN_close(ph);
    EN_deleteproject(ph);
    
    return 0;
}