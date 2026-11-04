# CSE323 Project P4 --- Page Replacement with Swap

## Detailed Implementation and Research Plan

**Course:** CSE323 --- Operating Systems\
**Project:** P4 --- Page Replacement with Swap\
**Group size:** 4\
**Status:** Working plan --- to be revised after inspecting the
instructor-provided starter code

------------------------------------------------------------------------

## 1. Project overview

The goal of P4 is to extend the selected xv6 environment with a swap
mechanism (if one is not already provided), then investigate
page-replacement policies that decide which resident user page should
leave physical memory when a new frame is required.

The project has two distinct parts:

1.  **Swapping mechanism:** the machinery that preserves a page's
    contents outside RAM and restores them when needed.
2.  **Replacement policy:** the decision procedure that selects a
    resident page as the next eviction candidate.

The mechanism must be correct and kept as constant as practical during
policy comparisons. The research work then compares a simple baseline
with more informed policies using controlled workloads and repeatable
measurements.

The project should not assume that upstream MIT xv6 already contains a
complete swap implementation. The group must first identify the exact
xv6 repository, branch/commit, architecture, and any course-specific
modifications. If the supplied starter already implements some or all of
swapping, reuse and audit that implementation rather than rebuilding it.

## 2. Scope and assumptions

### 2.1 Target environment

The likely starting point is MIT's public xv6 for RISC-V (`xv6-riscv`),
but this is provisional until the instructor or TA confirms the starter
kit.

Record these details before implementation:

-   Repository URL and exact commit hash.
-   Branch or release/tag, if applicable.
-   RISC-V target and QEMU version/configuration.
-   Available physical-memory configuration.
-   Existing disk image and device layout.
-   Whether the starter contains swap-space support, page metadata,
    counters, workloads, or policy hooks.
-   Any instructor-provided restrictions on changing mechanisms versus
    policies.

Do not assume code written for another xv6 version (for example, an
older x86 edition) will apply directly.

### 2.2 Scope boundary

The project should change the **page-eviction policy**, not unrelated
kernel subsystems. Some mechanism work may be necessary if the starter
lacks swapping, but it should be treated as enabling infrastructure.

Out of scope unless explicitly required or approved:

-   Replacing the whole virtual-memory subsystem.
-   Implementing a production-quality general-purpose swap system.
-   Changing the file system or block layer beyond what is needed to
    reserve/use swap storage.
-   Adding sophisticated page sharing, copy-on-write, or memory
    compression solely for this project.
-   Using information unavailable to the policy under the project rules.

The policy should use only information permitted by the assignment, such
as hardware-provided accessed and dirty bits. Any additional metadata
must be justified and used consistently across policies where
appropriate.

## 3. Initial repository and starter-kit audit

**Do this before designing the final implementation.** The current draft
does not establish whether the starter already contains swap support.

### 3.1 Inspect the repository

Start by building and booting the untouched starter. Save the exact
commands and output.

Inspect at least:

-   `kernel/kalloc.c` --- physical-page allocation and free-list
    management.
-   `kernel/vm.c` --- page-table creation, mapping, unmapping, and
    user-memory operations.
-   `kernel/trap.c` --- user page-fault handling.
-   `kernel/proc.c` --- process creation, fork, exit, and cleanup.
-   `kernel/defs.h` and relevant headers --- interfaces and PTE
    definitions.
-   RISC-V definitions in `kernel/riscv.h` or the equivalent file.
-   Disk, buffer-cache, and file-system code if swap storage uses the
    existing block device.

Search the entire tree for terms such as `swap`, `evict`, `pageout`,
`pagein`, `PTE_A`, `PTE_D`, `uvmunmap`, and `uvmcopy`.

### 3.2 Classify what already exists

Complete this checklist and keep the results in the project repository:

  ------------------------------------------------------------------------
  Capability         Present in        Evidence / file   Work remaining
                     starter?          and function      
  ------------------ ----------------- ----------------- -----------------
  Swap area or       Unknown                             
  reserved swap                                          
  blocks                                                 

  Swap-slot          Unknown                             
  allocator and                                          
  release                                                

  Per-page/frame     Unknown                             
  metadata                                               

  Resident-page      Unknown                             
  tracking                                               

  Victim-selection   Unknown                             
  hook                                                   

  Eviction and       Unknown                             
  page-out I/O                                           

  Swap-entry PTE     Unknown                             
  encoding                                               

  Swap-in page-fault Unknown                             
  handling                                               

  Process-exit swap  Unknown                             
  cleanup                                                

  Fork handling for  Unknown                             
  swapped pages                                          

  Page-fault / I/O   Unknown                             
  counters                                               

  Workload and       Unknown                             
  scoring tools                                          

  AI-use logging     Unknown                             
  tool                                                   
  ------------------------------------------------------------------------

