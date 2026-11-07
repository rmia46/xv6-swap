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

#if CURRENT_SWAP_POLICY == SWAP_POLICY_DYNAMIC_BIAS
#define FAULT_WINDOW_TICKS 4
#define BASE_BIAS 24
#define RATE_FACTOR 4
#define MIN_BIAS 16
#define MAX_BIAS 64

static uint32 fault_history[FAULT_WINDOW_TICKS];
static uint last_window_tick = 0;

static void fault_tracker_reset(void) {
  memset(fault_history, 0, sizeof(fault_history));
  last_window_tick = 0;
}

static void fault_tracker_record(uint cur_tick) {
  if (cur_tick < last_window_tick) {
    memset(fault_history, 0, sizeof(fault_history));
    last_window_tick = cur_tick;
  } else if (cur_tick > last_window_tick) {
    uint elapsed = cur_tick - last_window_tick;
    if (elapsed >= FAULT_WINDOW_TICKS) {
      memset(fault_history, 0, sizeof(fault_history));
    } else {
      for (uint i = 1; i <= elapsed; i++) {
        fault_history[(last_window_tick + i) % FAULT_WINDOW_TICKS] = 0;
      }
    }
    last_window_tick = cur_tick;
  }
  fault_history[cur_tick % FAULT_WINDOW_TICKS]++;
}

static uint32 compute_dynamic_bias(void) {
  uint cur_tick;
  acquire(&tickslock);
  cur_tick = ticks;
  release(&tickslock);

  acquire(&swap_stats_lock);
  if (cur_tick < last_window_tick) {
    memset(fault_history, 0, sizeof(fault_history));
    last_window_tick = cur_tick;
  } else if (cur_tick > last_window_tick) {
    uint elapsed = cur_tick - last_window_tick;
    if (elapsed >= FAULT_WINDOW_TICKS) {
      memset(fault_history, 0, sizeof(fault_history));
    } else {
      for (uint i = 1; i <= elapsed; i++) {
        fault_history[(last_window_tick + i) % FAULT_WINDOW_TICKS] = 0;
      }
    }
    last_window_tick = cur_tick;
  }

  uint32 total_faults = 0;
  for (int i = 0; i < FAULT_WINDOW_TICKS; i++) {
    total_faults += fault_history[i];
  }
  release(&swap_stats_lock);

  uint32 dynamic_bias = BASE_BIAS + (total_faults * RATE_FACTOR) / FAULT_WINDOW_TICKS;
  if (dynamic_bias < MIN_BIAS)
    dynamic_bias = MIN_BIAS;
  if (dynamic_bias > MAX_BIAS)
    dynamic_bias = MAX_BIAS;

  return dynamic_bias;
}
#endif

void swap_record_fault(void) {
  uint cur_tick;
  acquire(&tickslock);
  cur_tick = ticks;
  release(&tickslock);

  acquire(&swap_stats_lock);
  swap_stats.fault_total++;
#if CURRENT_SWAP_POLICY == SWAP_POLICY_DYNAMIC_BIAS
  fault_tracker_record(cur_tick);
#endif
  release(&swap_stats_lock);
}

// Swap slot allocation bitmap
// 1 bit per slot: 0 = free, 1 = in use
static uchar swap_bitmap[NSWAPSLOTS / 8 + 1];
static struct spinlock swap_lock;

void swap_init(void) {
  initlock(&swap_lock, "swap");
  initlock(&swap_stats_lock, "swap_stats");
#if CURRENT_SWAP_POLICY == SWAP_POLICY_DYNAMIC_BIAS
  fault_tracker_reset();
#endif
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
#if CURRENT_SWAP_POLICY == SWAP_POLICY_DYNAMIC_BIAS
  fault_tracker_reset();
#endif
  release(&swap_stats_lock);
}

