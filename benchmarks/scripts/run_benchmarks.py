#!/usr/bin/env python3
"""
run_benchmarks.py: Automated xv6 Paging & Replacement Policy Benchmark Harness

Executes standard workloads (sequential, locality, matrix, dirty_stress, phase_change)
across all supported page replacement policies (FIFO, CLOCK, ENHANCED_CLOCK, ADAPTIVE_CLOCK)
in headless QEMU. Collects deterministic subsystem counters, execution cycles, and timer ticks.
Runs 3 trials per configuration and calculates deterministic median statistics.

Outputs:
  - Raw execution logs: benchmarks/raw/<policy>_<workload>.log
  - Formatted CSV table: benchmarks/processed/results.csv
  - Summary Markdown table: benchmarks/processed/summary.md
"""

import os
import sys
import pty
import time
import re
import statistics
import subprocess

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC_DIR = os.path.join(REPO_ROOT, "src")
RAW_DIR = os.path.join(REPO_ROOT, "benchmarks", "raw")
PROCESSED_DIR = os.path.join(REPO_ROOT, "benchmarks", "processed")

POLICIES = ["FIFO", "CLOCK", "ENHANCED_CLOCK", "ADAPTIVE_CLOCK", "CAR_2Q", "FREQ_AGING", "DYNAMIC_BIAS"]
WORKLOADS = ["sequential", "locality", "matrix", "dirty_stress", "phase_change"]
NUM_TRIALS = 3

RESULT_REGEX = re.compile(
    r"RESULT\s+workload=(\S+)\s+ticks=(\d+)\s+cycles=(\d+)\s+faults=(\d+)\s+swapin=(\d+)\s+reads=(\d+)\s+writes=(\d+)\s+evict_clean=(\d+)\s+evict_dirty=(\d+)\s+scan_inspections=(\d+)"
)


def ensure_directories():
    os.makedirs(RAW_DIR, exist_ok=True)
    os.makedirs(PROCESSED_DIR, exist_ok=True)