### 3.3 Establish a clean baseline

Before changing code:

1.  Build xv6 using the provided instructions.
2.  Boot it in the expected QEMU configuration.
3.  Run the supplied tests, if any.
4.  Record baseline memory size and kernel configuration.
5.  Save a clean Git commit.
6.  Confirm how the TA expects the project to be built and evaluated.

**Deliverable:** a short `STARTER_AUDIT.md` describing what is supplied,
what is missing, and what the group must implement.

## 4. System design: mechanism before policy

### 4.1 High-level flow

When a user-page allocation needs a physical frame:

``` text
User-page allocation request
          |
          v
Try to obtain a free physical frame
          |
       +--+--+
       |     |
    Found   None
       |     |
       |     v
       |  Request eligible page reclamation
       |     |
       |     v
       |  Select resident user page
       |     |
       |     v
       |  Preserve contents if required
       |     |
       |     v
       |  Remove old resident mapping safely
       |     |
       |     v
       |  Reclaim physical frame
       |     |
       +-----+
          |
          v
      Use frame
```

On a later access to a swapped page:

``` text
User accesses virtual address
          |
          v
       Page fault
          |
          v
Is this a valid swap entry?
       /       \
     No         Yes
     |           |
Normal fault   Obtain a frame
handling          |
                  v
             Read page from swap
                  |
                  v
             Restore PTE and metadata
                  |
                  v
             Invalidate stale translation if needed
                  |
                  v
             Resume faulting instruction
```

This is a conceptual flow, not a claim that these exact hooks already
exist in the starter.

### 4.2 Keep the allocator and eviction responsibilities separate

Do not put the entire eviction and disk-I/O procedure blindly inside the
lowest-level `kalloc()` routine.

`kalloc()` may be used for page-table pages and other kernel
allocations, not only user data pages. Reclaiming user memory to satisfy
arbitrary kernel allocations can create recursion, lock-order problems,
or unexpected blocking.

A safer design is to make the user-memory allocation/reclamation path
explicitly request a reclaimable user frame. The exact hook depends on
the supplied xv6 code and assignment constraints.

Design requirements:

-   Distinguish user data frames from kernel-owned pages and page-table
    pages.
-   Do not evict a frame that is pinned, being used for I/O, or
    otherwise temporarily ineligible.
-   Do not perform blocking disk I/O while holding allocator or
    page-table locks that the I/O path may need.
-   Define lock ordering and ownership transitions.
-   Prevent two CPUs from evicting or swapping in the same page
    concurrently.
-   Ensure the eviction path cannot recursively request another reclaim
    operation without a deliberate, safe design.

### 4.3 Frame and page metadata

The policy needs to identify resident candidate pages. The mechanism
also needs to find the mapping associated with a selected physical
frame.

A possible starting point is per-frame metadata, but the final structure
must be derived from the starter's ownership and mapping model.

Metadata may need to represent:

-   Whether a frame is allocated and eligible for eviction.
-   Whether it contains a user page or kernel/page-table memory.
-   Owning process or mapping identity.
-   Virtual address (or a reverse-mapping structure).
-   Pin/in-use state.
-   Backing-store state and swap-slot identity.
-   Policy-specific history, if any.

Do not assume one `(pid, virtual address)` pair is always enough. Fork,
shared mappings (if supported), and concurrent address-space changes can
complicate ownership. Decide whether the project supports shared
physical pages or explicitly constrains the implementation to private
user pages.

Keep policy-specific fields separate from essential mechanism metadata
where practical.

### 4.4 Backing-store state is not the same as the dirty bit

A dirty bit indicates whether a page has been written since the relevant
mapping state was established. It does **not** by itself say whether the
page's contents can be reconstructed.

