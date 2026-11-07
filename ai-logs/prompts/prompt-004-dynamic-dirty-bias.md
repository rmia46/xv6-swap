# Prompt ID: PROMPT-004
- **Date:** 2026-11-07
- **Target Subsystem / Policy:** Phase II Candidate 3 — Dynamic Fault-Rate Sensitive Dirty Writeback Bias (Issue #10)
- **Model Used:** Gemini
- **Purpose:** Design an adaptive dirty writeback penalty mechanism that dynamically scales penalty based on instantaneous swap fault velocity $\Delta F / \Delta t$ to suppress write amplification when disk bandwidth is congested.

## Exact Prompt:
> Design a Dynamic Fault-Rate Sensitive Dirty Writeback Bias policy for xv6-RISC-V demand paging with `MAX_PHY_PAGES = 16`.
> Motivation:
> Static dirty bias constants (like +32 in Adaptive Clock) do not adapt when memory is under severe write-congestion versus light demand.
> Requirements:
> 1. In `src/kernel/swap.c`, track swap velocity:
>    - Windowed counter measuring swap faults over the last $K$ evictions or timer ticks: `recent_fault_rate`.
>    - Dynamically compute dirty bias:
>      `uint32 dynamic_dirty_bias = BASE_BIAS + (recent_fault_rate * RATE_FACTOR);` (clamped between 16 and 128).
> 2. Combined with Adaptive Aging:
>    - Candidate score: `score = (uint32)access_history[i] + ((*pte & PTE_D) ? dynamic_dirty_bias : 0);`
>    - Under heavy write churn, heavily penalizes dirty frames, forcing clean frame eviction; under light traffic, relaxes penalty to favor true LRU recency.
> 3. Add `POLICY ?= DYNAMIC_BIAS` in `src/Makefile` and `#define SWAP_POLICY_DYNAMIC_BIAS 6` in `src/kernel/swap.h`.
> 4. Clean compilation with `-Wall -Werror`, test QEMU integrity via `testswap`, and benchmark against `CAR_2Q` (467 faults, 934 I/O) and `ADAPTIVE_CLOCK` (474 faults, 948 I/O).
