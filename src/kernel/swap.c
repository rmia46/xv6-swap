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

// FIFO replacement policy: Evict the oldest resident page from process p.
// Preconditions: called when p->fifo_count >= MAX_PHY_PAGES.
// Returns 0 on success, -1 on failure.
int swap_evict_page(struct proc *p) {
  if (p->fifo_count <= 0)
    return -1;

  // Scan inspection count
  acquire(&swap_stats_lock);
  swap_stats.scan_inspections++;
  release(&swap_stats_lock);

  // Dequeue the oldest page from FIFO
  uint64 va = p->fifo_pages[p->fifo_head];
  p->fifo_head = (p->fifo_head + 1) % MAX_PHY_PAGES;
  p->fifo_count--;

  pte_t *pte = walk(p->pagetable, va, 0);
  if (pte == 0 || !(*pte & PTE_V)) {
    // Page was somehow unmapped or invalid; continue eviction
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
