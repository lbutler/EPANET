# Technical Implementation Decisions

## Decision Log

### Session 1 - Data Structure Design for Fire Flow State

**Problem:** Need to track fire flow state (Q history, x history, critical element) across iterations
**Options Considered:**
1. Extend the Hydraul struct with fire flow fields
2. Create a separate FireFlow struct
3. Use global variables

**Decision:** Create a separate FireFlow struct to encapsulate all fire flow state
**Rationale:** 
- Keeps fire flow logic modular and separate from core hydraulics
- Easier to enable/disable fire flow analysis
- Cleaner code organization
- Minimal changes to existing structures

**Proposed Structure:**
```c
typedef struct {
    int    Active;           // Fire flow analysis active flag
    int    HydrantNode;      // Node index of hydrant being analyzed
    int    CriticalElement;  // Current critical element index
    int    IsCriticalPipe;   // TRUE if critical element is pipe, FALSE if junction
    double Q[3];             // Last 3 fire flow guesses (Q_i, Q_i-1, Q_i-2)
    double X[3];             // Corresponding critical variable values
    double Qcurrent;         // Current iteration's fire flow guess
    double Xcritical;        // Current critical variable value
    double Xthreshold;       // Threshold value (pressure or velocity limit)
    double *RelCloseness;    // Relative closeness for all elements
    int    Iteration;        // Fire flow iteration counter
    double Tolerance;        // Convergence tolerance for fire flow
    int    UseHeuristic;     // Flag to use heuristic vs physical method
} FireFlow;
```

---

### Session 1 - Hydrant Node Identification

**Problem:** Need a way to identify which nodes are hydrants for fire flow analysis
**Options Considered:**
1. Add a flag to the Snode struct
2. Use node naming convention (e.g., nodes starting with "HYD")
3. Create a separate hydrant list/array
4. Use a special demand pattern ID

**Decision:** Initially use command-line or input file option to specify hydrant node
**Rationale:**
- Simplest approach for initial implementation
- Can be extended later to support multiple hydrants
- Avoids modifying core data structures initially
- Common approach in existing fire flow software

---

### Session 1 - Relative Closeness Normalization

**Problem:** Paper doesn't specify exact normalization for "range of variation"
**Options Considered:**
1. Use difference between available flow and static flow values
2. Use historical min/max range from iterations
3. Use standard deviation of x values
4. Use fixed percentage of threshold (e.g., 10% of Pmin)

**Decision:** Start with option 1 (available - static range) and validate against paper results
**Rationale:**
- Most physically meaningful - represents the full operating range
- Can be computed from initial runs (static, available)
- If results don't match paper, try other approaches

---

### Session 1 - Cholesky Decomposition Implementation

**Problem:** Need 3x3 matrix solver for quadratic regression
**Options Considered:**
1. Use existing Cholesky solver in smatrix.c (designed for large sparse matrices)
2. Implement simple 3x3 direct solver
3. Use analytical solution for 3x3 system

**Decision:** Implement simple direct 3x3 solver
**Rationale:**
- Existing solver is overkill for 3x3 dense matrix
- Direct solution is simple and efficient for small fixed size
- No dependencies on sparse matrix structures
- Can use standard formulas for 3x3 linear system

---

[Additional decisions below]