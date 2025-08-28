/*
 * test_fireflow_api.c - Test the fire flow API
 */

#include <stdio.h>
#include <stdlib.h>
#include "include/epanet2.h"
#include "include/epanet2_2.h"
#include "include/epanet2_fireflow.h"

int main(int argc, char *argv[])
{
    EN_Project ph;
    int errcode;
    int hydrantIndex;
    double fireFlow;
    int criticalNode, criticalLink;
    int converged, iterations;
    double relCloseness;
    char nodeID[256], linkID[256];
    
    printf("========================================\n");
    printf("Fire Flow API Test\n");
    printf("========================================\n\n");
    
    if (argc < 3) {
        printf("Usage: %s <input_file> <hydrant_node>\n", argv[0]);
        return 1;
    }
    
    // Create project
    errcode = EN_createproject(&ph);
    if (errcode) {
        printf("Error creating project: %d\n", errcode);
        return errcode;
    }
    
    // Open network
    printf("Opening network: %s\n", argv[1]);
    errcode = EN_open(ph, argv[1], "", "");
    if (errcode) {
        printf("Error opening network: %d\n", errcode);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Get hydrant node
    errcode = EN_getnodeindex(ph, argv[2], &hydrantIndex);
    if (errcode) {
        printf("Error finding node %s: %d\n", argv[2], errcode);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    printf("Hydrant: %s (index %d)\n\n", argv[2], hydrantIndex);
    
    // Enable fire flow analysis
    errcode = EN_setfireflow(ph, 1);
    if (errcode) {
        printf("Error enabling fire flow: %d\n", errcode);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Set hydrant
    errcode = EN_setfireflowhydrant(ph, hydrantIndex);
    if (errcode) {
        printf("Error setting hydrant: %d\n", errcode);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Set parameters (pressure in psi, velocity in ft/s, tolerance in gpm)
    errcode = EN_setfireflowparams(ph, 20.0, 8.0, 5.0);
    if (errcode) {
        printf("Error setting parameters: %d\n", errcode);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    printf("Parameters:\n");
    printf("  Pressure threshold: 20.0 psi\n");
    printf("  Velocity threshold: 8.0 ft/s\n");
    printf("  Tolerance: 5.0 gpm\n\n");
    
    // Open hydraulics
    errcode = EN_openH(ph);
    if (errcode) {
        printf("Error opening hydraulics: %d\n", errcode);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Run static analysis
    printf("Running static analysis...\n");
    errcode = EN_runfireflow(ph, EN_FF_STATIC, &fireFlow, &criticalNode, &criticalLink);
    if (errcode) {
        printf("Error in static analysis: %d\n", errcode);
    } else {
        printf("  Static flow: %.1f gpm\n\n", fireFlow);
    }
    
    // Run design fire flow analysis
    printf("Running design fire flow analysis...\n");
    errcode = EN_runfireflow(ph, EN_FF_DESIGN, &fireFlow, &criticalNode, &criticalLink);
    if (errcode) {
        printf("Error in fire flow analysis: %d\n", errcode);
    } else {
        printf("  Design fire flow: %.1f gpm\n", fireFlow);
        
        // Get critical element info
        if (criticalNode > 0) {
            EN_getnodeid(ph, criticalNode, nodeID);
            printf("  Critical element: Node %s\n", nodeID);
        } else if (criticalLink > 0) {
            EN_getlinkid(ph, criticalLink, linkID);
            printf("  Critical element: Pipe %s\n", linkID);
        }
        
        // Get convergence status
        errcode = EN_getfireflowstatus(ph, &converged, &iterations, &relCloseness);
        if (errcode == 0) {
            printf("  Converged: %s\n", converged ? "Yes" : "No");
            printf("  Iterations: %d\n", iterations);
            printf("  Relative closeness: %.4f\n", relCloseness);
        }
    }
    
    // Clean up
    EN_closeH(ph);
    EN_close(ph);
    EN_deleteproject(ph);
    
    printf("\n========================================\n");
    printf("Test completed\n");
    printf("========================================\n");
    
    return 0;
}