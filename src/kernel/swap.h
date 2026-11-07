#ifndef _SWAP_H_
#define _SWAP_H_

#include "param.h"
#include "types.h"

// Number of swap slots: each slot is 1 page = PGSIZE / BSIZE disk blocks
#define BLOCKS_PER_PAGE (PGSIZE / BSIZE)
#define NSWAPSLOTS (SWAPBLOCKS / BLOCKS_PER_PAGE)

struct swap_stats {
  uint64 fault_total;
  uint64 fault_swapin;
  uint64 fault_lazy;
  uint64 swap_reads;
  uint64 swap_writes;
  uint64 evict_clean;
  uint64 evict_dirty;
  uint64 scan_inspections;
};

extern struct swap_stats swap_stats;
extern struct spinlock swap_stats_lock;

void swap_init(void);
int swap_alloc(void);
void swap_free(int slot);
int swap_write(int slot, char *src_pa);
int swap_read(int slot, char *dst_pa);
void swap_stats_get(struct swap_stats *dst);
void swap_stats_reset(void);

struct proc;
// Policy hook: evict one resident page for process p
// Returns 0 on success, -1 on failure
int swap_evict_page(struct proc *p);

// Page replacement policy definitions
#define SWAP_POLICY_FIFO           0
#define SWAP_POLICY_CLOCK          1
#define SWAP_POLICY_ENHANCED_CLOCK 2
#define SWAP_POLICY_ADAPTIVE_CLOCK 3
#define SWAP_POLICY_CAR_2Q         4
#define SWAP_POLICY_FREQ_AGING     5

#if defined(POLICY_FREQ_AGING)
#define CURRENT_SWAP_POLICY SWAP_POLICY_FREQ_AGING
#elif defined(POLICY_CAR_2Q)
#define CURRENT_SWAP_POLICY SWAP_POLICY_CAR_2Q
#elif defined(POLICY_ADAPTIVE_CLOCK)
#define CURRENT_SWAP_POLICY SWAP_POLICY_ADAPTIVE_CLOCK
#elif defined(POLICY_ENHANCED_CLOCK)
#define CURRENT_SWAP_POLICY SWAP_POLICY_ENHANCED_CLOCK
#elif defined(POLICY_CLOCK)
#define CURRENT_SWAP_POLICY SWAP_POLICY_CLOCK
#elif defined(POLICY_FIFO)
#define CURRENT_SWAP_POLICY SWAP_POLICY_FIFO
#else
#define CURRENT_SWAP_POLICY SWAP_POLICY_CLOCK
#endif

#endif // _SWAP_H_
