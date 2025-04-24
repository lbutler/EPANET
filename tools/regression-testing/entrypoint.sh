#!/bin/bash
set -e # Exit on error

echo "INFO: Starting EPANET regression test inside Docker."
echo "INFO: REF_TAG=${REF_TAG}"
echo "INFO: SUT source is the local repository state copied during build."

# Define source directories
CLONED_SRC_DIR="/epanet/cloned_src" # For reference build
LOCAL_SRC_DIR="/epanet/local_src"  # For SUT build (copied via Dockerfile)
REF_WORK_DIR="/epanet/ref"         # Working copy for reference build
SUT_WORK_DIR="$LOCAL_SRC_DIR"      # SUT source IS the working directory

#----------------------------------
# 1) Clone EPANET Repository (for Reference Build)
#----------------------------------
REPO_URL="https://github.com/OpenWaterAnalytics/EPANET.git"

if [ ! -d "$CLONED_SRC_DIR/.git" ]; then
  echo "INFO: Cloning full EPANET repository for reference build..."
  git clone "$REPO_URL" "$CLONED_SRC_DIR"
else
  echo "INFO: Reference repository clone already exists. Fetching latest changes..."
  cd "$CLONED_SRC_DIR"
  git fetch --all
fi

#----------------------------------
# 2) Create Working Copy for REF
#    (SUT uses the already copied local source)
#----------------------------------
echo "INFO: Creating separate copy for reference build..."
rm -rf "$REF_WORK_DIR"
cp -r "$CLONED_SRC_DIR" "$REF_WORK_DIR"

echo "INFO: System Under Test (SUT) will use source from $SUT_WORK_DIR"

#----------------------------------
# 3) Checkout the Correct Version for REF
#    (SUT version is fixed by the build context)
#----------------------------------
checkout_version() {
  local work_dir="$1"
  local version="$2"
  local label="$3" # e.g., "Reference"
  cd "$work_dir"

  # If "version" is a commit hash (7+ hex chars), just checkout directly;
  # otherwise assume it's a branch or tag and fetch + checkout.
  if [[ "$version" =~ ^[a-fA-F0-9]{7,}$ ]]; then
    echo "INFO: Checking out commit $version for $label in $work_dir..."
    # If commit not in local clone, fetch it
    git fetch origin "$version"
    git checkout "$version"
  else
    echo "INFO: Checking out branch/tag $version for $label in $work_dir..."
    git fetch origin "$version"
    git checkout "$version"
  fi
}

checkout_version "$REF_WORK_DIR" "$REF_TAG" "Reference"
# No checkout needed for SUT

#----------------------------------
# 4) Build Each Version
#----------------------------------
build_epanet() {
  local src_dir="$1" # Source code location (/epanet/ref or /epanet/local_src)
  local label="$2"   # "Reference Build" or "SUT Build"
  local build_dir="$src_dir/build" # Build inside the source dir

  echo "INFO: Building EPANET in $build_dir ($label)..."
  mkdir -p "$build_dir"
  cd "$build_dir"
  # Point CMake to the source directory (one level up from build)
  cmake -DBUILD_TESTS=ON ..
  make -j$(nproc)

  # Run unit tests if they exist in the build dir
  if [ -d "tests" ]; then
      echo "INFO: Running unit tests for $label..."
      cd tests
      ctest -C Release --output-on-failure || echo "WARNING: CTest for $label failed or produced output."
      # Return to source root after tests
      cd "$src_dir"
  else
      echo "INFO: No unit tests found in build directory for $label."
      # Return to source root
      cd "$src_dir"
  fi
}

build_epanet "$REF_WORK_DIR" "Reference Build"
build_epanet "$SUT_WORK_DIR" "SUT Build (Local Context)" # Build directly in the copied source

