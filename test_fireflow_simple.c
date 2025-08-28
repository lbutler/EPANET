/*
 * test_fireflow_simple.c - Simple test to verify fire flow integration
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
    double pressure, demand, flow;
    long t;
    
    printf("EPANET Fire Flow Integration Test\n");
    printf("==================================\n\n");
    
    // Check command line arguments
    if (argc < 3) {
        printf("Usage: %s <input_file> <hydrant_node_id>\n", argv[0]);
        return 1;
    }
    
    // Create and open project
    errcode = EN_createproject(&ph);
    if (errcode) {
        printf("Error creating project: %d\n", errcode);
        return errcode;
    }
    
    printf("Opening network file: %s\n", argv[1]);
    errcode = EN_open(ph, argv[1], "", "");
    if (errcode) {
        printf("Error opening project: %d\n", errcode);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Get hydrant node index
    errcode = EN_getnodeindex(ph, argv[2], &hydrantNodeIndex);
    if (errcode) {
        printf("Error: Cannot find node '%s' in network\n", argv[2]);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    printf("Hydrant node: %s (index %d)\n\n", argv[2], hydrantNodeIndex);
    
    // Get the internal project structure
    Project *pr = (Project *)ph;
    
    // Check if fire flow module is initialized
    if (pr->fireflow == NULL) {
        printf("ERROR: Fire flow module not initialized!\n");
    } else {
        printf("SUCCESS: Fire flow module initialized\n");
        
        // Override thresholds for this small-pipe network
        pr->fireflow->PressureThreshold = 20.0 * 2.31;  // 20 psi
        pr->fireflow->VelocityThreshold = 50.0;  // 50 ft/s (more reasonable for small pipes)
        
        printf("  Enabled: %d\n", pr->fireflow->Enabled);
        printf("  Active: %d\n", pr->fireflow->Active);
        printf("  Pressure threshold: %.1f ft (%.1f psi)\n", 
               pr->fireflow->PressureThreshold, pr->fireflow->PressureThreshold/2.31);
        printf("  Velocity threshold: %.1f ft/s\n", pr->fireflow->VelocityThreshold);
        printf("  Tolerance: %.3f cfs\n", pr->fireflow->Tolerance);
    }
    
    // Open hydraulics
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
        EN_closeH(ph);
        EN_close(ph);
        EN_deleteproject(ph);
        return errcode;
    }
    
    // Run one hydraulic timestep
    printf("\nRunning hydraulic analysis...\n");
    errcode = EN_runH(ph, &t);
    if (errcode) {
        printf("Error running hydraulics: %d\n", errcode);
    } else {
        printf("Hydraulic analysis successful at time %ld\n", t);
        
        // Get results at hydrant
        EN_getnodevalue(ph, hydrantNodeIndex, EN_PRESSURE, &pressure);
        EN_getnodevalue(ph, hydrantNodeIndex, EN_DEMAND, &demand);
        printf("Results at hydrant:\n");
        printf("  Pressure: %.2f psi\n", pressure);
        printf("  Demand: %.2f gpm\n", demand);
    }
    
    // Test fire flow activation
    printf("\nActivating fire flow analysis...\n");
    if (pr->fireflow != NULL) {
        pr->fireflow->Enabled = 1;
        setFireFlowHydrant(pr, hydrantNodeIndex);
        
        if (isFireFlowActive(pr)) {
            printf("Fire flow is ACTIVE for node %d\n", hydrantNodeIndex);
            
            // Don't manually set Qcurrent - let the algorithm generate it
            printf("Starting fire flow algorithm...\n");
            
            // Run another timestep with fire flow
            errcode = EN_runH(ph, &t);
            if (errcode) {
                printf("Error running hydraulics with fire flow: %d\n", errcode);
            } else {
                printf("Hydraulic analysis with fire flow successful\n");
                
                // Get updated results
                EN_getnodevalue(ph, hydrantNodeIndex, EN_PRESSURE, &pressure);
                EN_getnodevalue(ph, hydrantNodeIndex, EN_DEMAND, &demand);
                printf("Results with fire flow:\n");
                printf("  Pressure: %.2f psi\n", pressure);
                printf("  Total demand: %.2f gpm\n", demand);
            }
        } else {
            printf("Fire flow is NOT ACTIVE\n");
        }
    }
    
    // Close hydraulics
    EN_closeH(ph);
    
    // Clean up
    EN_close(ph);
    EN_deleteproject(ph);
    
    printf("\nTest completed.\n");
    return 0;
}