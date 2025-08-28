/*
 ******************************************************************************
 Project:      OWA EPANET
 Module:       epanet2_fireflow.h
 Description:  Fire flow analysis API for EPANET based on Hernández (2024)
 Authors:      AI Implementation based on Hernández paper
 License:      see LICENSE
 ******************************************************************************
*/

#ifndef EPANET2_FIREFLOW_H
#define EPANET2_FIREFLOW_H

#include "epanet2.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fire flow analysis types
typedef enum {
    EN_FF_STATIC = 0,     // Static analysis (no fire flow)
    EN_FF_REQUIRED = 1,   // Required fire flow
    EN_FF_AVAILABLE = 2,  // Available fire flow  
    EN_FF_DESIGN = 3      // Design fire flow (respects constraints)
} EN_FireFlowType;

// Fire flow API functions

/**
 * @brief Enable/disable fire flow analysis
 * @param ph Project handle
 * @param enabled 1 to enable, 0 to disable
 * @return Error code
 */
int DLLEXPORT EN_setfireflow(EN_Project ph, int enabled);

/**
 * @brief Set hydrant node for fire flow analysis
 * @param ph Project handle
 * @param nodeIndex Index of hydrant node (1-based)
 * @return Error code
 */
int DLLEXPORT EN_setfireflowhydrant(EN_Project ph, int nodeIndex);

/**
 * @brief Set fire flow analysis parameters
 * @param ph Project handle
 * @param pressureThreshold Minimum pressure threshold (psi)
 * @param velocityThreshold Maximum velocity threshold (ft/s)
 * @param tolerance Convergence tolerance (cfs)
 * @return Error code
 */
int DLLEXPORT EN_setfireflowparams(EN_Project ph, 
                                   double pressureThreshold,
                                   double velocityThreshold, 
                                   double tolerance);

/**
 * @brief Run fire flow analysis
 * @param ph Project handle
 * @param analysisType Type of fire flow analysis
 * @param fireFlow Output fire flow value (gpm)
 * @param criticalNode Output critical node index (0 if pipe)
 * @param criticalLink Output critical link index (0 if node)
 * @return Error code
 */
int DLLEXPORT EN_runfireflow(EN_Project ph,
                             EN_FireFlowType analysisType,
                             double *fireFlow,
                             int *criticalNode,
                             int *criticalLink);

/**
 * @brief Get fire flow convergence status
 * @param ph Project handle
 * @param converged Output convergence status (1 if converged)
 * @param iterations Output number of iterations
 * @param relativeCloseness Output relative closeness of critical element
 * @return Error code
 */
int DLLEXPORT EN_getfireflowstatus(EN_Project ph,
                                   int *converged,
                                   int *iterations,
                                   double *relativeCloseness);

#ifdef __cplusplus
}
#endif

#endif // EPANET2_FIREFLOW_H