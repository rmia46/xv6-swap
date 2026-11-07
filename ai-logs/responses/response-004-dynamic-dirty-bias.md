# AI Response & Evaluation: RESPONSE-004

- **Prompt ID:** PROMPT-004
- **Date:** 2026-11-07
- **Target Policy:** Phase II Candidate 3 — Dynamic Fault-Rate Sensitive Dirty Writeback Bias (Issue #10)
- **Model:** Gemini
- **Status:** Implemented, Evaluated, and Verified (Candidate 1 CAR_2Q retains overall Phase II leadership)

---

## 1. Implementation Summary
In accordance with `ai-logs/prompts/prompt-004-dynamic-dirty-bias.md` and Issue #10:

1. **Sliding Window Fault-Rate Tracker (`src/kernel/swap.c`, `src/kernel/vm.c`):**
   - Implemented a time-windowed swap fault tracker in `src/kernel/swap.c` measuring fault velocity across `FAULT_WINDOW_TICKS = 4` timer ticks.
   - Synchronized window updates under `swap_stats_lock` while sampling `ticks` independently under `tickslock` to prevent lock ordering inversion deadlocks.
   - Added `void swap_record_fault(void)` invoked from `vmfault()` (`src/kernel/vm.c`) on all page fault events to advance the window and accumulate slot metrics.
   - Added `fault_tracker_reset()` integrated into `swap_init()` and `swap_stats_reset()` for deterministic benchmarking.

2. **Policy Definition & Build Infrastructure (`src/kernel/swap.h`, `src/kernel/defs.h`, `src/Makefile`):**
   - Added `#define SWAP_POLICY_DYNAMIC_BIAS 6` in `src/kernel/swap.h`.
   - Added macro dispatch for `POLICY_DYNAMIC_BIAS` to `CURRENT_SWAP_POLICY = SWAP_POLICY_DYNAMIC_BIAS`.
   - Documented `DYNAMIC_BIAS` in `src/Makefile` under `POLICY ?= ...`.
   - Clean compilation verified under `-Wall -Werror`.

3. **Scoring & Eviction Sweep (`src/kernel/swap.c`):**
   - Implemented dynamic dirty bias computation:
     $$\text{dynamic\_bias} = \text{clamp}\left(\text{BASE\_BIAS} + \frac{\text{total\_window\_faults} \times \text{RATE\_FACTOR}}{\text{FAULT\_WINDOW\_TICKS}}, \text{MIN\_BIAS}, \text{MAX\_BIAS}\right)$$
     with parameters `BASE_BIAS = 24`, `RATE_FACTOR = 4`, `MIN_BIAS = 16`, `MAX_BIAS = 64`.
   - Integrated with 8-bit access history aging:
     `score = (uint32)p->access_history[idx] + ((*pte & PTE_D) ? dynamic_bias : 0);`
   - Initialized new resident pages with `access_history[tail] = 0x80` in `fifo_enqueue()`.

---

## 2. Experimental Validation & Results

- **Comparative Results Against Phase II Baselines & Candidates:**
  - **Total Faults Across Workload Suite:**
    - Baseline P2 (Adaptive Clock): 474 faults
    - Candidate 2 (FREQ_AGING): 474 faults
    - Candidate 3 (DYNAMIC_BIAS): **475 faults** (Parity with Baseline P2 / FREQ_AGING)
    - Candidate 1 (CAR_2Q): **467 faults** (-8 faults lead retained by CAR_2Q)
  - **Total Swap I/O Pages:**
    - Baseline P2: 948 I/O pages
    - Candidate 2 (FREQ_AGING): 948 I/O pages
    - Candidate 3 (DYNAMIC_BIAS): **950 I/O pages**
    - Candidate 1 (CAR_2Q): **934 I/O pages** (-16 I/O lead retained by CAR_2Q)
  - **Suppression of Dirty Evictions in `dirty_stress`:**
    - Baseline P2 (Adaptive Clock): 143 faults, 87 clean evicts, 56 dirty evicts (60.8% clean ratio)
    - Candidate 2 (FREQ_AGING): 143 faults, 87 clean evicts, 56 dirty evicts (60.8% clean ratio)
    - Candidate 3 (DYNAMIC_BIAS): **140 faults**, **90 clean evicts**, **50 dirty evicts** (**64.3% clean ratio**)
    - DYNAMIC_BIAS successfully suppressed dirty evictions (-6 dirty writes) and reduced faults (-3 faults) in `dirty_stress` by escalating writeback penalty during congestion.
  - **Total Clean Evictions Across Suite:**
    - Candidate 3 (DYNAMIC_BIAS): **139 clean evictions** (surpassing Baseline P2's 129 and CAR_2Q's 137).

- **Data Integrity Suite (`testswap`):**
  - All tests (`test_swap_basic`, `test_swap_lazy`, `test_swap_fork`, `test_swap_clean_dirty`) passed with 100% data integrity and zero memory corruption in QEMU.