The mechanism must know the page's backing source. At a minimum, reason
about these cases:

  -----------------------------------------------------------------------
  Page category                       Possible eviction treatment
  ----------------------------------- -----------------------------------
  Clean file-backed page              May be discarded and re-read from
                                      its original file, if the
                                      implementation can reliably
                                      reconstruct it

  Anonymous page known to be all zero May be discarded and recreated as
                                      zero, if this state is tracked
                                      correctly

  Anonymous page with valid swap      May be discardable if the existing
  backing                             backing copy is still valid

  Anonymous page with no recoverable  Contents must be preserved before
  backing                             its frame is reused
  -----------------------------------------------------------------------

For the simplest reliable implementation, the group may choose to swap
every evicted user page, even if a clean page could theoretically be
reconstructed. This is easier to reason about but can increase I/O. If
choosing this simplification, document it and measure its consequences.
Do not claim that dirty pages are the only pages requiring preservation.

Define when swap slots are allocated, reused, retained after swap-in,
overwritten, and released. Avoid freeing a slot while a PTE or another
metadata structure can still refer to it.

### 4.5 Swap-space layout and slot lifecycle

Decide how swap storage is represented in the provided environment.
Possible approaches depend on the disk and file-system support in the
starter; do not select a layout until the TA's environment and
constraints are known.

Document:

-   Which disk blocks or device region are reserved for swap.
-   How the region is protected from ordinary file-system allocation.
-   Swap-slot size (normally one page) and valid slot range.
-   Allocation strategy and full-swap behavior.
-   Read/write interface and error handling.
-   Whether a page retains its slot while resident or releases it after
    successful swap-in.
-   Cleanup on process exit and address-space destruction.

Never allow swap storage to overlap file-system data or other disk
structures.

### 4.6 PTE encoding and fault classification

A non-valid PTE is not automatically a swap entry. It may represent an
unmapped address, lazy allocation, or another special state.

Define an explicit, version-compatible encoding for swap entries.
Investigate the RISC-V PTE software-reserved bits (RSW) and the
starter's existing conventions before choosing the encoding. Do not
assume that arbitrary upper PTE bits are available.

The fault handler must distinguish at least:

1.  A valid resident mapping.
2.  A valid swapped-out page.
3.  A legitimate lazy-allocation case, if supported.
4.  An unmapped or otherwise invalid address.
5.  Other fault causes that must retain their existing behavior.

Validate the faulting address against the process's permitted
virtual-memory range and mapping state before attempting swap-in.

### 4.7 Swap-in sequence

Specify and test the complete transition:

1.  Read the faulting virtual address and locate its page-table entry.
2.  Confirm that the entry is a valid swap entry belonging to the
    current address space.
3.  Reserve the page/slot state so another CPU cannot perform a
    conflicting operation.
4.  Obtain a physical frame, invoking safe reclamation if allowed and
    necessary.
5.  Read the page from the correct swap slot.
6.  Restore the PTE with the correct permissions and resident state.
7.  Update frame metadata, page ownership, and swap-slot bookkeeping.
8.  Perform the required TLB invalidation/fence for the target address
    and address space.
9.  Release locks and resume the faulting instruction.

Handle read errors and allocation failure without installing a partially
initialized mapping. Avoid infinite reclaim/swap-in loops. Preserve
original user/supervisor and read/write/execute permissions.

### 4.8 Lifecycle and less-common paths

The following paths are easy to overlook and can cause corruption if
they are not addressed:

-   **Process exit:** release resident frames and all swap slots owned
    by its address space.
-   **Address-space destruction:** do not treat swap PTEs as physical
    addresses to free.
-   **Fork (`uvmcopy()` or equivalent):** define how swapped pages are
    copied. The simplest correct implementation may swap the parent's
    page in before copying, if compatible with the starter and project
    constraints. More advanced shared-slot/reference-count designs are
    optional, not assumed.
-   **Unmapping (`uvmunmap()` or equivalent):** handle resident mappings
    and swap entries differently.
-   **Page-table freeing (`freewalk()` or equivalent):** ensure non-leaf
    and special leaf entries are handled safely.
-   **Growth and shrinkage:** ensure `sbrk`/heap changes do not leave
    stale swap entries.
-   **Kernel allocations:** ensure kernel and page-table pages are never
    selected as user-page victims.
-   **Concurrency:** handle process exit, faults, and eviction racing on
    the same page.

These paths should be added to the test plan, not left until the end.

## 5. Replacement policies to compare

Use the same swapping mechanism, workloads, memory limit, and
measurement instrumentation for every policy. Change only the
victim-selection policy and any explicitly identified policy-specific
metadata.

### 5.1 Baseline: FIFO

FIFO selects the resident, eligible page that has been in memory the
longest.

