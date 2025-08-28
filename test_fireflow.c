/*
 * test_fireflow.c - Simple test program for fire flow implementation
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
    char hydrantID[256];
    double pressure, demand, flow;
    int iterations;
    double relerr;
    
    printf("EPANET Fire Flow Test Program\n");
    printf("==============================\n\n");
    
    // Check command line arguments
    if (argc < 3) {
        printf("Usage: %s <input_file> <hydrant_node_id>\n", argv[0]);
        printf("Example: %s network.inp J10\n", argv[0]);
        return 1;
    }
    
    // Create and open project
    errcode = EN_createproject(&ph);
    if (errcode) {
        printf("Error creating project: %d\n", errcode);
        return errcode;
    }
    
    printf("Opening network file: %s\n", argv[1]);
    errcode = EN_open(ph, argv[1], "fireflow_report.txt", "");
    if (errcode) {
        printf("Error opening project: %d\n", errcode);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Get hydrant node index
    strcpy(hydrantID, argv[2]);
    errcode = EN_getnodeindex(ph, hydrantID, &hydrantNodeIndex);
    if (errcode) {
        printf("Error: Cannot find node '%s' in network\n", hydrantID);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    printf("Hydrant node: %s (index %d)\n\n", hydrantID, hydrantNodeIndex);
    
    // Get the internal project structure
    Project *pr = (Project *)ph;
    
    // Initialize and activate fire flow analysis
    if (pr->fireflow == NULL) {
        printf("Fire flow module not initialized properly!\n");
        EN_close(ph);
        EN_deleteproject(ph);
        return 1;
    }
    
    // Enable fire flow and set hydrant
    pr->fireflow->Enabled = 1;
    setFireFlowHydrant(pr, hydrantNodeIndex);
    
    // Set initial fire flow parameters
    pr->fireflow->PressureThreshold = 20.0 * 2.31;  // 20 psi in feet
    pr->fireflow->VelocityThreshold = 10.0;         // 10 ft/s
    pr->fireflow->Tolerance = 0.01;                 // 0.01 cfs
    
    printf("Fire Flow Analysis Settings:\n");
    printf("  Pressure threshold: %.1f psi (%.1f ft)\n", 20.0, pr->fireflow->PressureThreshold);
    printf("  Velocity threshold: %.1f ft/s\n", pr->fireflow->VelocityThreshold);
    printf("  Convergence tolerance: %.3f cfs\n\n", pr->fireflow->Tolerance);
    
    // Open hydraulics solver
    errcode = EN_openH(ph);
    if (errcode) {
        printf("Error opening hydraulics: %d\n", errcode);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Initialize hydraulics
    errcode = EN_initH(ph, EN_NOSAVE);
    if (errcode) {
        printf("Error initializing hydraulics: %d\n", errcode);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Run static analysis first (no fire flow)
    printf("Running static analysis (no fire flow)...\n");
    
    // Run hydraulic analysis
    long t;
    errcode = EN_runH(ph, &t);
    if (errcode) {
        printf("Error running hydraulics: %d\n", errcode);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Get static pressure
    EN_getnodevalue(ph, hydrantNodeIndex, EN_PRESSURE, &pressure);
    EN_getnodevalue(ph, hydrantNodeIndex, EN_DEMAND, &demand);
    printf("Static conditions at hydrant:\n");
    printf("  Pressure: %.2f psi\n", pressure);
    printf("  Demand: %.2f gpm\n", demand);
    
    // Store static pressure
    pr->fireflow->StaticPressure = pressure * 2.31;  // Convert to feet
    
    // Now run with maximum available flow (pressure at minimum)
    printf("\nDetermining available flow at minimum pressure...\n");
    
    // This would normally involve iterating to find maximum flow at min pressure
    // For now, we'll use a simple estimate
    pr->fireflow->AvailableFlow = 1000.0 / 448.831;  // 1000 gpm to cfs
    pr->fireflow->AvailablePressure = pr->fireflow->PressureThreshold;
    
    printf("Estimated available flow: %.0f gpm\n", pr->fireflow->AvailableFlow * 448.831);
    
    // Now run fire flow analysis
    printf("\nRunning fire flow analysis...\n");
    printf("Iteration | Fire Flow (gpm) | Critical Pressure (psi) | Converged\n");
    printf("----------|-----------------|------------------------|----------\n");
    
    // Activate fire flow analysis
    pr->fireflow->Active = 1;
    pr->fireflow->Converged = 0;
    
    // Run hydraulic solver with fire flow
    int maxIterations = 50;
    for (int i = 0; i < maxIterations && !pr->fireflow->Converged; i++) {
        // Run one hydraulic analysis iteration
        errcode = EN_solveH(ph);
        if (errcode) {
            printf("Error in hydraulic solution: %d\n", errcode);
            break;
        }
        
        // Print iteration results
        flow = pr->fireflow->Qcurrent * 448.831;  // Convert cfs to gpm
        double critPressure = pr->fireflow->Xcritical / 2.31;  // Convert ft to psi
        
        printf("%9d | %15.1f | %22.2f | %s\n", 
               i+1, flow, critPressure, 
               pr->fireflow->Converged ? "Yes" : "No");
        
        if (pr->fireflow->Converged) {
            break;
        }
    }
    
    if (pr->fireflow->Converged) {
        printf("\n*** Fire flow analysis converged! ***\n");
        printf("Design fire flow: %.1f gpm\n", pr->fireflow->Qcurrent * 448.831);
        
        // Identify critical element
        if (pr->fireflow->IsCriticalPipe) {
            printf("Critical element: Pipe %d (velocity constraint)\n", 
                   pr->fireflow->CriticalElement);
        } else {
            printf("Critical element: Junction %d (pressure constraint)\n", 
                   pr->fireflow->CriticalElement);
        }
    } else {
        printf("\n*** Fire flow analysis did not converge ***\n");
    }
    
    // Clean up
    EN_close(ph);
    EN_deleteproject(ph);
    
    printf("\nTest completed.\n");
    return 0;
}