// Eviction policy: Select victim resident page from process p.
// Returns victim virtual address on success, or 0 on failure.
static uint64 select_victim_page(struct proc *p) {
#if CURRENT_SWAP_POLICY == SWAP_POLICY_DYNAMIC_BIAS
  if (p->resident_count <= 0)
    return 0;

  uint32 dynamic_bias = compute_dynamic_bias();
  uint32 min_score = 0xFFFFFFFF;
  int victim_idx = -1;

  for (int i = 0; i < p->resident_count; i++) {
    int idx = (p->clock_hand + i) % p->resident_count;
    uint64 va = p->resident_pages[idx];

    acquire(&swap_stats_lock);
    swap_stats.scan_inspections++;
    release(&swap_stats_lock);

    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte == 0 || !(*pte & PTE_V)) {
      victim_idx = idx;
      break;
    }

    uint8 accessed = (*pte & PTE_A) ? 1 : 0;
    p->access_history[idx] = (p->access_history[idx] >> 1) | (accessed << 7);
    *pte &= ~PTE_A;
    sfence_vma();

    uint32 score = (uint32)p->access_history[idx] + ((*pte & PTE_D) ? dynamic_bias : 0);
    if (score < min_score) {
      min_score = score;
      victim_idx = idx;
    }
  }

  if (victim_idx < 0)
    victim_idx = p->clock_hand % p->resident_count;

  uint64 victim_va = p->resident_pages[victim_idx];
  p->clock_hand = (victim_idx + 1) % p->resident_count;
  fifo_remove(p, victim_va);
  return victim_va;

#elif CURRENT_SWAP_POLICY == SWAP_POLICY_FREQ_AGING
  if (p->resident_count <= 0)
    return 0;

  uint32 min_score = 0xFFFFFFFF;
  int victim_idx = -1;

  for (int i = 0; i < p->resident_count; i++) {
    int idx = (p->clock_hand + i) % p->resident_count;
    uint64 va = p->resident_pages[idx];

    acquire(&swap_stats_lock);
    swap_stats.scan_inspections++;
    release(&swap_stats_lock);

    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte == 0 || !(*pte & PTE_V)) {
      // Unmapped or invalid entry; select immediately
      victim_idx = idx;
      break;
    }

    if (*pte & PTE_A) {
      if (p->ref_frequency[idx] < 15) {
        p->ref_frequency[idx]++;
      }
      p->access_history[idx] = (p->access_history[idx] >> 1) | 0x80;
      *pte &= ~PTE_A;
      sfence_vma();
    } else {
      p->ref_frequency[idx] = (p->ref_frequency[idx] * 3) / 4;
      p->access_history[idx] = (p->access_history[idx] >> 1);
    }

    uint32 score = (uint32)p->access_history[idx] +
                   ((uint32)p->ref_frequency[idx] * 16) +
                   ((*pte & PTE_D) ? 32 : 0);
    if (score < min_score) {
      min_score = score;
      victim_idx = idx;
    }
  }

  if (victim_idx < 0)
    victim_idx = p->clock_hand % p->resident_count;

  uint64 victim_va = p->resident_pages[victim_idx];
  p->clock_hand = (victim_idx + 1) % p->resident_count;
  fifo_remove(p, victim_va);
  return victim_va;

