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
    if (pr->fireflow == NULL) {
        pr->fireflow = (FireFlow*)calloc(1, sizeof(FireFlow));
    }
    
    if (pr->fireflow == NULL) return;
    
    // Initialize fields
    pr->fireflow->Enabled = 0;  // Disabled by default
    pr->fireflow->Active = 0;
    pr->fireflow->HydrantNode = 0;
    pr->fireflow->CriticalElement = 0;
    pr->fireflow->IsCriticalPipe = 0;
    pr->fireflow->HistoryCount = 0;
    pr->fireflow->Iteration = 0;
    pr->fireflow->Converged = 0;
    pr->fireflow->UseHeuristic = 0;
    
    // Set default parameters
    pr->fireflow->Tolerance = 0.01;  // 0.01 cfs convergence tolerance
    pr->fireflow->PressureThreshold = 20.0 * 2.31;  // 20 psi converted to feet
    pr->fireflow->VelocityThreshold = 10.0;  // 10 ft/s default
    pr->fireflow->HeuristicMultUp = 1.1;
    pr->fireflow->HeuristicMultDown = -0.4;
    
    // Allocate relative closeness array
    // Size = Njuncs (for pressure) + Nlinks (for velocity)
    int totalElements = net->Njuncs + net->Nlinks;
    pr->fireflow->RelClosenessSize = totalElements;
    pr->fireflow->RelCloseness = (double*)calloc(totalElements + 1, sizeof(double));
    
    // Initialize history arrays
    for (int i = 0; i < 3; i++) {
        pr->fireflow->Q[i] = 0.0;
        pr->fireflow->X[i] = 0.0;
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
    if (pr->fireflow != NULL) {
        if (pr->fireflow->RelCloseness != NULL) {
            free(pr->fireflow->RelCloseness);
            pr->fireflow->RelCloseness = NULL;
        }
        free(pr->fireflow);
        pr->fireflow = NULL;
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
    if (pr->fireflow == NULL) return;
    
    pr->fireflow->Active = 0;
    pr->fireflow->CriticalElement = 0;
    pr->fireflow->IsCriticalPipe = 0;
    pr->fireflow->HistoryCount = 0;
    pr->fireflow->Iteration = 0;
    pr->fireflow->Converged = 0;
    pr->fireflow->UseHeuristic = 0;
    pr->fireflow->Qcurrent = 0.0;
    pr->fireflow->LastQ = 0.0;
    pr->fireflow->LastDirection = 0.0;
    pr->fireflow->LastDeltaQ = 0.0;
    
    for (int i = 0; i < 3; i++) {
        pr->fireflow->Q[i] = 0.0;
        pr->fireflow->X[i] = 0.0;
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
    return (pr->fireflow != NULL && pr->fireflow->Active);
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
    if (pr->fireflow == NULL) return;
    
    pr->fireflow->HydrantNode = nodeIndex;
    pr->fireflow->Active = (nodeIndex > 0);
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
    if (pr->fireflow == NULL) return;
    
    // Shift history
    pr->fireflow->Q[2] = pr->fireflow->Q[1];
    pr->fireflow->Q[1] = pr->fireflow->Q[0];
    pr->fireflow->Q[0] = Q;
    
    pr->fireflow->X[2] = pr->fireflow->X[1];
    pr->fireflow->X[1] = pr->fireflow->X[0];
    pr->fireflow->X[0] = X;
    
    // Update history count
    if (pr->fireflow->HistoryCount < 3) {
        pr->fireflow->HistoryCount++;
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
    if (pr->fireflow == NULL || !pr->fireflow->Active) return;
    
    double newQ = 0.0;
    
    // Check if we have enough history for regression
    if (pr->fireflow->HistoryCount >= 3 && !pr->fireflow->UseHeuristic) {
        // Try physically-based quadratic regression
        double coeffs[3];
        computeQuadraticRegression(pr->fireflow->X, pr->fireflow->Q, coeffs);
        
        // Debug: print the regression data
        printf("      Quadratic data: (X,Q) = (%.2f,%.3f), (%.2f,%.3f), (%.2f,%.3f)\n",
               pr->fireflow->X[0], pr->fireflow->Q[0],
               pr->fireflow->X[1], pr->fireflow->Q[1],
               pr->fireflow->X[2], pr->fireflow->Q[2]);
        printf("      Coeffs: c1=%.6f, c2=%.6f, c3=%.6f\n", coeffs[0], coeffs[1], coeffs[2]);
        
        // Use the appropriate threshold for the critical element
        // The threshold should match what the critical variable X represents
        double xThreshold = pr->fireflow->PressureThreshold;  // Default to pressure
        
        // If we have a consistent critical element type, use its threshold
        if (pr->fireflow->IsCriticalPipe) {
            xThreshold = pr->fireflow->VelocityThreshold;
        }
        
        // Evaluate quadratic at threshold
        newQ = coeffs[0] * xThreshold * xThreshold + 
               coeffs[1] * xThreshold + 
               coeffs[2];
        
        // Check if the guess is reasonable
        double maxReasonableFlow = pr->fireflow->Qcurrent * 3.0;  // Allow up to 3x current
        if (pr->fireflow->AvailableFlow > 0) {
            maxReasonableFlow = pr->fireflow->AvailableFlow * 2.0;
        }
        
        if (newQ < 0.0 || newQ > maxReasonableFlow) {
            // Unreasonable guess, switch to heuristic
            pr->fireflow->UseHeuristic = 1;
            printf("      Quadratic gave unreasonable Q=%.3f, switching to heuristic\n", newQ);
        } else {
            printf("      Using quadratic regression: Q=%.3f cfs (%.1f gpm)\n", 
                   newQ, newQ * 448.831);
        }
    }
    
    // Use heuristic method if needed
    if (pr->fireflow->HistoryCount < 3 || pr->fireflow->UseHeuristic) {
        if (pr->fireflow->Iteration == 0) {
            // First iteration: start with a small flow
            // If available flow is not yet computed, use a reasonable default
            if (pr->fireflow->AvailableFlow > 0) {
                newQ = pr->fireflow->AvailableFlow * 0.5;
            } else {
                // Start with 100 gpm as initial guess
                newQ = 100.0 / 448.831;  // Convert gpm to cfs
            }
        } else {
            // Heuristic update
            if (pr->fireflow->LastDeltaQ == 0.0) {
                // No previous change, make an initial step
                // Increase flow by 10% or 50 gpm, whichever is larger
                double increment = fmax(pr->fireflow->Qcurrent * 0.1, 50.0 / 448.831);
                newQ = pr->fireflow->Qcurrent + increment;
            } else {
                // Check if we need to back off due to constraint violation
                double rcCritical = 0.0;
                if (pr->fireflow->CriticalElement > 0) {
                    if (pr->fireflow->IsCriticalPipe) {
                        rcCritical = pr->fireflow->RelCloseness[pr->network.Njuncs + 
                                                              pr->fireflow->CriticalElement];
                    } else {
                        rcCritical = pr->fireflow->RelCloseness[pr->fireflow->CriticalElement];
                    }
                }
                
                // Use direction-based heuristic
                double currentDirection = (pr->fireflow->Qcurrent > pr->fireflow->LastQ) ? 1.0 : -1.0;
                double multiplier;
                
                // If constraint violated, force reduction
                if (rcCritical < 0.0) {
                    // Use smaller steps when close to constraint
                    if (rcCritical > -0.1) {
                        multiplier = -0.1;  // Small adjustment near boundary
                    } else {
                        multiplier = -0.5;  // Larger back off when far from constraint
                    }
                    if (pr->fireflow->Iteration < 15) {
                        printf("      Constraint violated (RC=%.4f), backing off with multiplier %.2f\n", 
                               rcCritical, multiplier);
                    }
                } else if (pr->fireflow->LastDirection == 0.0) {
                    // First direction
                    multiplier = pr->fireflow->HeuristicMultUp;
                } else if (currentDirection == pr->fireflow->LastDirection) {
                    // Same direction
                    multiplier = pr->fireflow->HeuristicMultUp;
                } else {
                    // Direction changed
                    multiplier = pr->fireflow->HeuristicMultDown;
                }
                
                newQ = pr->fireflow->Qcurrent + multiplier * pr->fireflow->LastDeltaQ;
                pr->fireflow->LastDirection = currentDirection;
            }
        }
    }
    
    // Store the new guess
    pr->fireflow->LastQ = pr->fireflow->Qcurrent;
    pr->fireflow->LastDeltaQ = newQ - pr->fireflow->Qcurrent;
    pr->fireflow->Qcurrent = newQ;
    
    // Ensure non-negative flow
    if (pr->fireflow->Qcurrent < 0.0) {
        pr->fireflow->Qcurrent = 0.0;
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
    
    if (pr->fireflow == NULL) return 1.0;  // No violation
    
    double x, xThreshold, range;
    
    if (isPipe) {
        // Velocity constraint for pipes
        double q = fabs(hyd->LinkFlow[element]);
        // Diameter is already in feet internally
        double diam_ft = net->Link[element].Diam;
        double area = 0.7854 * diam_ft * diam_ft;  // Area in sq ft
        x = q / area;  // Velocity in ft/s
        xThreshold = pr->fireflow->VelocityThreshold;
        
        // Debug output for first few iterations
        if (pr->fireflow && pr->fireflow->Iteration < 3 && element == pr->fireflow->CriticalElement) {
            printf("        Pipe %s: D=%.2f ft (%.1f\"), Q=%.4f cfs, A=%.4f ft², V=%.2f ft/s\n",
                   net->Link[element].ID, diam_ft, diam_ft * 12.0, q, area, x);
        }
        
        // Use a fixed range for velocity (e.g., 0 to 2*threshold)
        range = 2.0 * xThreshold;
    } else {
        // Pressure constraint for junctions
        x = hyd->NodeHead[element] - net->Node[element].El;  // Pressure in ft
        xThreshold = pr->fireflow->PressureThreshold;
        
        // Use range from available flow analysis
        range = pr->fireflow->AvailablePressure - pr->fireflow->StaticPressure;
        if (range <= 0.0) range = xThreshold;  // Fallback
    }
    
    // Calculate relative closeness
    // Positive means constraint satisfied, negative means violated
    double relCloseness;
    if (isPipe) {
        // For velocity (maximum constraint), violation when x > threshold
        relCloseness = (xThreshold - x) / range;
    } else {
        // For pressure (minimum constraint), violation when x < threshold
        relCloseness = (x - xThreshold) / range;
    }
    
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
    
    if (pr->fireflow == NULL || !pr->fireflow->Active) return;
    
    double minCloseness = 1e10;
    int criticalElement = 0;
    int isCriticalPipe = 0;
    
    // Check all junctions for pressure constraints
    for (int i = 1; i <= net->Njuncs; i++) {
        double rc = computeRelativeCloseness(pr, i, 0);
        pr->fireflow->RelCloseness[i] = rc;
        
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
        pr->fireflow->RelCloseness[net->Njuncs + k] = rc;
        
        if (rc < minCloseness) {
            minCloseness = rc;
            criticalElement = k;
            isCriticalPipe = 1;
        }
    }
    
    // Update critical element
    pr->fireflow->CriticalElement = criticalElement;
    pr->fireflow->IsCriticalPipe = isCriticalPipe;
    
    // Get critical variable value for history
    if (isCriticalPipe) {
        Hydraul *hyd = &pr->hydraul;
        double q = fabs(hyd->LinkFlow[criticalElement]);
        double diam_ft = net->Link[criticalElement].Diam;  // Already in feet
        double area = 0.7854 * diam_ft * diam_ft;
        pr->fireflow->Xcritical = q / area;  // Velocity
    } else {
        Hydraul *hyd = &pr->hydraul;
        pr->fireflow->Xcritical = hyd->NodeHead[criticalElement] - 
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
    if (pr->fireflow == NULL || !pr->fireflow->Active) return 1;
    
    // Don't check convergence until we have some iterations
    if (pr->fireflow->Iteration < 2) return 0;
    
    // Check flow change convergence
    double deltaQ = fabs(pr->fireflow->Qcurrent - pr->fireflow->LastQ);
    double relativeChange = deltaQ / (pr->fireflow->Qcurrent + 0.001);  // Avoid div by 0
    
    // Get critical element's relative closeness
    double rcCritical = pr->fireflow->RelCloseness[pr->fireflow->CriticalElement];
    if (pr->fireflow->IsCriticalPipe) {
        rcCritical = pr->fireflow->RelCloseness[pr->network.Njuncs + 
                                              pr->fireflow->CriticalElement];
    }
    
    // Debug output
    if (pr->fireflow->Iteration < 15) {
        printf("      Convergence check: deltaQ=%.4f, relChange=%.4f, RC=%.4f\n",
               deltaQ, relativeChange, rcCritical);
    }
    
    // Check convergence criteria:
    // 1. Flow change is small (absolute or relative)
    // 2. Critical element is close to threshold (-0.05 <= RC <= 0.05)
    if ((deltaQ < pr->fireflow->Tolerance || relativeChange < 0.01) && 
        rcCritical >= -0.05 && rcCritical <= 0.05) {
        pr->fireflow->Converged = 1;
        printf("      CONVERGED: Q=%.3f cfs (%.1f gpm), RC=%.4f\n",
               pr->fireflow->Qcurrent, pr->fireflow->Qcurrent * 448.831, rcCritical);
        return 1;
    }
    
    // If constraint is violated, reduce flow
    if (rcCritical < 0.0) {
        // We've exceeded the constraint, need to back off
        // This will be handled in the guess generation
        pr->fireflow->UseHeuristic = 1;  // Switch to heuristic to back off
    }
    
    return 0;
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
    if (pr->fireflow == NULL || !pr->fireflow->Active) return;
    
    // Generate new fire flow guess
    generateFireFlowGuess(pr);
    
    // Debug output (temporary)
    if (pr->fireflow->Iteration < 10) {
        printf("    FF Iter %d: Q=%.3f cfs (%.1f gpm), LastQ=%.3f, DeltaQ=%.3f\n", 
               pr->fireflow->Iteration, 
               pr->fireflow->Qcurrent,
               pr->fireflow->Qcurrent * 448.831,
               pr->fireflow->LastQ,
               pr->fireflow->LastDeltaQ);
    }
    
    // The actual demand will be added in nodecoeffs() function
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
    if (pr->fireflow == NULL || !pr->fireflow->Active) return;
    
    // Update critical element and relative closeness
    updateCriticalElement(pr);
    
    // Debug output (temporary)
    if (pr->fireflow->Iteration < 10) {
        char elemID[256];
        if (pr->fireflow->IsCriticalPipe) {
            strcpy(elemID, pr->network.Link[pr->fireflow->CriticalElement].ID);
        } else {
            strcpy(elemID, pr->network.Node[pr->fireflow->CriticalElement].ID);
        }
        printf("      Critical: %s %s (idx %d), X=%.2f, RC=%.4f\n",
               pr->fireflow->IsCriticalPipe ? "Pipe" : "Node",
               elemID,
               pr->fireflow->CriticalElement,
               pr->fireflow->Xcritical,
               pr->fireflow->IsCriticalPipe ? 
                   pr->fireflow->RelCloseness[pr->network.Njuncs + pr->fireflow->CriticalElement] :
                   pr->fireflow->RelCloseness[pr->fireflow->CriticalElement]);
    }
    
    // Update history with current values
    updateFireFlowHistory(pr, pr->fireflow->Qcurrent, pr->fireflow->Xcritical);
    
    // Increment iteration counter
    pr->fireflow->Iteration++;
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