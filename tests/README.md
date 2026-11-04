# Test Suites (`tests/`)

Automated and interactive test programs used to verify swap mechanism correctness and evaluate replacement policy behavior.

## Directory Structure
- `correctness/`: Programs verifying memory integrity, fault recovery, and termination under memory exhaustion.
- `workloads/`: Synthetic access patterns designed to trigger specific page replacement behaviors:
  - **Sequential Access:** Stresses simple LRU/Clock loops.
  - **Looping Reference (Scan):** Demonstrates thrashing when working set exceeds RAM.
  - **Temporal & Spatial Locality:** Clustered references to verify working set retention.
  - **Random Access:** Uniform access across memory space.
