# Prompt ID: PROMPT-003
- **Date:** 2026-11-07
- **Target Subsystem / Policy:** Phase II Candidate 2 — Frequency-Weighted Access Aging (LFU-Recency Blend) (Issue #9)
- **Model Used:** Gemini
- **Purpose:** Design an LFU-LRU hybrid replacement algorithm that tracks both temporal recency (8-bit shift register) and reference frequency (4-bit saturating counter) to optimize high-intensity inner loops like matrix multiplication without sacrificing recency responsiveness.

## Exact Prompt:
> Design a Frequency-Weighted Access Aging (LFU-Recency Blend) page replacement policy for xv6-RISC-V demand paging with `MAX_PHY_PAGES = 16`.
> Requirements:
> 1. In `struct proc`, track:
>    - `uint8 access_history[MAX_PHY_PAGES];` (8-bit recency shift register)
>    - `uint8 ref_frequency[MAX_PHY_PAGES];` (4-bit saturating frequency counter, range 0..15)
> 2. On eviction sweep:
>    - Sample hardware `PTE_A`.
>    - If `PTE_A == 1`:
>      - Increment frequency: `ref_frequency[i] = min(15, ref_frequency[i] + 1)`
>      - Shift recency: `access_history[i] = (access_history[i] >> 1) | 0x80`
>      - Clear `PTE_A` and issue `sfence_vma()`.
>    - If `PTE_A == 0`:
>      - Exponentially decay frequency: `ref_frequency[i] = (ref_frequency[i] * 3) / 4`
>      - Shift recency: `access_history[i] = (access_history[i] >> 1)`
> 3. Composite Candidate Scoring:
>    - Formulate eviction score:
>      `score = (uint32)access_history[i] + ((uint32)ref_frequency[i] * 16) + ((*pte & PTE_D) ? 32 : 0);`
>    - Victim selected is the resident frame with the minimal composite score (least recently and least frequently used, favoring clean over dirty).
> 4. Clean compilation with `make POLICY=FREQ_AGING` with `-Wall -Werror`.
> 5. Benchmark against `CAR_2Q` (467 faults, 934 I/O) and `ADAPTIVE_CLOCK` (474 faults, 948 I/O).
