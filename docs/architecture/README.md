# Architecture Documentation (`docs/architecture/`)

This directory contains technical architectural specifications, data structure designs, and interface definitions for the virtual memory and swapping subsystem.

## Topics to Document
1. **Core Memory Subsystem:** Page tables, physical frame tracking, inverted page tables / core map.
2. **Swap Mechanism:** Disk backing store, swap space allocation (block bitmap vs swap file), swap PTE encoding.
3. **Trap / Page Fault Workflow:** Fault path, demand paging sequence, dirty page writeback.
4. **Policy Engine Interface:** Clean separation of mechanism and replacement algorithms.
