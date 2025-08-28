/*
 ******************************************************************************
 Project:      OWA EPANET with Fire Flow Analysis
 Module:       fireflow.c
 Description:  Implementation of Hernández fire flow algorithm
 Author:       AI Implementation based on Hernández (2024) paper
 License:      see LICENSE
 ******************************************************************************
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "types.h"
#include "funcs.h"
#include "fireflow.h"

// Module-level fire flow state (to be added to Project struct later)
static FireFlow* gFireFlow = NULL;

void initFireFlow(Project *pr)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**  Output:  none
**  Purpose: initializes fire flow analysis data structures
**--------------------------------------------------------------
*/
{
    Network *net = &pr->network;
    
    // Allocate fire flow structure
    if (gFireFlow == NULL) {
        gFireFlow = (FireFlow*)calloc(1, sizeof(FireFlow));
    }
    
    if (gFireFlow == NULL) return;
    
    // Initialize fields
    gFireFlow->Enabled = 0;  // Disabled by default
    gFireFlow->Active = 0;
    gFireFlow->HydrantNode = 0;
    gFireFlow->CriticalElement = 0;
    gFireFlow->IsCriticalPipe = 0;
    gFireFlow->HistoryCount = 0;
    gFireFlow->Iteration = 0;
    gFireFlow->Converged = 0;
    gFireFlow->UseHeuristic = 0;
    
    // Set default parameters
    gFireFlow->Tolerance = 0.01;  // 0.01 cfs convergence tolerance
    gFireFlow->PressureThreshold = 20.0 * 2.31;  // 20 psi converted to feet
    gFireFlow->VelocityThreshold = 10.0;  // 10 ft/s default
    gFireFlow->HeuristicMultUp = 1.1;
    gFireFlow->HeuristicMultDown = -0.4;
    
    // Allocate relative closeness array
    // Size = Njuncs (for pressure) + Nlinks (for velocity)
    int totalElements = net->Njuncs + net->Nlinks;
    gFireFlow->RelClosenessSize = totalElements;
    gFireFlow->RelCloseness = (double*)calloc(totalElements + 1, sizeof(double));
    
    // Initialize history arrays
    for (int i = 0; i < 3; i++) {
        gFireFlow->Q[i] = 0.0;
        gFireFlow->X[i] = 0.0;
    }
}

void freeFireFlow(Project *pr)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**  Output:  none
**  Purpose: frees fire flow analysis data structures
**--------------------------------------------------------------
*/
{
    if (gFireFlow != NULL) {
        if (gFireFlow->RelCloseness != NULL) {
            free(gFireFlow->RelCloseness);
            gFireFlow->RelCloseness = NULL;
        }
        free(gFireFlow);
        gFireFlow = NULL;
    }
}

void resetFireFlow(Project *pr)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**  Output:  none
**  Purpose: resets fire flow state for new analysis
**--------------------------------------------------------------
*/
{
    if (gFireFlow == NULL) return;
    
    gFireFlow->Active = 0;
    gFireFlow->CriticalElement = 0;
    gFireFlow->IsCriticalPipe = 0;
    gFireFlow->HistoryCount = 0;
    gFireFlow->Iteration = 0;
    gFireFlow->Converged = 0;
    gFireFlow->UseHeuristic = 0;
    gFireFlow->Qcurrent = 0.0;
    gFireFlow->LastQ = 0.0;
    gFireFlow->LastDirection = 0.0;
    gFireFlow->LastDeltaQ = 0.0;
    
    for (int i = 0; i < 3; i++) {
        gFireFlow->Q[i] = 0.0;
        gFireFlow->X[i] = 0.0;
    }
}

int isFireFlowActive(Project *pr)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**  Output:  returns 1 if fire flow analysis is active, 0 otherwise
**  Purpose: checks if fire flow analysis is currently running
**--------------------------------------------------------------
*/
{
    return (gFireFlow != NULL && gFireFlow->Active);
}

void setFireFlowHydrant(Project *pr, int nodeIndex)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**           nodeIndex = index of hydrant node (1-based)
**  Output:  none
**  Purpose: sets the hydrant node for fire flow analysis
**--------------------------------------------------------------
*/
{
    if (gFireFlow == NULL) return;
    
    gFireFlow->HydrantNode = nodeIndex;
    gFireFlow->Active = (nodeIndex > 0);
}

