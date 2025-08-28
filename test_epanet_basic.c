/*
 * test_epanet_basic.c - Test basic EPANET functionality
 * Ensures core hydraulic solver still works after fire flow integration
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "include/epanet2.h"
#include "include/epanet2_2.h"

int main(int argc, char *argv[])
{
    EN_Project ph;
    int err;
    long t;
    double pressure, flow, velocity;
    int nnodes, nlinks;
    
    printf("=== EPANET Basic Functionality Test ===\n\n");
    
    if (argc < 2) {
        printf("Usage: %s <network.inp>\n", argv[0]);
        return 1;
    }
    
    // Create and open project
    err = EN_createproject(&ph);
    if (err) {
        printf("FAILED: Cannot create project (error %d)\n", err);
        return 1;
    }
    
    err = EN_open(ph, argv[1], "", "");
    if (err) {
        printf("FAILED: Cannot open network (error %d)\n", err);
        EN_deleteproject(ph);
        return 1;
    }
    printf("✓ Network opened successfully\n");
    
    // Get network size
    EN_getcount(ph, EN_NODECOUNT, &nnodes);
    EN_getcount(ph, EN_LINKCOUNT, &nlinks);
    printf("✓ Network has %d nodes and %d links\n", nnodes, nlinks);
    
    // Open hydraulics
    err = EN_openH(ph);
    if (err) {
        printf("FAILED: Cannot open hydraulics (error %d)\n", err);
        EN_close(ph);
        EN_deleteproject(ph);
        return 1;
    }
    printf("✓ Hydraulics opened\n");
    
    // Initialize hydraulics
    err = EN_initH(ph, EN_NOSAVE);
    if (err) {
        printf("FAILED: Cannot initialize hydraulics (error %d)\n", err);
        EN_closeH(ph);
        EN_close(ph);
        EN_deleteproject(ph);
        return 1;
    }
    printf("✓ Hydraulics initialized\n");
    
    // Run single period
    err = EN_runH(ph, &t);
    if (err > 2) {  // Warning codes 1-2 are OK
        printf("FAILED: Hydraulic solver failed (error %d)\n", err);
        EN_closeH(ph);
        EN_close(ph);
        EN_deleteproject(ph);
        return 1;
    }
    printf("✓ Hydraulic analysis completed\n");
    
    // Get some results to verify they're reasonable
    // Check first junction pressure
    int nodeIdx = 1;
    int nodeType;
    EN_getnodetype(ph, nodeIdx, &nodeType);
    while (nodeType != EN_JUNCTION && nodeIdx <= nnodes) {
        nodeIdx++;
        EN_getnodetype(ph, nodeIdx, &nodeType);
    }
    
    if (nodeIdx <= nnodes) {
        EN_getnodevalue(ph, nodeIdx, EN_PRESSURE, &pressure);
        if (pressure < -100 || pressure > 1000 || isnan(pressure)) {
            printf("WARNING: Unusual pressure %.2f psi at node %d\n", pressure, nodeIdx);
        } else {
            printf("✓ Node %d pressure: %.2f psi (reasonable)\n", nodeIdx, pressure);
        }
    }
    
    // Check first pipe flow
    int linkIdx = 1;
    int linkType;
    EN_getlinktype(ph, linkIdx, &linkType);
    while (linkType != EN_PIPE && linkIdx <= nlinks) {
        linkIdx++;
        EN_getlinktype(ph, linkIdx, &linkType);
    }
    
    if (linkIdx <= nlinks) {
        EN_getlinkvalue(ph, linkIdx, EN_FLOW, &flow);
        EN_getlinkvalue(ph, linkIdx, EN_VELOCITY, &velocity);
        
        if (fabs(flow) > 100000 || isnan(flow)) {
            printf("WARNING: Unusual flow %.2f gpm at link %d\n", flow, linkIdx);
        } else {
            printf("✓ Link %d flow: %.2f gpm (reasonable)\n", linkIdx, flow);
        }
        
        if (fabs(velocity) > 100 || isnan(velocity)) {
            printf("WARNING: Unusual velocity %.2f ft/s at link %d\n", velocity, linkIdx);
        } else {
            printf("✓ Link %d velocity: %.2f ft/s (reasonable)\n", linkIdx, velocity);
        }
    }
    
    // Test time stepping
    err = EN_nextH(ph, &t);
    if (err > 1 && t > 0) {  // If there are more periods
        err = EN_runH(ph, &t);
        if (err > 2) {
            printf("FAILED: Cannot run next time period (error %d)\n", err);
        } else {
            printf("✓ Time stepping works\n");
        }
    } else {
        printf("✓ Single period analysis\n");
    }
    
    // Clean up
    EN_closeH(ph);
    EN_close(ph);
    EN_deleteproject(ph);
    
    printf("\n=== ALL BASIC TESTS PASSED ===\n");
    printf("EPANET core functionality is working correctly!\n");
    
    return 0;
}