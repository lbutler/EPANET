Here’s your HTML cleaned up into Markdown so an LLM can read it easily:

---

# engproc-69-00051

_Eng. Proc._ **2024**, _69_(1), 51; doi: [10.3390/engproc2024069051](https://doi.org/10.3390/engproc2024069051)

**Proceeding Paper**

**Title:** Fast Firefighting Water Capacity Assessment Using a Streamlined Single-Loop Hybrid Search †
**Author:** Felipe Hernández [ORCID](https://orcid.org/0000-0002-0397-0702)
**Affiliation:** Autodesk Inc., Pittsburgh, PA 15237, USA

† Presented at the 3rd International Joint Conference on Water Distribution Systems Analysis & Computing and Control for the Water Industry (WDSA/CCWI 2024), Ferrara, Italy, 1–4 July 2024.

**Citation:** Hernández, F. Fast Firefighting Water Capacity Assessment Using a Streamlined Single-Loop Hybrid Search. _Eng. Proc._ **2024**, _69_, 51. [https://doi.org/10.3390/engproc2024069051](https://doi.org/10.3390/engproc2024069051)

**Academic Editors:** Stefano Alvisi, Marco Franchini, Valentina Marsili, Filippo Mazzoni
**Published:** 4 September 2024

---

## Abstract

The water distribution system firefighting capacity is widely estimated using decades-old methods that are inefficient for the scale of typical models nowadays. This article introduces an updated algorithm that streamlines the estimation by using a single iterative loop that simultaneously solves for hydrant capacity and the hydraulic effects on the network. The method features a hybrid physically based and heuristic local search approach, and a strategy to easily rank the criticality of network elements that might violate service level constraints. Tests on three models of varying size demonstrate the significant accuracy and efficiency benefits of the proposed approach.

**Keywords:** water distribution; numerical modelling; firefighting; heuristic algorithm

---

## 1. Introduction

Water distribution systems should possess sufficient spare capacity for firefighting. Regulations in several countries require that each hydrant provide sufficient flow while maintaining regular service pressure \[1].

Although determining capacity for individual hydrants is straightforward, large utilities struggle to conduct systemwide assessments efficiently due to network complexity and the large number of hydrants. Existing engines rely on iterative methods developed decades ago \[2], which are inefficient for complex networks. These methods require:

1. Sequentially searching for the “critical element” (junction or pipe likely to violate constraints first).
2. Using different search algorithms depending on pressure or velocity constraints.

This results in nested loops with computation times of hours or days for large networks. The new algorithm streamlines the process into a single loop.

---

## 2. Methods

### 2.1 Fire Flow Demand Levels

Three levels of firefighting demand at each hydrant:

- **Required flow**: sufficient to extinguish fires.
- **Available flow**: maximum extractable with minimum pressure.
- **Design flow**: maximum available without violating service constraints.

These are central to firefighting planning and decision-making \[3].

### 2.2 Traditional Approach

Traditionally, design flow is computed through nested loops:

1. Outer loop → potential critical elements.
2. Middle loop → guesses for design flow.
3. Inner loop → hydraulic impact evaluation \[5].

This is computationally expensive.

### 2.3 Proposed Approach

Based on EPANET’s Global Gradient Algorithm (GGA) \[6], the method expands steps to simultaneously find:

- Critical element (j)
- Design flow (Q)
- Resulting hydraulic impact

All in a **single loop** until convergence.

---

### 2.1 Producing Design Fire Flow Guesses

#### 2.1.1 Physically Based Guess

A Cholesky decomposition regression model:

```
Q = c1·x² + c2·x + c3   (1)
```

Where **x** = pressure (junction) or velocity (pipe). Initial guesses come from static, required, and available flow runs.

#### 2.1.2 Heuristic Backup Guess

When the physically based guess fails (e.g., pumps/valves non-linearities), a heuristic update is applied:

```
Qi = Qi-1 + mi·Δi-1     (2)
```

Where **mi = 1.1** if direction is consistent, or **-0.4** if overshot.

---

### 2.2 Selecting Critical Element

Criticality ranked using “relative closeness”:

```
rj = (xj - xj*) / variation range
```

- rj < 0 → violation
- rj ≥ 0 → no violation

Critical element chosen as the one with smallest rj.

---

### 2.3 Convergence Criteria

Convergence occurs if:

1. No violations (rcrit ≥ 0).
2. Change in last two Q guesses < ε.

---

## 3. Experimental Setup and Results

- Implemented in C++ within InfoWater Pro.
- Tested on three models: **Small, Medium, Large**.
- Compared pressure-only vs. pressure+velocity constraints.

**Performance:**

- Large model, 51 hydrants, Intel i7 CPU, 32 GB RAM.
- Pressure only: Legacy = 6.12 s, Proposed = 6.18 s.
- Pressure + velocity: Legacy = 48.51 s, Proposed = 15.28 s.

**Table 1.** Characteristics and results (MAE, RMAE, critical element shifts).

---

## 4. Conclusions

- Proposed algorithm improved design fire flow accuracy.
- Errors reduced up to **41% worst-case** and **0.5% best-case** vs. legacy InfoWater.
- Largest improvements when velocity constraints included.
- Speed-up factor = **3.17** in velocity + pressure tests.
- Validated the need to simultaneously consider all critical elements.
- Released in **InfoWater Pro v2024.3**.

---

## Funding

This research received no external funding.

## IRB Statement

Not applicable.

## Informed Consent

Not applicable.

## Data Availability

InfoWater Pro is available commercially. “Small” model is included as “Sample”; other models proprietary.

## Conflicts of Interest

The author is employed by Autodesk Inc., seller of InfoWater Pro.

---

## References

1. American Water Works Association. _Distribution System Requirements for Fire Protection_. AWWA: Washington, DC, 2008.
2. Boulos, P.F.; Rossman, L.A.; Orr, C.H.; et al. _Fire Flow Computation with Network Models_. J. Am. Water Works Assoc. 1997, 89, 51–56.
3. Kanta, L.; Zechman, E.; Brumbelow, K. _Multiobjective Evolutionary Computation Approach for Redesigning Water Distribution Systems_. J. Water Resour. Plan. Manag. 2012, 138, 144–152.
4. Todini, E.; Pilati, S. _A Gradient Algorithm for the Analysis of Pipe Networks_. In _Computer Applications in Water Supply_, 1988.
5. Autodesk Inc. _InfoWater Pro Documentation_. [link](https://help.autodesk.com/view/INFWP/ENU/)
6. Rossman, L.A.; Woo, H.; Tryby, M.; et al. _EPANET 2.2 User Manual_. US EPA, 2020.
7. EPANET Hydraulic Solver Source Code. [GitHub](https://github.com/OpenWaterAnalytics/EPANET/blob/dev/src/hydsolver.c)
8. Hernández, F.; Liang, X. _Hybridizing Bayesian and Variational Data Assimilation for High-Resolution Hydrologic Forecasting_. Hydrol. Earth Syst. Sci. 2018, 22, 5759–5779.

---

**Figure 1.** Step loop with proposed modifications in italics.

Figure 1. Flowchart of the proposed single-loop hybrid search algorithm.

Step 1. Compute headloss coefficients for links.

Step 2. Compute node matrix coefficients (including nodal demands). A guess of the design flow is produced and added to the hydrant’s nodal demand before computing coefficients.

Step 3. Solve the linear matrix system to estimate nodal head values (pressures) based on current flow estimates.

Step 4. Compute flow values in links (and velocities) based on head estimates. Determine the critical element and whether its fire flow constraint is satisfied based on current pressure and velocity estimates.

Step 5. Check for convergence. Add both a fire flow guess convergence clause and a non-violation clause to establish convergence.

If convergence is not achieved, return to Step 1.

If convergence is achieved, the algorithm terminates.

**Table 1.** Characteristics of the three test models and results.

---

© 2024 by the author. Open access under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

---

Would you like me to also **simplify the math notation** into plain-text equations (for an LLM that doesn’t handle LaTeX well), or keep the LaTeX-style formatting as above?
