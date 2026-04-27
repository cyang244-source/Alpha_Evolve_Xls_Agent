# XLS Agent Workflow

This repository is based on the upstream [google/xls](https://github.com/google/xls) project and adds a local agent-driven workflow for exploring new scheduling heuristics and measuring their PPA impact.

This document explains the project-specific flow from build, to `run_agent.py`, to feature extraction.

## What is project-specific in this checkout

The upstream XLS repository provides the compiler, scheduler, and benchmark tooling.

This local project adds the following workflow files:

- `xls/run_agent.py`: Python orchestrator for iterative agent-guided scheduler exploration
- `xls/PPA_script.sh`: runs `benchmark_main` over every IR file in a dataset directory
- `xls/extract_ppa_features.sh`: parses benchmark reports and writes a summary CSV
- `xls/Agent_docs/`: local instructions used by the agent during each iteration
- `my_ir_dataset/`: IR benchmark inputs used by the local workflow

## XLS tools used in this project

This project uses several upstream XLS tools, but they are not all used in the same phase of the work.

### Tools used during the broader project

The following XLS tools are relevant in this repository:

- `xlscc`: used when translating supported C++ inputs into XLS IR
- `opt_main`: used to optimize raw IR before evaluation
- `codegen_main`: used when generating Verilog or inspecting downstream code generation behavior
- `benchmark_main`: used to evaluate scheduling and collect PPA-related report data

In other words, this project has used `xlscc`, `opt_main`, and `codegen_main` in the larger experimentation context, but the AI-agent loop described in this document is centered on scheduling evaluation.

### Tool used by the AI agent workflow

For the automated agent workflow, the main XLS tool is:

- `benchmark_main`

This is the critical point for this project: because the current workflow focuses on scheduling strategy exploration, it does not need to drive the full XLS toolchain end to end for each agent attempt.

Instead, the workflow evaluates scheduling behavior on existing IR inputs in `my_ir_dataset/`, so building the benchmark target is usually enough for the agent loop itself.

## Prerequisites

Before using the workflow, make sure the following are available:

- Bazel in the local environment
- Python 3
- The `codex` CLI, because `xls/run_agent.py` invokes `codex exec`
- Python package `matplotlib`, because `run_agent.py` imports it

This workflow assumes you are running from the repository root:

```bash
cd /path/to/xls
```

## Build the required XLS target

The project workflow depends on the benchmark binary:

- `//xls/dev_tools:benchmark_main`

For the AI-agent workflow, this is the main Bazel target that matters.

Because the workflow operates on existing IR files and focuses on the scheduling subsystem, you generally do not need to build the entire XLS repository just to run the agent loop.

The same target is built inside `xls/run_agent.py`, but it is often useful to build it once manually first:

```bash
bazel --batch build --jobs=16 -c opt //xls/dev_tools:benchmark_main
```

After a successful build, the workflow uses:

```bash
./bazel-bin/xls/dev_tools/benchmark_main
```

### Why `benchmark_main` is enough for this workflow

`benchmark_main` is the binary that `xls/PPA_script.sh` runs on each `.ir` file in `my_ir_dataset/`. It is responsible for invoking the scheduling flow and producing the text reports that are later parsed into CSV features.

Since your current AI-agent loop:

- starts from existing IR inputs
- modifies scheduling-related code
- evaluates scheduler behavior and PPA-related metrics

the workflow mainly depends on the benchmark path rather than on `xlscc`, `opt_main`, or `codegen_main`.

### Other Bazel targets you may still build outside the agent loop

Even though they are not part of the current automated workflow, you may still build other XLS tools for setup, preprocessing, or separate experiments, for example:

```bash
bazel build -c opt //xls/contrib/xlscc:xlscc
bazel build -c opt //xls/tools:opt_main
bazel build -c opt //xls/tools:codegen_main
```

These are useful when you want to:

- translate C++ into IR
- optimize IR before benchmarking
- generate Verilog or inspect code generation behavior

However, they are not the primary runtime dependency of `xls/run_agent.py`.

## Input dataset

The current local dataset directory is:

- `my_ir_dataset/`

At the time of writing, it contains these IR benchmarks:

- `adler32.ir`
- `crc32.ir`
- `matmul_4x4_opt_ir.ir`
- `prefix_sum.ir`
- `sha256.ir`
- `sparse_prefix_sum.ir`

## End-to-end automated flow with `run_agent.py`

Run the full workflow with:

```bash
python3 xls/run_agent.py
```

### What `run_agent.py` does

For each iteration from `1` to `5`, and for each attempt from `1` to `5`, the script performs the following steps:

1. Collects historical CSV results from:
   - `PPA_report_baseline/summary_metrics.csv`
   - earlier iteration attempt outputs such as `iteration_i/try_j/summary_metrics.csv`
2. Generates an agent prompt in:
   - `xls/generated_prompts/prompt_iter_<iteration>_try_<attempt>.txt`
3. Runs Codex with:

```bash
codex exec -C <repo_root> --dangerously-bypass-approvals-and-sandbox <prompt_file>
```

4. Expects the agent to write:
   - `iteration_<i>/next_attempt.json`
   - `iteration_<i>/strategy_notes.txt`
5. Reads the selected pipeline stage from `next_attempt.json`
6. Builds `//xls/dev_tools:benchmark_main` once per iteration
7. Runs the benchmark sweep with `xls/PPA_script.sh`
8. Extracts features with `xls/extract_ppa_features.sh`
9. Records the chosen stage in:
   - `iteration_<i>/try_<j>/used_stage.json`

### Agent decision rules encoded in `run_agent.py`

The orchestrator instructs the agent to:

- create or update scheduling strategy `iter_<i>`
- use stage `5` for attempt `1`
- choose a new stage between `3` and `20` for later attempts
- avoid reusing a stage already tried in the same iteration
- choose stages based on prior CSV evidence rather than randomly

The prompt also tells the agent not to run Bazel or shell scripts directly. The Python orchestrator is responsible for execution.

## Manual benchmark execution

If you want to run a single attempt manually instead of the full orchestrator, use:

```bash
bash xls/PPA_script.sh my_ir_dataset iteration_1/try_1 iter_1 5
```

Arguments:

1. input IR directory
2. output directory
3. scheduling strategy name
4. pipeline stage count

### What `PPA_script.sh` does

For every `*.ir` file in the input directory, the script runs:

```bash
./bazel-bin/xls/dev_tools/benchmark_main \
  --delay_model=sky130 \
  --generator=pipeline \
  --pipeline_stages=<N> \
  --scheduling_strategy=<strategy> \
  <input_ir>
```

Each benchmark writes a report file with this naming pattern:

```text
<benchmark>_<strategy>_s<stages>_sched_report.txt
```

Example:

```text
iteration_1/try_1/sha256_iter_1_s5_sched_report.txt
```

## Feature extraction

After benchmark reports are generated, summarize them into one CSV with:

```bash
bash xls/extract_ppa_features.sh iteration_1/try_1 iteration_1/try_1/summary_metrics.csv
```

If the output CSV path is omitted, the script defaults to:

```text
<input_dir>/summary_metrics.csv
```

### What `extract_ppa_features.sh` extracts

For each `*_sched_report.txt`, the script parses:

- `benchmark`
- `scheduler`
- `pipeline_stages`
- `status`
- `scheduling_time_ms`
- `min_clock_period_ps`
- `total_pipeline_flops`
- `duplicate_flops`
- `min_stage_slack`
- `lines_of_verilog`
- `total_delay_ps`
- `total_area_um2`
- `error_note`
- `report_path`

The output CSV header is:

```text
benchmark,scheduler,pipeline_stages,status,scheduling_time_ms,min_clock_period_ps,total_pipeline_flops,duplicate_flops,min_stage_slack,lines_of_verilog,total_delay_ps,total_area_um2,error_note,report_path
```

### Status values

The extraction script classifies each report as one of:

- `success`
- `schedule_ok_codegen_failed`
- `failed`
- `unknown`

## Typical output layout

After one automated run, you should expect to see:

```text
iteration_1/
  next_attempt.json
  strategy_notes.txt
  try_1/
    summary_metrics.csv
    used_stage.json
    *_sched_report.txt
  try_2/
  ...
xls/generated_prompts/
xls/codex_logs/
```

## Recommended GitHub placement

If you want this documentation visible in the repository on GitHub, a good first step is to keep this file at the repository root and link it from `README.md`.

If you later want it integrated into a docs site, the same content can be moved into `docs_src/` and adapted to the upstream XLS documentation style.
