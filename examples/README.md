# Fire Flow Analysis Examples

This directory contains example programs demonstrating the EPANET fire flow analysis capability.

## Example Programs

### 1. fireflow_basic.c
**Basic fire flow analysis for a single hydrant**

Simple example showing how to:
- Enable fire flow analysis
- Set a hydrant location
- Configure constraints
- Run analysis and display results

```bash
# Compile
gcc -o fireflow_basic fireflow_basic.c -I.. -L../build/lib -lepanet2 -lm

# Run
./fireflow_basic ../example-networks/Net1.inp 11
```

### 2. fireflow_batch.c
**Batch analysis of all hydrants in a network**

Demonstrates:
- Analyzing every junction as a potential hydrant
- Sorting results by fire flow capacity
- Generating statistics and rankings
- Identifying system-wide bottlenecks

```bash
# Compile
gcc -o fireflow_batch fireflow_batch.c -I.. -L../build/lib -lepanet2 -lm

# Run
./fireflow_batch ../example-networks/Net2.inp
```

Output includes:
- Top 20 hydrants by flow capacity
- Critical elements for each hydrant
- Network-wide statistics

### 3. fireflow_sensitivity.c
**Sensitivity analysis of fire flow to parameters**

Shows how fire flow varies with:
- Pressure thresholds (10-40 psi)
- Velocity thresholds (5-20 ft/s)
- Convergence tolerances
- 2D parameter matrices

```bash
# Compile
gcc -o fireflow_sensitivity fireflow_sensitivity.c -I.. -L../build/lib -lepanet2 -lm

# Run
./fireflow_sensitivity ../example-networks/Net1.inp 11
```

Features visual bar graphs and matrices showing parameter impacts.

## Building All Examples

Use the provided Makefile:

```bash
cd examples
make all
```

Or build individually:

```bash
make fireflow_basic
make fireflow_batch
make fireflow_sensitivity
```

## Required Files

- EPANET library (`libepanet2.so` or `epanet2.dll`)
- Header files:
  - `epanet2.h`
  - `epanet2_2.h`
  - `epanet2_fireflow.h`
- Network input file (`.inp` format)

## Network Files

Example networks are provided in `../example-networks/`:
- `Net1.inp` - Small network (11 nodes, 9 pipes)
- `Net2.inp` - Medium network (36 nodes, 35 pipes)
- `Net3.inp` - Larger network (97 nodes, 117 pipes)

## Typical Results

### Net1.inp Analysis
```
Hydrant 11: 3557 gpm (converged)
Critical element: Pipe 11 (velocity constraint)
Iterations: 12
```

### Performance Benchmarks

| Network | Nodes | Time (single) | Time (batch) |
|---------|-------|---------------|--------------|
| Net1    | 11    | < 0.1s       | < 1s         |
| Net2    | 36    | < 0.2s       | < 5s         |
| Net3    | 97    | < 0.5s       | < 30s        |

## Troubleshooting

### Compilation Errors

If you get "cannot find -lepanet2":
```bash
export LD_LIBRARY_PATH=../build/lib:$LD_LIBRARY_PATH
```

### Runtime Errors

Common issues and solutions:

1. **"Error opening network"**
   - Check file path is correct
   - Ensure INP file is valid EPANET format

2. **"Cannot find node"**
   - Verify node ID exists in network
   - Node IDs are case-sensitive

3. **"Analysis did not converge"**
   - Try relaxing constraints (higher velocity, lower pressure)
   - Increase tolerance parameter
   - Check for closed valves or disconnected areas

## Extending the Examples

To modify for your needs:

1. **Different constraints**: Edit the `EN_setfireflowparams()` call
2. **Multiple hydrants**: Loop through node IDs
3. **Output format**: Modify printf statements or export to CSV
4. **GUI integration**: Use the core logic in your application

## API Quick Reference

```c
// Enable fire flow
EN_setfireflow(ph, 1);

// Set hydrant
EN_setfireflowhydrant(ph, nodeIndex);

// Configure (pressure psi, velocity ft/s, tolerance gpm)
EN_setfireflowparams(ph, 20.0, 10.0, 5.0);

// Run analysis
EN_runfireflow(ph, EN_FF_DESIGN, &flow, &critNode, &critLink);

// Check status
EN_getfireflowstatus(ph, &converged, &iterations, &relCloseness);
```

## Further Documentation

- [Fire Flow User Guide](../docs/FIRE_FLOW_USER_GUIDE.md)
- [API Reference](../docs/FIRE_FLOW_API_REFERENCE.md)
- [Integration Guide](../docs/FIRE_FLOW_INTEGRATION_GUIDE.md)

## Support

For issues or questions:
- Check the troubleshooting section in the User Guide
- Review the API Reference for function details
- Submit issues to the EPANET GitHub repository