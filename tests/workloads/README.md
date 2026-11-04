# Workload Generators (`tests/workloads/`)

This directory will hold userland benchmark workloads targeting memory access characteristics:

- `workload_sequential.c`: Linear scan over large arrays.
- `workload_locality.c`: 80/20 Zipfian/Pareto distributed memory references.
- `workload_matrix.c`: Matrix multiplication causing stride-based page churn.
- `workload_thrash.c`: Multi-process workload designed to induce heavy page eviction and disk I/O.
