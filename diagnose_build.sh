#!/bin/bash

# Diagnostic script for build issues

echo "EPANET Build Diagnostics"
echo "========================"
echo ""

# Check for uncommitted files that might be causing issues
echo "1. Checking for untracked files that might interfere..."
if [ -d .git ]; then
    git status --porcelain | grep "^??" | head -10
    if [ $? -eq 0 ]; then
        echo "  Warning: Untracked files found (shown above)"
    else
        echo "  No untracked files"
    fi
else
    echo "  Not a git repository"
fi
echo ""

# Check for rules parser files
echo "2. Looking for rules parser related files..."
find . -name "*rules_parser*" -o -name "*ast_*" -o -name "*lexer*" 2>/dev/null | grep -v ".git" | head -10
if [ $? -ne 0 ]; then
    echo "  No rules parser files found"
fi
echo ""

# Check CMake cache
echo "3. Checking CMake cache..."
if [ -f build/CMakeCache.txt ]; then
    grep -E "BUILD_TESTS|Boost" build/CMakeCache.txt | head -5
else
    echo "  No CMake cache found"
fi
echo ""

# Check for test directories
echo "4. Checking test directories..."
if [ -d tests ]; then
    echo "  tests/ directory exists"
    ls tests/*.c 2>/dev/null | head -5
    if [ $? -ne 0 ]; then
        echo "  No .c files in tests/"
    fi
else
    echo "  No tests/ directory"
fi
echo ""

# Try standard build
echo "5. Testing standard build (without tests)..."
rm -rf build_test
mkdir build_test
cd build_test
cmake .. > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "  ✓ CMake configuration successful"
    make -j4 > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "  ✓ Build successful"
    else
        echo "  ✗ Build failed"
        echo "  Running verbose build to see error:"
        make VERBOSE=1 2>&1 | grep -A 2 -B 2 "error:" | head -20
    fi
else
    echo "  ✗ CMake configuration failed"
fi
cd ..
rm -rf build_test
echo ""

echo "6. Checking for fireflow files..."
ls -la src/fireflow* 2>/dev/null
if [ $? -ne 0 ]; then
    echo "  No fireflow files found"
fi
echo ""

echo "Diagnostics complete."
echo ""
echo "RECOMMENDATIONS:"
echo "----------------"
echo "If you're getting rules_parser errors:"
echo "1. Make sure you have a clean checkout (git clean -fdx)"
echo "2. Remove any build directories (rm -rf build)"
echo "3. Use the standard build command:"
echo "   mkdir build && cd build"
echo "   cmake .."
echo "   cmake --build . --config Release"
echo ""
echo "For tests (requires Boost):"
echo "1. Install Boost development libraries"
echo "2. Then build with:"
echo "   cmake -DBUILD_TESTS=ON .."
echo "   cmake --build . --config Release"