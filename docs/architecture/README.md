# Architecture Specification: Virtual Memory & Page Swapping Subsystem

This document provides the definitive architectural specification for the demand paging and page replacement subsystem in xv6-RISC-V (Sv39). It outlines data structures, storage representations, page table layouts, fault handling pipelines, hardware synchronization barriers, and the modular replacement policy engine.

---

## 1. Storage Architecture & Disk Layout

xv6 utilizes a raw VirtIO block device with a block size of $BSIZE = 1024$ bytes (`1 KB`). The virtual memory subsystem operates at a page granularity of $PGSIZE = 4096$ bytes (`4 KB`). Consequently, each swap slot exactly spans:

$$\text{Blocks per Page} = \frac{PGSIZE}{BSIZE} = \frac{4096}{1024} = 4 \text{ contiguous disk blocks}$$

### Disk Partitioning

The disk layout is defined in [src/kernel/param.h](file:///data/rspace/codespace/projects/xv6-swap/src/kernel/param.h):

```
+--------------------------+-------------------------------------------------------+
| File System Area         | Swap Backing Store Area                               |
| Blocks 0 .. 1999         | Blocks 2000 .. 9999                                   |
| (FSSIZE = 2000 blocks)   | (SWAPBLOCKS = 8000 blocks -> NSWAPSLOTS = 2000 slots) |
+--------------------------+-------------------------------------------------------+
0                          2000 (SWAPSTART)                                        10000
```

- `SWAPSTART = FSSIZE = 2000`: Sector offset where swap space begins.
- `SWAPBLOCKS = 8000`: Disk blocks designated for swap backing storage.
- `NSWAPSLOTS = SWAPBLOCKS / BLOCKS_PER_PAGE = 2000`: Total independent 4 KB page frames that can reside on disk simultaneously.

### Swap Slot Disk Address Calculation

Given a swap slot number $k \in [0, \text{NSWAPSLOTS} - 1]$, the starting disk block $B_k$ is computed as:

$$B_k = \text{SWAPSTART} + k \times \text{BLOCKS\_PER\_PAGE} = 2000 + 4k$$

Disk I/O is performed synchronously via the buffer cache (`bread()`, `bwrite()`, and `brelse()`) in [src/kernel/swap.c](file:///data/rspace/codespace/projects/xv6-swap/src/kernel/swap.c#L63-L99) across the 4 contiguous blocks: $\{B_k, B_k+1, B_k+2, B_k+3\}$.

---

## 2. Swap Space Bitmap Allocator

Swap space allocation uses a 1-bit-per-slot in-memory bitmap protected by a kernel spinlock:

```c
static uchar swap_bitmap[NSWAPSLOTS / 8 + 1];
static struct spinlock swap_lock;
```

- **Bit Value `0`:** Swap slot is unallocated / free.
- **Bit Value `1`:** Swap slot is occupied by a swapped-out page.

### Allocator Operations
1. **`swap_alloc()`**:
   - Acquires `swap_lock`.
   - Performs a linear scan over bits $0 \dots \text{NSWAPSLOTS}-1$.
   - Sets the first found zero-bit to 1 and returns the slot index $k$.
   - Returns `-1` if all 2000 slots are exhausted.
   - Releases `swap_lock`.
2. **`swap_free(int slot)`**:
   - Validates $0 \le \text{slot} < \text{NSWAPSLOTS}$.
   - Acquires `swap_lock`.
   - Asserts bit is 1 (panics on double-free).
   - Clears bit to 0 and releases `swap_lock`.

---

## 3. RISC-V Sv39 Page Table Entry (PTE) Encoding

In RISC-V Sv39 architecture, virtual addresses are 39 bits wide (translated via a three-level page table tree), and PTEs are 64 bits wide.

### Standard Resident Hardware PTE Format
```
 63      54 53                             10 9   8 7 6 5 4 3 2 1 0
+----------+---------------------------------+-----+---+-+-+-+-+-+-+
| Reserved | Physical Page Number (PPN)      | RSW | D |A|G|U|X|W|R|V|
| (10 bits)| (44 bits)                       | (2) | 1 |1|1|1|1|1|1|1|
+----------+---------------------------------+-----+---+-+-+-+-+-+-+
```
- `V` (bit 0): Valid bit. Hardware MMU checks this bit during address translation.
- `R, W, X` (bits 1..3): Read, Write, Execute permission flags.
- `U` (bit 4): User accessible.
- `G` (bit 5): Global mapping.
- `A` (bit 6): Hardware Accessed bit. Set by MMU upon read/write/instruction fetch.
- `D` (bit 7): Hardware Dirty bit. Set by MMU upon page write.
- `RSW` (bits 8..9): Reserved for Supervisor Software. Unused by hardware.

### Swapped-Out PTE Encoding (RSW Bit 8: `PTE_S`)
When a resident page is evicted to swap, its physical frame is reclaimed (`kfree`), and the PTE is converted to represent the swapped state:
```
 63                         32 31           10 9   8 7 6 5 4 3 2 1 0
+-----------------------------+---------------+-----+---+-+-+-+-+-+-+
|          Reserved           | Swap Slot No. | RSW | D |A|G|U|X|W|R|V|
|          (32 bits)          | (22 bits)     | 0 1 | 0 |0|0|1|x|x|x|0|
+-----------------------------+---------------+-----+---+-+-+-+-+-+-+
                                               ^   ^                 ^
                                            bit 9  bit 8 (PTE_S)    bit 0 (PTE_V = 0)
```

The encoding satisfies three invariants:
1. **Hardware MMU Invalidation:** Bit 0 (`PTE_V`) is set to `0`. Any user-space access immediately traps to the kernel with a Page Fault (`scause` 13 or 15).
2. **Software Signature:** Bit 8 (`PTE_S = 1L << 8`, inside the supervisor RSW field) is set to `1`. This distinguishes a swapped-out page from a lazily allocated or completely unmapped page.
3. **Slot Identification & User Permissions:**
   - Swap slot index ($0 \dots 1999$) is stored in bits 10..31: `(slot & 0x3FFFFF) << 10`.
   - User permissions (`PTE_R | PTE_W | PTE_X | PTE_U`) are preserved in bits 1..4 so they can be restored upon swap-in without reconsulting metadata.

### Encoding Macros ([src/kernel/riscv.h](file:///data/rspace/codespace/projects/xv6-swap/src/kernel/riscv.h#L403-L415))
```c
#define PTE_S            (1L << 8)         // Swapped to disk (software bit in RSW)
#define SWAP_SLOT_MASK   0x3FFFFF          // 22 bits
#define ENCODE_SWAP_PTE(slot, flags) \
    ((((uint64)(slot) & SWAP_SLOT_MASK) << 10) | ((flags) & 0x3FF) | PTE_S)
#define PTE2SWAPSLOT(pte) (((pte) >> 10) & SWAP_SLOT_MASK)
```

---

## 4. Demand Paging & Page Fault Pipeline (`vmfault()`)

When user space attempts to read, write, or execute an address whose PTE has `PTE_V == 0`, the RISC-V hart generates an exception:
- `scause = 13`: Page Load Fault
- `scause = 15`: Page Store/AMO Fault

The trap handler in [src/kernel/trap.c](file:///data/rspace/codespace/projects/xv6-swap/src/kernel/trap.c) catches the fault, extracts the offending address from `r_stval()`, and invokes `vmfault(p->pagetable, p->sz, va, is_read)`.

### Flowchart: Page Fault Resolution

```mermaid
flowchart TD
    Start(["User Trap: Page Fault (r_stval = va)"]) --> Walk["walk(pagetable, va, 0)"]
    Walk --> CheckPTE{"PTE exists?"}
    CheckPTE -- No --> CheckSbrk{"va < p->sz?"}
    CheckPTE -- Yes --> CheckValid{"PTE_V == 1?"}
    
    CheckValid -- Yes --> SIGSEGV["Segmentation Fault / Kill Process"]
    CheckValid -- No --> CheckSwap{"PTE_S == 1?"}
    
    CheckSwap -- Yes --> SwapInBranch["Swap-In Fault Path"]
    CheckSwap -- No --> CheckSbrk
    
    CheckSbrk -- No --> SIGSEGV
    CheckSbrk -- Yes --> LazyBranch["Lazy Allocation Path"]
    
    subgraph SwapIn ["Swap-In Execution"]
        SwapInBranch --> CountSwapin["swap_stats.fault_swapin++"]
        CountSwapin --> ExtractMeta["Extract slot = PTE2SWAPSLOT(*pte)<br/>Extract orig_flags = PTE_FLAGS(*pte)"]
        ExtractMeta --> CheckResLimit1{"p->resident_count >= MAX_PHY_PAGES?"}
        CheckResLimit1 -- Yes --> Evict1["swap_evict_page(p)"]
        CheckResLimit1 -- No --> AllocFrame1["kalloc() physical frame"]
        Evict1 --> AllocFrame1
        AllocFrame1 --> ReadDisk["swap_read(slot, pa)"]
        ReadDisk --> FreeSlot["swap_free(slot)"]
        FreeSlot --> RestorePTE["*pte = PA2PTE(pa) | perm | PTE_V<br/>sfence_vma()"]
        RestorePTE --> Enqueue1["fifo_enqueue(p, va)"]
    end
    
    subgraph LazyAlloc ["Lazy Allocation Execution"]
        LazyBranch --> CountLazy["swap_stats.fault_lazy++"]
        CountLazy --> CheckResLimit2{"p->resident_count >= MAX_PHY_PAGES?"}
        CheckResLimit2 -- Yes --> Evict2["swap_evict_page(p)"]
        CheckResLimit2 -- No --> AllocFrame2["kalloc() physical frame"]
        Evict2 --> AllocFrame2
        AllocFrame2 --> ZeroFrame["memset(pa, 0, PGSIZE)"]
        ZeroFrame --> MapPages["mappages(pagetable, va, PGSIZE, pa, perm)"]
        MapPages --> Enqueue2["fifo_enqueue(p, va)"]
    end
    
    Enqueue1 --> ReturnSuccess(["Resume User Execution"])
    Enqueue2 --> ReturnSuccess
```

### TLB Synchronization Barrier (`sfence_vma`)
The RISC-V translation lookaside buffer caches address translations and flag bits (including `PTE_V`, `PTE_A`, and `PTE_D`).
Any modification to active page tables mandates an immediate synchronization barrier:
```c
sfence_vma();
```
- **Eviction:** After clearing `PTE_V` and writing `ENCODE_SWAP_PTE`, `sfence_vma()` guarantees subsequent memory references will trap instead of hitting stale TLB entries.
- **Reference Bit Sweeping:** When clearing `*pte &= ~PTE_A` in Clock algorithms, `sfence_vma()` ensures the MMU does not rely on a cached `A=1` entry and sets `PTE_A` in the page table on subsequent access.
- **Swap-In:** After writing `*pte = PA2PTE(mem) | perm | PTE_V`, `sfence_vma()` guarantees the hardware MMU fetches the new valid PTE mapping.

---

## 5. Per-Process Resident Set Management

xv6 enforces a per-process resident physical page limit defined in [src/kernel/param.h](file:///data/rspace/codespace/projects/xv6-swap/src/kernel/param.h#L19):

```c
#define MAX_PHY_PAGES 16
```

Tracking structures are embedded directly in `struct proc` in [src/kernel/proc.h](file:///data/rspace/codespace/projects/xv6-swap/src/kernel/proc.h#L102-L106):
```c
struct proc {
  ...
  uint64 resident_pages[MAX_PHY_PAGES]; // Virtual addresses of mapped user pages
  uint8  access_history[MAX_PHY_PAGES]; // Reference history shift registers (Adaptive Clock)
  int    clock_hand;                    // Scanning hand / FIFO head index
  int    resident_count;                // Number of currently resident physical pages (0..16)
  ...
};
```

- When a new page is mapped (`vmfault`, `sbrk`, or `exec`), `fifo_enqueue(p, va)` appends the virtual address and initializes `access_history[tail] = 0x80`.
- When a victim page is selected, `fifo_remove(p, va)` compacts the remaining array elements and decrements `resident_count`.

---

## 6. Replacement Policy Specifications

The replacement engine separates the **mechanism** (I/O transfer, bitmap allocation, PTE encoding, TLB shootdown in `swap_evict_page()`) from the **policy** (`select_victim_page()`).

Compile-time selection is controlled via `src/Makefile`:
```make
# Options: FIFO, CLOCK, ENHANCED_CLOCK, ADAPTIVE_CLOCK
POLICY ?= ADAPTIVE_CLOCK
```

```
           +---------------------------------------------+
           |           swap_evict_page(proc)             |
           +----------------------+----------------------+
                                  |
                                  v
           +---------------------------------------------+
           |             select_victim_page()            |
           +---------------------------------------------+
                     |             |              |
     +---------------+      +------+------+       +---------------+
     |                      |             |                       |
     v                      v             v                       v
[FIFO Policy]        [Clock Policy] [Enhanced Clock]     [Adaptive Clock]
Queue Head Eviction  1-bit Second   4-Class (A, D)       8-bit Shift Reg
(Order of Arrival)   Chance Sweep   Multi-pass Search    Recency + Dirty
```

---

### Policy 0: First-In, First-Out (FIFO)
- **Concept:** Oldest resident page frame is evicted first, regardless of access frequency or recency.
- **Algorithm:**
  1. Inspect queue head at `p->clock_hand` (typically index 0 in the compacted queue).
  2. Select `va = p->resident_pages[p->clock_hand]`.
  3. Evict page and remove from resident list.
- **Characteristics:** Suffers from Belady's Anomaly and thrashing on cyclical reference patterns. Serves as our experimental control baseline.

---

### Policy 1: Second-Chance Clock
- **Concept:** Uses the hardware-managed `PTE_A` (Accessed) bit to give recently referenced pages a second chance.
- **Algorithm:**
  1. Let `attempts = 0`, `max_attempts = 2 * p->resident_count + 1`.
  2. For candidate index `idx = p->clock_hand`:
     - Advance `p->clock_hand = (p->clock_hand + 1) % p->resident_count`.
     - Inspect PTE: `pte = walk(p->pagetable, va, 0)`.
     - If `*pte & PTE_A`:
       - Clear reference bit: `*pte &= ~PTE_A`.
       - Flush TLB: `sfence_vma()`.
       - Grant second chance; continue loop.
     - Else (`PTE_A == 0`):
       - Select `va` as victim. Return immediately.
  3. If loop exhausts `max_attempts`, evict candidate at original hand.
- **Theoretical Bound:** At most two complete revolutions around the circular buffer ($2N$).

---

### Policy 2: Enhanced Clock (4-Class Accessed + Dirty)
- **Concept:** Acknowledges the asymmetry of I/O cost between clean pages (discardable if backed elsewhere, or low I/O overhead) and dirty pages (requiring expensive disk writes).
- **Classification Matrix:**
  - **Class 0 `(0, 0)`:** Neither recently accessed nor dirty $\rightarrow$ *Best victim*.
  - **Class 1 `(0, 1)`:** Not recently accessed, but dirty $\rightarrow$ *Second best victim* (requires writeback).
  - **Class 2 `(1, 0)`:** Recently accessed, but clean $\rightarrow$ *Third best victim* (may be accessed again).
  - **Class 3 `(1, 1)`:** Recently accessed and dirty $\rightarrow$ *Worst victim*.
- **Algorithm (Up to 4 Passes):**
  - **Step 1:** Scan from `clock_hand` for `A == 0 && D == 0`. Do **not** modify Accessed bits. If found, evict and advance hand.
  - **Step 2:** Scan for `A == 0 && D == 1`. For every page examined with `A == 1`, clear `PTE_A` (`*pte &= ~PTE_A; sfence_vma()`). If Class 1 candidate is found, evict and advance hand.
  - **Step 3:** Repeat Step 1 looking for Class 0 (`A == 0 && D == 0`). (Pages demoted in Step 2 may now qualify).
  - **Step 4:** Repeat Step 2 looking for Class 1 (`A == 0 && D == 1`). If no victim found, evict at original hand.

---

### Policy 3: Adaptive Clock (Sampled Access History & Aging)
- **Concept:** Combines pseudo-LRU 8-bit decaying shift registers with a dirty write-back penalty bias.
- **Data Structure:**
  - `uint8 access_history[MAX_PHY_PAGES]`: Shift register storing access history over the last 8 eviction epochs.
  - Initialized to `0x80` (`10000000b`) on allocation/enqueue, giving newly loaded pages initial residency protection.
- **Aging & Scoring Transformation:**
  On eviction demand, the algorithm performs a single deterministic $O(N)$ pass over all resident pages:
  $$\text{history}_i = (\text{history}_i \gg 1) \mid (\text{Accessed}_i \ll 7)$$
  $$\text{PTE\_A}_i \leftarrow 0; \quad \texttt{sfence\_vma}()$$
  $$\text{Score}_i = \text{history}_i + (\text{Dirty}_i \,?\, 32 : 0)$$
- **Victim Selection:**
  - Select page with the minimum $\text{Score}_i$.
  - Low history indicates cold/infrequently accessed frames.
  - The $+32$ bias protects dirty frames against clean frames of comparable recency, saving swap disk writes.
  - Advance `clock_hand = (victim_idx + 1) % resident_count`.
- **Complexity:** Exactly $N = 16$ inspections per eviction. Deterministic, loop-free, and cache-friendly.

---

## 7. Subsystem Verification & Statistics

The swapping subsystem tracks kernel-wide diagnostic counters via `struct swap_stats` in [src/kernel/swap.h](file:///data/rspace/codespace/projects/xv6-swap/src/kernel/swap.h#L11-L20):
```c
struct swap_stats {
  uint64 fault_total;       // Total page faults handled by vmfault()
  uint64 fault_swapin;      // Page faults requiring disk read from swap
  uint64 fault_lazy;        // Page faults satisfied via lazy zeroed allocation
  uint64 swap_reads;        // Total 4KB page read operations from disk
  uint64 swap_writes;       // Total 4KB page write operations to disk
  uint64 evict_clean;       // Total clean pages evicted
  uint64 evict_dirty;       // Total dirty pages evicted
  uint64 scan_inspections;  // Total candidate frames examined across clock sweeps
};
```
User-space benchmarks retrieve snapshot statistics via `sys_swapstats()`.
