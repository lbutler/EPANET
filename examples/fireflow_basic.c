/*
 * fireflow_basic.c
 * 
 * Basic example of fire flow analysis using EPANET
 * 
 * Usage: ./fireflow_basic <network.inp> <hydrant_id>
 * Example: ./fireflow_basic Net1.inp 11
 */

#include <stdio.h>
#include <stdlib.h>
#include "../include/epanet2.h"
#include "../include/epanet2_2.h"
#include "../include/epanet2_fireflow.h"

int main(int argc, char *argv[])
{
    EN_Project ph;
    int err, hydrantIdx;
    double fireFlow;
    int criticalNode, criticalLink;
    char elementID[256];
    
    // Check arguments
    if (argc != 3) {
        printf("Usage: %s <network.inp> <hydrant_id>\n", argv[0]);
        printf("Example: %s Net1.inp 11\n", argv[0]);
        return 1;
    }
    
    // Create EPANET project
    err = EN_createproject(&ph);
    if (err) {
        printf("Error creating project: %d\n", err);
        return err;
    }
    
    // Open the network file
    printf("Opening network: %s\n", argv[1]);
    err = EN_open(ph, argv[1], "", "");
    if (err) {
        printf("Error opening network: %d\n", err);
        EN_deleteproject(ph);
        return err;
    }
    
    // Find the hydrant node
    err = EN_getnodeindex(ph, argv[2], &hydrantIdx);
    if (err) {
        printf("Error: Cannot find node '%s'\n", argv[2]);
        EN_close(ph);
        EN_deleteproject(ph);
        return err;
    }
    
    // Enable fire flow analysis
    EN_setfireflow(ph, 1);
    
    // Set the hydrant location
    EN_setfireflowhydrant(ph, hydrantIdx);
    
    // Configure constraints
    // 20 psi minimum pressure, 10 ft/s maximum velocity, 5 gpm tolerance
    EN_setfireflowparams(ph, 20.0, 10.0, 5.0);
    
    // Open hydraulics solver
    EN_openH(ph);
    
    // Run fire flow analysis
    printf("\nAnalyzing fire flow at node %s...\n", argv[2]);
    err = EN_runfireflow(ph, EN_FF_DESIGN, &fireFlow, 
                        &criticalNode, &criticalLink);
    
    if (err && err != 2) {  // Error 2 is convergence warning, which is OK
        printf("Error in analysis: %d\n", err);
    } else {
        // Display results
        printf("\n=== RESULTS ===\n");
        printf("Design Fire Flow: %.1f gpm\n", fireFlow);
        
        // Show critical element
        if (criticalNode > 0) {
            EN_getnodeid(ph, criticalNode, elementID);
            printf("Critical Element: Node %s (pressure constraint)\n", elementID);
        } else if (criticalLink > 0) {
            EN_getlinkid(ph, criticalLink, elementID);
            printf("Critical Element: Pipe %s (velocity constraint)\n", elementID);
        }
        
        // Get convergence details
        int converged, iterations;
        double relCloseness;
        EN_getfireflowstatus(ph, &converged, &iterations, &relCloseness);
        
        printf("Convergence: %s\n", converged ? "Yes" : "No");
        printf("Iterations: %d\n", iterations);
        printf("Constraint Margin: %.2f%%\n", relCloseness * 100);
    }
    
    // Clean up
    EN_closeH(ph);
    EN_close(ph);
    EN_deleteproject(ph);
    
    return 0;
}