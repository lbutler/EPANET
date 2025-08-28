# Makefile for EPANET Fire Flow Examples
#
# Usage:
#   make all          - Build all examples
#   make clean        - Remove compiled examples
#   make test         - Run basic tests
#   make help         - Show this help

# Compiler settings
CC = gcc
CFLAGS = -Wall -O2 -I..
LDFLAGS = -L../build/lib -Wl,-rpath,../build/lib
LIBS = -lepanet2 -lm

# Source files
SOURCES = fireflow_basic.c fireflow_batch.c fireflow_sensitivity.c
TARGETS = $(SOURCES:.c=)

# Default target
.PHONY: all
all: $(TARGETS)
	@echo "All examples built successfully!"

# Build rules
fireflow_basic: fireflow_basic.c
	@echo "Building $@..."
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)

fireflow_batch: fireflow_batch.c
	@echo "Building $@..."
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)

fireflow_sensitivity: fireflow_sensitivity.c
	@echo "Building $@..."
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)

# Clean target
.PHONY: clean
clean:
	@echo "Cleaning..."
	rm -f $(TARGETS)
	rm -f *.o

# Test target - run basic tests
.PHONY: test
test: all
	@echo "Running basic test..."
	./fireflow_basic ../example-networks/Net1.inp 11
	@echo ""
	@echo "Test completed successfully!"

# Run all examples
.PHONY: run-all
run-all: all
	@echo "=== Running Basic Example ==="
	./fireflow_basic ../example-networks/Net1.inp 11
	@echo ""
	@echo "=== Running Batch Example ==="
	./fireflow_batch ../example-networks/Net1.inp
	@echo ""
	@echo "=== Running Sensitivity Example ==="
	./fireflow_sensitivity ../example-networks/Net1.inp 11

# Help target
.PHONY: help
help:
	@echo "EPANET Fire Flow Examples Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build all example programs"
	@echo "  clean            - Remove compiled programs"
	@echo "  test             - Run basic test with Net1.inp"
	@echo "  run-all          - Run all examples"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Individual targets:"
	@echo "  fireflow_basic   - Build basic example"
	@echo "  fireflow_batch   - Build batch analysis example"
	@echo "  fireflow_sensitivity - Build sensitivity analysis example"
	@echo ""
	@echo "Example usage:"
	@echo "  make all"
	@echo "  ./fireflow_basic ../example-networks/Net1.inp 11"

# Debug build
.PHONY: debug
debug: CFLAGS += -g -DDEBUG
debug: all

# Install examples to system (optional)
PREFIX ?= /usr/local
.PHONY: install
install: all
	@echo "Installing examples to $(PREFIX)/share/epanet/examples..."
	mkdir -p $(PREFIX)/share/epanet/examples
	cp $(TARGETS) $(PREFIX)/share/epanet/examples/
	cp README.md $(PREFIX)/share/epanet/examples/
	@echo "Installation complete!"

.PHONY: uninstall
uninstall:
	@echo "Removing examples from $(PREFIX)/share/epanet/examples..."
	rm -rf $(PREFIX)/share/epanet/examples
	@echo "Uninstallation complete!"

# Check dependencies
.PHONY: check-deps
check-deps:
	@echo "Checking dependencies..."
	@test -f ../build/lib/libepanet2.so || test -f ../build/lib/libepanet2.dylib || test -f ../build/lib/epanet2.dll || \
		(echo "Error: EPANET library not found. Please build EPANET first." && exit 1)
	@test -f ../include/epanet2.h || \
		(echo "Error: epanet2.h not found." && exit 1)
	@test -f ../include/epanet2_fireflow.h || \
		(echo "Error: epanet2_fireflow.h not found." && exit 1)
	@echo "All dependencies found!"

# Phony targets
.PHONY: all clean test run-all help debug install uninstall check-deps