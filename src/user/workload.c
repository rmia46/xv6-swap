#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/swap.h"
#include "user/user.h"

#define NUM_PAGES 32
#define PARETO_REFS 500
#define MATRIX_DIM 64

// Linear Congruential Generator for reproducible deterministic random access
static uint32 rand_state = 123456789;
static uint32 rand_lcg(void) {
  rand_state = rand_state * 1664525 + 1013904223;
  return rand_state;
}

static inline uint64 get_cycles(void) {
  uint64 c;
  asm volatile("rdcycle %0" : "=r"(c));
  return c;
}

static inline uint64 get_time(void) {
  uint64 t;
  asm volatile("rdtime %0" : "=r"(t));
  return t;
}

// 1. sequential: linear streaming scan over 32 pages (2x MAX_PHY_PAGES) for 5 passes
static void run_sequential(char *buf) {
  volatile char *ptr = buf;
  for (int pass = 0; pass < 5; pass++) {
    for (int p = 0; p < NUM_PAGES; p++) {
      int offset = p * PGSIZE;
      ptr[offset] = (char)((p + pass) & 0xFF);
      volatile char val = ptr[offset];
      (void)val;
    }
  }
}

// 2. locality: 80/20 Pareto distribution (frequently accesses 4 hot pages, occasionally 28 cold pages) for 500 references
static void run_locality(char *buf) {
  volatile char *ptr = buf;
  // Initialize pages so they exist in memory/swap
  for (int p = 0; p < NUM_PAGES; p++) {
    ptr[p * PGSIZE] = (char)(p & 0xFF);
  }

  rand_state = 42; // Fixed seed for determinism
  for (int i = 0; i < PARETO_REFS; i++) {
    int p;
    if ((rand_lcg() % 100) < 80) {
      // 80% of accesses to 4 hot pages: 0, 1, 2, 3
      p = rand_lcg() % 4;
    } else {
      // 20% of accesses to 28 cold pages: 4..31
      p = 4 + (rand_lcg() % (NUM_PAGES - 4));
    }
    int offset = p * PGSIZE + ((rand_lcg() % (PGSIZE / 4)) * 4);
    if ((i % 2) == 0) {
      ptr[offset] = (char)((ptr[offset] + 1) & 0xFF);
    } else {
      volatile char val = ptr[offset];
      (void)val;
    }
  }
}

// 3. matrix: Strided row/column matrix multiplication across multiple pages
static void run_matrix(char *buf) {
  // 64x64 ints is 4096 ints = 16KB = 4 pages per matrix
  // Total for 3 matrices A, B, C is 48KB = 12 pages (within 32 pages)
  int *A = (int *)buf;
  int *B = (int *)(buf + 4 * PGSIZE);
  int *C = (int *)(buf + 8 * PGSIZE);

  // Initialize A and B
  for (int i = 0; i < MATRIX_DIM; i++) {
    for (int j = 0; j < MATRIX_DIM; j++) {
      A[i * MATRIX_DIM + j] = (i + j) % 17;
      B[i * MATRIX_DIM + j] = (i * 3 + j) % 19;
      C[i * MATRIX_DIM + j] = 0;
    }
  }

  // Multiply C = A * B
  // Reading B column-wise causes large strides (MATRIX_DIM ints = 256 bytes per step)
  for (int i = 0; i < MATRIX_DIM; i++) {
    for (int j = 0; j < MATRIX_DIM; j++) {
      int sum = 0;
      for (int k = 0; k < MATRIX_DIM; k++) {
        sum += A[i * MATRIX_DIM + k] * B[k * MATRIX_DIM + j];
      }
      C[i * MATRIX_DIM + j] = sum;
    }
  }
}