#elif CURRENT_SWAP_POLICY == SWAP_POLICY_CAR_2Q
  if (p->resident_count <= 0)
    return 0;

  #define CAR_T2_TARGET 12

  int t2_count = 0;
  for (int i = 0; i < p->resident_count; i++) {
    if (p->is_protected[i])
      t2_count++;
  }
  int t1_count = p->resident_count - t2_count;

  int victim_idx = -1;
  int fallback_dirty_idx = -1;

  // Step 1: If T1 is not empty and T2 has not exceeded target capacity limit, sweep T1
  if (t1_count > 0 && t2_count < CAR_T2_TARGET) {
    int attempts = 0;
    int max_attempts = 2 * p->resident_count + 1;

    while (attempts < max_attempts && victim_idx < 0) {
      int idx = p->clock_hand;
      p->clock_hand = (p->clock_hand + 1) % p->resident_count;
      attempts++;

      // Skip pages in T2
      if (p->is_protected[idx])
        continue;

      acquire(&swap_stats_lock);
      swap_stats.scan_inspections++;
      release(&swap_stats_lock);

      uint64 va = p->resident_pages[idx];
      pte_t *pte = walk(p->pagetable, va, 0);
      if (pte == 0 || !(*pte & PTE_V)) {
        victim_idx = idx;
        break;
      }

      if (*pte & PTE_A) {
        // Accessed: clear hardware bit
        *pte &= ~PTE_A;
        sfence_vma();

        // Dirty pages are expensive to write out: promote to T2 to avoid swap writes.
        // Clean pages are promoted if re-referenced, otherwise kept in T1 for cheap eviction.
        int should_promote = 0;
        if (*pte & PTE_D) {
          should_promote = 1;
        } else {
          p->access_history[idx]++;
          if (p->access_history[idx] >= 2)
            should_promote = 1;
        }

        if (should_promote) {
          p->is_protected[idx] = 1;
          p->access_history[idx] = 0;
          t2_count++;
          t1_count--;

          // If T2 reached target capacity or T1 is empty, switch to T2 sweep
          if (t2_count >= CAR_T2_TARGET || t1_count == 0)
            break;
        }
        continue;
      }

      // PTE_A == 0: Candidate victim in T1. Prefer clean pages.
      if (!(*pte & PTE_D)) {
        victim_idx = idx;
        break;
      } else {
        if (fallback_dirty_idx < 0)
          fallback_dirty_idx = idx;
      }

      if (attempts >= p->resident_count && fallback_dirty_idx >= 0) {
        victim_idx = fallback_dirty_idx;
        break;
      }
    }

    if (victim_idx < 0 && fallback_dirty_idx >= 0) {
      victim_idx = fallback_dirty_idx;
    }
  }

  // Step 2: If no victim found in T1 (or T1 is empty, or T2 exceeded target capacity), sweep T2
  if (victim_idx < 0) {
    int fallback_dirty_t2 = -1;
    int attempts = 0;
    int max_attempts = 2 * p->resident_count + 1;

    while (attempts < max_attempts && victim_idx < 0) {
      int idx = p->clock_hand;
      p->clock_hand = (p->clock_hand + 1) % p->resident_count;
      attempts++;

      // Skip pages in T1
      if (!p->is_protected[idx])
        continue;

      acquire(&swap_stats_lock);
      swap_stats.scan_inspections++;
      release(&swap_stats_lock);

      uint64 va = p->resident_pages[idx];
      pte_t *pte = walk(p->pagetable, va, 0);
      if (pte == 0 || !(*pte & PTE_V)) {
        victim_idx = idx;
        break;
      }

      if (*pte & PTE_A) {
        // Second chance in T2
        *pte &= ~PTE_A;
        sfence_vma();
      } else {
        // PTE_A == 0: Candidate for demotion to T1 or eviction from T2
        // Demote to T1 so a truly cold/clean page in T1 can be chosen as victim
        p->is_protected[idx] = 0;
        t2_count--;
        t1_count++;

        if (!(*pte & PTE_D)) {
          victim_idx = idx;
          break;
        } else {
          if (fallback_dirty_t2 < 0)
            fallback_dirty_t2 = idx;
        }
      }

      if (attempts >= p->resident_count && fallback_dirty_t2 >= 0) {
        victim_idx = fallback_dirty_t2;
        break;
      }
    }

    if (victim_idx < 0 && fallback_dirty_t2 >= 0) {
      victim_idx = fallback_dirty_t2;
    }
  }

  // Step 3: Absolute fallback
  if (victim_idx < 0) {
    victim_idx = p->clock_hand % p->resident_count;
  }

  uint64 victim_va = p->resident_pages[victim_idx];
  p->clock_hand = victim_idx % (p->resident_count > 1 ? (p->resident_count - 1) : 1);
  fifo_remove(p, victim_va);
  return victim_va;

#elif CURRENT_SWAP_POLICY == SWAP_POLICY_ADAPTIVE_CLOCK
  if (p->resident_count <= 0)
    return 0;

  uint32 min_score = 0xFFFFFFFF;
  int victim_idx = -1;

  for (int i = 0; i < p->resident_count; i++) {
    int idx = (p->clock_hand + i) % p->resident_count;
    uint64 va = p->resident_pages[idx];

    acquire(&swap_stats_lock);
    swap_stats.scan_inspections++;
    release(&swap_stats_lock);

    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte == 0 || !(*pte & PTE_V)) {
      // Unmapped or invalid entry; select immediately
      victim_idx = idx;
      break;
    }

    uint8 accessed = (*pte & PTE_A) ? 1 : 0;
    p->access_history[idx] = (p->access_history[idx] >> 1) | (accessed << 7);
    *pte &= ~PTE_A;
    sfence_vma();

    uint32 score = (uint32)p->access_history[idx] + ((*pte & PTE_D) ? 32 : 0);
    if (score < min_score) {
      min_score = score;
      victim_idx = idx;
    }
  }

  if (victim_idx < 0)
    victim_idx = p->clock_hand % p->resident_count;

  uint64 victim_va = p->resident_pages[victim_idx];
  p->clock_hand = (victim_idx + 1) % p->resident_count;
  fifo_remove(p, victim_va);
  return victim_va;

