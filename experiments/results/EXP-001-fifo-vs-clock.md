# Experiment Record: EXP-001 — Baseline 0 (FIFO) vs Baseline 1 (Clock)

- **Experiment ID:** EXP-001
- **Date:** 2026-11-07
- **Git Commits:** 
  - Baseline 0 (FIFO): `ec678dc`
  - Baseline 1 (Clock): `7482550`
- **Author(s):** Systems Engineering Team

---

## 1. Objective & Hypothesis
- **Policies Evaluated:** Baseline 0 (FIFO) vs Baseline 1 (Clock / Second-Chance)
- **Workload:** 32-Page Memory Stress (`testswap`: eager sbrk, linear write phase, linear read-verify phase, lazy allocation phase)
- **Hypothesis:** 
  1. For a purely linear, non-revisiting access pattern (`testswap_basic`), total page faults are constrained by the working set size ($N > M$).
  2. The Clock algorithm will actively inspect and clear Accessed (`PTE_A`) bits on referenced pages, resulting in higher scan inspection counts (`scan_inspections`) reflecting second-chance decisions.
  3. Correctness, TLB invalidation (`sfence_vma()`), and data integrity are preserved with zero corruption.

---

## 2. Experimental Configuration
- **Architecture:** RISC-V 64-bit Sv39 (QEMU `qemu-system-riscv64`, `-machine virt`)
- **Physical Memory Limit (`MAX_PHY_PAGES`):** 16 resident pages per process
- **Swap Space Allocation:** 8,000 disk blocks = 2,000 swap slots (4KB each)
- **Workload Parameters:** 32 pages allocated, distinct byte patterns written and verified

---

## 3. Empirical Measurements

### Comparative Metric Table

| Phase / Metric | Baseline 0 (FIFO) | Baseline 1 (Clock) | Delta / Analysis |
|---|---|---|---|
| **Eager Allocation Writes** | 16 | 16 | 0% (identical initial cold eviction) |
| **Write Phase Faults (`fault_swapin`)** | 32 | 32 | Identical on linear first-touch pass |
| **Write Phase Inspections** | 48 | 64 | **+33.3%** (Clock grants second chances) |
| **Verify Phase Faults (`fault_swapin`)** | 64 | 64 | Identical under streaming scan |
| **Verify Phase Inspections** | 80 | 128 | **+60.0%** (Clock sweeps referenced pages) |
| **Lazy Phase Inspections** | 64 | 128 | **+100%** (Active second chance evaluation) |
| **Total Disk Reads (`swap_reads`)** | 64 pages | 64 pages | 0% |
| **Total Disk Writes (`swap_writes`)** | 80 pages | 80 pages | 0% |
| **Data Integrity Verification** | **PASSED** | **PASSED** | 100% data match, zero corruption |

---

## 4. Observations & Discussion
1. **Clock Mechanism Functionality:** The significant increase in `scan_inspections` (from 80 under FIFO to 128 under Clock) demonstrates that the hardware `PTE_A` bit is being set by QEMU's MMU on memory accesses, and the kernel Clock hand is correctly reading, clearing the bit, issuing `sfence_vma()`, and giving accessed pages a second chance before falling back to eviction.
2. **Scan Pollution Confirmation:** Under a single sequential loop larger than physical memory ($32 > 16$), pure Clock degrades to FIFO behavior in terms of hit rate (as predicted by OS memory management theory). This confirms the need for our upcoming **locality workloads** (`wl_locality`, 80/20 Pareto distribution) and **Enhanced Clock / Adaptive Clock** to demonstrate hit-rate improvements over FIFO.

---

## 5. Conclusion & Baseline Status
- **Baseline 1 (Clock) is functional, verified, and integrated.**
- **Decision:** Establish Clock as **`BASELINE_1`**.
- **Next Step:** Implement **Enhanced Clock `(A, D)`** (Issue #3) and the **Standard Workload Suite** (Issue #5).
