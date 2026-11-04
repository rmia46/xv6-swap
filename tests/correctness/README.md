# Correctness Tests (`tests/correctness/`)

This directory will contain C test programs run inside xv6 to validate correctness:

1. `test_swap_basic.c`: Allocates pages exceeding physical memory limits, verifies data round-trip.
2. `test_dirty_writeback.c`: Modifies selected swapped pages, verifies non-modified pages skip disk write.
3. `test_fork_swap.c`: Verifies swapped pages behave correctly across `fork()` and `exit()`.
4. `test_oom_recovery.c`: Tests graceful process failure or disk swap exhaustion.
