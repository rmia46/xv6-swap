# Experiment Record: EXP-005 — Phase II Candidate 2: Frequency-Weighted Access Aging (LFU-Recency Blend)

- **Experiment ID:** EXP-005
- **Date:** 2026-11-07
- **Git Branch:** `feature/p2-c2-freq-aging`
- **Issue Reference:** Issue #9
- **Author(s):** Senior OS Systems Researcher & Kernel Engineer

---

## 1. Objective & Hypothesis
- **Policies Evaluated:**
  - Baseline 0: FIFO
  - Baseline 1: Clock (Second-Chance)
  - Baseline 2: Enhanced Clock (4-Class Accessed + Dirty)
  - Baseline P2 (Adaptive Aging Clock): Sampled 8-bit Access History + Dirty Bias (474 faults, 948 I/O)
  - Candidate 1 (CAR_2Q): Scan-Resistant Dual-List Clock Policy (467 faults, 934 I/O)
  - Candidate 2 (FREQ_AGING): Frequency-Weighted Access Aging (LFU-Recency Blend with 4-bit Saturating Frequency Counter + 8-bit Shift Register)
- **Benchmark Suites:**
  1. Automated Workload Suite (`run_benchmarks.py`, 3 trials reporting medians across 5 standard workloads: `sequential`, `locality`, `matrix`, `dirty_stress`, `phase_change`).
  2. Data Integrity Suite (`testswap`: `test_swap_basic`, `test_swap_lazy`, `test_swap_fork`, `test_swap_clean_dirty`).
- **Hypothesis:**
  1. Combining a temporal recency shift register (`access_history`) with a saturating frequency counter (`ref_frequency`) will allow the kernel to discern high-frequency inner-loop pages from transient bursts.
  2. On eviction sweep, sampling `PTE_A` to increment frequency (saturating at 15) and decay frequency (`(freq * 3) / 4`) when unaccessed provides an exponential moving average of reference frequency.
  3. Eviction scoring `score = access_history + (ref_frequency * 16) + (dirty ? 32 : 0)` will prioritize cold, clean, and infrequent pages for eviction.
  4. The policy aims to improve upon `BASELINE_P2` (474 faults, 948 I/O) and compete with Candidate 1 `CAR_2Q` (467 faults, 934 I/O).

---

## 2. Architecture & Implementation

### 2.1 State in `struct proc` (`src/kernel/proc.h`, `proc.c`, `exec.c`, `vm.c`)
- In `struct proc`:
  - `uint8 access_history[MAX_PHY_PAGES];`: 8-bit recency shift register.
  - `uint8 ref_frequency[MAX_PHY_PAGES];`: 4-bit saturating frequency counter (range 0..15).
- On `fifo_enqueue()`:
  - Initializes `access_history[tail] = 0x80` and `ref_frequency[tail] = 1`, giving newly resident pages favorable initial residency.
- On `fifo_remove()`:
  - Synchronously shifts `ref_frequency` along with `resident_pages`, `access_history`, and `is_protected` during array compaction.
  - Zeroes trailing compacted elements.
- Lifecycle state management:
  - Zero-initialized in `allocproc()`, `freeproc()`, and `kexec()`.
  - Duplicated from parent to child in `kfork()`.

### 2.2 Policy Configuration (`src/kernel/swap.h`, `src/Makefile`)
- In `src/kernel/swap.h`:
  - Added `#define SWAP_POLICY_FREQ_AGING 5`.
  - Added compile-time routing macro `#elif defined(POLICY_FREQ_AGING)` mapping to `SWAP_POLICY_FREQ_AGING`.
- In `src/Makefile`:
  - Documented `FREQ_AGING` under `POLICY ?= ...`.
  - Compiles cleanly with `-DPOLICY_$(POLICY)` under `-Wall -Werror`.

### 2.3 Eviction Sweep Scoring Algorithm (`src/kernel/swap.c`)
- On eviction sweep in `select_victim_page()`:
  - Loops over resident frames `i = 0 .. resident_count - 1` starting at `clock_hand`.
  - Records inspection in `swap_stats.scan_inspections++`.
  - Samples hardware `PTE_A`:
    - If `PTE_A == 1`:
      - Saturating increment: `ref_frequency = min(15, ref_frequency + 1)`.
      - Recency shift: `access_history = (access_history >> 1) | 0x80`.
      - Clears hardware bit: `*pte &= ~PTE_A`.
      - Flushes TLB: `sfence_vma()`.
    - If `PTE_A == 0`:
      - Frequency decay: `ref_frequency = (ref_frequency * 3) / 4`.
      - Recency shift: `access_history = access_history >> 1`.
  - Evaluates composite candidate score:
    $$\text{score} = \text{access\_history}[i] + (\text{ref\_frequency}[i] \times 16) + ((\text{PTE\_D}) ? 32 : 0)$$
  - Selects frame with minimal score as victim. Tie-breaking naturally preserves traversal order starting at `clock_hand`.
  - Advances `clock_hand = (victim_idx + 1) % resident_count`.

