/*
 ******************************************************************************
 Project:      OWA EPANET with Fire Flow Analysis
 Module:       fireflow.h
 Description:  Header file for Hernández fire flow algorithm implementation
 Author:       AI Implementation based on Hernández (2024) paper
 License:      see LICENSE
 ******************************************************************************
*/

#ifndef FIREFLOW_H
#define FIREFLOW_H

// Fire flow analysis state structure
typedef struct {
    // Control flags
    int    Active;              // Fire flow analysis active flag
    int    Enabled;             // Fire flow feature enabled flag
    
    // Node and element tracking
    int    HydrantNode;         // Node index of hydrant being analyzed (1-based)
    int    CriticalElement;     // Current critical element index
    int    IsCriticalPipe;      // TRUE if critical element is pipe, FALSE if junction
    
    // Fire flow history for regression
    double Q[3];                // Last 3 fire flow guesses (Q_i, Q_i-1, Q_i-2)
    double X[3];                // Corresponding critical variable values
    int    HistoryCount;        // Number of valid history entries (0-3)
    
    // Current iteration values
    double Qcurrent;            // Current iteration's fire flow guess
    double Xcritical;           // Current critical variable value
    double LastQ;               // Previous iteration's Q for convergence check
    
    // Thresholds and constraints
    double PressureThreshold;   // Minimum pressure threshold (psi -> ft conversion needed)
    double VelocityThreshold;   // Maximum velocity threshold (ft/s)
    
    // Relative closeness array
    double *RelCloseness;       // Relative closeness for all elements
    int    RelClosenessSize;    // Size of RelCloseness array
    
    // Convergence parameters
    int    Iteration;           // Fire flow iteration counter
    double Tolerance;           // Convergence tolerance for fire flow (cfs)
    int    Converged;           // Fire flow convergence flag
    
    // Method selection
    int    UseHeuristic;        // Flag to use heuristic vs physical method
    double LastDirection;       // Sign of last flow change for heuristic
    double LastDeltaQ;          // Last flow change magnitude
    
    // Normalization parameters (for relative closeness)
    double StaticPressure;      // Pressure at hydrant with no fire flow
    double AvailablePressure;   // Pressure at hydrant with available flow
    double StaticFlow;          // Zero (no fire flow initially)
    double AvailableFlow;       // Maximum available flow at min pressure
    
    // Heuristic parameters
    double HeuristicMultUp;     // Multiplier when direction consistent (default 1.1)
    double HeuristicMultDown;   // Multiplier when direction changes (default -0.4)
    
} FireFlow;

// Function prototypes
void  initFireFlow(Project *pr);
void  freeFireFlow(Project *pr);
void  resetFireFlow(Project *pr);
int   isFireFlowActive(Project *pr);
void  setFireFlowHydrant(Project *pr, int nodeIndex);

// Core algorithm functions
void  generateFireFlowGuess(Project *pr);
void  updateCriticalElement(Project *pr);
int   checkFireFlowConvergence(Project *pr);

// Helper functions
void  computeQuadraticRegression(double x[3], double y[3], double coeffs[3]);
void  solve3x3System(double A[3][3], double b[3], double x[3]);
double computeRelativeCloseness(Project *pr, int element, int isPipe);
void  updateFireFlowHistory(Project *pr, double Q, double X);

// Integration functions (called from hydsolver.c)
void  fireFlowBeforeMatrixCoeffs(Project *pr);
void  fireFlowAfterNewFlows(Project *pr);
int   fireFlowCheckConvergence(Project *pr);

#endif // FIREFLOW_H