Specify:

-   How insertion order is recorded.
-   How evicted/freed pages are removed from the queue.
-   How ineligible or pinned pages are skipped.
-   How the queue is updated on swap-in.
-   How the implementation handles an empty candidate set.

FIFO is a useful simple baseline, not necessarily a strong policy.

### 5.2 Policy 1: Clock (Second Chance)

Clock maintains a circular hand over eligible resident pages.

Conceptual procedure:

1.  Inspect the page at the hand.
2.  If its accessed bit is set, clear it and give the page another
    chance.
3.  If its accessed bit is clear, select it as a candidate.
4.  Advance the hand consistently after each inspection/selection.

The exact behavior must be specified for pinned pages, pages undergoing
I/O, and a full scan in which every page was recently accessed.

Verify that the target RISC-V/QEMU setup actually updates accessed bits
as expected. Confirm how the kernel clears the bit and performs the
required TLB invalidation/fence so that subsequent accesses can be
observed correctly.

### 5.3 Policy 2: Enhanced Clock (accessed + dirty)

Enhanced Clock considers both recent use and the cost of preserving a
page.

A common conceptual preference order is:

    Accessed (A)   Dirty (D) Preference
  -------------- ----------- --------------------------------------
               0           0 First: not recently accessed, clean
               0           1 Second: not recently accessed, dirty
               1           0 Third: recently accessed, clean
               1           1 Fourth: recently accessed, dirty

This table is a preference model, not a complete scan algorithm. The
implementation must specify how it clears A bits, revisits candidates,
handles a scan with all A bits set, and behaves when no clean candidate
exists.

Also consider backing-store state. A clean anonymous page without
another valid copy cannot simply be discarded. The policy's dirty-bit
preference must not override the mechanism's data-preservation
requirements.

### 5.4 Policy 3: Adaptive Clock / sampled access history (candidate)

The original draft proposed an AI-assisted working-set approximation.
Make the algorithm concrete before treating it as a project deliverable.

A practical candidate is a Clock-based policy with a small sampled
history:

-   At fixed sampling intervals, inspect each eligible resident page's
    accessed bit.
-   Shift the sampled bit into a short per-page history.
-   Clear the accessed bit using the verified architecture-appropriate
    procedure.
-   Prefer pages whose recent history indicates little observed use.
-   Consider dirty/backing-store cost as a secondary criterion.
-   Bound scan work and define a fallback if all candidates appear
    active.

Illustrative history update:

``` c
history = (history << 1) | accessed;
```

A history of `00000000` means the page was not observed accessed in the
last eight sampling intervals. It does not prove the page was never used
between samples.

The group must define:

-   Sampling interval and how it is triggered.
-   History width.
-   How history is initialized on allocation and swap-in.
-   Whether the history is reset or aged on phase changes.
-   Candidate scoring and tie-breaking.
-   Maximum scan effort and fallback behavior.

This policy is a research candidate, not a guaranteed improvement. It
should only be implemented after FIFO, Clock, and Enhanced Clock are
correct and measured.

### 5.5 Role of AI

AI should generate candidate **policy changes**, not silently redesign
the swapping mechanism.

For each candidate:

1.  Provide the policy code, constraints, baseline measurements, and
    relevant workload characteristics.
2.  Ask for a small, reviewable change with an explanation.
3.  Inspect the proposed code manually.
4.  Build and run correctness tests.
5.  Run the same benchmark suite.
6.  Record the prompt, full response, code revision, and results.
7.  Keep or reject the candidate based on measured evidence.

If OpenEvolve or another automated search tool is provided, first
establish a stable test harness and objective function. Ensure the
search cannot modify mechanism code or exploit workload-specific
shortcuts. Use held-out workloads to detect overfitting.

## 6. Workload and benchmark design

A policy comparison is only meaningful when workloads are reproducible
and sufficiently distinct.

First inspect the course-provided test tool. Extend it only where
necessary; do not duplicate an existing harness without reason.