#elif CURRENT_SWAP_POLICY == SWAP_POLICY_ENHANCED_CLOCK
  if (p->resident_count <= 0)
    return 0;

  int original_hand = p->clock_hand;

  // Step 1: Scan for Class 0 (A=0, D=0). Do not modify Accessed bits.
  for (int i = 0; i < p->resident_count; i++) {
    int idx = (original_hand + i) % p->resident_count;
    uint64 va = p->resident_pages[idx];

    acquire(&swap_stats_lock);
    swap_stats.scan_inspections++;
    release(&swap_stats_lock);

    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte == 0 || !(*pte & PTE_V)) {
      p->clock_hand = (idx + 1) % p->resident_count;
      fifo_remove(p, va);
      return va;
    }

    if (!(*pte & PTE_A) && !(*pte & PTE_D)) {
      // Found Class 0 victim
      p->clock_hand = (idx + 1) % p->resident_count;
      fifo_remove(p, va);
      return va;
    }
  }

  // Step 2: Scan for Class 1 (A=0, D=1). For each page with A=1 examined, clear PTE_A and sfence_vma().
  for (int i = 0; i < p->resident_count; i++) {
    int idx = (original_hand + i) % p->resident_count;
    uint64 va = p->resident_pages[idx];

    acquire(&swap_stats_lock);
    swap_stats.scan_inspections++;
    release(&swap_stats_lock);

    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte == 0 || !(*pte & PTE_V)) {
      p->clock_hand = (idx + 1) % p->resident_count;
      fifo_remove(p, va);
      return va;
    }

    if (!(*pte & PTE_A) && (*pte & PTE_D)) {
      // Found Class 1 victim
      p->clock_hand = (idx + 1) % p->resident_count;
      fifo_remove(p, va);
      return va;
    }

    if (*pte & PTE_A) {
      *pte &= ~PTE_A;
      sfence_vma();
    }
  }

  // Step 3: Repeat Step 1 looking for Class 0 (A=0, D=0).
  for (int i = 0; i < p->resident_count; i++) {
    int idx = (original_hand + i) % p->resident_count;
    uint64 va = p->resident_pages[idx];

    acquire(&swap_stats_lock);
    swap_stats.scan_inspections++;
    release(&swap_stats_lock);

    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte == 0 || !(*pte & PTE_V)) {
      p->clock_hand = (idx + 1) % p->resident_count;
      fifo_remove(p, va);
      return va;
    }

    if (!(*pte & PTE_A) && !(*pte & PTE_D)) {
      // Found Class 0 victim
      p->clock_hand = (idx + 1) % p->resident_count;
      fifo_remove(p, va);
      return va;
    }
  }

  // Step 4: Repeat Step 2 looking for Class 1 (A=0, D=1) (or first candidate).
  for (int i = 0; i < p->resident_count; i++) {
    int idx = (original_hand + i) % p->resident_count;
    uint64 va = p->resident_pages[idx];

    acquire(&swap_stats_lock);
    swap_stats.scan_inspections++;
    release(&swap_stats_lock);

    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte == 0 || !(*pte & PTE_V)) {
      p->clock_hand = (idx + 1) % p->resident_count;
      fifo_remove(p, va);
      return va;
    }

    if (!(*pte & PTE_A) && (*pte & PTE_D)) {
      // Found Class 1 victim
      p->clock_hand = (idx + 1) % p->resident_count;
      fifo_remove(p, va);
      return va;
    }

    if (*pte & PTE_A) {
      *pte &= ~PTE_A;
      sfence_vma();
    }
  }

  // Fallback: evict at original_hand
  int fallback_idx = original_hand % p->resident_count;
  uint64 va = p->resident_pages[fallback_idx];
  p->clock_hand = (fallback_idx + 1) % p->resident_count;
  fifo_remove(p, va);
  return va;

#elif CURRENT_SWAP_POLICY == SWAP_POLICY_CLOCK
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
