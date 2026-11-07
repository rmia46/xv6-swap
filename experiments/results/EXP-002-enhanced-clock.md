# Experiment Record: EXP-002 — Baseline 2 Enhanced Clock (A, D) vs Clock and FIFO

- **Experiment ID:** EXP-002
- **Date:** 2026-11-07
- **Git Branch:** `feature/baseline-2-enhanced-clock`
- **Author(s):** Senior Kernel Systems Engineer

---

## 1. Objective & Hypothesis
- **Policies Evaluated:** Baseline 0 (FIFO) vs Baseline 1 (Clock / Second-Chance) vs Baseline 2 (Enhanced Clock / 4-Class Accessed + Dirty)
- **Workloads:** 
  1. `testswap_basic` & `testswap_lazy`: Sequential 32-page streaming write/verify patterns (`MAX_PHY_PAGES = 16`).
  2. `testswap_clean_dirty`: Targeted evaluation with mixed resident working set of 8 dirty pages (`A=1, D=1`, Class 3) and 8 clean pages (`A=1, D=0`, Class 2), followed by 8 new allocations forcing 8 page evictions.
- **Hypothesis:**
  1. Under mixed clean and dirty resident memory pressure, Enhanced Clock will categorize pages into 4 classes and prioritize evicting clean pages (`D=0`) over dirty pages (`D=1`), achieving 100% clean evictions (8 clean, 0 dirty) during the mixed workload.
  2. FIFO and standard Clock (which are dirty-agnostic) will evict based purely on FIFO enqueue order or A-bit alone, failing to prioritize clean pages over dirty pages. In our workload, FIFO and standard Clock evict 8 dirty pages and 0 clean pages.
  3. Scan inspections in Enhanced Clock reflect the multi-pass 4-step search algorithm.

---

## 2. Experimental Configuration
- **Architecture:** RISC-V 64-bit Sv39 (QEMU `qemu-system-riscv64`, `-machine virt`)
- **Physical Memory Limit (`MAX_PHY_PAGES`):** 16 resident pages per process
- **Swap Space Allocation:** 8,000 disk blocks = 2,000 swap slots (4KB each)

---

## 3. Empirical Measurements

### Comparative Policy Evaluation

| Workload / Phase Metric | Baseline 0 (FIFO) | Baseline 1 (Clock) | Baseline 2 (Enhanced Clock) | Delta / Analysis |
|---|---|---|---|---|
| **Clean vs Dirty Test: Clean Evictions** | 0 | 0 | **8** | **+100% clean victim selection** |
| **Clean vs Dirty Test: Dirty Evictions** | 8 | 8 | **0** | **-100% dirty evictions avoided** |
| **Clean vs Dirty Test: Extra Disk Writes** | 8 pages | 8 pages | **0 pages** | **Zero disk writes needed!** |
| **Clean vs Dirty Test: Scan Inspections** | 24 | 56 | 96 | Reflects 4-step (A, D) sweeps |
| **Linear Basic Test: Swap-in Faults** | 64 | 64 | 64 | Identical under sequential scan |
| **Linear Basic Test: Scan Inspections** | 80 | 128 | 688 | Multi-pass sweep categorization |
| **Data Integrity Verification** | **PASSED** | **PASSED** | **PASSED** | Zero corruption across all tests |

---

## 4. Key Takeaways
1. **Dirty-Aware Victim Selection:** Enhanced Clock successfully avoids costly disk write-backs by prioritizing clean pages (Class 0: `(0,0)` and Class 2 demoted to `(0,0)`), saving disk I/O bandwidth.
2. **Modular Architecture:** Policy selection remains completely decoupled in `src/kernel/swap.c` and configured through compile-time flags (`POLICY ?= ENHANCED_CLOCK`).
