# Page Replacement Subsystem Benchmark Summary

Generated on: 2026-11-07 09:26:47 UTC  
Configuration: 3 trials per workload, reporting median values.  
Hardware Invariants: `MAX_PHY_PAGES = 16`, `NUM_PAGES = 32`.  

### Workload: `sequential`

| Policy | Faults (Total/Swapin) | Disk Reads | Disk Writes | Evict Clean | Evict Dirty | Clean Evict Ratio | Scan Inspections | Ticks | CPU Cycles |
|---|---|---|---|---|---|---|---|---|---|
| **FIFO** | 160 / 160 | 160 | 160 | 0 | 160 | 0.0% | 160 | 20 | 5471348331 |
| **CLOCK** | 160 / 160 | 160 | 160 | 0 | 160 | 0.0% | 320 | 17 | 4686907593 |
| **ENHANCED_CLOCK** | 160 / 160 | 160 | 160 | 0 | 160 | 0.0% | 3040 | 17 | 4797340072 |
| **ADAPTIVE_CLOCK** | 160 / 160 | 160 | 160 | 0 | 160 | 0.0% | 2560 | 19 | 5149619144 |
| **CAR_2Q** | 160 / 160 | 160 | 160 | 0 | 160 | 0.0% | 1763 | 15 | 4283212789 |
| **FREQ_AGING** | 160 / 160 | 160 | 160 | 0 | 160 | 0.0% | 2560 | 19 | 5444479992 |

### Workload: `locality`

| Policy | Faults (Total/Swapin) | Disk Reads | Disk Writes | Evict Clean | Evict Dirty | Clean Evict Ratio | Scan Inspections | Ticks | CPU Cycles |
|---|---|---|---|---|---|---|---|---|---|
| **FIFO** | 110 / 110 | 110 | 110 | 33 | 77 | 30.0% | 110 | 13 | 3674448484 |
| **CLOCK** | 97 / 97 | 97 | 97 | 26 | 71 | 26.8% | 256 | 9 | 2680869743 |
| **ENHANCED_CLOCK** | 99 / 99 | 99 | 99 | 31 | 68 | 31.3% | 1608 | 9 | 2544658088 |
| **ADAPTIVE_CLOCK** | 103 / 103 | 103 | 103 | 42 | 61 | 40.8% | 1648 | 9 | 2629974928 |
| **CAR_2Q** | 104 / 104 | 104 | 104 | 44 | 60 | 42.3% | 821 | 12 | 3415551923 |
| **FREQ_AGING** | 103 / 103 | 103 | 103 | 42 | 61 | 40.8% | 1648 | 11 | 3322929885 |

### Workload: `matrix`

| Policy | Faults (Total/Swapin) | Disk Reads | Disk Writes | Evict Clean | Evict Dirty | Clean Evict Ratio | Scan Inspections | Ticks | CPU Cycles |
|---|---|---|---|---|---|---|---|---|---|
| **FIFO** | 12 / 12 | 12 | 12 | 0 | 12 | 0.0% | 12 | 1 | 424214988 |
| **CLOCK** | 12 / 12 | 12 | 12 | 0 | 12 | 0.0% | 28 | 1 | 417188870 |
| **ENHANCED_CLOCK** | 12 / 12 | 12 | 12 | 0 | 12 | 0.0% | 236 | 1 | 303165846 |
| **ADAPTIVE_CLOCK** | 12 / 12 | 12 | 12 | 0 | 12 | 0.0% | 192 | 1 | 314278365 |
| **CAR_2Q** | 11 / 11 | 11 | 11 | 0 | 11 | 0.0% | 109 | 1 | 337505317 |
| **FREQ_AGING** | 12 / 12 | 12 | 12 | 0 | 12 | 0.0% | 192 | 1 | 315787200 |

### Workload: `dirty_stress`

| Policy | Faults (Total/Swapin) | Disk Reads | Disk Writes | Evict Clean | Evict Dirty | Clean Evict Ratio | Scan Inspections | Ticks | CPU Cycles |
|---|---|---|---|---|---|---|---|---|---|
| **FIFO** | 192 / 192 | 192 | 192 | 108 | 84 | 56.2% | 192 | 24 | 6872464850 |
| **CLOCK** | 192 / 192 | 192 | 192 | 108 | 84 | 56.2% | 384 | 20 | 5488173158 |
| **ENHANCED_CLOCK** | 192 / 192 | 192 | 192 | 108 | 84 | 56.2% | 2168 | 19 | 5558423528 |
| **ADAPTIVE_CLOCK** | 143 / 143 | 143 | 143 | 87 | 56 | 60.8% | 2288 | 14 | 3818268897 |
| **CAR_2Q** | 139 / 139 | 139 | 139 | 93 | 46 | 66.9% | 1056 | 14 | 4040904957 |
| **FREQ_AGING** | 143 / 143 | 143 | 143 | 87 | 56 | 60.8% | 2288 | 17 | 4714913288 |

### Workload: `phase_change`

| Policy | Faults (Total/Swapin) | Disk Reads | Disk Writes | Evict Clean | Evict Dirty | Clean Evict Ratio | Scan Inspections | Ticks | CPU Cycles |
|---|---|---|---|---|---|---|---|---|---|
| **FIFO** | 56 / 56 | 56 | 56 | 0 | 56 | 0.0% | 56 | 6 | 1745429683 |
| **CLOCK** | 56 / 56 | 56 | 56 | 0 | 56 | 0.0% | 120 | 6 | 1586203994 |
| **ENHANCED_CLOCK** | 56 / 56 | 56 | 56 | 0 | 56 | 0.0% | 1080 | 6 | 1556251165 |
| **ADAPTIVE_CLOCK** | 56 / 56 | 56 | 56 | 0 | 56 | 0.0% | 896 | 5 | 1492071638 |
| **CAR_2Q** | 53 / 53 | 53 | 53 | 0 | 53 | 0.0% | 586 | 6 | 1686674825 |
| **FREQ_AGING** | 56 / 56 | 56 | 56 | 0 | 56 | 0.0% | 896 | 8 | 2080824813 |

### Total Subsystem Traffic & Overhead Across All Workloads

| Policy | Total Faults | Total Disk Reads | Total Disk Writes | Total Swap I/O Pages | Total Clean Evicts | Total Scans |
|---|---|---|---|---|---|---|
| **FIFO** | 530 | 530 | 530 | **1060** | 141 | 530 |
| **CLOCK** | 517 | 517 | 517 | **1034** | 134 | 1108 |
| **ENHANCED_CLOCK** | 519 | 519 | 519 | **1038** | 139 | 8132 |
| **ADAPTIVE_CLOCK** | 474 | 474 | 474 | **948** | 129 | 7584 |
| **CAR_2Q** | 467 | 467 | 467 | **934** | 137 | 4335 |
| **FREQ_AGING** | 474 | 474 | 474 | **948** | 129 | 7584 |

