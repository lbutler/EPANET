# Fire Flow Integration Guide

This guide explains how to integrate the EPANET fire flow capability into existing applications.

## Table of Contents
1. [Integration Overview](#integration-overview)
2. [C/C++ Applications](#cc-applications)
3. [Python Integration](#python-integration)
4. [MATLAB Integration](#matlab-integration)
5. [GUI Applications](#gui-applications)
6. [Web Services](#web-services)
7. [Best Practices](#best-practices)

## Integration Overview

The fire flow module is built directly into EPANET 2.3 and can be accessed through:
- Native C API functions
- Language bindings (Python, MATLAB, etc.)
- Command-line tools
- GUI applications

### Key Integration Points

1. **Initialization**: Enable fire flow and set parameters
2. **Execution**: Run analysis within hydraulic simulation
3. **Results**: Extract fire flow value and critical elements
4. **Visualization**: Display results in maps/reports

## C/C++ Applications

### Basic Integration

```cpp
// Existing EPANET code
EN_Project project;
EN_createproject(&project);
EN_open(project, "network.inp", "", "");

// ADD: Fire flow capability
#include <epanet2_fireflow.h>

void analyzeFireFlow(EN_Project ph, const char* hydrantID) {
    int hydrantIdx;
    double flow;
    int critNode, critLink;
    
    // Get hydrant index
    EN_getnodeindex(ph, hydrantID, &hydrantIdx);
    
    // Configure fire flow
    EN_setfireflow(ph, 1);
    EN_setfireflowhydrant(ph, hydrantIdx);
    EN_setfireflowparams(ph, 20.0, 10.0, 5.0);
    
    // Run analysis
    EN_openH(ph);
    EN_runfireflow(ph, EN_FF_DESIGN, &flow, &critNode, &critLink);
    EN_closeH(ph);
    
    printf("Fire flow: %.1f gpm\n", flow);
}
```

### Advanced Integration with Error Handling

```cpp
class FireFlowAnalyzer {
private:
    EN_Project m_project;
    bool m_enabled;
    
public:
    FireFlowAnalyzer(EN_Project project) 
        : m_project(project), m_enabled(false) {}
    
    int Initialize(double minPressure, double maxVelocity) {
        int err = EN_setfireflow(m_project, 1);
        if (err) return err;
        
        err = EN_setfireflowparams(m_project, minPressure, 
                                   maxVelocity, 5.0);
        if (err) return err;
        
        m_enabled = true;
        return 0;
    }
    
    struct FireFlowResult {
        double flow;
        std::string criticalElement;
        bool isPressureConstrained;
        bool converged;
        double margin;
    };
    
    FireFlowResult Analyze(const std::string& hydrantID) {
        FireFlowResult result = {0};
        
        if (!m_enabled) {
            throw std::runtime_error("Fire flow not initialized");
        }
        
        int hydrantIdx;
        int err = EN_getnodeindex(m_project, hydrantID.c_str(), 
                                  &hydrantIdx);
        if (err) {
            throw std::runtime_error("Invalid hydrant ID");
        }
        
        EN_setfireflowhydrant(m_project, hydrantIdx);
        
        double flow;
        int critNode, critLink;
        err = EN_runfireflow(m_project, EN_FF_DESIGN, &flow, 
                            &critNode, &critLink);
        
        if (err && err != 2) {  // 2 = convergence warning
            throw std::runtime_error("Analysis failed");
        }
        
        result.flow = flow;
        
        // Get critical element
        char id[256];
        if (critNode > 0) {
            EN_getnodeid(m_project, critNode, id);
            result.criticalElement = id;
            result.isPressureConstrained = true;
        } else if (critLink > 0) {
            EN_getlinkid(m_project, critLink, id);
            result.criticalElement = id;
            result.isPressureConstrained = false;
        }
        
        // Get status
        int conv, iter;
        double rc;
        EN_getfireflowstatus(m_project, &conv, &iter, &rc);
        result.converged = (conv == 1);
        result.margin = rc * 100;  // Convert to percentage
        
        return result;
    }
};
```

## Python Integration

### Using WNTR (Water Network Tool for Resilience)

```python
# Assuming WNTR is updated with fire flow support
import wntr

# Load network
wn = wntr.network.WaterNetworkModel('network.inp')

# Create fire flow analyzer
ff = wntr.sim.FireFlowAnalyzer(wn)
ff.set_constraints(min_pressure=20, max_velocity=10)

# Analyze single hydrant
result = ff.analyze_hydrant('J-100')
print(f"Fire flow: {result.flow:.1f} gpm")
print(f"Critical: {result.critical_element}")

# Analyze all hydrants
results = ff.analyze_all_hydrants()
results.to_csv('fireflow_results.csv')
```

### Using PyEPANET

```python
import epanet as en

class FireFlowAnalyzer:
    def __init__(self, inp_file):
        self.inp_file = inp_file
        en.open(inp_file)
        
    def analyze(self, hydrant_id, min_pressure=20, max_velocity=10):
        # Get hydrant index
        hydrant_idx = en.getnodeindex(hydrant_id)
        
        # Configure fire flow
        en.setfireflow(1)
        en.setfireflowhydrant(hydrant_idx)
        en.setfireflowparams(min_pressure, max_velocity, 5.0)
        
        # Run analysis
        en.openH()
        flow, crit_node, crit_link = en.runfireflow(en.FF_DESIGN)
        en.closeH()
        
        # Get critical element name
        if crit_node > 0:
            crit_name = en.getnodeid(crit_node)
            crit_type = "pressure"
        elif crit_link > 0:
            crit_name = en.getlinkid(crit_link)
            crit_type = "velocity"
        else:
            crit_name = None
            crit_type = None
            
        return {
            'hydrant': hydrant_id,
            'flow_gpm': flow,
            'critical_element': crit_name,
            'constraint_type': crit_type
        }
    
    def analyze_all(self):
        results = []
        
        # Get all junction IDs
        num_nodes = en.getcount(en.NODECOUNT)
        for i in range(1, num_nodes + 1):
            if en.getnodetype(i) == en.JUNCTION:
                node_id = en.getnodeid(i)
                try:
                    result = self.analyze(node_id)
                    results.append(result)
                except:
                    pass  # Skip if analysis fails
                    
        return results
    
    def close(self):
        en.close()

# Usage
analyzer = FireFlowAnalyzer('network.inp')
result = analyzer.analyze('J-100')
print(f"Fire flow at J-100: {result['flow_gpm']:.1f} gpm")
analyzer.close()
```

## MATLAB Integration

### Basic MATLAB Wrapper

```matlab
function [flow, critical] = analyzeFireFlow(network_file, hydrant_id)
    % ANALYZEFIREFLOW Compute fire flow at hydrant
    %   [flow, critical] = analyzeFireFlow('network.inp', 'J-100')
    
    % Load EPANET library
    loadlibrary('epanet2', 'epanet2.h');
    loadlibrary('epanet2', 'epanet2_fireflow.h');
    
    % Create project
    ph = libpointer('voidPtr');
    calllib('epanet2', 'EN_createproject', ph);
    
    % Open network
    calllib('epanet2', 'EN_open', ph, network_file, '', '');
    
    % Get hydrant index
    hydrant_idx = 0;
    calllib('epanet2', 'EN_getnodeindex', ph, hydrant_id, hydrant_idx);
    
    % Configure fire flow
    calllib('epanet2', 'EN_setfireflow', ph, 1);
    calllib('epanet2', 'EN_setfireflowhydrant', ph, hydrant_idx);
    calllib('epanet2', 'EN_setfireflowparams', ph, 20.0, 10.0, 5.0);
    
    % Run analysis
    calllib('epanet2', 'EN_openH', ph);
    flow = 0;
    crit_node = 0;
    crit_link = 0;
    calllib('epanet2', 'EN_runfireflow', ph, 3, flow, crit_node, crit_link);
    
    % Get critical element
    if crit_node > 0
        critical = calllib('epanet2', 'EN_getnodeid', ph, crit_node);
    elseif crit_link > 0
        critical = calllib('epanet2', 'EN_getlinkid', ph, crit_link);
    else
        critical = 'None';
    end
    
    % Clean up
    calllib('epanet2', 'EN_closeH', ph);
    calllib('epanet2', 'EN_close', ph);
    calllib('epanet2', 'EN_deleteproject', ph);
    
    % Unload library
    unloadlibrary('epanet2');
end
```

### MATLAB Class Implementation

```matlab
classdef FireFlowAnalyzer < handle
    properties (Access = private)
        project
        network_file
    end
    
    methods
        function obj = FireFlowAnalyzer(network_file)
            obj.network_file = network_file;
            obj.project = epanet(network_file);
        end
        
        function results = analyzeAll(obj, varargin)
            % Parse optional parameters
            p = inputParser;
            addParameter(p, 'MinPressure', 20, @isnumeric);
            addParameter(p, 'MaxVelocity', 10, @isnumeric);
            addParameter(p, 'Tolerance', 5, @isnumeric);
            parse(p, varargin{:});
            
            % Get all junctions
            node_ids = obj.project.getNodeNameID();
            node_types = obj.project.getNodeType();
            junctions = node_ids(node_types == 0);
            
            % Preallocate results
            n = length(junctions);
            results = table();
            results.Hydrant = junctions';
            results.FireFlow = zeros(n, 1);
            results.Critical = cell(n, 1);
            results.Converged = false(n, 1);
            
            % Analyze each junction
            for i = 1:n
                [results.FireFlow(i), results.Critical{i}, ...
                 results.Converged(i)] = obj.analyzeHydrant(...
                    junctions{i}, p.Results);
            end
            
            % Sort by fire flow
            results = sortrows(results, 'FireFlow', 'descend');
        end
        
        function [flow, critical, converged] = analyzeHydrant(obj, ...
                hydrant_id, params)
            % Implementation using EPANET-MATLAB toolkit
            % ... (similar to Python example)
        end
        
        function plotResults(obj, results)
            % Create visualization
            figure('Name', 'Fire Flow Analysis Results');
            
            % Bar chart of top 20 hydrants
            subplot(2, 1, 1);
            bar(results.FireFlow(1:min(20, height(results))));
            xlabel('Hydrant Rank');
            ylabel('Fire Flow (gpm)');
            title('Top 20 Hydrants by Fire Flow');
            
            % Histogram of all flows
            subplot(2, 1, 2);
            histogram(results.FireFlow, 30);
            xlabel('Fire Flow (gpm)');
            ylabel('Count');
            title('Fire Flow Distribution');
        end
    end
end
```

## GUI Applications

### Integration with EPANET GUI

For applications like EPANET-UI or custom GUIs:

```cpp
// Add menu item for fire flow
void MainWindow::createMenus() {
    QMenu* analysisMenu = menuBar()->addMenu("&Analysis");
    
    QAction* fireFlowAction = new QAction("&Fire Flow Analysis", this);
    fireFlowAction->setStatusTip("Analyze fire flow capacity");
    connect(fireFlowAction, &QAction::triggered, 
            this, &MainWindow::runFireFlow);
    analysisMenu->addAction(fireFlowAction);
}

void MainWindow::runFireFlow() {
    // Get selected node
    QString nodeId = getSelectedNode();
    if (nodeId.isEmpty()) {
        QMessageBox::warning(this, "Warning", 
                           "Please select a hydrant node");
        return;
    }
    
    // Show parameter dialog
    FireFlowDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    
    // Run analysis
    double flow = 0;
    QString critical;
    
    try {
        FireFlowAnalyzer analyzer(m_project);
        analyzer.Initialize(dialog.minPressure(), 
                          dialog.maxVelocity());
        auto result = analyzer.Analyze(nodeId.toStdString());
        
        flow = result.flow;
        critical = QString::fromStdString(result.criticalElement);
    } catch (std::exception& e) {
        QMessageBox::critical(this, "Error", e.what());
        return;
    }
    
    // Display results
    QString msg = QString("Fire Flow Results:\n\n"
                         "Hydrant: %1\n"
                         "Flow: %2 gpm\n"
                         "Critical Element: %3")
                  .arg(nodeId)
                  .arg(flow, 0, 'f', 1)
                  .arg(critical);
    
    QMessageBox::information(this, "Fire Flow Analysis", msg);
    
    // Highlight critical element on map
    highlightElement(critical);
}
```

### Web-Based GUI (JavaScript)

```javascript
// Using EPANET-JS (WebAssembly version)
class FireFlowAnalyzer {
    constructor(epanetModule) {
        this.Module = epanetModule;
        this.project = null;
    }
    
    async loadNetwork(file) {
        // Load INP file into WebAssembly filesystem
        const data = await file.arrayBuffer();
        const view = new Uint8Array(data);
        this.Module.FS.writeFile('/network.inp', view);
        
        // Create and open project
        this.project = this.Module._EN_createproject();
        this.Module._EN_open(this.project, '/network.inp', '', '');
    }
    
    analyzeHydrant(hydrantId, minPressure = 20, maxVelocity = 10) {
        // Get hydrant index
        const idx = this.Module._EN_getnodeindex(this.project, hydrantId);
        
        // Configure fire flow
        this.Module._EN_setfireflow(this.project, 1);
        this.Module._EN_setfireflowhydrant(this.project, idx);
        this.Module._EN_setfireflowparams(this.project, 
                                          minPressure, maxVelocity, 5);
        
        // Run analysis
        this.Module._EN_openH(this.project);
        const result = this.Module._EN_runfireflow(this.project, 3);
        this.Module._EN_closeH(this.project);
        
        return {
            flow: result.flow,
            criticalNode: result.criticalNode,
            criticalLink: result.criticalLink
        };
    }
    
    cleanup() {
        if (this.project) {
            this.Module._EN_close(this.project);
            this.Module._EN_deleteproject(this.project);
        }
    }
}

// Usage in web app
async function runFireFlowAnalysis() {
    const fileInput = document.getElementById('network-file');
    const hydrantInput = document.getElementById('hydrant-id');
    
    if (!fileInput.files[0]) {
        alert('Please select a network file');
        return;
    }
    
    // Load EPANET WebAssembly module
    const Module = await loadEPANETModule();
    const analyzer = new FireFlowAnalyzer(Module);
    
    try {
        await analyzer.loadNetwork(fileInput.files[0]);
        const result = analyzer.analyzeHydrant(hydrantInput.value);
        
        // Display results
        document.getElementById('result-flow').textContent = 
            `${result.flow.toFixed(1)} gpm`;
        
        // Update map visualization
        updateMapWithResults(result);
        
    } catch (error) {
        console.error('Analysis failed:', error);
        alert('Analysis failed: ' + error.message);
    } finally {
        analyzer.cleanup();
    }
}
```

## Web Services

### REST API Implementation

```python
# Flask REST API for fire flow analysis
from flask import Flask, request, jsonify
import epanet as en
import tempfile
import os

app = Flask(__name__)

@app.route('/api/fireflow', methods=['POST'])
def analyze_fireflow():
    """
    POST /api/fireflow
    Body: {
        "network": "<base64 encoded INP file>",
        "hydrant": "node_id",
        "min_pressure": 20,
        "max_velocity": 10
    }
    """
    try:
        data = request.json
        
        # Decode and save network file
        import base64
        network_data = base64.b64decode(data['network'])
        with tempfile.NamedTemporaryFile(suffix='.inp', delete=False) as f:
            f.write(network_data)
            temp_file = f.name
        
        # Run analysis
        en.open(temp_file)
        en.openH()
        
        hydrant_idx = en.getnodeindex(data['hydrant'])
        en.setfireflow(1)
        en.setfireflowhydrant(hydrant_idx)
        en.setfireflowparams(
            data.get('min_pressure', 20),
            data.get('max_velocity', 10),
            5.0
        )
        
        flow, crit_node, crit_link = en.runfireflow(en.FF_DESIGN)
        conv, iter, rc = en.getfireflowstatus()
        
        # Get critical element name
        if crit_node > 0:
            crit_name = en.getnodeid(crit_node)
            crit_type = 'pressure'
        elif crit_link > 0:
            crit_name = en.getlinkid(crit_link)
            crit_type = 'velocity'
        else:
            crit_name = None
            crit_type = None
        
        result = {
            'success': True,
            'hydrant': data['hydrant'],
            'flow_gpm': flow,
            'critical_element': crit_name,
            'constraint_type': crit_type,
            'converged': bool(conv),
            'iterations': iter,
            'margin_percent': rc * 100
        }
        
        en.closeH()
        en.close()
        os.unlink(temp_file)
        
        return jsonify(result)
        
    except Exception as e:
        return jsonify({
            'success': False,
            'error': str(e)
        }), 400

@app.route('/api/fireflow/batch', methods=['POST'])
def analyze_fireflow_batch():
    """
    Analyze multiple hydrants
    POST /api/fireflow/batch
    Body: {
        "network": "<base64 encoded INP file>",
        "hydrants": ["node1", "node2", ...],
        "min_pressure": 20,
        "max_velocity": 10
    }
    """
    # Similar implementation for batch analysis
    pass

if __name__ == '__main__':
    app.run(debug=True)
```

### GraphQL Implementation

```javascript
// GraphQL schema for fire flow
const typeDefs = `
  type FireFlowResult {
    hydrant: String!
    flow: Float!
    criticalElement: String
    constraintType: String
    converged: Boolean!
    iterations: Int!
    margin: Float!
  }
  
  input FireFlowParams {
    minPressure: Float = 20
    maxVelocity: Float = 10
    tolerance: Float = 5
  }
  
  type Query {
    analyzeFireFlow(
      networkId: ID!
      hydrantId: String!
      params: FireFlowParams
    ): FireFlowResult!
    
    analyzeAllHydrants(
      networkId: ID!
      params: FireFlowParams
    ): [FireFlowResult!]!
  }
`;

const resolvers = {
  Query: {
    analyzeFireFlow: async (parent, args, context) => {
      const { networkId, hydrantId, params } = args;
      
      // Load network from database
      const network = await context.db.getNetwork(networkId);
      
      // Run EPANET analysis
      const analyzer = new FireFlowAnalyzer(network.data);
      const result = await analyzer.analyze(hydrantId, params);
      
      return result;
    },
    
    analyzeAllHydrants: async (parent, args, context) => {
      // Implementation for batch analysis
    }
  }
};
```

## Best Practices

### 1. Error Handling

Always handle potential errors:

```cpp
enum FireFlowError {
    FF_SUCCESS = 0,
    FF_NO_NETWORK = 1,
    FF_INVALID_HYDRANT = 2,
    FF_CONVERGENCE_FAILED = 3,
    FF_INVALID_PARAMS = 4
};

FireFlowError safeAnalyze(EN_Project ph, const char* hydrant,
                          double* flow, std::string* error) {
    try {
        // Check network is loaded
        int count;
        if (EN_getcount(ph, EN_NODECOUNT, &count) != 0) {
            *error = "No network loaded";
            return FF_NO_NETWORK;
        }
        
        // Validate hydrant exists
        int idx;
        if (EN_getnodeindex(ph, hydrant, &idx) != 0) {
            *error = "Invalid hydrant ID";
            return FF_INVALID_HYDRANT;
        }
        
        // Run analysis with timeout
        // ... analysis code ...
        
        return FF_SUCCESS;
        
    } catch (std::exception& e) {
        *error = e.what();
        return FF_CONVERGENCE_FAILED;
    }
}
```

### 2. Performance Optimization

For large-scale analysis:

```python
# Parallel processing with multiprocessing
from multiprocessing import Pool
import epanet as en

def analyze_hydrant_worker(args):
    network_file, hydrant_id, params = args
    en.open(network_file)
    # ... run analysis ...
    en.close()
    return result

def analyze_network_parallel(network_file, hydrant_ids, num_workers=4):
    args = [(network_file, hid, params) for hid in hydrant_ids]
    
    with Pool(num_workers) as pool:
        results = pool.map(analyze_hydrant_worker, args)
    
    return results
```

### 3. Caching Results

```python
import hashlib
import json
import redis

class CachedFireFlowAnalyzer:
    def __init__(self, network_file):
        self.network_file = network_file
        self.cache = redis.Redis()
        
    def _get_cache_key(self, hydrant, params):
        # Create unique key for this analysis
        data = f"{self.network_file}:{hydrant}:{params}"
        return hashlib.md5(data.encode()).hexdigest()
    
    def analyze(self, hydrant, **params):
        # Check cache first
        key = self._get_cache_key(hydrant, params)
        cached = self.cache.get(key)
        
        if cached:
            return json.loads(cached)
        
        # Run analysis
        result = self._run_analysis(hydrant, **params)
        
        # Cache result for 1 hour
        self.cache.setex(key, 3600, json.dumps(result))
        
        return result
```

### 4. Validation

Always validate inputs:

```cpp
bool validateFireFlowParams(double minPressure, double maxVelocity,
                           std::string& error) {
    if (minPressure <= 0 || minPressure > 100) {
        error = "Pressure must be between 0 and 100 psi";
        return false;
    }
    
    if (maxVelocity <= 0 || maxVelocity > 30) {
        error = "Velocity must be between 0 and 30 ft/s";
        return false;
    }
    
    return true;
}
```

### 5. Logging and Monitoring

```python
import logging
import time

logger = logging.getLogger('fireflow')

def analyze_with_logging(network, hydrant, **params):
    start_time = time.time()
    
    logger.info(f"Starting fire flow analysis for {hydrant}")
    logger.debug(f"Parameters: {params}")
    
    try:
        result = run_fireflow_analysis(network, hydrant, **params)
        
        elapsed = time.time() - start_time
        logger.info(f"Analysis completed in {elapsed:.2f}s")
        logger.info(f"Result: {result['flow']:.1f} gpm")
        
        # Log warnings
        if not result['converged']:
            logger.warning(f"Analysis did not converge for {hydrant}")
        
        if result['margin'] < 5:
            logger.warning(f"Operating near constraint boundary")
        
        return result
        
    except Exception as e:
        logger.error(f"Analysis failed for {hydrant}: {e}")
        raise
```

## Summary

The fire flow module integrates seamlessly with existing EPANET applications. Key points:

1. **Include Headers**: Add `epanet2_fireflow.h` to your includes
2. **Enable Analysis**: Call `EN_setfireflow()` before running
3. **Configure Parameters**: Set appropriate constraints for your system
4. **Handle Errors**: Check return codes and convergence status
5. **Optimize Performance**: Use caching and parallel processing for large networks
6. **Validate Inputs**: Ensure parameters are within reasonable ranges

For additional examples and support, see the [Fire Flow User Guide](FIRE_FLOW_USER_GUIDE.md) and [API Reference](FIRE_FLOW_API_REFERENCE.md).