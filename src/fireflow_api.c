/*
 ******************************************************************************
 Project:      OWA EPANET
 Module:       fireflow_api.c
 Description:  Fire flow analysis API implementation
 Authors:      AI Implementation based on Hernández (2024)
 License:      see LICENSE
 ******************************************************************************
*/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "types.h"
#include "funcs.h"
#include "fireflow.h"
#include "../include/epanet2.h"
#include "../include/epanet2_2.h"
#include "../include/epanet2_fireflow.h"

int EN_setfireflow(EN_Project ph, int enabled)
{
    Project *pr = (Project *)ph;
    
    if (pr == NULL) return 102;  // No network data
    if (pr->fireflow == NULL) initFireFlow(pr);
    if (pr->fireflow == NULL) return 101;  // Insufficient memory
    
    pr->fireflow->Enabled = enabled;
    return 0;
}

int EN_setfireflowhydrant(EN_Project ph, int nodeIndex)
{
    Project *pr = (Project *)ph;
    
    if (pr == NULL) return 102;
    if (pr->fireflow == NULL) return 105;  // Fire flow not initialized
    if (nodeIndex < 1 || nodeIndex > pr->network.Nnodes) return 203;  // Invalid node
    
    setFireFlowHydrant(pr, nodeIndex);
    return 0;
}

int EN_setfireflowparams(EN_Project ph, 
                         double pressureThreshold,
                         double velocityThreshold, 
                         double tolerance)
{
    Project *pr = (Project *)ph;
    
    if (pr == NULL) return 102;
    if (pr->fireflow == NULL) return 105;
    
    // Validate parameters
    if (pressureThreshold <= 0 || velocityThreshold <= 0 || tolerance <= 0) {
        return 202;  // Illegal numeric value
    }
    
    // Convert pressure from psi to feet
    pr->fireflow->PressureThreshold = pressureThreshold * 2.31;
    pr->fireflow->VelocityThreshold = velocityThreshold;
    pr->fireflow->Tolerance = tolerance / 448.831;  // Convert gpm to cfs
    
    return 0;
}

int EN_runfireflow(EN_Project ph,
                   EN_FireFlowType analysisType,
                   double *fireFlow,
                   int *criticalNode,
                   int *criticalLink)
{
    Project *pr = (Project *)ph;
    int errcode;
    long t;
    
    if (pr == NULL) return 102;
    if (pr->fireflow == NULL) return 105;
    if (!pr->hydraul.OpenHflag) return 103;  // Hydraulics not opened
    if (pr->fireflow->HydrantNode == 0) return 203;  // No hydrant set
    
    // Initialize outputs
    *fireFlow = 0.0;
    *criticalNode = 0;
    *criticalLink = 0;
    
    // Reset fire flow state
    resetFireFlow(pr);
    
    switch (analysisType) {
        case EN_FF_STATIC:
            // Run without fire flow
            pr->fireflow->Active = 0;
            errcode = EN_initH(ph, EN_NOSAVE);
            if (errcode) return errcode;
            errcode = EN_runH(ph, &t);
            if (errcode) return errcode;
            *fireFlow = 0.0;
            break;
            
        case EN_FF_DESIGN:
            // Run design fire flow analysis
            pr->fireflow->Active = 1;
            errcode = EN_initH(ph, EN_NOSAVE);
            if (errcode) return errcode;
            
            // Run hydraulic solver (which will call fire flow functions)
            errcode = EN_runH(ph, &t);
            // Ignore convergence warnings for fire flow
            if (errcode == 2) errcode = 0;
            
            // Return results
            *fireFlow = pr->fireflow->Qcurrent * 448.831;  // Convert to gpm
            if (pr->fireflow->IsCriticalPipe) {
                *criticalLink = pr->fireflow->CriticalElement;
                *criticalNode = 0;
            } else {
                *criticalNode = pr->fireflow->CriticalElement;
                *criticalLink = 0;
            }
            break;
            
        case EN_FF_REQUIRED:
        case EN_FF_AVAILABLE:
            // Not yet implemented - would need additional logic
            return 251;  // Function not implemented
            
        default:
            return 251;  // Invalid analysis type
    }
    
    return errcode;
}

int EN_getfireflowstatus(EN_Project ph,
                         int *converged,
                         int *iterations,
                         double *relativeCloseness)
{
    Project *pr = (Project *)ph;
    
    if (pr == NULL) return 102;
    if (pr->fireflow == NULL) return 105;
    
    *converged = pr->fireflow->Converged;
    *iterations = pr->fireflow->Iteration;
    
    // Get relative closeness of critical element
    if (pr->fireflow->CriticalElement > 0) {
        if (pr->fireflow->IsCriticalPipe) {
            *relativeCloseness = pr->fireflow->RelCloseness[pr->network.Njuncs + 
                                                           pr->fireflow->CriticalElement];
        } else {
            *relativeCloseness = pr->fireflow->RelCloseness[pr->fireflow->CriticalElement];
        }
    } else {
        *relativeCloseness = 0.0;
    }
    
    return 0;
}