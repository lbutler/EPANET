import struct
import numpy as np

# Constants from C Code
WORDSIZE = 4
PROLOGUE = 884
MAXID_P1 = 32  # Max ID length + 1
NNODERESULTS = 4
NLINKRESULTS = 8

# Mapping EPANET binary element types to expected attributes
ELEMENT_TYPES = {
    "NODE": NNODERESULTS,
    "LINK": NLINKRESULTS
}

def read_int32(file):
    """Reads a 4-byte integer from a binary file."""
    return struct.unpack("<i", file.read(WORDSIZE))[0]

def read_float32(file):
    """Reads a 4-byte float from a binary file."""
    return struct.unpack("<f", file.read(WORDSIZE))[0]

def output_generator(filepath):
    """
    Parses an EPANET binary output file (.out) and yields element attributes.

    Yields:
        tuple: (numpy array of element attributes, (element type, period, attribute))
    """
    with open(filepath, "rb") as file:
        # --- Read Network Size ---
        file.seek(2 * WORDSIZE)  # Skip magic number and version
        node_count = read_int32(file)
        tank_count = read_int32(file)
        link_count = read_int32(file)
        pump_count = read_int32(file)
        valve_count = read_int32(file)

        # --- Read Number of Reporting Periods ---
        file.seek(-3 * WORDSIZE, 2)  # Move to epilogue
        report_periods = read_int32(file)

        # --- Compute Data Offsets ---
        bytecount = PROLOGUE
        bytecount += MAXID_P1 * node_count  # Node names
        bytecount += MAXID_P1 * link_count  # Link names
        bytecount += 3 * WORDSIZE * link_count  # Connectivity
        bytecount += 2 * WORDSIZE * tank_count  # Tank data
        bytecount += WORDSIZE * node_count  # Node elevations
        bytecount += 2 * WORDSIZE * link_count  # Link length & diameter
        bytecount += 7 * WORDSIZE * pump_count + WORDSIZE  # Pump energy
        output_start_pos = bytecount
        bytes_per_period = (NNODERESULTS * node_count + NLINKRESULTS * link_count) * WORDSIZE

        # --- Read Simulation Results ---
        for period_index in range(report_periods):
            file.seek(output_start_pos + period_index * bytes_per_period)

            # Read NODE attributes
            for attr_index in range(NNODERESULTS):
                values = np.array([read_float32(file) for _ in range(node_count)])
                yield (values, ("NODE", period_index, attr_index))

            # Read LINK attributes
            for attr_index in range(NLINKRESULTS):
                values = np.array([read_float32(file) for _ in range(link_count)])
                yield (values, ("LINK", period_index, attr_index))
