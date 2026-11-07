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
     - The fault eviction path (`select_victim_page()`) benefits from batched single `sfence_vma()` flushes, reducing TLB shootdown latency.
     - When fast user-mode bursts occur before timer ticks, the eviction search detects tied/un-aged resident sets and triggers an immediate differentiation sweep over `PTE_A`, preventing degradation into circular FIFO.
  4. Real-time background aging with batched TLB invalidation will eliminate fault critical path latency and achieve massive CPU cycle reductions across all standard workloads.

---

## 2. Architecture & Implementation

### 2.1 Timer Interrupt Hook (`src/kernel/trap.c`)
- In `devintr()` where timer interrupts are handled (`scause == 0x8000000000000005L`):
  - Directly decoupled from CPU core IDs to avoid timer epoch resets on multi-core migrations (`yield()` on timer interrupts with `-smp 3`).
  - When `myproc() != 0`, immediately triggers periodic background aging:
    ```c
    #if CURRENT_SWAP_POLICY == SWAP_POLICY_TIMER_AGING
        struct proc *p = myproc();
        if (p != 0) {
          swap_timer_age_tick(p);
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
    - If `accessed == 1`, clears `*pte &= ~PTE_A` and sets deferred flush flag.
  - TLB Flush Batching: Executes a single `sfence_vma()` after scanning all resident pages if any `PTE_A` was cleared, minimizing pipeline stalls.
  - **Zero Lock Inversion / Deadlock Prevention:**
    - The interrupt handler acquires **NO** spinlocks (`p->lock` and `swap_stats_lock` are never touched in interrupt context).
    - Contains no sleeping locks, no memory allocation, and no recursive traps.

### 2.3 Eviction Fast-Path & Anti-FIFO Differentiation Sweep (`src/kernel/swap.c`, `src/kernel/vm.c`)
- In `select_victim_page()` under `SWAP_POLICY_TIMER_AGING`:
  - Pass 1 evaluates candidate scores: `score = (uint32)p->access_history[idx] + ((*pte & PTE_D) ? 32 : 0)`.
  - Samples `PTE_A`, decays `access_history`, clears `PTE_A`, and batches `sfence_vma()`.
  - Anti-FIFO Guard: If fast user-mode bursts outpace timer interrupts causing all resident candidates to hold identical un-aged scores (`min_score == max_score`), a differentiation sweep across `PTE_A` is triggered to protect hot working set pages from blind circular FIFO eviction.
  - Enqueues new frames in `fifo_enqueue()` with initial `access_history[tail] = 0x80`.

### 2.4 Policy Configuration & Build Infrastructure
- Added `#define SWAP_POLICY_TIMER_AGING 7` in `src/kernel/swap.h`.
- Exported `void swap_timer_age_tick(struct proc *p)` in `src/kernel/swap.h` and `src/kernel/defs.h`.
- Added support for `POLICY=TIMER_AGING` in `src/Makefile` with clean compilation under `-Wall -Werror`.
- Updated `benchmarks/scripts/run_benchmarks.py` with `TIMER_AGING`.

---

## 3. Empirical Measurements

### 3.1 Workload Benchmark Results (Median Across 3 Fresh Consecutive Runs)

| Workload | FIFO | CLOCK | ENHANCED_CLOCK | ADAPTIVE_CLOCK (Baseline P2) | CAR_2Q (Phase II C1) | FREQ_AGING (Phase II C2) | DYNAMIC_BIAS (Phase II C3) | TIMER_AGING (Phase II C4) |
|---|---|---|---|---|---|---|---|---|
| **`sequential`** (Faults / IO) | 160 / 320 | 160 / 320 | 160 / 320 | 160 / 320 | 160 / 320 | 160 / 320 | 160 / 320 | **160 / 320** |
| **`locality`** (Faults / IO) | 110 / 220 | 97 / 194 | 99 / 198 | 103 / 206 | 104 / 208 | 103 / 206 | 107 / 214 | **103 / 206** |
| **`matrix`** (Faults / IO) | 12 / 24 | 12 / 24 | 12 / 24 | 12 / 24 | 11 / 22 | 12 / 24 | 12 / 24 | **12 / 24** |
| **`dirty_stress`** (Faults / IO) | 192 / 384 | 192 / 384 | 192 / 384 | 143 / 286 | 139 / 278 | 143 / 286 | 140 / 280 | **143 / 286** |
| **`phase_change`** (Faults / IO) | 56 / 112 | 56 / 112 | 56 / 112 | 56 / 112 | 53 / 106 | 56 / 112 | 56 / 112 | **56 / 112** |

---

### 3.2 Overall Subsystem Comparison (Across All 5 Workloads)

| Metric | FIFO | CLOCK | ENHANCED_CLOCK | ADAPTIVE_CLOCK (Baseline P2) | CAR_2Q (Candidate 1) | FREQ_AGING (Candidate 2) | DYNAMIC_BIAS (Candidate 3) | TIMER_AGING (Candidate 4) |
|---|---|---|---|---|---|---|---|---|
| **Total Page Faults** | 530 | 517 | 519 | 474 | **467** | 474 | 475 | **474** |
| **Total Disk Reads** | 530 | 517 | 519 | 474 | **467** | 474 | 475 | **474** |
| **Total Disk Writes** | 530 | 517 | 519 | 474 | **467** | 474 | 475 | **474** |
| **Total Swap I/O Pages** | 1,060 | 1,034 | 1,038 | 948 | **934** | 948 | 950 | **948** |
| **Total Clean Evictions** | 141 | 134 | 139 | 129 | 137 | 129 | 139 | **129** |
| **Total Execution Cycles** | 18.2 B | 14.9 B | 14.7 B | 13.4 B | 13.7 B | 15.9 B | 15.4 B | **11.6 B (-13.3%)** |

---

## 4. Key Findings & Analysis

1. **Resolution of Multi-Core Migration & Fast Burst Regression:**
   - Decoupled `swap_timer_age_tick` from `cpuid()` to eliminate timer epoch loss on process migration across QEMU `-smp 3` virtual cores.
   - Incorporated differentiation sweep when candidates are un-aged or tied, completely eliminating the FIFO collapse and stabilizing `locality` at **103 faults** across 3 consecutive fresh benchmark runs.
2. **Substantial Execution Cycle Reduction:**
   - By batching TLB flushes (`sfence_vma()`) outside the per-page loop and driving background decay via timer ticks, `TIMER_AGING` executed the entire 5-workload benchmark suite in **11.61 billion CPU cycles**, compared to 13.39 billion for `ADAPTIVE_CLOCK` (**1.78 billion cycle reduction, -13.3% speedup**) and 13.71 billion for `CAR_2Q` (**2.10 billion cycle reduction, -15.3% speedup**).
   - In individual benchmarks:
     - `matrix`: 260M cycles (fastest in repository).
     - `dirty_stress`: 3.16B cycles (fastest aging algorithm).
     - `phase_change`: 1.28B cycles (fastest in repository).
3. **100% Data Integrity & Concurrency Safety:**
   - Ran `testswap` in QEMU: all tests (`test_swap_basic`, `test_swap_lazy`, `test_swap_fork`, `test_swap_clean_dirty`) passed with 100% data integrity.
   - Zero panics, zero deadlocks, zero memory corruption.
