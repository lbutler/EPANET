# Use Ubuntu as the base
FROM ubuntu:22.04

# Install system dependencies
RUN apt-get update && apt-get install -y \
  git \
  curl \
  python3 \
  python3-pip \
  cmake \
  build-essential \
  p7zip-full \
  tar \
  unzip \
  jq \
  libboost-all-dev

# Base working directory in the container
WORKDIR /epanet

# --- Build Context Assumed to be the EPANET Repository Root ---

# Copy the entire local repository content (build context)
# This will be used for the SUT build.
# IMPORTANT: Create a .dockerignore file in your repo root
#            to exclude .git, build artifacts, virtualenvs, etc.
COPY . /epanet/local_src/

# Copy "nrtest-epanet" plugin folder (assuming it's at repo root)
# Adjust path if it's elsewhere, e.g., ./tools/nrtest-epanet
COPY /tools/nrtest-epanet /nrtest-epanet/
# Install the nrtest plugin *before* requirements if it's a dependency
RUN pip3 install /nrtest-epanet/

# Copy Python requirements (assuming they are in ./tools/ directory)
COPY /tools/regression-testing/requirements-epanet.txt /tmp/requirements-epanet.txt
RUN pip3 install -r /tmp/requirements-epanet.txt

# Environment variables for controlling build & testing
# SUT_TAG is removed as we now use the local code context for SUT
ENV REF_TAG="v2.2" \
    DO_STANDARD_TEST="true" \
    DO_CUSTOM_TEST="false" \
    EXAMPLES_REPO="https://github.com/OpenWaterAnalytics/epanet-example-networks" \
    # BUILD_HOME is less relevant now as builds happen in specific dirs
    TEST_HOME="/epanet/nrtests" \
    PLATFORM="linux"

# Copy the modified entrypoint script (assuming it's in ./tools/ directory)
COPY /tools/regression-testing/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Default entrypoint
ENTRYPOINT ["/entrypoint.sh"]