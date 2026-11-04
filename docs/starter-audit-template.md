# Starter Code Audit Checklist & Report Template

Use this document to inspect, audit, and document the instructor-provided starter code upon delivery.

---

## 1. Baseline Metadata
- **Date Received:** YYYY-MM-DD
- **Source Origin:** (e.g., zip file `p4-starter.zip` / GitHub repository URL)
- **Base Architecture:** [ ] x86 (32-bit) / [ ] RISC-V (64-bit) / [ ] Other: _____
- **Baseline Git Hash (if applicable):**
- **QEMU Version Tested:**

---

## 2. Kernel Subsystems Audit

Check off the status of each subsystem in the received starter kit:

### A. Memory Management (`kalloc.c`, `vm.c`, `mmu.h`)
- [ ] Physical allocator: standard linked-list free page stack or modified?
- [ ] PTE representation: Which bits are used for Present, Read/Write, User, Accessed, Dirty?
- [ ] Is there an existing software-defined bit representing "Swapped / Page-Out"?
- [ ] Is there an active page tracking data structure (e.g. core map / inverted page table)?

### B. Trap & Fault Handling (`trap.c`, `traps.h`)
- [ ] Does the page-fault vector/handler exist (`T_PGFLT`)?
- [ ] Is `CR2` (on x86) or `stval` (on RISC-V) read to extract the faulting virtual address?
- [ ] Does the kernel panic on page fault, or route into a demand-paging handler?

### C. Swap Device / Backing Store (`ide.c`, `virtio_disk.c`, `bio.c`)
- [ ] Is swap storage mapped to a dedicated partition, raw disk blocks, or a swap file?
- [ ] Are synchronous or asynchronous read/write interfaces provided for swapping?
- [ ] Is there a swap bitmap or allocation table already implemented?

### D. Policy Layer
- [ ] Is a page replacement policy hook provided (e.g., `select_victim_page()`)?
- [ ] Does an initial policy exist (FIFO, random, etc.)?

---

## 3. Audit Findings & Notes
*(Record any unexpected modifications, starter bugs, or course constraints here)*

- **Finding 1:**
- **Finding 2:**

---

## 4. Immediate Action Items
- [ ] Commit pristine starter files to branch `starter-baseline`.
- [ ] Verify `make qemu` boots without warnings.
- [ ] Define initial architecture decision record (ADR) based on audit results.
