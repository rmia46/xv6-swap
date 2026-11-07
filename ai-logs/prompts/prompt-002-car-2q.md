# Prompt ID: PROMPT-002
- **Date:** 2026-11-07
- **Target Subsystem / Policy:** Phase II Candidate 1 — 2Q / CAR Scan-Resistant Dual-List Clock Policy (Issue #8)
- **Model Used:** Gemini
- **Purpose:** Design an adaptive scan-resistant page replacement algorithm for xv6-RISC-V that segments resident pages into a Probationary List ($T_1$) and a Protected Frequency List ($T_2$).

## Exact Prompt:
> Design a 2Q / Clock with Adaptive Replacement (CAR) hybrid algorithm for xv6-RISC-V demand paging with a fixed physical resident budget of 16 pages (`MAX_PHY_PAGES = 16`).
> Requirements:
> 1. Partition resident frames into:
>    - $T_1$ (Probationary list): Newly allocated or fault-in pages start here with a single-reference probationary status.
>    - $T_2$ (Protected list): Pages that are referenced again while in $T_1$ (or whose `PTE_A == 1` upon second inspection) are promoted to $T_2$.
> 2. Eviction policy:
>    - Always attempt eviction from $T_1$ first (sweeping with a clock hand, promoting pages with `PTE_A == 1` to $T_2$, and evicting pages with `PTE_A == 0`).
>    - Only evict from $T_2$ if $T_1$ is empty or $T_2$ has exceeded its target capacity limit (e.g., target 12 pages, minimum 4 for $T_1$).
> 3. Scan Resistance:
>    - A linear scan (`wl_sequential`) touching many pages once will cycle exclusively through $T_1$ without evicting hot, frequently reused pages protected in $T_2$.
> 4. Concurrency & Lifecycles:
>    - Update `allocproc`, `freeproc`, `kfork`, `kexec`, and `uvmunmap` so both lists and clock hands are cleanly maintained without frame leaks.
> 5. Evaluation:
>    - Add `POLICY ?= CAR_2Q` option in `src/Makefile` and benchmark against `BASELINE_P2` (Adaptive Aging Clock: 474 faults, 948 swap I/O).