// 4. dirty_stress: Controlled mix of read-only access (clean) and write access (dirty) across pages
static void run_dirty_stress(char *buf) {
  volatile char *ptr = buf;
  // Initialize all pages
  for (int p = 0; p < NUM_PAGES; p++) {
    ptr[p * PGSIZE] = (char)(p & 0xFF);
  }

  // For 5 passes:
  // Pages with (p % 4 == 0) are dirty (written to)
  // Pages with (p % 4 != 0) are read-only (clean)
  for (int pass = 0; pass < 5; pass++) {
    for (int p = 0; p < NUM_PAGES; p++) {
      int offset = p * PGSIZE;
      if ((p % 4) == 0) {
        ptr[offset] = (char)((ptr[offset] + pass) & 0xFF);
      } else {
        volatile char val = ptr[offset];
        (void)val;
      }
    }
  }
}

// 5. phase_change: Repeatedly accesses working set A, then abruptly shifts to working set B
static void run_phase_change(char *buf) {
  volatile char *ptr = buf;
  // Initialize all pages
  for (int p = 0; p < NUM_PAGES; p++) {
    ptr[p * PGSIZE] = (char)(p & 0xFF);
  }

  // Phase 1: Heavy access to Working Set A (pages 0..11) for 15 rounds
  for (int round = 0; round < 15; round++) {
    for (int p = 0; p < 12; p++) {
      int offset = p * PGSIZE;
      ptr[offset] = (char)((ptr[offset] + round) & 0xFF);
      volatile char val = ptr[offset];
      (void)val;
    }
  }

  // Abrupt Shift -> Phase 2: Heavy access to Working Set B (pages 16..27) for 15 rounds
  for (int round = 0; round < 15; round++) {
    for (int p = 16; p < 28; p++) {
      int offset = p * PGSIZE;
      ptr[offset] = (char)((ptr[offset] + round) & 0xFF);
      volatile char val = ptr[offset];
      (void)val;
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(2, "Usage: workload <sequential|locality|matrix|dirty_stress|phase_change>\n");
    exit(1);
  }

  char *type = argv[1];

  // Allocate 32 pages buffer using sbrk
  char *buf = sbrk(NUM_PAGES * PGSIZE);
  if (buf == SBRK_ERROR) {
    fprintf(2, "workload: sbrk failed\n");
    exit(1);
  }

  // Touch all pages once to guarantee page allocation and establish base state
  for (int p = 0; p < NUM_PAGES; p++) {
    buf[p * PGSIZE] = 0;
  }

  // Reset swap statistics immediately before workload execution
  reset_swap_stats();

  uint64 start_ticks = (uint64)uptime();
  uint64 start_cycles = get_cycles();
  (void)get_time();

  if (strcmp(type, "sequential") == 0) {
    run_sequential(buf);
  } else if (strcmp(type, "locality") == 0) {
    run_locality(buf);
  } else if (strcmp(type, "matrix") == 0) {
    run_matrix(buf);
  } else if (strcmp(type, "dirty_stress") == 0) {
    run_dirty_stress(buf);
  } else if (strcmp(type, "phase_change") == 0) {
    run_phase_change(buf);
  } else {
    fprintf(2, "workload: unknown type %s\n", type);
    exit(1);
  }

  uint64 end_cycles = get_cycles();
  uint64 end_ticks = (uint64)uptime();

  // Sample swap statistics after workload execution
  struct swap_stats st;
  get_swap_stats(&st);

  uint64 total_ticks = end_ticks >= start_ticks ? (end_ticks - start_ticks) : 0;
  uint64 total_cycles = end_cycles >= start_cycles ? (end_cycles - start_cycles) : (end_cycles);

  // Print machine-parsable RESULT line:
  // RESULT workload=<name> ticks=<t> cycles=<c> faults=<f> swapin=<si> reads=<r> writes=<w> evict_clean=<ec> evict_dirty=<ed> scan_inspections=<si>
  printf("RESULT workload=%s ticks=%ld cycles=%ld faults=%ld swapin=%ld reads=%ld writes=%ld evict_clean=%ld evict_dirty=%ld scan_inspections=%ld\n",
         type, total_ticks, total_cycles, st.fault_total, st.fault_swapin,
         st.swap_reads, st.swap_writes, st.evict_clean, st.evict_dirty,
         st.scan_inspections);

  exit(0);
}
