// clang-format off
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/swap.h"
#include "user/user.h"
// clang-format on

#define NUM_PAGES 32 // More than MAX_PHY_PAGES (16) to force swapping

void print_stats(const char *tag, struct swap_stats *st) {
  printf("[%s] fault_total=%d fault_swapin=%d fault_lazy=%d\n", tag,
         (int)st->fault_total, (int)st->fault_swapin, (int)st->fault_lazy);
  printf("       swap_reads=%d swap_writes=%d evict_clean=%d evict_dirty=%d "
         "scan_inspections=%d\n",
         (int)st->swap_reads, (int)st->swap_writes, (int)st->evict_clean,
         (int)st->evict_dirty, (int)st->scan_inspections);
}

void test_swap_basic(void) {
  printf("--- test_swap_basic: allocating %d pages (MAX_PHY_PAGES=16) ---\n",
         NUM_PAGES);
  reset_swap_stats();

  char *ptr = sbrk(NUM_PAGES * PGSIZE);
  if (ptr == SBRK_ERROR) {
    printf("FAILED: sbrk failed\n");
    exit(1);
  }

  struct swap_stats st;
  get_swap_stats(&st);
  print_stats("after eager sbrk", &st);

  // Since sbrk is eager, uvmalloc allocates all 32 pages immediately.
  // With MAX_PHY_PAGES = 16, pages 16..31 cause evictions!
  if (st.swap_writes == 0) {
    printf("FAILED: expected swap writes during allocation of %d pages\n",
           NUM_PAGES);
    exit(1);
  }

  // Write distinct data patterns into each page
  printf("Writing pattern to %d pages...\n", NUM_PAGES);
  for (int p = 0; p < NUM_PAGES; p++) {
    char *page = ptr + p * PGSIZE;
    for (int i = 0; i < PGSIZE; i++) {
      page[i] = (char)((p * 37 + i) & 0xFF);
    }
  }

  get_swap_stats(&st);
  print_stats("after writing patterns", &st);

  // Verify pattern from each page (this will cause swap-ins and swap-outs)
  printf("Verifying pattern across all %d pages...\n", NUM_PAGES);
  for (int p = 0; p < NUM_PAGES; p++) {
    char *page = ptr + p * PGSIZE;
    for (int i = 0; i < PGSIZE; i++) {
      char expected = (char)((p * 37 + i) & 0xFF);
      if (page[i] != expected) {
        printf(
            "FAILED: data mismatch at page %d offset %d: expected %d, got %d\n",
            p, i, expected, page[i]);
        exit(1);
      }
    }
  }

  get_swap_stats(&st);
  print_stats("after verifying patterns", &st);

  if (st.swap_reads == 0 || st.fault_swapin == 0) {
    printf("FAILED: expected swap reads and swap-in faults\n");
    exit(1);
  }

  printf("test_swap_basic PASSED!\n");
}

void test_swap_lazy(void) {
  printf("\n--- test_swap_lazy: allocating %d pages with sbrklazy ---\n",
         NUM_PAGES);
  reset_swap_stats();

  char *ptr = sbrklazy(NUM_PAGES * PGSIZE);
  if (ptr == SBRK_ERROR) {
    printf("FAILED: sbrklazy failed\n");
    exit(1);
  }

  struct swap_stats st;
  get_swap_stats(&st);
  print_stats("after sbrklazy", &st);

  if (st.fault_lazy != 0 || st.swap_writes != 0) {
    printf("FAILED: lazy sbrk should not allocate pages upfront\n");
    exit(1);
  }

  printf("Writing pattern to lazy pages...\n");
  for (int p = 0; p < NUM_PAGES; p++) {
    char *page = ptr + p * PGSIZE;
    for (int i = 0; i < PGSIZE; i++) {
      page[i] = (char)((p * 17 + i * 3) & 0xFF);
    }
  }

  get_swap_stats(&st);
  print_stats("after writing lazy pages", &st);

  if (st.fault_lazy < NUM_PAGES) {
    printf("FAILED: expected at least %d lazy faults, got %d\n", NUM_PAGES,
           (int)st.fault_lazy);
    exit(1);
  }
  if (st.swap_writes == 0) {
    printf("FAILED: expected swap writes for %d lazy pages\n", NUM_PAGES);
    exit(1);
  }

  printf("Verifying pattern in lazy pages...\n");
  for (int p = 0; p < NUM_PAGES; p++) {
    char *page = ptr + p * PGSIZE;
    for (int i = 0; i < PGSIZE; i++) {
      char expected = (char)((p * 17 + i * 3) & 0xFF);
      if (page[i] != expected) {
        printf("FAILED: lazy data mismatch at page %d offset %d\n", p, i);
        exit(1);
      }
    }
  }

  get_swap_stats(&st);
  print_stats("after verifying lazy pages", &st);

  printf("test_swap_lazy PASSED!\n");
}

void test_swap_fork(void) {
  printf("\n--- test_swap_fork: testing fork with swapped pages ---\n");
  reset_swap_stats();

  char *ptr = sbrk(NUM_PAGES * PGSIZE);
  if (ptr == SBRK_ERROR) {
    printf("FAILED: sbrk failed\n");
    exit(1);
  }

  for (int p = 0; p < NUM_PAGES; p++) {
    ptr[p * PGSIZE] = (char)(p + 1);
  }

  int pid = fork();
  if (pid < 0) {
    printf("FAILED: fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // Child checks data
    for (int p = 0; p < NUM_PAGES; p++) {
      if (ptr[p * PGSIZE] != (char)(p + 1)) {
        printf("FAILED: child data mismatch at page %d\n", p);
        exit(1);
      }
      ptr[p * PGSIZE] = (char)(p + 100);
    }
    exit(0);
  }

  int status;
  wait(&status);
  if (status != 0) {
    printf("FAILED: child exited with error\n");
    exit(1);
  }

  // Parent checks original data unchanged
  for (int p = 0; p < NUM_PAGES; p++) {
    if (ptr[p * PGSIZE] != (char)(p + 1)) {
      printf("FAILED: parent data corrupted by child at page %d\n", p);
      exit(1);
    }
  }

  printf("test_swap_fork PASSED!\n");
}

int main(int argc, char *argv[]) {
  printf("testswap starting...\n");

  test_swap_basic();
  test_swap_lazy();
  test_swap_fork();

  printf("\nALL SWAP TESTS PASSED!\n");
  exit(0);
}
