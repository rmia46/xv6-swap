# Testing Strategy & Test Plans (`docs/testing/`)

This directory documents test plans, correctness test matrix, and verification procedures.

## Verification Tiers
1. **Tier 1: Basic Sanity & Booting**
   - Kernel boots cleanly in QEMU.
   - Userland shell executes standard xv6 commands (`ls`, `echo`, `cat`).
2. **Tier 2: Swapping Mechanism Correctness**
   - Physical memory exhaustion causes evictions without panic.
   - Evicted pages can be read back with 100% data integrity.
   - Dirty pages are written to swap before eviction; clean pages are evicted without redundant writes.
3. **Tier 3: Policy Correctness & Edge Cases**
   - FIFO strictly follows arrival order.
   - Clock gives second chance to accessed pages.
   - Enhanced Clock prioritizes clean pages before dirty pages.
   - Multi-process memory pressure does not leak frames or swap blocks.
