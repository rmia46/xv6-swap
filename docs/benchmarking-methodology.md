# Benchmarking Methodology & Metric Definitions

This specification outlines the evaluation metrics, measurement methodology, benchmark workloads, and the iterative baseline-advancement cycle for page replacement with swap in xv6-RISC-V.

---

## 1. Primary Evaluation Metrics

To avoid emulation artifacts (where wall-clock time can be noisy due to host CPU throttling, hypervisor scheduling, and background processes), we split metrics into **Deterministic Algorithmic & Hardware Counters** and **Emulation-Aware Execution Timings**.

### 1.1 Deterministic Subsystem Counters (Primary Truth)

These counters provide 100% reproducible, hardware-independent evaluation across runs:

| Category | Metric | Definition / Source | Unit |
|---|---|---|---|
| **Fault Metrics** | `fault_total` | Total user page faults intercepted in `usertrap()` | count |
| | `fault_swapin` | Page faults that successfully restore a page from swap | count |
| | `fault_lazy` | Lazy allocation page faults (isolated from swap faults) | count |
| **I/O Metrics** | `swap_reads` | Number of page-sized reads issued to disk/virtio | pages |
| | `swap_writes` | Number of page-sized writes issued to disk/virtio (dirty writebacks) | pages |
| | `io_volume_bytes` | `(swap_reads + swap_writes) * PGSIZE` | bytes |
| **Eviction Metrics** | `evict_attempts` | Number of times page eviction was triggered | count |
| | `evict_success` | Pages successfully unmapped and evicted | count |
| | `evict_clean` | Evictions that required no disk write (PTE Dirty bit was 0) | count |
| | `evict_dirty` | Evictions requiring disk writeback (PTE Dirty bit was 1) | count |
| **Policy Cost** | `scan_inspections`| Total candidate frames inspected during eviction searches | count |

### 1.2 Derived Quality Metrics

- **Clean Eviction Ratio:** $\frac{\text{evict\_clean}}{\text{evict\_success}}$ (Higher is better: clean pages avoid costly disk write overhead)
- **Swap Writes per Eviction:** $\frac{\text{swap\_writes}}{\text{evict\_success}}$ (Lower is better: measures I/O write amplification)
- **Average Scan Overhead:** $\frac{\text{scan\_inspections}}{\text{evict\_success}}$ (Lower is better: search efficiency of the policy hand/queue)
- **Hit Ratio Equivalent:** $1.0 - \frac{\text{fault\_swapin}}{\text{total\_memory\_references}}$

### 1.3 Emulation-Aware Execution Metrics

Running inside QEMU introduces measurement caveats. Wall-clock time on the host is vulnerable to noise. We measure:
1. **RISC-V CPU Cycles / `rdtime`:** Read via RISC-V CSR (`rdtime` or `rdcycle`) inside xv6 user space and kernel around workload execution.
2. **xv6 Timer Ticks:** Kernel timer ticks (`ticks` global incremented on timer interrupts).
3. **Statistical Aggregation:** Minimum 3 to 5 trials per workload; report **Median** and **IQR** (Interquartile Range) rather than a single noisy run.

---

## 2. Standard Workload Suite

All policies must be evaluated across an identical battery of workloads with fixed random seeds and memory constraints:

| Workload | Access Characteristics | Evaluation Goal |
|---|---|---|
| `wl_sequential` | Linear scan across memory exceeding RAM by 1.5x–2.0x | Basic streaming eviction & Belady's anomaly check |
| `wl_repeated_scan` | Cyclic traversal over working set larger than RAM | Thrashing boundary and looping behavior |
| `wl_locality` | 80/20 Pareto distribution (hot working set + cold pages) | Ability to protect active working set |
| `wl_matrix` | Strided row/column matrix multiplication | Realistic non-trivial access pattern |
| `wl_dirty_stress` | Controlled mix of reads (clean) and writes (dirty) | Evaluates Enhanced Clock's preference for clean pages |
| `wl_phase_change` | Hot set A $\to$ abrupt switch to Hot set B | Policy responsiveness to dynamic working set shift |

---

## 3. Iterative Baseline-Advancement Process

```
[ Step 1: Baseline ]
   Run FIFO on Workload Suite
   Record All Counters & Cycles
   Freeze as BASELINE_0
          │
          ▼
[ Step 2: Implementation / Optimization ]
   Develop Policy / Algorithmic Improvement
   (Clock -> Enhanced Clock -> Adaptive Clock -> Heuristic Search)
          │
          ▼
[ Step 3: Strict Quality Gate ]
   1. Correctness: Passes all memory stress & data integrity tests
   2. Zero panics, zero frame leaks, zero swap block leaks
          │
          ▼
[ Step 4: Benchmark Comparison ]
   Run identical workload suite (same seed, frames, repetitions)
   Compute Δ in Swap I/O, Faults, and CPU Cycles
          │
    ┌─────┴─────────────────────────────────┐
    │ Is improvement statistically significant? │
    └─────┬─────────────────────────────────┘
          │ YES                               │ NO
          ▼                                   ▼
[ Step 5: Advance Baseline ]         [ Reject or Re-tune ]
   New Implementation becomes           Document failure & hypotheses
   BASELINE_N. Archive all logs.        in experiments/results/
          │
          ▼
[ Step 6: Diminishing Returns Check ]
   Are improvements < threshold (e.g. < 2% I/O reduction)?
   If YES -> Declare optimal policy, finalize paper/report.
   If NO  -> Next iteration.
```

---

## 4. Documentation & Reproducibility Requirements

For **every** single pass:
1. **Commit Pin:** Exact git commit SHA before running tests.
2. **AI Log Reference:** Verbatim prompt and response stored in `ai-logs/` if an AI assistant suggested the tweak.
3. **Experiment Record:** Fill out [`experiments/template.md`](file:///data/rspace/codespace/projects/xv6-swap/experiments/template.md) and save as `experiments/results/EXP-###-<policy>-<workload>.md`.
4. **Summary Update:** Update active baseline in `benchmarks/README.md`.