def build_xv6(policy):
    print(f"\n==========================================")
    print(f"[*] Building xv6 for POLICY={policy}...")
    print(f"==========================================")
    # Clean first to ensure all object files and fs.img are rebuilt for the policy
    subprocess.run(["make", "-C", SRC_DIR, "clean"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    # Build kernel and fs.img
    subprocess.run(
        ["make", "-C", SRC_DIR, f"POLICY={policy}", "-j4"],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    subprocess.run(
        ["make", "-C", SRC_DIR, f"POLICY={policy}", "fs.img"],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    print(f"[+] Build successful for {policy}.")


def run_workload_trial(policy, workload, trial_num, timeout_sec=20):
    master, slave = pty.openpty()
    os.set_blocking(master, False)

    qemu_cmd = [
        "qemu-system-riscv64",
        "-machine", "virt",
        "-bios", "none",
        "-kernel", "kernel/kernel",
        "-m", "128M",
        "-smp", "3",
        "-nographic",
        "-global", "virtio-mmio.force-legacy=false",
        "-drive", "file=fs.img,if=none,format=raw,id=x0",
        "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
    ]

    proc = subprocess.Popen(
        qemu_cmd,
        stdin=slave,
        stdout=slave,
        stderr=slave,
        cwd=SRC_DIR,
    )
    os.close(slave)

    full_output = b""
    start_time = time.time()

    # Step 1: Wait for shell prompt
    while time.time() - start_time < timeout_sec:
        try:
            data = os.read(master, 1024)
            if data:
                full_output += data
                if b"$ " in full_output:
                    break
        except BlockingIOError:
            time.sleep(0.05)

    if b"$ " not in full_output:
        proc.terminate()
        os.close(master)
        raise RuntimeError(f"Timeout waiting for xv6 shell prompt (Policy={policy}, Workload={workload})")

    # Step 2: Send workload command
    cmd_bytes = f"workload {workload}\n".encode("utf-8")
    os.write(master, cmd_bytes)

    # Step 3: Wait for RESULT line and next shell prompt
    workload_start = time.time()
    result_matched = None

    while time.time() - workload_start < timeout_sec:
        try:
            data = os.read(master, 1024)
            if data:
                full_output += data
                # Check for RESULT line
                text = full_output.decode("latin1", "replace")
                m = RESULT_REGEX.search(text)
                if m and "$ " in text[m.end():]:
                    result_matched = m
                    break
        except BlockingIOError:
            time.sleep(0.05)

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
    os.close(master)

    text_output = full_output.decode("latin1", "replace")
    if not result_matched:
        m = RESULT_REGEX.search(text_output)
        if m:
            result_matched = m
        else:
            raise RuntimeError(
                f"Failed to capture RESULT for policy={policy}, workload={workload}, trial={trial_num}.\nOutput:\n{text_output}"
            )

    parsed = {
        "workload": result_matched.group(1),
        "ticks": int(result_matched.group(2)),
        "cycles": int(result_matched.group(3)),
        "faults": int(result_matched.group(4)),
        "swapin": int(result_matched.group(5)),
        "reads": int(result_matched.group(6)),
        "writes": int(result_matched.group(7)),
        "evict_clean": int(result_matched.group(8)),
        "evict_dirty": int(result_matched.group(9)),
        "scan_inspections": int(result_matched.group(10)),
    }

    return parsed, text_output


def main():
    ensure_directories()
    results = {}

    target_policies = [p for p in sys.argv[1:] if p in POLICIES]
    if not target_policies:
        target_policies = POLICIES

    print("================================================================")
    print("       xv6 Paging & Swapping Subsystem Benchmark Suite          ")
    print(f" Policies:  {', '.join(target_policies)}")
    print(f" Workloads: {', '.join(WORKLOADS)}")
    print(f" Trials:    {NUM_TRIALS} per configuration")
    print("================================================================\n")

    for policy in target_policies:
        build_xv6(policy)
        results[policy] = {}

        for workload in WORKLOADS:
            print(f"  -> Running {workload} on {policy} ({NUM_TRIALS} trials)...", end="", flush=True)
            raw_log_path = os.path.join(RAW_DIR, f"{policy}_{workload}.log")
            trial_metrics = []

            with open(raw_log_path, "w") as log_file:
                log_file.write(f"=== Policy: {policy} | Workload: {workload} ===\n")
                log_file.write(f"Date: {time.strftime('%Y-%m-%d %H:%M:%S')}\n\n")

                for trial in range(1, NUM_TRIALS + 1):
                    log_file.write(f"--- TRIAL {trial} ---\n")
                    try:
                        metrics, raw_text = run_workload_trial(policy, workload, trial)
                        log_file.write(raw_text + "\n\n")
                        trial_metrics.append(metrics)
                    except Exception as e:
                        log_file.write(f"ERROR in trial {trial}: {str(e)}\n\n")
                        print(f" [FAIL: {e}]", flush=True)
                        raise

            # Calculate deterministic median
            keys = [
                "ticks",
                "cycles",
                "faults",
                "swapin",
                "reads",
                "writes",
                "evict_clean",
                "evict_dirty",
                "scan_inspections",
            ]
            median_stats = {k: int(statistics.median([m[k] for m in trial_metrics])) for k in keys}
            results[policy][workload] = median_stats
            print(
                f" Done. (faults={median_stats['faults']}, reads={median_stats['reads']}, writes={median_stats['writes']}, scans={median_stats['scan_inspections']})"
            )

    # 1. Output CSV Table (merge with existing results if available)
    csv_path = os.path.join(PROCESSED_DIR, "results.csv")
    all_results = {}
    if os.path.exists(csv_path):
        try:
            with open(csv_path, "r") as f:
                lines = [line.strip().split(",") for line in f if line.strip()]
            if lines and lines[0][0] == "policy":
                for row in lines[1:]:
                    pol, wl = row[0], row[1]
                    if pol not in all_results:
                        all_results[pol] = {}
                    all_results[pol][wl] = {
                        "ticks": int(row[2]),
                        "cycles": int(row[3]),
                        "faults": int(row[4]),
                        "swapin": int(row[5]),
                        "reads": int(row[6]),
                        "writes": int(row[7]),
                        "evict_clean": int(row[8]),
                        "evict_dirty": int(row[9]),
                        "scan_inspections": int(row[10]),
                    }
        except Exception:
            pass

    for pol in results:
        all_results[pol] = results[pol]

    print(f"\n[*] Generating CSV at {csv_path}...")
    headers = [
        "policy",
        "workload",
        "ticks",
        "cycles",
        "fault_total",
        "fault_swapin",
        "swap_reads",
        "swap_writes",
        "evict_clean",
        "evict_dirty",
        "scan_inspections",
        "clean_evict_ratio",
        "writes_per_evict",
        "avg_scan_len",
    ]

    active_policies = [p for p in POLICIES if p in all_results]

    with open(csv_path, "w") as f:
        f.write(",".join(headers) + "\n")
        for policy in active_policies:
            for workload in WORKLOADS:
                if workload not in all_results[policy]:
                    continue
                m = all_results[policy][workload]
                evict_total = m["evict_clean"] + m["evict_dirty"]
                clean_ratio = (m["evict_clean"] / evict_total) if evict_total > 0 else 0.0
                writes_per_evict = (m["writes"] / evict_total) if evict_total > 0 else 0.0
                avg_scan = (m["scan_inspections"] / evict_total) if evict_total > 0 else 0.0

                row = [
                    policy,
                    workload,
                    str(m["ticks"]),
                    str(m["cycles"]),
                    str(m["faults"]),
                    str(m["swapin"]),
                    str(m["reads"]),
                    str(m["writes"]),
                    str(m["evict_clean"]),
                    str(m["evict_dirty"]),
                    str(m["scan_inspections"]),
                    f"{clean_ratio:.4f}",
                    f"{writes_per_evict:.4f}",
                    f"{avg_scan:.4f}",
                ]
                f.write(",".join(row) + "\n")

    # 2. Output Markdown Summary Table
    md_path = os.path.join(PROCESSED_DIR, "summary.md")
    print(f"[*] Generating Markdown Summary at {md_path}...")
    with open(md_path, "w") as f:
        f.write("# Page Replacement Subsystem Benchmark Summary\n\n")
        f.write(f"Generated on: {time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime())}  \n")
        f.write(f"Configuration: 3 trials per workload, reporting median values.  \n")
        f.write(f"Hardware Invariants: `MAX_PHY_PAGES = 16`, `NUM_PAGES = 32`.  \n\n")

        for workload in WORKLOADS:
            f.write(f"### Workload: `{workload}`\n\n")
            f.write(
                "| Policy | Faults (Total/Swapin) | Disk Reads | Disk Writes | Evict Clean | Evict Dirty | Clean Evict Ratio | Scan Inspections | Ticks | CPU Cycles |\n"
            )
            f.write(
                "|---|---|---|---|---|---|---|---|---|---|\n"
            )
            for policy in active_policies:
                if workload not in all_results[policy]:
                    continue
                m = all_results[policy][workload]
                evict_total = m["evict_clean"] + m["evict_dirty"]
                clean_ratio = (m["evict_clean"] / evict_total * 100.0) if evict_total > 0 else 0.0
                f.write(
                    f"| **{policy}** | {m['faults']} / {m['swapin']} | {m['reads']} | {m['writes']} | {m['evict_clean']} | {m['evict_dirty']} | {clean_ratio:.1f}% | {m['scan_inspections']} | {m['ticks']} | {m['cycles']} |\n"
                )
            f.write("\n")

        # Add Overall Comparison Table
        f.write("### Total Subsystem Traffic & Overhead Across All Workloads\n\n")
        f.write(
            "| Policy | Total Faults | Total Disk Reads | Total Disk Writes | Total Swap I/O Pages | Total Clean Evicts | Total Scans |\n"
        )
        f.write("|---|---|---|---|---|---|---|\n")
        for policy in active_policies:
            if not all(w in all_results[policy] for w in WORKLOADS):
                continue
            tot_faults = sum(all_results[policy][w]["faults"] for w in WORKLOADS)
            tot_reads = sum(all_results[policy][w]["reads"] for w in WORKLOADS)
            tot_writes = sum(all_results[policy][w]["writes"] for w in WORKLOADS)
            tot_io = tot_reads + tot_writes
            tot_clean = sum(all_results[policy][w]["evict_clean"] for w in WORKLOADS)
            tot_scans = sum(all_results[policy][w]["scan_inspections"] for w in WORKLOADS)
            f.write(
                f"| **{policy}** | {tot_faults} | {tot_reads} | {tot_writes} | **{tot_io}** | {tot_clean} | {tot_scans} |\n"
            )
        f.write("\n")

    print("\n[+] Benchmark suite completed successfully!")
    print(f"[+] Results saved to:")
    print(f"    - Raw logs:  {RAW_DIR}/")
    print(f"    - CSV:       {csv_path}")
    print(f"    - Summary:   {md_path}")


if __name__ == "__main__":
    main()
