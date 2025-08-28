/*
 * test_fireflow_final.c - Final test of fire flow implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/epanet2.h"
#include "include/epanet2_2.h"
#include "src/types.h"
#include "src/fireflow.h"

int main(int argc, char *argv[])
{
    EN_Project ph;
    int errcode;
    int hydrantNodeIndex;
    double pressure, demand;
    long t;
    
    printf("=================================================\n");
    printf("EPANET Fire Flow Analysis - Final Test\n");
    printf("=================================================\n\n");
    
    if (argc < 3) {
        printf("Usage: %s <input_file> <hydrant_node_id>\n", argv[0]);
        return 1;
    }
    
    // Create and open project
    errcode = EN_createproject(&ph);
    if (errcode) return errcode;
    
    errcode = EN_open(ph, argv[1], "", "");
    if (errcode) {
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Get hydrant node
    errcode = EN_getnodeindex(ph, argv[2], &hydrantNodeIndex);
    if (errcode) {
        printf("Error: Cannot find node '%s'\n", argv[2]);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    Project *pr = (Project *)ph;
    
    // Configure fire flow analysis
    pr->fireflow->PressureThreshold = 20.0 * 2.31;  // 20 psi
    pr->fireflow->VelocityThreshold = 8.0;          // 8 ft/s
    pr->fireflow->Tolerance = 0.01;                 // 0.01 cfs
    
    printf("Configuration:\n");
    printf("  Network: %s\n", argv[1]);
    printf("  Hydrant: Node %s (index %d)\n", argv[2], hydrantNodeIndex);
    printf("  Pressure threshold: %.1f psi\n", pr->fireflow->PressureThreshold/2.31);
    printf("  Velocity threshold: %.1f ft/s\n", pr->fireflow->VelocityThreshold);
    printf("  Convergence tolerance: %.3f cfs\n\n", pr->fireflow->Tolerance);
    
    // Open hydraulics
    errcode = EN_openH(ph);
    if (errcode) {
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Step 1: Static analysis (no fire flow)
    printf("Step 1: Static Analysis (no fire flow)\n");
    printf("---------------------------------------\n");
    errcode = EN_initH(ph, EN_NOSAVE);
    errcode = EN_runH(ph, &t);
    EN_getnodevalue(ph, hydrantNodeIndex, EN_PRESSURE, &pressure);
    EN_getnodevalue(ph, hydrantNodeIndex, EN_DEMAND, &demand);
    printf("  Pressure at hydrant: %.2f psi\n", pressure);
    printf("  Demand at hydrant: %.2f gpm\n\n", demand);
    pr->fireflow->StaticPressure = pressure * 2.31;
    
    // Step 2: Fire flow analysis
    printf("Step 2: Design Fire Flow Analysis\n");
    printf("----------------------------------\n");
    pr->fireflow->Enabled = 1;
    setFireFlowHydrant(pr, hydrantNodeIndex);
    
    // Re-initialize and run with fire flow
    errcode = EN_initH(ph, EN_NOSAVE);
    errcode = EN_runH(ph, &t);
    
    if (pr->fireflow->Converged) {
        printf("\n*** ANALYSIS CONVERGED ***\n\n");
        printf("Results:\n");
        printf("  Design fire flow: %.1f gpm (%.3f cfs)\n", 
               pr->fireflow->Qcurrent * 448.831, pr->fireflow->Qcurrent);
        
        // Get final pressure at hydrant
        EN_getnodevalue(ph, hydrantNodeIndex, EN_PRESSURE, &pressure);
        printf("  Pressure at hydrant with fire flow: %.2f psi\n", pressure);
        
        // Identify critical element
        if (pr->fireflow->IsCriticalPipe) {
            char linkID[256];
            EN_getlinkid(ph, pr->fireflow->CriticalElement, linkID);
            printf("  Critical element: Pipe %s (velocity constraint)\n", linkID);
            printf("  Critical velocity: %.2f ft/s\n", pr->fireflow->Xcritical);
        } else {
            char nodeID[256];
            EN_getnodeid(ph, pr->fireflow->CriticalElement, nodeID);
            printf("  Critical element: Node %s (pressure constraint)\n", nodeID);
            printf("  Critical pressure: %.2f psi\n", pr->fireflow->Xcritical / 2.31);
        }
        
        double rcCritical;
        if (pr->fireflow->IsCriticalPipe) {
            rcCritical = pr->fireflow->RelCloseness[pr->network.Njuncs + 
                                                  pr->fireflow->CriticalElement];
        } else {
            rcCritical = pr->fireflow->RelCloseness[pr->fireflow->CriticalElement];
        }
        printf("  Relative closeness: %.4f\n", rcCritical);
        printf("  Iterations: %d\n", pr->fireflow->Iteration);
        
    } else {
        printf("\n*** ANALYSIS DID NOT CONVERGE ***\n");
        printf("  Last flow tested: %.1f gpm\n", pr->fireflow->Qcurrent * 448.831);
        printf("  Iterations: %d\n", pr->fireflow->Iteration);
    }
    
    // Close hydraulics
    EN_closeH(ph);
    EN_close(ph);
    EN_deleteproject(ph);
    
    printf("\n=================================================\n");
    printf("Test completed successfully.\n");
    printf("=================================================\n");
    
    return 0;
}