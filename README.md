# CSE323 — Project P4: Page Replacement with Swap

Operating Systems university research and development project extending MIT xv6 (targeted for xv6 x86, pending official starter confirmation) with demand paging / swapping mechanisms and evaluating comparative page replacement algorithms.

---

> **Current Status:** 🟡 **Phase 0 — Repository Preparation & Starter Audit Phase**  
> Baseline kernel source has not yet been introduced. The repository is organized and awaiting delivery and auditing of the instructor's starter code.

---

## 1. Project Overview

Memory management is fundamental to operating system design. When physical memory capacity is exceeded, an operating system must evict resident pages to secondary storage (swap) to make room for active processes.

This project addresses two decoupled concerns:
1. **Swapping Mechanism:** The low-level machinery responsible for backing up physical page frames to disk, invalidating page table entries (PTEs), intercepting page faults, and restoring page contents on demand.
2. **Page Replacement Policy:** The decision algorithm determining *which* resident physical frame to evict when memory is exhausted.

### Candidate Replacement Policies
- **Baseline:** FIFO (First-In, First-Out)
- **Clock:** Standard Second-Chance Clock using hardware Accessed (`A`) bits
- **Enhanced Clock:** Multi-class classification using Accessed (`A`) and Dirty (`D`) bits: `(0,0)`, `(0,1)`, `(1,0)`, `(1,1)`
- **Adaptive / Research Candidate:** Adaptive Clock or dynamic decay algorithm (subject to experimentation and AI exploration)

---

## 2. Target Environment (Provisional)

- **OS Target:** xv6 (x86 architecture target provisional update; pending starter kit confirmation)
- **Virtual Machine:** QEMU (x86 / `qemu-system-i386`)
- **Toolchain:** GCC / Clang with cross-compilation support for 32-bit x86 ELF
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

## 4. Setup & Building (Pending Confirmation)

> [!NOTE]
> The build instructions below will be finalized as soon as the official starter code is added to `src/`.

Typical expected workflow for xv6:

```bash
# Ensure 32-bit development libraries and QEMU are installed (Debian/Ubuntu)
sudo apt-get install build-essential gcc-multilib qemu-system-x86

# Build kernel and user utilities
cd src && make

# Run in QEMU
make qemu
```

---

## 5. Team & Governance

- **Course:** CSE323 — Operating Systems
- **Team Size:** 4 members
- **Workflow:** Pull requests into `dev`, with strict peer code review. See [CONTRIBUTING.md](file:///data/rspace/codespace/projects/xv6-swap/CONTRIBUTING.md) for branch strategy, commit conventions, and review guidelines.

---

## 6. Key References

- [Detailed Implementation and Research Plan](file:///data/rspace/codespace/projects/xv6-swap/docs/CSE323_P4_Detailed_Plan.md)
- [Starter Code Audit Checklist](file:///data/rspace/codespace/projects/xv6-swap/docs/starter-audit-template.md)
- [Architecture Decision Records](file:///data/rspace/codespace/projects/xv6-swap/docs/decisions/README.md)