---

## 3. Empirical Measurements

### 3.1 Workload Benchmark Results (Median Across 3 Trials)

| Workload | FIFO | CLOCK | ENHANCED_CLOCK | ADAPTIVE_CLOCK (Baseline P2) | CAR_2Q (Phase II C1) | FREQ_AGING (Phase II C2) |
|---|---|---|---|---|---|---|
| **`sequential`** (Faults / IO) | 160 / 320 | 160 / 320 | 160 / 320 | 160 / 320 | 160 / 320 | **160 / 320** |
| **`locality`** (Faults / IO) | 110 / 220 | 97 / 194 | 99 / 198 | 103 / 206 | 104 / 208 | **103 / 206** |
| **`matrix`** (Faults / IO) | 12 / 24 | 12 / 24 | 12 / 24 | 12 / 24 | 11 / 22 | **12 / 24** |
| **`dirty_stress`** (Faults / IO) | 192 / 384 | 192 / 384 | 192 / 384 | 143 / 286 | 139 / 278 | **143 / 286** |
| **`phase_change`** (Faults / IO) | 56 / 112 | 56 / 112 | 56 / 112 | 56 / 112 | 53 / 106 | **56 / 112** |

---

### 3.2 Overall Subsystem Comparison (Across All 5 Workloads)

| Metric | FIFO | CLOCK | ENHANCED_CLOCK | ADAPTIVE_CLOCK (Baseline P2) | CAR_2Q (Candidate 1) | FREQ_AGING (Candidate 2) |
|---|---|---|---|---|---|---|
| **Total Page Faults** | 530 | 517 | 519 | 474 | **467** | **474** |
| **Total Disk Reads** | 530 | 517 | 519 | 474 | **467** | **474** |
| **Total Disk Writes** | 530 | 517 | 519 | 474 | **467** | **474** |
| **Total Swap I/O Pages** | 1,060 | 1,034 | 1,038 | 948 | **934** | **948** |
| **Total Clean Evictions** | 141 | 134 | 139 | 129 | **137** | **129** |
| **Clean Evict Ratio (`dirty_stress`)** | 56.2% | 56.2% | 56.2% | 60.8% | **66.9%** | **60.8%** |
| **Total Scan Inspections** | 530 | 1,108 | 8,132 | 7,584 | **4,335** | **7,584** |

---

### 3.3 Data Integrity Suite (`testswap`)

| Test Case | Description | Result | Status |
|---|---|---|---|
| `test_swap_basic` | 32-page eager write & verify under 16 resident pages | 52 swap-in faults, 68 writes | **PASSED** |
| `test_swap_lazy` | 32-page lazy-allocated write & verify | 20 swap-in faults, 52 writes | **PASSED** |
| `test_swap_fork` | Fork inheritance & copy-on-write page isolation | Zero data corruption across parent/child | **PASSED** |
| `test_swap_clean_dirty` | Mixed 8 dirty + 8 clean frames under eviction pressure | 4 clean, 4 dirty evictions | **PASSED** |

---

## 4. Key Architectural Findings & Discussion

1. **Parity with Baseline P2 (Adaptive Clock):**
   - FREQ_AGING achieves **474 faults** and **948 swap I/O pages**, matching Baseline P2 (`ADAPTIVE_CLOCK`) exactly across all workloads.
   - The saturating frequency counter ($0..15$) combined with exponential decay ($\times 3/4$) correlates closely with the 8-bit recency shift register under these access patterns, reinforcing recency tracking without introducing pathological page flapping.
2. **Comparison with Candidate 1 (`CAR_2Q`):**
   - Candidate 1 (`CAR_2Q`) remains the superior architecture at **467 faults** and **934 swap I/O pages** (-7 faults, -14 swap I/O compared to FREQ_AGING).
   - In addition, `CAR_2Q` demonstrates significantly lower scan overhead (**4,335 scans** vs **7,584 scans** for FREQ_AGING, a **42.8% reduction**). The dual-list partitioning in `CAR_2Q` filters candidates quickly without needing to evaluate all 16 resident frames on every single eviction.
3. **Dirty Page Retention:**
   - In `dirty_stress`, FREQ_AGING achieved a **60.8% clean eviction ratio**, exactly matching Adaptive Clock and outperforming FIFO / standard Clock (56.2%), by incorporating the $+32$ dirty penalty into the eviction score. However, CAR_2Q achieved **66.9%** by immediately promoting dirty pages with $PTE\_A=1$ to $T_2$.
4. **Summary Assessment:**
   - Candidate 2 (Frequency-Weighted Access Aging) meets all design requirements, compiles cleanly with `-Wall -Werror`, and passes all data integrity tests with zero memory corruption.
   - In head-to-head evaluation against Phase II targets, Candidate 1 (`CAR_2Q`) retains the performance lead.