### 6.1 Core workloads

  -----------------------------------------------------------------------
  Workload                Access pattern          What it tests
  ----------------------- ----------------------- -----------------------
  Sequential scan         Walk a region larger    Basic eviction behavior
                          than available RAM once and streaming pollution

  Repeated scan           Revisit a fixed region  Whether recently used
                          over multiple passes    pages are retained

  Hot/cold working set    Frequently access a hot Protection of
                          subset; occasionally    frequently used pages
                          access cold pages       

  Thrashing pressure      Active working set      Behavior under
                          exceeds available       sustained memory
                          frames                  pressure

  Mixed read/write        Mix reads and writes at Swap-write cost and
                          controlled proportions  dirty-page handling

  Hot set + cold stream   Repeatedly access hot   Resistance to scan
                          pages while streaming   pollution
                          through one-time data   

  Phase change            Switch from one hot     Adaptation to changing
                          region to a different   locality
                          region                  
  -----------------------------------------------------------------------

Do not assume a particular policy wins a workload in advance. Workload
descriptions should define the reference pattern; measured outcomes
determine the conclusion.

### 6.2 Specify workload parameters

For each workload, record:

-   Number of processes.
-   Number of virtual pages touched.
-   Available physical frames.
-   Access sequence or deterministic generation rule.
-   Number of passes/iterations.
-   Read/write ratio.
-   Hot-set size and access probability, if applicable.
-   Phase-change point, if applicable.
-   Random seed, if randomness is used.

The working-set size must be described relative to available physical
frames. For example, saying that 80% of accesses target 20% of pages is
not enough unless the total page count and RAM limit are also specified.

### 6.3 Reproducibility

Keep constant across policy runs:

-   xv6 commit and build configuration.
-   QEMU version and machine settings.
-   Physical-memory limit.
-   Swap capacity.
-   Workload input and seed.
-   Number of repetitions.
-   Measurement method.

Run each workload multiple times where execution-time variation is
meaningful. Report the median and a measure of spread (for example,
minimum/maximum or interquartile range). Do not report more precision
than the measurement method supports.

## 7. Metrics and instrumentation

Separate fault types and count events at well-defined points.

  Metric                   Definition / purpose
  ------------------------ ----------------------------------------------------
  Total user page faults   All relevant user-mode page faults
  Swap-in faults           Faults that successfully restore swapped-out pages
  Lazy-allocation faults   Kept separate from swap-in faults
  Eviction attempts        Requests to find a victim
  Successful evictions     Pages actually removed from RAM
  Swap reads               Page-sized reads from swap storage
  Swap writes              Page-sized writes to swap storage
  Clean evictions          Evictions that did not require a new swap write
  Dirty evictions          Evictions that required preservation/write-back
  Eviction scan length     Number of candidate inspections per eviction
  Execution time           End-to-end workload duration
  Swap-space utilization   Peak and final number of occupied slots

Useful derived metrics:

-   **Swap writes per eviction** = swap writes / successful evictions.
-   **Clean-eviction ratio** = clean evictions / successful evictions.
-   **Average scan length** = candidate inspections / successful
    evictions.
-   **Swap I/O volume** = (swap reads + swap writes) × page size, if
    every operation is one page.

Define counter semantics precisely. For example, a fault that begins
swap-in but fails should not be counted as a successful swap-in. Decide
whether retries are counted separately.

Do not use one ambiguous "page fault" counter to represent both lazy
allocation and swap-in.

### Instrumentation rules

-   Counter updates must be concurrency-safe.
-   Counters should not change the replacement decision.
-   Keep instrumentation identical across policies.
-   Validate counters with small hand-checkable tests.
-   Measure policy scan overhead, not just disk I/O.
-   If the course score tool defines metrics differently, follow its
    definition and document any additional metrics separately.

## 8. Correctness and stress testing

Correctness is a prerequisite for performance claims. A fast policy that
corrupts a process is not an improvement.

### 8.1 Mechanism tests

At minimum, test:

1.  Allocate more user memory than physical RAM can hold.
2.  Write a recognizable pattern to each page.
3.  Force eviction, then read the pages back and verify every value.
4.  Modify pages before eviction and verify modified contents survive.
5.  Read untouched/zero-filled pages and verify their semantics.
6.  Repeatedly swap the same page out and in.
7.  Fill swap space and verify the documented failure behavior.
8.  Exit a process with resident and swapped pages; verify resources are
    released.
9.  Fork when the address space contains swapped pages; verify the
    chosen fork behavior.
10. Grow and shrink the address space after swapping.
11. Run multiple processes under memory pressure.
12. Confirm kernel memory and page-table pages are never evicted as user
    data.
13. Exercise invalid addresses and permission violations; they must not
    be mistaken for swap entries.
14. Test simultaneous faults/evictions if the environment supports
    multiple CPUs.

