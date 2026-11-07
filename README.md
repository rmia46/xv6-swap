# CSE323 — Project P4: Page Replacement with Swap

Operating Systems university research and development project extending MIT xv6 (targeting xv6-RISC-V 64-bit) with demand paging / swapping mechanisms and evaluating comparative page replacement algorithms.

---

> **Current Status:** 🟢 **Phase II Complete — All 8 Page Replacement Policies Implemented, Benchmarked & Synthesized**  
> The virtual memory swapping subsystem, demand-paging engine, VirtIO backing store, comprehensive benchmark harness, all 8 classic and advanced replacement policies, and the empirical Pareto Frontier / Stopping Criterion analysis are fully operational, tested, and documented.

---

## 1. Project Overview

Memory management is fundamental to operating system design. When physical memory capacity is exceeded, an operating system must evict resident pages to secondary storage (swap) to make room for active processes.

This project addresses two decoupled concerns:
1. **Swapping Mechanism:** The low-level machinery responsible for backing up physical page frames to disk, invalidating page table entries (PTEs) using the supervisor software bit `PTE_S`, intercepting page faults in `vmfault()`, enforcing TLB invalidation (`sfence_vma()`), and restoring page contents on demand.
2. **Page Replacement Policy:** The decision algorithm determining *which* resident physical frame to evict when the per-process resident limit (`MAX_PHY_PAGES = 16`) is reached.

### Implemented Replacement Policies (8 Supported Algorithms)

The repository implements eight distinct replacement policies partitioned across two research phases:

#### Phase I Baselines
- **Baseline 0 — FIFO (First-In, First-Out):** Queue-based arrival order eviction selecting the oldest resident frame. Serves as experimental control baseline.
- **Baseline 1 — Clock (Second-Chance):** Circular sweep inspecting the hardware Accessed (`PTE_A`) bit, giving referenced pages a second chance and clearing the bit.
- **Baseline 2 — Enhanced Clock (Accessed + Dirty):** Multi-pass search categorizing resident pages into 4 classes `(0,0)`, `(0,1)`, `(1,0)`, `(1,1)` to prioritize clean page evictions and eliminate unnecessary disk writes.
- **Baseline P2 — Adaptive Aging Clock:** Pseudo-LRU 8-bit decaying shift register access history combined with a dirty write-back penalty bias, executing deterministic $O(N)$ sweeps per eviction.

#### Phase II Advanced Research Candidates
- **Candidate 1 — CAR_2Q (Dual-List Scan Resistance):** Partitioned resident memory into a probationary list $T_1$ and protected list $T_2$ ($TARGET\_T2 = 12$) to eliminate scan pollution and protect multi-touch working sets. **Fault Minimization Leader (467 faults, 934 swap I/O pages, 4,335 scan inspections).**
- **Candidate 2 — FREQ_AGING (Frequency-Weighted Access Aging):** Blends a 4-bit saturating frequency counter ($0..15$) with an 8-bit recency shift register and exponential decay ($\times 3/4$), harmonizing LFU and LRU dynamics.
- **Candidate 3 — DYNAMIC_BIAS (Dynamic Fault-Rate Sensitive Writeback Bias):** Sliding window tracker ($W = 4$ ticks $\approx 400$ ms) dynamically modulates dirty eviction penalties from $+16$ to $+64$ based on real-time fault velocity. **Write Amplification Suppression Leader (64.3% clean eviction ratio in `dirty_stress`, 139 clean evictions across suite).**
- **Candidate 4 — TIMER_AGING (Timer-Interrupt Driven Background Aging):** Decouples access bit decay from the page fault critical path into kernel timer interrupt handlers (`devintr()`) with batched TLB invalidation (`sfence_vma()`) and an anti-FIFO fast path. **Execution Latency Speedup Leader (11.61B cycles, -13.3% cycle time speedup across benchmark suite).**

### Empirical Pareto Frontier & Stopping Criterion

