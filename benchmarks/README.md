# Benchmarks (`benchmarks/`)

Automation harness and data pipelines for evaluating policy performance.

## Structure
- `scripts/`: Driver scripts (Python/Bash) executing QEMU headless runs and extracting telemetry.
- `raw/`: Unprocessed console outputs, execution logs, and cycle counters.
- `processed/`: Formatted markdown tables, CSV files, and charts.

## Primary Metrics
1. **Page Fault Count:** Total minor and major faults per workload.
2. **Disk I/O Operations:** Read vs write operations to swap storage.
3. **Execution Latency:** Total CPU cycles or elapsed wall-clock ticks to completion.
4. **Eviction Cost:** Overhead of replacement policy decision logic (e.g., number of clock hand checks).
