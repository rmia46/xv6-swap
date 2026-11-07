// clang-format off
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "buf.h"
#include "swap.h"
// clang-format on

struct swap_stats swap_stats;
struct spinlock swap_stats_lock;

// Swap slot allocation bitmap
// 1 bit per slot: 0 = free, 1 = in use
static uchar swap_bitmap[NSWAPSLOTS / 8 + 1];
static struct spinlock swap_lock;

void swap_init(void) {
  initlock(&swap_lock, "swap");
  initlock(&swap_stats_lock, "swap_stats");
  memset(swap_bitmap, 0, sizeof(swap_bitmap));
  memset(&swap_stats, 0, sizeof(swap_stats));
}

// Allocate a swap slot. Returns slot number (0 .. NSWAPSLOTS-1) or -1 if full.
int swap_alloc(void) {
  acquire(&swap_lock);
  for (int i = 0; i < NSWAPSLOTS; i++) {
    int byte = i / 8;
    int bit = i % 8;
    if ((swap_bitmap[byte] & (1 << bit)) == 0) {
      swap_bitmap[byte] |= (1 << bit);
      release(&swap_lock);
      return i;
    }
  }
  release(&swap_lock);
  return -1;
}

// Free a swap slot.
void swap_free(int slot) {
  if (slot < 0 || slot >= NSWAPSLOTS)
    panic("swap_free: invalid slot");

  acquire(&swap_lock);
  int byte = slot / 8;
  int bit = slot % 8;
  if ((swap_bitmap[byte] & (1 << bit)) == 0) {
    release(&swap_lock);
    panic("swap_free: double free");
  }
  swap_bitmap[byte] &= ~(1 << bit);
  release(&swap_lock);
}

// Write a 4KB physical page to disk slot.
// 1 page = BLOCKS_PER_PAGE (4) blocks.
int swap_write(int slot, char *src_pa) {
  if (slot < 0 || slot >= NSWAPSLOTS)
    return -1;

  uint start_block = SWAPSTART + slot * BLOCKS_PER_PAGE;
  for (int b = 0; b < BLOCKS_PER_PAGE; b++) {
    struct buf *buf = bread(ROOTDEV, start_block + b);
    memmove(buf->data, src_pa + b * BSIZE, BSIZE);
    bwrite(buf);
    brelse(buf);
  }

  acquire(&swap_stats_lock);
  swap_stats.swap_writes++;
  release(&swap_stats_lock);

  return 0;
}

// Read a 4KB physical page from disk slot into dst_pa.
int swap_read(int slot, char *dst_pa) {
  if (slot < 0 || slot >= NSWAPSLOTS)
    return -1;

  uint start_block = SWAPSTART + slot * BLOCKS_PER_PAGE;
  for (int b = 0; b < BLOCKS_PER_PAGE; b++) {
    struct buf *buf = bread(ROOTDEV, start_block + b);
    memmove(dst_pa + b * BSIZE, buf->data, BSIZE);
    brelse(buf);
  }

  acquire(&swap_stats_lock);
  swap_stats.swap_reads++;
  release(&swap_stats_lock);

  return 0;
}

void swap_stats_get(struct swap_stats *dst) {
  acquire(&swap_stats_lock);
  memmove(dst, &swap_stats, sizeof(struct swap_stats));
  release(&swap_stats_lock);
}

void swap_stats_reset(void) {
  acquire(&swap_stats_lock);
  memset(&swap_stats, 0, sizeof(struct swap_stats));
  release(&swap_stats_lock);
}

// Eviction policy: Select victim resident page from process p.
// Returns victim virtual address on success, or 0 on failure.
static uint64 select_victim_page(struct proc *p) {
#if CURRENT_SWAP_POLICY == SWAP_POLICY_CLOCK
  if (p->resident_count <= 0)
    return 0;

  int original_hand = p->clock_hand;
  int attempts = 0;
  // Maximum 2 full loops across all resident pages: in loop 1, Accessed bits get cleared.
  // In loop 2, at least one page with PTE_A==0 is guaranteed to be chosen.
  int max_attempts = 2 * p->resident_count + 1;

  while (attempts < max_attempts) {
    int idx = p->clock_hand;
    uint64 va = p->resident_pages[idx];

    // Advance clock hand to next resident page
    p->clock_hand = (p->clock_hand + 1) % p->resident_count;
    attempts++;

    // Count inspection
    acquire(&swap_stats_lock);
    swap_stats.scan_inspections++;
    release(&swap_stats_lock);

    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte == 0 || !(*pte & PTE_V)) {
      // Unmapped or invalid entry; evict it
      fifo_remove(p, va);
      return va;
    }

    if (*pte & PTE_A) {
      // Second chance: clear Accessed bit and flush TLB so MMU will set it again on access
      *pte &= ~PTE_A;
      sfence_vma();
    } else {
      // Found candidate with Accessed == 0
      fifo_remove(p, va);
      return va;
    }
  }

  // Fallback: evict at original_hand
  int fallback_idx = original_hand % p->resident_count;
  uint64 va = p->resident_pages[fallback_idx];
  fifo_remove(p, va);
  return va;

#else // SWAP_POLICY_FIFO
  if (p->resident_count <= 0)
    return 0;

  // Scan inspection count
  acquire(&swap_stats_lock);
  swap_stats.scan_inspections++;
  release(&swap_stats_lock);

  // Dequeue oldest page
  uint64 va = p->resident_pages[p->clock_hand];
  fifo_remove(p, va);
  return va;
#endif
}

// Replacement policy: Evict one resident page from process p.
// Preconditions: called when p->resident_count >= MAX_PHY_PAGES.
// Returns 0 on success, -1 on failure.
int swap_evict_page(struct proc *p) {
  uint64 va = select_victim_page(p);
  if (va == 0)
    return -1;

  pte_t *pte = walk(p->pagetable, va, 0);
  if (pte == 0 || !(*pte & PTE_V)) {
    // Page was somehow unmapped or invalid
    return -1;
  }

  uint64 pa = PTE2PA(*pte);
  uint64 flags = PTE_FLAGS(*pte);

  // Allocate swap slot
  int slot = swap_alloc();
  if (slot < 0) {
    printk("swap_evict_page: out of swap space\n");
    return -1;
  }

  // Write page to swap
  if (swap_write(slot, (char *)pa) < 0) {
    swap_free(slot);
    return -1;
  }

  // Track dirty/clean eviction metrics
  acquire(&swap_stats_lock);
  if (flags & PTE_D) {
    swap_stats.evict_dirty++;
  } else {
    swap_stats.evict_clean++;
  }
  release(&swap_stats_lock);

  // Clear PTE_V, set PTE_S, record swap slot, keep user access permission flags
  uint64 perm = (flags & (PTE_R | PTE_W | PTE_X | PTE_U));
  *pte = ENCODE_SWAP_PTE(slot, perm);

  // Flush TLB
  sfence_vma();

  // Free the physical frame
  kfree((void *)pa);

  return 0;
}
