# Baseline PPA Report

This document records the first baseline pass for comparing XLS scheduling
algorithms before changing scheduler heuristics.

## Intended benchmark inputs

The original `xlscc` examples selected for the first pass are:

- `simple_pipelined_loop.cc`
- `simple_backwards_pipelined_loop.cc`
- `simple_nested_pipelined_loop.cc`
- `simple_unsigned_pipelined_loop.cc`

These are all under:

- `xls/contrib/xlscc/examples/`

## Measurement path

The baseline flow uses the existing XLS tools already built in this checkout:

1. `xlscc` to translate C++ to raw IR
2. `opt_main` to create optimized IR
3. `benchmark_main` to run the original scheduler and report:
   - scheduling time
   - total delay
   - total area
   - minimum clock period

The reusable driver script is:

- [run_baseline_ppa.sh](/home/banshaw/spring_2026/Google_XLS/xls/run_baseline_ppa.sh)

Outputs are written to:

- `xls/baseline_results/summary.csv`
- `xls/baseline_results/logs/`

## Current status

On April 13, 2026, the reporting path was validated on the existing IR file
`opt.ir`, and `benchmark_main` successfully compared scheduler behavior on that
input.

Current generated artifacts:

- [summary.csv](/home/banshaw/spring_2026/Google_XLS/xls/baseline_results/summary.csv)
- [simple_pipelined_loop translate log](/home/banshaw/spring_2026/Google_XLS/xls/baseline_results/logs/simple_pipelined_loop_translate.log)
- [simple_backwards_pipelined_loop translate log](/home/banshaw/spring_2026/Google_XLS/xls/baseline_results/logs/simple_backwards_pipelined_loop_translate.log)
- [simple_nested_pipelined_loop translate log](/home/banshaw/spring_2026/Google_XLS/xls/baseline_results/logs/simple_nested_pipelined_loop_translate.log)
- [simple_unsigned_pipelined_loop translate log](/home/banshaw/spring_2026/Google_XLS/xls/baseline_results/logs/simple_unsigned_pipelined_loop_translate.log)

Validated sanity benchmark results on `opt.ir`:

- `SDC`: success, total delay `351ps`, total area `21.0000 um2`, minimum clock period `351ps`
- `RANDOM`: success, total delay `351ps`, total area `21.0000 um2`, minimum clock period `351ps`
- `MIN_CUT`: failed on this input because the scheduler rejected the constraint set
- `ASAP`: failed on this input because the scheduling problem was infeasible under the requested setup

Current blockers for the original `xlscc` loop examples:

- Direct `xlscc` translation of the four loop examples fails in this workspace
  with a translator check in `translate_loops.cc`.
- Other original `xlscc` examples that depend on `xls_int.h` additionally need
  the expected `ac_types` include path layout.
- Bazel rebuilding is currently unreliable in this environment because the
  local Bazel server crashes during startup when using a fresh output root.

## What this means for iteration 1

We can already do a clean baseline comparison at the IR level with the original
scheduler implementations.

To use the four chosen C++ examples as the benchmark set, the next unblocker is
to fix or work around the `xlscc` translation path for those loop examples.

Once that is fixed, the same baseline script can be rerun without changing the
reporting pipeline.