Use data patterns that make corruption visible, not merely tests that
check whether a process survives.

### 8.2 Policy tests

For each policy:

-   Confirm it selects only eligible resident user pages.
-   Confirm it terminates even when many candidates are
    pinned/ineligible.
-   Confirm the hand/queue/history remains consistent after allocation,
    eviction, swap-in, unmapping, and process exit.
-   Confirm it does not leak swap slots or frames.
-   Confirm it behaves sensibly when all pages have been accessed
    recently.
-   Confirm it behaves sensibly when no clean victim exists.

### 8.3 Held-out tests

Do not tune against every workload the instructor will use. Reserve some
workload variants or parameter combinations for final evaluation.

A policy should not depend on hard-coded process IDs, known virtual
addresses, fixed benchmark order, or other workload-specific tricks.

## 9. Experimental procedure

Use a consistent procedure for each policy:

1.  Check out the same mechanism revision.
2.  Select the policy through a controlled build-time or runtime
    setting.
3.  Build from a clean state.
4.  Run correctness tests.
5.  Run the benchmark workload with fixed parameters.
6.  Save raw counters and timing output.
7.  Repeat the run as specified.
8.  Calculate summary statistics.
9.  Compare against FIFO and the other policies.
10. Record the code commit and configuration.

Suggested result table:

  -------------------------------------------------------------------------------------
  Workload     Policy        Faults   Evictions      Swap      Swap Avg. scan   Runtime
                                                    reads    writes    length 
  ------------ ---------- --------- ----------- --------- --------- --------- ---------
  Sequential   FIFO                                                           
  scan                                                                        

  Sequential   Clock                                                          
  scan                                                                        

  Sequential   Enhanced                                                       
  scan         Clock                                                          

  Sequential   Adaptive                                                       
  scan         Clock                                                          
  -------------------------------------------------------------------------------------

Keep raw run results in CSV or another machine-readable format. Do not
only save averaged values.

## 10. Ablation study and explanation

The assignment asks the group to explain which parts of its best policy
help. Plan this from the beginning.

For an adaptive policy, possible ablations include:

-   Remove sampled access history.
-   Remove dirty-bit preference.
-   Disable adaptive parameter changes.
-   Change history width.
-   Change sampling interval.
-   Replace the policy-specific score with ordinary Clock behavior.

Change one component at a time while keeping the mechanism, workload,
and other parameters fixed.

For each ablation, report:

-   What was removed or changed.
-   Why it might matter.
-   Which workloads should be affected.
-   Measured change in faults, I/O, scan length, and runtime.
-   Whether the result supports the original hypothesis.

Do not claim that a component helps merely because one run improved.
Look for consistent evidence and explain trade-offs.

## 11. Group responsibilities

All four members must understand the complete system and be able to
explain the group's work individually. Work can be divided by primary
responsibility, but interfaces and design decisions should be reviewed
together.

A suggested division:

  -----------------------------------------------------------------------
  Member                  Primary responsibility  Shared review
                                                  responsibility
  ----------------------- ----------------------- -----------------------
  Member 1                Starter audit,          Overall architecture
                          memory-management code  and code review
                          study, integration      

  Member 2                Swap-space and page     Slot lifecycle and
                          metadata mechanism      cleanup tests

  Member 3                Eviction policies and   Algorithm correctness
                          policy instrumentation  and fairness

  Member 4                Workload harness,       Reproducibility and
                          metrics, experiment     result analysis
                          automation              
  -----------------------------------------------------------------------

This is a proposed division, not a reason for members to work in
isolation. Each member should be able to explain allocation, eviction,
swap-in, page-table state, and the policy comparisons.

## 12. Development milestones and deliverables

### Milestone 0 --- Confirm the environment

-   Obtain the exact starter repository and instructions.
-   Build and boot untouched xv6.
-   Complete the starter audit.
-   Confirm what the TA expects the group to implement.

**Deliverable:** `STARTER_AUDIT.md`, clean baseline commit.

### Milestone 1 --- Finalize mechanism design

-   Define frame ownership and metadata.
-   Define swap storage and slot lifecycle.
-   Define PTE encoding and fault classification.
-   Define lock ordering and concurrency behavior.
-   Decide how fork, exit, unmap, and address-space cleanup work.

**Deliverable:** design note and reviewed interfaces.

### Milestone 2 --- Implement and validate swapping

