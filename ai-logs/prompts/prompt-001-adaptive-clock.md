# Prompt ID: PROMPT-001
- **Date:** 2026-11-07
- **Target Subsystem / Policy:** Adaptive Clock / Sampled Access History Page Replacement Policy (Issue #4)
- **Model Used:** Gemini
- **Purpose:** Design and implement a robust, lightweight Adaptive Aging Clock policy for xv6-RISC-V Sv39 that tracks page reference frequency/recency via shift registers, discriminates against streaming scan pollution, and dynamically adapts candidate selection.

## Exact Prompt:
> Design and implement the Adaptive Clock policy for xv6-RISC-V demand paging.
> Requirements:
> 1. Each resident page tracks an 8-bit reference history (`uint8 access_history[MAX_PHY_PAGES]`).
> 2. On eviction sweeps or periodic sampling ticks, shift the history right and insert the hardware `PTE_A` bit at the MSB: `history = (history >> 1) | (accessed << 7)`.
> 3. Clear `PTE_A` and issue `sfence_vma()`.
> 4. Victim selection computes an eviction score combining reference history and dirty bit cost: `score = history + (dirty ? DIRTY_BIAS : 0)`.
> 5. Select candidate with the lowest score (the frame untouched for the most consecutive observation epochs, preferring clean over dirty).
> 6. Include compile-time integration (`POLICY=ADAPTIVE_CLOCK`).
> 7. Verify correctness and document benchmark metrics against Baseline 0, 1, and 2.
