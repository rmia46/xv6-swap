# Contributing Guidelines

Welcome to the CSE323 Project P4 engineering repository. This document outlines our team workflow, code standards, branching conventions, and quality gates for a 4-person collaborative team.

---

## 1. Branch Strategy

We use a lightweight, structured branching model:

```text
main (Protected, stable releases, submission milestones)
  └── dev (Integration branch, validated PRs)
        ├── feature/swap-framework
        ├── feature/clock-policy
        ├── fix/page-fault-handler
        ├── experiment/adaptive-clock
        └── docs/architecture-updates
```

### Branch Roles
- **`main`**: Production/submission branch. Strictly contains reviewed, fully tested milestones. No direct commits allowed.
- **`dev`**: Main integration branch. All features, fixes, and experiments merge here via Pull Requests.
- **`feature/<name>`**: New development tasks (e.g., `feature/swap-allocator`, `feature/fifo-policy`).
- **`fix/<name>`**: Bug fixes for regression issues or kernel panics.
- **`experiment/<name>`**: Experimental replacement algorithms or parameter variations.
- **`docs/<name>`**: Non-code documentation or research write-ups.

---

## 2. Commit Message Conventions

We enforce [Conventional Commits](https://www.conventionalcommits.org/) to maintain an auditable Git history:

```text
<type>(<scope>): <short summary in imperative mood>

[optional body explaining WHY, context, and rationale]

[optional issue reference or footer, e.g., Refs #12]
```

### Allowed Types
- `feat`: A new feature or policy implementation
- `fix`: A bug fix or patch
- `docs`: Documentation changes only
- `test`: Adding or updating tests / workloads
- `refactor`: Code change that neither fixes a bug nor adds a feature
- `perf`: A code change that improves performance
- `chore`: Repository maintenance, scripts, tooling

### Examples
- `feat(swap): implement disk block bitmap allocation for swap partition`
- `fix(trap): clear accessed bit on page table sync during clock sweep`
- `test(workload): add matrix multiply synthetic memory stress test`
- `docs(adr): record decision on global vs local page eviction`

---

## 3. Pull Request & Code Review Process

1. **Create Branch**: Branch off latest `dev`:
   ```bash
   git checkout dev && git pull
   git checkout -b feature/my-feature
   ```
2. **Local Verification**:
   - Code must compile with zero warnings (`-Wall -Werror`).
   - Run existing correctness test suites.
   - Verify kernel boots and shuts down cleanly in QEMU without panics.
3. **Open Pull Request**:
   - Target `dev` (never target `main` directly for feature branches).
   - Provide a clear PR description detailing:
     - Changes introduced
     - Evidence of local verification (test logs, QEMU console output)
     - Impact on existing policies or mechanisms
4. **Peer Review**:
   - At least **one team member** must review and approve the PR before merging.
   - Reviewers must check for:
     - Concurrency/locking issues (spinlock discipline).
     - Memory leaks in page allocation or swap block freeing.
     - Preservation of clean architecture boundaries.
5. **Merge**:
   - Use **Squash and Merge** or **Rebase and Merge** to maintain a linear, clean history on `dev`.

---

## 4. Code Standards & Kernel Safety

- **Language Standard**: C99 / GNU C following MIT xv6 conventions.
- **Formatting**: Respect existing indentation and naming styles in xv6 (tabs or spaces matching the respective file, typically 2 or 4 spaces / standard BSD K&R).
- **Separation of Concerns**: Keep swap **mechanism** code decoupled from swap **policy** code.
- **Locking & Concurrency**:
  - Never hold spinlocks across I/O sleep (e.g., disk read/write).
  - Document all lock acquisition orders to prevent deadlocks.
- **Page Table Discipline**:
  - Always invalidate TLB when modifying present/valid or permission bits (`lcr3` on x86 or `sfence.vma` on RISC-V).

---

## 5. Experiment & AI Usage Logging

Any work involving AI assistance (prompts, suggestions, algorithm drafts) must be recorded in the `ai-logs/` directory:
- Record the exact prompt in `ai-logs/prompts/`.
- Record the output and critique in `ai-logs/responses/`.
- Cross-reference the log file in the PR description and commit message.

All experimental policy runs must document reproducible metrics in `experiments/results/` using the provided template.
