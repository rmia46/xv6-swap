# Experiment Record: EXP-___

- **Experiment ID:** EXP-XXXX
- **Date:** YYYY-MM-DD
- **Git Commit:** `[commit-hash]`
- **Author(s):** [Team Member Names]

---

## 1. Objective & Hypothesis
- **Policy Evaluated:** [FIFO | Clock | Enhanced Clock | Adaptive Clock / Other]
- **Target Workload:** [Sequential | Locality | Thrash | Custom]
- **Hypothesis:** What specific outcome or behavior do we expect to observe and why?

---

## 2. Experimental Configuration
- **Architecture:** RISC-V 64-bit Sv39 (QEMU `qemu-system-riscv64`)
- **Physical Memory Limit (RAM):** e.g., 2MB / 4MB / 512 frames
- **Swap Space Allocation:** e.g., 1024 disk blocks
- **Workload Parameters:** (e.g., iterations, working set size in pages)
- **Policy Tuning Parameters:** (e.g., decay interval, hand advance count)
- **AI Reference / Log ID:** (e.g., `ai-logs/prompts/prompt-003.md`, or N/A)

---

## 3. Code & Kernel Changes
Briefly summarize what specific changes were active for this experiment:
- File(s) modified:
- Key mechanisms altered:

---

## 4. Empirical Measurements

| Metric | Measured Value | Baseline Comparison |
|---|---|---|
| Total Page Faults | | |
| Swap Reads (Page In) | | |
| Swap Writes (Page Out) | | |
| Total Execution Ticks | | |
| Eviction Policy Overhead | | |

---

## 5. Observations & Discussion
- Did the data support the hypothesis?
- Were there unexpected anomalies (e.g., Belady's anomaly, unexpected thrashing)?
- Any edge-case panics or stability issues?

---

## 6. Conclusion & Next Steps
- Conclusion drawn from this run:
- Recommended follow-up experiment:
