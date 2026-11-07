# ADR-002: Modular Swap Architecture, Resident Limits, and Eviction Policy Design

- **Status:** Accepted
- **Date:** 2026-11-07
- **Authors:** Kernel Architecture & Virtual Memory Team
- **Deciders:** Lead Systems Architect, Senior Kernel Engineer, Technical Writer
- **Related Issues:** Issue #1 (FIFO baseline), Issue #2 (Clock), Issue #3 (Enhanced Clock), Issue #4 (Adaptive Clock), Issue #6 (Comparative Analysis)

---

## 1. Context and Problem Statement

xv6-RISC-V natively lacks demand paging, secondary storage backing, and frame eviction. When physical memory is saturated, allocations fail or abort. To study page replacement policies scientifically and support workloads requiring more memory than physical limits permit, xv6 must be extended with:
1. A robust backing store and demand-paging mechanism.
2. A strict memory constraint that induces controlled, repeatable page eviction.
3. An architectural boundary decoupling swap mechanism from replacement policy algorithms.
4. Support for evaluating classic and research-grade replacement algorithms: FIFO, Clock, Enhanced Clock (Accessed + Dirty), and Adaptive Aging Clock.

How should xv6 structure swap state, resident page budgets, and eviction decisions to maximize architectural modularity, maintain strict kernel safety, and provide fair empirical evaluation?

---

## 2. Considered Options & Architectural Alternatives

### 2.1 Separation of Mechanism vs. Policy
- **Option A (Tight Coupling):** Embed eviction logic directly inside `vmfault()` and `kalloc()`.
- **Option B (Modular Decoupling):** Abstract swap mechanisms (`swap_alloc`, `swap_free`, `swap_read`, `swap_write`, `vmfault` restoration, TLB shootdown) into kernel VM machinery, while exposing a dedicated policy interface (`select_victim_page()`) driven by compile-time flag (`POLICY ?= ...`).
  - *Outcome:* Option B chosen. Allows identical I/O paths across all experiments, ensuring that performance differences stem purely from replacement algorithms.

### 2.2 Resident Memory Budget (`MAX_PHY_PAGES = 16`)
- **Option A (Dynamic Global Pool):** Evict pages globally across all processes when free system frames fall below a watermark.
- **Option B (Per-Process Resident Set Limit = 16 Pages):** Restrict each process to at most 16 resident 4 KB physical pages (`64 KB`).
  - *Rationale:* Global page replacement introduces non-deterministic inter-process scheduling noise, complicating comparative scientific evaluation. A fixed per-process resident cap of 16 pages forces frequent, reproducible evictions under compact synthetic workloads (`testswap` with 32 pages stresses memory at a $2:1$ overcommit ratio).

### 2.3 Enhanced Clock Classification (4-Class Matrix)
- **Option A (2-Pass Modified Clock):** Prioritize unreferenced clean, then unreferenced dirty, ignoring reference reset cycles.
- **Option B (Classic 4-Class Accessed + Dirty Search):**
  - Class 0: `(0, 0)` — Best victim (clean, unreferenced).
  - Class 1: `(0, 1)` — Second best (dirty, unreferenced).
  - Class 2: `(1, 0)` — Referenced clean.
  - Class 3: `(1, 1)` — Referenced dirty.
  - Executes up to 4 circular search passes, clearing `PTE_A` during odd passes and demoting candidate classes.
  - *Rationale:* Implements standard OS literature specification (Silberschatz/Tanenbaum) and empirically proves 100% clean-page eviction priority during mixed workloads.

### 2.4 Adaptive Clock Design (Sampled History Shift Registers)
- **Option A (Hardware Timestamp / Counter):** Store high-resolution timer ticks per PTE access. (Requires hardware timer virtualization per access; impossible without software emulation trapping every read/write).
- **Option B (8-Bit Decaying Shift Register + Dirty Bias Score):**
  - Store `uint8 access_history[MAX_PHY_PAGES]` in `struct proc`.
  - Shift right and inject current `PTE_A` into MSB during sweeps: $\text{history} = (\text{history} \gg 1) \mid (\text{accessed} \ll 7)$.
  - Add dirty bias penalty: $\text{score} = \text{history} + (\text{dirty} \mathrel{?} 32 : 0)$.
  - *Rationale:* Extremely lightweight (1 byte per resident page frame), deterministic $O(N)$ sweep (exactly 16 candidate inspections per eviction), avoids multi-pass clock loops, and rewards pages accessed repeatedly across multiple epochs.

---

## 3. Decision Outcome

We formally adopted:
1. **Strict Mechanism / Policy Decoupling:**
   - [src/kernel/swap.c](file:///data/rspace/codespace/projects/xv6-swap/src/kernel/swap.c) handles block I/O, bitmap allocation, PTE RSW bit tagging (`PTE_S`), and TLB flushes (`sfence_vma()`).
   - `select_victim_page()` implements the active policy without leaking storage or trap logic.
2. **Per-Process Resident Cap of 16 Pages (`MAX_PHY_PAGES = 16`):**
   - Maintained in `struct proc` via `resident_pages[16]`, `access_history[16]`, and `clock_hand`.
3. **PTE Format with Bit 8 `PTE_S`:**
   - Preserves user permissions (`PTE_R | PTE_W | PTE_X | PTE_U`) and packs 22-bit swap slot indexes when `PTE_V == 0`.
4. **4 Distinct Replacement Algorithms:**
   - `SWAP_POLICY_FIFO` (0)
   - `SWAP_POLICY_CLOCK` (1)
   - `SWAP_POLICY_ENHANCED_CLOCK` (2)
   - `SWAP_POLICY_ADAPTIVE_CLOCK` (3)
   - Selectable via `make POLICY=<NAME>` in [src/Makefile](file:///data/rspace/codespace/projects/xv6-swap/src/Makefile).

### Rationale & Trade-offs
- **Deterministic Evaluation:** The 16-page budget guarantees that any workload allocating $\ge 17$ pages will trigger deterministic eviction behavior without exhausting total QEMU physical memory.
- **Fair Comparison:** All four policies execute on identical swap primitives and identical test harnesses (`testswap`), ensuring that variance in swap writes, inspections, and swap-in faults is attributable solely to algorithmic decisions.
- **Hardware Coherence:** RISC-V Sv39 sets `PTE_A` and `PTE_D` in hardware. By executing `sfence_vma()` whenever `PTE_A` is cleared or `PTE_S` is assigned, we maintain coherence across hardware MMU address translations.

---

## 4. Consequences & Verification

### Positive Consequences
- **Elimination of Thrashing in Adaptive Clock:** EXP-003 demonstrated an **18.75% reduction in swap-in faults** (from 64 down to 52 in `testswap_basic`) and **37.5% reduction** (from 32 down to 20 in `testswap_lazy`), saving 15% to 18.75% of disk writes.
- **I/O Overhead Minimization in Enhanced Clock:** EXP-002 demonstrated **100% clean victim selection** (avoiding 8 unnecessary dirty writebacks during mixed pressure tests).
- **Zero Data Corruption:** All test suites (`testswap_basic`, `testswap_lazy`, `testswap_fork`, `testswap_clean_dirty`) pass with 100% byte verification.

### Accepted Limitations
- Swap space is statically bounded at 2000 slots (8 MB total swap capacity), which is more than sufficient for educational workloads.
- The 16-page resident limit is per-process; inter-process swapping dynamics are intentionally isolated to maintain repeatability.

### Empirical Artifacts
- Baseline FIFO vs Clock: [experiments/results/EXP-001-fifo-vs-clock.md](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-001-fifo-vs-clock.md)
- Enhanced Clock Evaluation: [experiments/results/EXP-002-enhanced-clock.md](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-002-enhanced-clock.md)
- Adaptive Aging Clock Evaluation: [experiments/results/EXP-003-adaptive-clock.md](file:///data/rspace/codespace/projects/xv6-swap/experiments/results/EXP-003-adaptive-clock.md)