Comprehensive evaluation across all 5 standard workloads and 8 policies formalizes a three-dimensional **Pareto Frontier**:
- **Fault & I/O Minimization:** `CAR_2Q` (467 total faults, 934 swap I/O pages, 4,335 scans).
- **Execution Latency & Throughput:** `TIMER_AGING` (11.61 billion CPU cycles, -13.3% cycle time vs Adaptive Clock, -15.6% vs CAR_2Q, -36.1% vs FIFO).
- **Write Amplification Suppression:** `DYNAMIC_BIAS` (64.3% clean ratio, 50 dirty writes in `dirty_stress`, 139 clean evictions across suite).

**Stopping Criterion:** Marginal fault reductions between iterations drop below **1.5%** ($\Delta_{\text{rel}} < 1.5\%$), establishing that the system has reached the theoretical **Physical Memory Working-Set Saturation Limit** under $MAX\_PHY\_PAGES = 16$. See [ADR-003](file:///data/rspace/codespace/projects/xv6-swap/docs/decisions/ADR-003-phase2-advanced-policies-and-stopping-criterion.md) for the complete proof and trade-off analysis.

---

## 2. Target Environment

- **OS Target:** xv6-RISC-V (64-bit Sv39 paging)
- **Virtual Machine:** QEMU (`qemu-system-riscv64`)
- **Toolchain:** `riscv64-unknown-elf-gcc` or `riscv64-linux-gnu-gcc`
- **Build System:** GNU Make

---

## 3. Repository Layout

```text
.
├── README.md                           # Main project entry point & overview
├── CONTRIBUTING.md                     # Workflow, branch model, commit conventions
├── .gitignore                          # Build artifacts, logs, swap disk images ignore rules
├── .gitattributes                     # Line ending & git attribute configurations
│
├── docs/                               # All documentation and planning specifications
│   ├── CSE323_P4_Detailed_Plan.md      # Primary specification and reference document
│   ├── starter-audit-template.md       # Audit checklist for incoming instructor starter code
│   ├── architecture/                   # VM, swapping, and page table architecture designs
│   ├── research/                       # Replacement policy literature & comparative studies
│   ├── implementation/                 # Subsystem implementation notes & guides
│   ├── testing/                        # Verification plans and test matrices
│   └── decisions/                      # Architectural Decision Records (ADRs)
│
├── src/                                # Kernel and user space source (introduced post-audit)
│   └── README.md
│
├── tests/                              # Automated test suites & verification harnesses
│   ├── correctness/                    # Page fault handling, swap round-trip, corner cases
│   ├── workloads/                      # Synthetic memory access workloads (locality, sequential)
│   └── README.md
│
├── benchmarks/                         # Performance benchmarking harness and data
│   ├── scripts/                        # Automation scripts to run workloads & collect metrics
│   ├── raw/                            # Unprocessed log outputs and raw counter data
│   ├── processed/                      # Aggregated tables, CSVs, and plots
│   └── README.md
│
├── experiments/                        # Controlled scientific policy comparisons
│   ├── configurations/                 # Workload and memory limit parameters
│   ├── results/                        # Documented experimental runs & reproducibility logs
│   ├── template.md                     # Experiment recording template
│   └── README.md
│
├── ai-logs/                            # Transparency log for AI prompts and assistance
│   ├── prompts/                        # Exact prompts issued to AI assistants
│   ├── responses/                      # Captured AI proposals and algorithm designs
│   └── README.md
│
└── scripts/                            # Helper utilities for QEMU execution, tracing, formatting
    └── README.md
```

---

## 4. Setup, Building & Running

Ensure RISC-V toolchain and QEMU are installed (Debian/Ubuntu):
```bash
sudo apt-get install build-essential gcc-riscv64-linux-gnu g++-riscv64-linux-gnu qemu-system-misc
```

### Compiling with a Specific Replacement Policy
The replacement policy is selected at compile-time via the `POLICY` variable:

```bash
cd src

# --- Phase I Baselines ---
make clean && make POLICY=FIFO            # Baseline 0: First-In, First-Out
make clean && make POLICY=CLOCK           # Baseline 1: Second-Chance Clock
make clean && make POLICY=ENHANCED_CLOCK  # Baseline 2: Enhanced 4-Class Clock
make clean && make POLICY=ADAPTIVE_CLOCK  # Baseline P2: 8-Bit Aging Clock

# --- Phase II Advanced Research Candidates ---
make clean && make POLICY=CAR_2Q          # Candidate 1: Dual-List Scan-Resistant Clock
make clean && make POLICY=FREQ_AGING      # Candidate 2: Frequency-Weighted Aging
make clean && make POLICY=DYNAMIC_BIAS    # Candidate 3: Dynamic Fault-Rate Sensitive Bias
make clean && make POLICY=TIMER_AGING     # Candidate 4: Timer-Driven Background Aging
```

### Running and Testing in QEMU
```bash
# Launch xv6 in QEMU
make qemu

# Inside xv6 shell, run comprehensive swapping test suite:
$ testswap
```

To run non-interactive verification:
```bash
python3 test-xv6.py testswap
```

To execute the automated multi-trial benchmark suite across all workloads:
```bash
python3 benchmarks/scripts/run_benchmarks.py
```

---

## 5. Team & Governance

- **Course:** CSE323 — Operating Systems
- **Team Size:** 4 members
- **Workflow:** Pull requests into `dev`, with strict peer code review. See [CONTRIBUTING.md](file:///data/rspace/codespace/projects/xv6-swap/CONTRIBUTING.md) for branch strategy, commit conventions, and review guidelines.

---

## 6. Key References & Verification Evidence

- [Architecture Specification: Virtual Memory & Page Swapping Subsystem](file:///data/rspace/codespace/projects/xv6-swap/docs/architecture/README.md)
- [ADR-002: Modular Swap Architecture, Resident Limits, and Eviction Policies](file:///data/rspace/codespace/projects/xv6-swap/docs/decisions/ADR-002-swap-architecture-and-policies.md)
- [ADR-003: Phase II Advanced Policies, Pareto Frontier & Stopping Criterion](file:///data/rspace/codespace/projects/xv6-swap/docs/decisions/ADR-003-phase2-advanced-policies-and-stopping-criterion.md)
- [Consolidated Benchmark Summary & Analysis](file:///data/rspace/codespace/projects/xv6-swap/benchmarks/processed/summary.md)
- [Consolidated Empirical Results Dataset (CSV)](file:///data/rspace/codespace/projects/xv6-swap/benchmarks/processed/results.csv)
- [Detailed Implementation and Research Plan](file:///data/rspace/codespace/projects/xv6-swap/docs/CSE323_P4_Detailed_Plan.md)
- [Benchmarking Methodology & Metric Definitions](file:///data/rspace/codespace/projects/xv6-swap/docs/benchmarking-methodology.md)
- [EXP-001: Baseline 0 FIFO vs Baseline 1 Clock](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-001-fifo-vs-clock.md)
- [EXP-002: Baseline 2 Enhanced Clock (Accessed + Dirty)](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-002-enhanced-clock.md)
- [EXP-003: Baseline P2 Adaptive Aging Clock](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-003-adaptive-clock.md)
- [EXP-004: Phase II Candidate 1 CAR_2Q (Dual-List Clock)](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-004-car-2q.md)
- [EXP-005: Phase II Candidate 2 FREQ_AGING (LFU-Recency Blend)](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-005-freq-aging.md)
- [EXP-006: Phase II Candidate 3 DYNAMIC_BIAS (Dynamic Writeback Bias)](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-006-dynamic-dirty-bias.md)
- [EXP-007: Phase II Candidate 4 TIMER_AGING (Timer-Driven Background Aging)](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-007-timer-bg-aging.md)
