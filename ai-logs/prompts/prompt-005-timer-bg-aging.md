# Prompt ID: PROMPT-005
- **Date:** 2026-11-07
- **Target Subsystem / Policy:** Phase II Candidate 4 — Timer-Interrupt Driven Background Aging (Issue #11)
- **Model Used:** Gemini
- **Purpose:** Decouple reference aging sweeps from the critical allocation fault path by hooking periodic decay into the xv6 timer interrupt handler in `trap.c`, providing $O(1)$ instantaneous victim selection during memory pressure.

## Exact Prompt:
> Design a Timer-Interrupt Driven Background Aging policy for xv6-RISC-V demand paging with `MAX_PHY_PAGES = 16`.
> Background:
> All previous aging policies (Adaptive Clock, Frequency Aging, Dynamic Bias) perform the $O(N)$ sweep synchronously on the critical path of page allocation/eviction when the process is blocked on a page fault.
> Requirements:
> 1. In `src/kernel/trap.c`, inside `clockintr()` or the timer interrupt path:
>    - Every $K$ timer ticks (e.g. `TICKS_PER_AGE_EPOCH = 2`), invoke a background aging routine: `void swap_timer_age_tick(void);`
>    - The routine checks currently active/running user processes:
>      - For each resident page in the process, samples `PTE_A`.
>      - Shifts `access_history[i] = (access_history[i] >> 1) | (accessed << 7)`.
>      - Clears `PTE_A` and issues `sfence_vma()`.
> 2. Eviction Path Optimization:
>    - When `swap_evict_page()` is called, it does NOT need to perform a multi-epoch sweep or clear bits!
>    - It immediately finds the minimum `score = access_history[i] + (dirty ? 32 : 0)` in a single $O(N)$ pass, or $O(1)$ pre-cached index, significantly reducing scan overhead during fault handling.
> 3. Add `#define SWAP_POLICY_TIMER_AGING 7` in `src/kernel/swap.h` and support `make POLICY=TIMER_AGING` in `src/Makefile`.
> 4. Ensure thread/interrupt safety (proper locking when accessing process page tables from timer interrupt context, avoiding deadlock with `p->lock` or `swap_stats_lock`).
> 5. Verify data integrity with `testswap` in QEMU and benchmark against `CAR_2Q` (467 faults, 934 I/O) and `ADAPTIVE_CLOCK` (474 faults, 948 I/O).
