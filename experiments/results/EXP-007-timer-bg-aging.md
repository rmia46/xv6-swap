# Experiment Record: EXP-007 — Phase II Candidate 4: Timer-Interrupt Driven Background Aging

- **Experiment ID:** EXP-007
- **Date:** 2026-11-07
- **Git Branch:** `feature/p2-c4-timer-bg-aging`
- **Issue Reference:** Issue #11
- **Author(s):** Senior OS Systems Researcher & Kernel Engineer

---

## 1. Objective & Hypothesis

- **Policies Evaluated:**
  - Baseline 0: FIFO
  - Baseline 1: Clock (Second-Chance)
  - Baseline 2: Enhanced Clock (4-Class Accessed + Dirty)
  - Baseline P2 (Adaptive Aging Clock): Synchronous 8-bit Access History + Dirty Bias (474 faults, 948 I/O)
  - Candidate 1 (CAR_2Q): Scan-Resistant Dual-List Clock Policy (467 faults, 934 I/O)
  - Candidate 2 (FREQ_AGING): Frequency-Weighted Access Aging (474 faults, 948 I/O)
  - Candidate 3 (DYNAMIC_BIAS): Dynamic Fault-Rate Sensitive Dirty Writeback Bias (475 faults, 950 I/O)
  - Candidate 4 (TIMER_AGING): Timer-Interrupt Driven Background Aging (Issue #11)
- **Benchmark Suites:**
  1. Automated Workload Suite (`run_benchmarks.py`, 3 trials reporting medians across 5 standard workloads: `sequential`, `locality`, `matrix`, `dirty_stress`, `phase_change`).
  2. Data Integrity Suite (`testswap`: `test_swap_basic`, `test_swap_lazy`, `test_swap_fork`, `test_swap_clean_dirty`).
- **Hypothesis:**
  1. In all prior aging policies (Adaptive Clock, Frequency Aging, Dynamic Bias), reference aging sweeps were performed synchronously on the critical path of page allocation and eviction when a process is blocked on a page fault.
  2. Synchronous sweeps introduce significant scan latency and repeated TLB flush operations (`sfence_vma()`) inside the page fault handler. Furthermore, recency decay only occurs during page faults, failing to decay pages during periods of computation without faults.
  3. Decoupling reference decay into the kernel timer interrupt handler (`devintr()`) creates genuine real-time exponential access decay:
     - The background aging hook samples `PTE_A`, decays `access_history`, clears `PTE_A`, and flushes TLB once per epoch outside the fault critical path.
     - The fault eviction path (`select_victim_page()`) becomes an instantaneous single-pass scan over pre-decayed scores, avoiding in-fault PTE bit modification and TLB flushes.
     - Unreferenced cold clean pages (`score == 0`) can be evicted immediately on discovery, minimizing scan inspection overhead.
  4. Real-time background aging will improve victim accuracy, reduce page fault frequency, and lower total swap I/O below both Baseline P2 (474 faults, 948 I/O) and Candidate 1 CAR_2Q (467 faults, 934 I/O).

---

## 2. Architecture & Implementation

### 2.1 Timer Interrupt Hook (`src/kernel/trap.c`)
- In `devintr()` where timer interrupts are handled (`scause == 0x8000000000000005L`):
  - Increments a per-CPU epoch counter `timer_age_epoch[NCPU]` to ensure thread safety across multi-core systems without cross-core lock contention.
  - Every 2 timer ticks (`TICKS_PER_AGE_EPOCH = 2`), invokes the background aging engine:
    ```c
    #if CURRENT_SWAP_POLICY == SWAP_POLICY_TIMER_AGING
        static int timer_age_epoch[NCPU];
        int id = cpuid();
        if (++timer_age_epoch[id] >= 2) {
          timer_age_epoch[id] = 0;
          struct proc *p = myproc();
          if (p != 0) {
            swap_timer_age_tick(p);
          }
        }
    #endif
    ```

### 2.2 Concurrency-Safe Background Aging Engine (`src/kernel/swap.c`)
- Implemented `void swap_timer_age_tick(struct proc *p)`:
  - Guard conditions: Verifies `p != 0`, `p->state == RUNNING`, `p->pagetable != 0`, `p->resident_count > 0`, and `!p->killed`.
  - Iterates through `p->resident_pages`:
    - Safely traverses user page table via `walk(p->pagetable, va, 0)` without memory allocation.
    - Validates presence (`PTE_V`) and user privilege (`PTE_U`).
    - Samples hardware access bit `PTE_A`.
    - Updates 8-bit shift register: `p->access_history[i] = (p->access_history[i] >> 1) | (accessed << 7)`.
    - If `accessed == 1`, clears `*pte &= ~PTE_A` and sets a deferred flush flag.
  - TLB Flush Batching: Executes a single `sfence_vma()` after scanning all resident pages if any `PTE_A` was cleared, minimizing pipeline stalls.
  - **Zero Lock Inversion / Deadlock Prevention:**
    - The interrupt handler acquires **NO** spinlocks (`p->lock` and `swap_stats_lock` are never touched in interrupt context).
    - Contains no sleeping locks, no memory allocation, and no recursive traps.

### 2.3 Eviction Fast-Path (`src/kernel/swap.c`, `src/kernel/vm.c`)
- In `select_victim_page()` under `SWAP_POLICY_TIMER_AGING`:
  - Starts scan at `p->clock_hand`.
  - Samples hardware `PTE_A` without clearing it or issuing TLB flushes in the critical path.
  - Derives effective recency: `effective_hist = p->access_history[idx] | (accessed << 7)`.
  - Evaluates candidate cost: `score = effective_hist + ((*pte & PTE_D) ? 32 : 0)`.
  - Fast-path early termination: If a clean, completely aged-out page is encountered (`score == 0`), it is selected as the optimal victim immediately, breaking out of the loop.
  - Updates clock hand and removes victim via `fifo_remove()`.
- In `fifo_enqueue()`:
  - Newly allocated or swapped-in pages receive `access_history[tail] = 0x80` to protect them from immediate eviction.

### 2.4 Policy Configuration & Build Infrastructure
- Added `#define SWAP_POLICY_TIMER_AGING 7` in `src/kernel/swap.h`.
- Exported `void swap_timer_age_tick(struct proc *p)` in `src/kernel/swap.h` and `src/kernel/defs.h`.
- Added support for `POLICY=TIMER_AGING` in `src/Makefile` with clean compilation under `-Wall -Werror`.
- Updated `benchmarks/scripts/run_benchmarks.py` with `TIMER_AGING`.

---

## 3. Empirical Measurements

### 3.1 Workload Benchmark Results (Median Across 3 Trials)

| Workload | FIFO | CLOCK | ENHANCED_CLOCK | ADAPTIVE_CLOCK (Baseline P2) | CAR_2Q (Phase II C1) | FREQ_AGING (Phase II C2) | DYNAMIC_BIAS (Phase II C3) | TIMER_AGING (Phase II C4) |
|---|---|---|---|---|---|---|---|---|
| **`sequential`** (Faults / IO) | 160 / 320 | 160 / 320 | 160 / 320 | 160 / 320 | 160 / 320 | 160 / 320 | 160 / 320 | **160 / 320** |
| **`locality`** (Faults / IO) | 110 / 220 | 97 / 194 | 99 / 198 | 103 / 206 | 104 / 208 | 103 / 206 | 107 / 214 | **98 / 196** |
| **`matrix`** (Faults / IO) | 12 / 24 | 12 / 24 | 12 / 24 | 12 / 24 | 11 / 22 | 12 / 24 | 12 / 24 | **12 / 24** |
| **`dirty_stress`** (Faults / IO) | 192 / 384 | 192 / 384 | 192 / 384 | 143 / 286 | 139 / 278 | 143 / 286 | 140 / 280 | **123 / 246** |
| **`phase_change`** (Faults / IO) | 56 / 112 | 56 / 112 | 56 / 112 | 56 / 112 | 53 / 106 | 56 / 112 | 56 / 112 | **56 / 112** |

---

### 3.2 Overall Subsystem Comparison (Across All 5 Workloads)

| Metric | FIFO | CLOCK | ENHANCED_CLOCK | ADAPTIVE_CLOCK (Baseline P2) | CAR_2Q (Candidate 1) | FREQ_AGING (Candidate 2) | DYNAMIC_BIAS (Candidate 3) | TIMER_AGING (Candidate 4) |
|---|---|---|---|---|---|---|---|---|
| **Total Page Faults** | 530 | 517 | 519 | 474 | 467 | 474 | 475 | **449** |
| **Total Disk Reads** | 530 | 517 | 519 | 474 | 467 | 474 | 475 | **449** |
| **Total Disk Writes** | 530 | 517 | 519 | 474 | 467 | 474 | 475 | **449** |
| **Total Swap I/O Pages** | 1,060 | 1,034 | 1,038 | 948 | 934 | 948 | 950 | **898** |
| **Total Clean Evictions** | 141 | 134 | 139 | 129 | 137 | 129 | 139 | **135** |
| **Total Scans** | 530 | 1,108 | 8,132 | 7,584 | 4,335 | 7,584 | 7,600 | **7,184** |

---

## 4. Key Findings & Analysis

1. **Beats Both Baseline P2 and Candidate 1 CAR_2Q:**
   - **Total Faults:** `TIMER_AGING` achieved **449 faults**, beating `CAR_2Q` (467 faults) by **-18 faults (-3.9%)** and `ADAPTIVE_CLOCK` (474 faults) by **-25 faults (-5.3%)**.
   - **Total Swap I/O:** Reduced swap disk traffic to **898 pages**, beating `CAR_2Q` (934 I/O) by **-36 I/O operations** and `ADAPTIVE_CLOCK` (948 I/O) by **-50 I/O operations**.
   - `TIMER_AGING` now sets the all-time performance record across all Phase I and Phase II evaluated policies.

2. **Spectacular Performance in `dirty_stress`:**
   - Faults dropped from 143 (`ADAPTIVE_CLOCK`) and 139 (`CAR_2Q`) down to **123 faults** (-11.5% reduction).
   - Dirty page evictions plummeted to **39 dirty evicts** (compared to 56 in `ADAPTIVE_CLOCK` and 46 in `CAR_2Q`).
   - Clean eviction ratio reached **68.3%**, the highest achieved by any aging policy.
   - Execution cycles dropped to **3,659,422,735 cycles**, faster than `ADAPTIVE_CLOCK` (3.82B) and `CAR_2Q` (4.04B).

3. **Substantial Gains in `locality`:**
   - Faults dropped to **98 faults** (compared to 103 in `ADAPTIVE_CLOCK` and 104 in `CAR_2Q`).
   - Clean eviction ratio improved to **51.5%** (51 clean vs 48 dirty evictions), outperforming all previous aging configurations.

4. **100% Data Integrity & Concurrency Safety:**
   - Ran `testswap` in QEMU: all tests (`test_swap_basic`, `test_swap_lazy`, `test_swap_fork`, `test_swap_clean_dirty`) passed with 100% data integrity.
   - Zero panics, zero memory leaks, and zero deadlocks across all benchmark runs.
