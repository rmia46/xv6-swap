# CSE323 — Project P4: Page Replacement with Swap

Operating Systems university research and development project extending MIT xv6 (targeting xv6-RISC-V 64-bit) with demand paging / swapping mechanisms and evaluating comparative page replacement algorithms.

---

> **Current Status:** 🟢 **Phase 4 Operational — All 4 Page Replacement Policies Implemented & Verified**  
> The virtual memory swapping subsystem, disk backing store, demand-paging fault engine, diagnostic telemetry counters, and four page replacement algorithms are implemented, fully tested, and scientifically benchmarked.

---

## 1. Project Overview

Memory management is fundamental to operating system design. When physical memory capacity is exceeded, an operating system must evict resident pages to secondary storage (swap) to make room for active processes.

This project addresses two decoupled concerns:
1. **Swapping Mechanism:** The low-level machinery responsible for backing up physical page frames to disk, invalidating page table entries (PTEs) using the supervisor software bit `PTE_S`, intercepting page faults in `vmfault()`, enforcing TLB invalidation (`sfence_vma()`), and restoring page contents on demand.
2. **Page Replacement Policy:** The decision algorithm determining *which* resident physical frame to evict when the per-process resident limit (`MAX_PHY_PAGES = 16`) is reached.

### Implemented Replacement Policies
All four target page replacement algorithms are fully implemented, verified, and operational:
- **Baseline 0 — FIFO (First-In, First-Out):** Queue-based eviction selecting the oldest resident page frame. Serves as the control baseline.
- **Baseline 1 — Clock (Second-Chance):** Circular sweep inspecting the hardware Accessed (`PTE_A`) bit, giving referenced pages a second chance and clearing the bit.
- **Baseline 2 — Enhanced Clock (Accessed + Dirty):** Multi-pass search categorizing resident pages into 4 classes: `(0,0)`, `(0,1)`, `(1,0)`, `(1,1)` to prioritize clean page evictions and eliminate unnecessary disk writes.
- **Research Candidate — Adaptive Aging Clock:** Pseudo-LRU 8-bit decaying shift register access history combined with a dirty write-back penalty bias, executing deterministic $O(N)$ sweeps per eviction.

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

# 1. First-In, First-Out (Baseline 0)
make clean && make POLICY=FIFO

# 2. Second-Chance Clock (Baseline 1)
make clean && make POLICY=CLOCK

# 3. Enhanced Clock (Baseline 2: Accessed + Dirty)
make clean && make POLICY=ENHANCED_CLOCK

# 4. Adaptive Aging Clock (Research Candidate)
make clean && make POLICY=ADAPTIVE_CLOCK
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

---

## 5. Team & Governance

- **Course:** CSE323 — Operating Systems
- **Team Size:** 4 members
- **Workflow:** Pull requests into `dev`, with strict peer code review. See [CONTRIBUTING.md](file:///data/rspace/codespace/projects/xv6-swap/CONTRIBUTING.md) for branch strategy, commit conventions, and review guidelines.

---

## 6. Key References & Verification Evidence

- [Architecture Specification: Virtual Memory & Page Swapping Subsystem](file:///data/rspace/codespace/projects/xv6-swap/docs/architecture/README.md)
- [ADR-002: Modular Swap Architecture, Resident Limits, and Eviction Policies](file:///data/rspace/codespace/projects/xv6-swap/docs/decisions/ADR-002-swap-architecture-and-policies.md)
- [Detailed Implementation and Research Plan](file:///data/rspace/codespace/projects/xv6-swap/docs/CSE323_P4_Detailed_Plan.md)
- [Empirical Experiment EXP-001: FIFO vs Clock](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-001-fifo-vs-clock.md)
- [Empirical Experiment EXP-002: Enhanced Clock (Clean vs Dirty)](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-002-enhanced-clock.md)
- [Empirical Experiment EXP-003: Adaptive Aging Clock](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-003-adaptive-clock.md)