void solve3x3System(double A[3][3], double b[3], double x[3])
/*
**--------------------------------------------------------------
**  Input:   A = 3x3 coefficient matrix
**           b = right-hand side vector
**  Output:  x = solution vector
**  Purpose: solves a 3x3 linear system using direct method
**--------------------------------------------------------------
*/
{
    // Use Cramer's rule for 3x3 system
    // Calculate determinant of A
    double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
               - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
               + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    
    if (fabs(det) < 1e-10) {
        // Singular matrix, return zeros
        x[0] = x[1] = x[2] = 0.0;
        return;
    }
    
    // Calculate x[0] = det(A0) / det(A)
    double det0 = b[0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
                - A[0][1] * (b[1] * A[2][2] - A[1][2] * b[2])
                + A[0][2] * (b[1] * A[2][1] - A[1][1] * b[2]);
    
    // Calculate x[1] = det(A1) / det(A)
    double det1 = A[0][0] * (b[1] * A[2][2] - A[1][2] * b[2])
                - b[0] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
                + A[0][2] * (A[1][0] * b[2] - b[1] * A[2][0]);
    
    // Calculate x[2] = det(A2) / det(A)
    double det2 = A[0][0] * (A[1][1] * b[2] - b[1] * A[2][1])
                - A[0][1] * (A[1][0] * b[2] - b[1] * A[2][0])
                + b[0] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    
    x[0] = det0 / det;
    x[1] = det1 / det;
    x[2] = det2 / det;
}

void computeQuadraticRegression(double x[3], double y[3], double coeffs[3])
/*
**--------------------------------------------------------------
**  Input:   x = array of x values (critical variable)
**           y = array of y values (fire flow Q)
**  Output:  coeffs = quadratic coefficients [c1, c2, c3]
**           where y = c1*x^2 + c2*x + c3
**  Purpose: fits quadratic model using least squares
**--------------------------------------------------------------
*/
{
    // Build normal equations matrix A and vector b
    // A * coeffs = b
    double A[3][3], b[3];
    
    // Initialize
    for (int i = 0; i < 3; i++) {
        b[i] = 0.0;
        for (int j = 0; j < 3; j++) {
            A[i][j] = 0.0;
        }
    }
    
    // Build system: sum over 3 data points
    for (int k = 0; k < 3; k++) {
        double xk = x[k];
        double xk2 = xk * xk;
        double xk3 = xk2 * xk;
        double xk4 = xk2 * xk2;
        
        // Matrix A (symmetric)
        A[0][0] += xk4;     // sum(x^4)
        A[0][1] += xk3;     // sum(x^3)
        A[0][2] += xk2;     // sum(x^2)
        A[1][0] += xk3;     // sum(x^3)
        A[1][1] += xk2;     // sum(x^2)
        A[1][2] += xk;      // sum(x)
        A[2][0] += xk2;     // sum(x^2)
        A[2][1] += xk;      // sum(x)
        A[2][2] += 1.0;     // sum(1) = 3
        
        // Vector b
        b[0] += y[k] * xk2; // sum(y*x^2)
        b[1] += y[k] * xk;  // sum(y*x)
        b[2] += y[k];       // sum(y)
    }
    
    // Solve the system
    solve3x3System(A, b, coeffs);
}

void updateFireFlowHistory(Project *pr, double Q, double X)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**           Q = fire flow value
**           X = critical variable value
**  Output:  none
**  Purpose: updates the fire flow history arrays
**--------------------------------------------------------------
*/
{
    if (gFireFlow == NULL) return;
    
    // Shift history
    gFireFlow->Q[2] = gFireFlow->Q[1];
    gFireFlow->Q[1] = gFireFlow->Q[0];
    gFireFlow->Q[0] = Q;
    
    gFireFlow->X[2] = gFireFlow->X[1];
    gFireFlow->X[1] = gFireFlow->X[0];
    gFireFlow->X[0] = X;
    
    // Update history count
    if (gFireFlow->HistoryCount < 3) {
        gFireFlow->HistoryCount++;
    }
}

void generateFireFlowGuess(Project *pr)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**  Output:  none
**  Purpose: generates next fire flow guess using physical or
**           heuristic method
**--------------------------------------------------------------
*/
{
    if (gFireFlow == NULL || !gFireFlow->Active) return;
    
    double newQ = 0.0;
    
    // Check if we have enough history for regression
    if (gFireFlow->HistoryCount >= 3 && !gFireFlow->UseHeuristic) {
        // Try physically-based quadratic regression
        double coeffs[3];
        computeQuadraticRegression(gFireFlow->X, gFireFlow->Q, coeffs);
        
        // Determine threshold based on critical element type
        double xThreshold;
        if (gFireFlow->IsCriticalPipe) {
            xThreshold = gFireFlow->VelocityThreshold;
        } else {
            xThreshold = gFireFlow->PressureThreshold;
        }
        
        // Evaluate quadratic at threshold
        newQ = coeffs[0] * xThreshold * xThreshold + 
               coeffs[1] * xThreshold + 
               coeffs[2];
        
        // Check if the guess is reasonable
        if (newQ < 0.0 || newQ > gFireFlow->AvailableFlow * 2.0) {
            // Unreasonable guess, switch to heuristic
            gFireFlow->UseHeuristic = 1;
        }
    }
    
    // Use heuristic method if needed
    if (gFireFlow->HistoryCount < 3 || gFireFlow->UseHeuristic) {
        if (gFireFlow->Iteration == 0) {
            // First iteration: use a fraction of available flow
            newQ = gFireFlow->AvailableFlow * 0.5;
        } else {
            // Heuristic update
            double currentDirection = (gFireFlow->Qcurrent > gFireFlow->LastQ) ? 1.0 : -1.0;
            double multiplier;
            
            if (gFireFlow->LastDirection == 0.0) {
                // First direction
                multiplier = gFireFlow->HeuristicMultUp;
            } else if (currentDirection == gFireFlow->LastDirection) {
                // Same direction
                multiplier = gFireFlow->HeuristicMultUp;
            } else {
                // Direction changed
                multiplier = gFireFlow->HeuristicMultDown;
            }
            
            newQ = gFireFlow->Qcurrent + multiplier * gFireFlow->LastDeltaQ;
            gFireFlow->LastDirection = currentDirection;
        }
    }
    
    // Store the new guess
    gFireFlow->LastQ = gFireFlow->Qcurrent;
    gFireFlow->LastDeltaQ = newQ - gFireFlow->Qcurrent;
    gFireFlow->Qcurrent = newQ;
    
    // Ensure non-negative flow
    if (gFireFlow->Qcurrent < 0.0) {
        gFireFlow->Qcurrent = 0.0;
    }
}

double computeRelativeCloseness(Project *pr, int element, int isPipe)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**           element = element index (1-based)
**           isPipe = 1 if element is a pipe, 0 if junction
**  Output:  returns relative closeness value
**  Purpose: computes relative closeness to constraint violation
**--------------------------------------------------------------
*/
{
    Network *net = &pr->network;
    Hydraul *hyd = &pr->hydraul;
    
    if (gFireFlow == NULL) return 1.0;  // No violation
    
    double x, xThreshold, range;
    
    if (isPipe) {
        // Velocity constraint for pipes
        double q = fabs(hyd->LinkFlow[element]);
        double area = 0.7854 * net->Link[element].Diam * net->Link[element].Diam;
        x = q / area;  // Velocity in ft/s
        xThreshold = gFireFlow->VelocityThreshold;
        
        // Use a fixed range for velocity (e.g., 0 to 2*threshold)
        range = 2.0 * xThreshold;
    } else {
        // Pressure constraint for junctions
        x = hyd->NodeHead[element] - net->Node[element].El;  // Pressure in ft
        xThreshold = gFireFlow->PressureThreshold;
        
        // Use range from available flow analysis
        range = gFireFlow->AvailablePressure - gFireFlow->StaticPressure;
        if (range <= 0.0) range = xThreshold;  // Fallback
    }
    
    // Calculate relative closeness
    // Positive means constraint satisfied, negative means violated
    double relCloseness = (x - xThreshold) / range;
    
    return relCloseness;
}

void updateCriticalElement(Project *pr)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**  Output:  none
**  Purpose: identifies the critical element (minimum relative closeness)
**--------------------------------------------------------------
*/
{
    Network *net = &pr->network;
    
    if (gFireFlow == NULL || !gFireFlow->Active) return;
    
    double minCloseness = 1e10;
    int criticalElement = 0;
    int isCriticalPipe = 0;
    
    // Check all junctions for pressure constraints
    for (int i = 1; i <= net->Njuncs; i++) {
        double rc = computeRelativeCloseness(pr, i, 0);
        gFireFlow->RelCloseness[i] = rc;
        
        if (rc < minCloseness) {
            minCloseness = rc;
            criticalElement = i;
            isCriticalPipe = 0;
        }
    }
    
    // Check all pipes for velocity constraints
    for (int k = 1; k <= net->Nlinks; k++) {
        // Skip non-pipe elements
        if (net->Link[k].Type != PIPE && net->Link[k].Type != CVPIPE) continue;
        
        double rc = computeRelativeCloseness(pr, k, 1);
        gFireFlow->RelCloseness[net->Njuncs + k] = rc;
        
        if (rc < minCloseness) {
            minCloseness = rc;
            criticalElement = k;
            isCriticalPipe = 1;
        }
    }
    
    // Update critical element
    gFireFlow->CriticalElement = criticalElement;
    gFireFlow->IsCriticalPipe = isCriticalPipe;
    
    // Get critical variable value for history
    if (isCriticalPipe) {
        Hydraul *hyd = &pr->hydraul;
        double q = fabs(hyd->LinkFlow[criticalElement]);
        double area = 0.7854 * net->Link[criticalElement].Diam * 
                      net->Link[criticalElement].Diam;
        gFireFlow->Xcritical = q / area;  // Velocity
    } else {
        Hydraul *hyd = &pr->hydraul;
        gFireFlow->Xcritical = hyd->NodeHead[criticalElement] - 
                               net->Node[criticalElement].El;  // Pressure
    }
}

int checkFireFlowConvergence(Project *pr)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**  Output:  returns 1 if converged, 0 otherwise
**  Purpose: checks fire flow convergence criteria
**--------------------------------------------------------------
*/
{
    if (gFireFlow == NULL || !gFireFlow->Active) return 1;
    
    // Check flow change convergence
    double deltaQ = fabs(gFireFlow->Qcurrent - gFireFlow->LastQ);
    if (deltaQ > gFireFlow->Tolerance) return 0;
    
    // Check constraint satisfaction
    double rcCritical = gFireFlow->RelCloseness[gFireFlow->CriticalElement];
    if (gFireFlow->IsCriticalPipe) {
        rcCritical = gFireFlow->RelCloseness[pr->network.Njuncs + 
                                              gFireFlow->CriticalElement];
    }
    
    if (rcCritical < 0.0) return 0;  // Constraint violated
    
    // Both criteria met
    gFireFlow->Converged = 1;
    return 1;
}

// Integration functions to be called from hydsolver.c

void fireFlowBeforeMatrixCoeffs(Project *pr)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**  Output:  none
**  Purpose: called before matrixcoeffs() to generate fire flow guess
**--------------------------------------------------------------
*/
{
    if (gFireFlow == NULL || !gFireFlow->Active) return;
    
    // Generate new fire flow guess
    generateFireFlowGuess(pr);
    
    // The actual demand will be added in a modified demandcoeffs() function
}

void fireFlowAfterNewFlows(Project *pr)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**  Output:  none
**  Purpose: called after newflows() to update critical element
**--------------------------------------------------------------
*/
{
    if (gFireFlow == NULL || !gFireFlow->Active) return;
    
    // Update critical element and relative closeness
    updateCriticalElement(pr);
    
    // Update history with current values
    updateFireFlowHistory(pr, gFireFlow->Qcurrent, gFireFlow->Xcritical);
    
    // Increment iteration counter
    gFireFlow->Iteration++;
}

int fireFlowCheckConvergence(Project *pr)
/*
**--------------------------------------------------------------
**  Input:   pr = project data structure pointer
**  Output:  returns 1 if converged, 0 otherwise
**  Purpose: checks if fire flow analysis has converged
**--------------------------------------------------------------
*/
{
    return checkFireFlowConvergence(pr);
}