-   Implement only the missing mechanism.
-   Start with FIFO or the simplest permitted baseline.
-   Pass data-integrity and lifecycle tests.
-   Verify swap-space exhaustion behavior.

**Deliverable:** working swapping implementation and correctness-test
log.

### Milestone 3 --- Baseline measurements

-   Implement/verify counters.
-   Build deterministic workloads.
-   Measure FIFO across all core workloads.
-   Validate measurement repeatability.

**Deliverable:** raw baseline results and benchmark documentation.

### Milestone 4 --- Standard policies

-   Implement Clock.
-   Implement Enhanced Clock.
-   Verify accessed/dirty-bit behavior on the target environment.
-   Compare policies under identical conditions.

**Deliverable:** policy implementations and comparison table.

### Milestone 5 --- AI-assisted policy search

-   Define a constrained policy interface.
-   Provide AI with baseline code, constraints, and results.
-   Generate small candidate changes.
-   Log every prompt and reply as required.
-   Test candidates and reject unsafe or overfitted changes.

**Deliverable:** AI-use log, candidate history, selected policy and
rationale.

### Milestone 6 --- Held-out evaluation and ablation

-   Freeze the selected policy.
-   Run unseen workloads.
-   Perform one-component-at-a-time ablations.
-   Explain improvements and regressions.

**Deliverable:** final evaluation data and explanation.

## 13. Risks and mitigation

  -----------------------------------------------------------------------
  Risk                    Why it matters          Mitigation
  ----------------------- ----------------------- -----------------------
  Starter differs from    The design may          Audit exact commit
  assumptions             duplicate or conflict   before coding
                          with supplied           
                          functionality           

  Incorrect PTE encoding  Invalid addresses may   Define and test
                          be misclassified as     explicit entry states
                          swapped pages           

  Incorrect page backing  Data can be lost when   Track backing state
  assumptions             clean pages are         separately from A/D
                          discarded               bits

  Locking and recursion   Eviction may deadlock   Separate reclaim path;
                          or sleep while holding  document lock order
                          unsafe locks            

  Missing reverse mapping Kernel cannot reliably  Design frame-to-mapping
                          locate the victim's PTE tracking

  Fork/exit cleanup bugs  Stale slots, leaked     Add lifecycle tests
                          frames, or corruption   early

  Hardware A/D behavior   Clock decisions may be  Verify on the exact
  differs                 unreliable              RISC-V/QEMU setup

  Workload overfitting    A policy may perform    Use held-out workloads
                          well only on known      
                          tests                   

  Noisy timing            Small differences may   Repeat runs and report
                          be meaningless          spread

  Scope expansion         Mechanism work may      Reuse starter features
                          consume all project     and freeze mechanism
                          time                    
  -----------------------------------------------------------------------

## 14. Open decisions

These decisions should remain explicitly open until the group inspects
the starter and consults the TA:

-   Which exact xv6 repository/version/commit is required?
-   Does the starter already include any swap mechanism?
-   Is swap storage expected to use a reserved disk region, a file, or a
    provided abstraction?
-   Which page types are eligible for eviction?
-   Is the project expected to support fork with swapped-out pages?
-   Is the target single-core or multi-core?
-   Does QEMU provide hardware-managed accessed/dirty bits in the
    required configuration?
-   Which metrics and workloads are already provided by the course?
-   Is OpenEvolve required, optional, or merely suggested?
-   Which code regions are groups allowed to modify?

Do not resolve these questions by assumption. Record the TA's answers
and update this plan.

## 15. References to consult

Use the versions that match the selected starter:

-   MIT xv6 RISC-V source repository:
    https://github.com/mit-pdos/xv6-riscv
-   MIT xv6 book:
    https://pdos.csail.mit.edu/6.1810/2025/xv6/book-riscv-rev5.pdf
-   *Operating Systems: Three Easy Pieces* (OSTEP), especially the
    chapters on paging, page replacement, and swapping:
    https://pages.cs.wisc.edu/\~remzi/OSTEP/

The course-provided starter and instructions take precedence over
generic examples or a different xv6 release.

------------------------------------------------------------------------

## Current guiding principle

**First make swapping correct; then compare replacement policies.**

Do not interpret an algorithm's lower fault count as an improvement if
it loses data, leaks swap slots, breaks process lifecycle operations, or
requires disproportionate scanning. Keep the mechanism stable, measure
each policy under the same conditions, and use held-out tests to check
that an AI-generated policy has learned a useful general rule rather
than a benchmark-specific trick.
