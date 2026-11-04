# Benchmark Scripts (`benchmarks/scripts/`)

Host automation scripts to run benchmarks reproducibly across policies.

## Expected Scripts:
- `run_workload.py`: Launches QEMU with a specific policy flag and workload, capturing serial console output.
- `parse_metrics.py`: Parses page fault, swap read/write, and clock tick counters into CSV.
- `generate_plots.py`: Produces comparative bar charts and line graphs for final project reports.
