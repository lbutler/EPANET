#!/bin/bash

# EPANET Fire Flow Implementation - Build Verification Script
# This script verifies that the build is complete and all tests pass

echo "=========================================="
echo "EPANET Fire Flow Build Verification"
echo "=========================================="
echo ""

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

ERRORS=0
WARNINGS=0

# Function to check if a file exists
check_file() {
    if [ -f "$1" ]; then
        echo -e "${GREEN}✓${NC} File exists: $1"
    else
        echo -e "${RED}✗${NC} File missing: $1"
        ((ERRORS++))
    fi
}

# Function to run a command and check result
run_test() {
    echo -n "Testing: $1 ... "
    if eval "$2" > /dev/null 2>&1; then
        echo -e "${GREEN}PASS${NC}"
    else
        echo -e "${RED}FAIL${NC}"
        ((ERRORS++))
    fi
}

echo "1. Checking source files..."
echo "----------------------------"
check_file "src/fireflow.h"
check_file "src/fireflow.c"
check_file "src/fireflow_api.c"
check_file "include/epanet2_fireflow.h"
echo ""

echo "2. Checking documentation..."
echo "----------------------------"
check_file "docs/FIRE_FLOW_USER_GUIDE.md"
check_file "docs/FIRE_FLOW_API_REFERENCE.md"
check_file "docs/FIRE_FLOW_INTEGRATION_GUIDE.md"
check_file "examples/README.md"
echo ""

echo "3. Checking build artifacts..."
echo "------------------------------"
check_file "build/lib/libepanet2.so"
check_file "build/bin/runepanet"
echo ""

echo "4. Building library..."
echo "----------------------"
cd build
if make -j4 > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} Library builds successfully"
else
    echo -e "${RED}✗${NC} Build failed"
    ((ERRORS++))
fi
cd ..
echo ""

echo "5. Building examples..."
echo "-----------------------"
cd examples
if make clean > /dev/null 2>&1 && make all > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} All examples build successfully"
    check_file "fireflow_basic"
    check_file "fireflow_batch"
    check_file "fireflow_sensitivity"
else
    echo -e "${RED}✗${NC} Example build failed"
    ((ERRORS++))
fi
cd ..
echo ""

echo "6. Running functionality tests..."
echo "---------------------------------"
export LD_LIBRARY_PATH=./build/lib

run_test "Basic EPANET" "./build/bin/runepanet example-networks/Net1.inp /tmp/test.rpt"
run_test "Fire Flow API" "./test_fireflow_api example-networks/Net1.inp 11"
run_test "Basic Example" "./examples/fireflow_basic example-networks/Net1.inp 11"
run_test "Core Hydraulics" "./test_epanet_basic example-networks/Net1.inp"
echo ""

echo "7. Checking for debug output..."
echo "--------------------------------"
if grep -q "printf" src/fireflow.c | grep -v "//" > /dev/null 2>&1; then
    echo -e "${YELLOW}⚠${NC} Warning: Possible debug output in fireflow.c"
    ((WARNINGS++))
else
    echo -e "${GREEN}✓${NC} No debug output found"
fi
echo ""

echo "=========================================="
echo "VERIFICATION SUMMARY"
echo "=========================================="

if [ $ERRORS -eq 0 ]; then
    echo -e "${GREEN}✓ BUILD VERIFICATION PASSED${NC}"
    echo "  All components built successfully"
    echo "  All tests passed"
    if [ $WARNINGS -gt 0 ]; then
        echo -e "  ${YELLOW}$WARNINGS warning(s) found${NC}"
    fi
else
    echo -e "${RED}✗ BUILD VERIFICATION FAILED${NC}"
    echo "  $ERRORS error(s) found"
    echo "  Please check the output above for details"
    exit 1
fi

echo ""
echo "Fire flow implementation is ready for use!"
exit 0