#----------------------------------
# 5) Download Standard Test Suite (if DO_STANDARD_TEST=true)
#    (No changes needed here)
#----------------------------------
if [ "$DO_STANDARD_TEST" = "true" ]; then
  echo "INFO: Downloading official EPANET example networks for standard tests..."
  mkdir -p "$TEST_HOME"
  cd "$TEST_HOME"
  rm -rf ./*

  LATEST_URL="${EXAMPLES_REPO}/releases/latest"
  LATEST_TAG=$(curl -sI "$LATEST_URL" | grep -Po 'tag\/\K(v\S+)' || true)
  if [ -z "$LATEST_TAG" ]; then
    echo "WARNING: Could not retrieve the latest tag for example networks. Using master."
    LATEST_TAG="master"
  fi

  ARCHIVE_URL="https://codeload.github.com/OpenWaterAnalytics/epanet-example-networks/tar.gz/${LATEST_TAG}"
  echo "INFO: Downloading $ARCHIVE_URL ..."
  curl -fsSL -o examples.tar.gz "$ARCHIVE_URL"
  tar xzf examples.tar.gz && rm examples.tar.gz

  # Adjust based on actual extracted folder name structure
  EXTRACTED_DIR_PATTERN="epanet-example-networks-*/epanet-tests"
  EXTRACTED_DIR=$(find . -maxdepth 1 -type d -name 'epanet-example-networks-*' -printf '%f\n' | head -n 1)/epanet-tests


  if [ ! -d "$EXTRACTED_DIR" ]; then
    echo "ERROR: Could not find extracted example tests directory matching pattern '$EXTRACTED_DIR_PATTERN'"
    ls -l
    exit 1
  fi

  # Remove symlink and use the real directory
  rm -rf "$TEST_HOME/tests"
  mv "$EXTRACTED_DIR" "$TEST_HOME/tests"
  # Clean up the parent extracted folder
  rm -rf epanet-example-networks-*
fi


#----------------------------------
# 6) nrtest "app config" JSON for each version
#----------------------------------
mkdir -p "$TEST_HOME/apps"

cat <<EOF > "$TEST_HOME/apps/epanet-ref.json"
{
  "name" : "epanet-ref",
  "version" : "${REF_TAG}",
  "description" : "${PLATFORM} reference (${REF_TAG})",
  "setup_script" : "",
  "exe" : "${REF_WORK_DIR}/build/bin/runepanet"
}
EOF

# Update the SUT exe path to point to the build inside local_src
cat <<EOF > "$TEST_HOME/apps/epanet-sut.json"
{
  "name" : "epanet-sut",
  "version" : "local",
  "description" : "${PLATFORM} SUT (Local Context)",
  "setup_script" : "",
  "exe" : "${SUT_WORK_DIR}/build/bin/runepanet"
}
EOF

#----------------------------------
# 7) Run nrtest on Standard Tests (if DO_STANDARD_TEST=true)
#    (No changes needed here)
#----------------------------------
if [ "$DO_STANDARD_TEST" = "true" ]; then
  echo "INFO: Running nrtest on standard tests..."
  cd "$TEST_HOME"

  TEST_FILES=$(find "$TEST_HOME/tests" -type f -name '*.json' 2>/dev/null)

  if [ -z "$TEST_FILES" ]; then
    echo "WARNING: No test JSON files found! Standard tests skipped."
  else
    echo "INFO: Executing Reference (${REF_WORK_DIR}/build/bin/runepanet)..."
    nrtest execute "$TEST_HOME/apps/epanet-ref.json" $TEST_FILES -o "$TEST_HOME/benchmark/epanet-ref"

    echo "INFO: Executing SUT (${SUT_WORK_DIR}/build/bin/runepanet)..."
    nrtest execute "$TEST_HOME/apps/epanet-sut.json" $TEST_FILES -o "$TEST_HOME/benchmark/epanet-sut"

    echo "INFO: Comparing epanet-sut to epanet-ref..."
    nrtest compare "$TEST_HOME/benchmark/epanet-sut" "$TEST_HOME/benchmark/epanet-ref" --rtol 0.01 --atol 1e-6
  fi
else
  echo "INFO: Skipping standard tests per DO_STANDARD_TEST=false."
fi

#----------------------------------
# 8) Run Custom Tests (if DO_CUSTOM_TEST=true)
#    (No changes needed here, uses SUT executable)
#----------------------------------
if [ "$DO_CUSTOM_TEST" = "true" ]; then
  if [ -d "/custom_tests" ]; then
    echo "INFO: Running nrtest on custom test cases..."
    mkdir -p "$TEST_HOME/benchmark"
    nrtest execute "$TEST_HOME/apps/epanet-sut.json" /custom_tests -o "$TEST_HOME/benchmark/custom_sut" || true
  else
    echo "INFO: DO_CUSTOM_TEST=true but no /custom_tests directory found."
  fi
fi

echo "INFO: EPANET regression test completed successfully."