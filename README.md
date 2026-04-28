# Alpha Evolve XLS Agent

Alpha Evolve XLS Agent is a research project that applies an AlphaEvolve-style AI-agent workflow to the scheduling stage of [Google XLS](https://github.com/google/xls). The goal is to let an AI agent propose scheduling heuristics, evaluate them through the real XLS toolchain, and use measured results to guide further exploration.

This repository is intended as a clean project wrapper around that workflow. It contains the project scripts, benchmark inputs, documentation, and agent guidance used in the experiments.

## Overview

This project focuses on two related tasks:

- generating new heuristic schedulers for XLS using an AI agent
- exploring pipeline-stage configurations based on benchmark feedback

The workflow is centered on `benchmark_main`, which makes it possible to evaluate scheduling behavior directly on existing XLS IR benchmarks without repeatedly running the full front-end compilation flow.

## Key Features

- AI-agent-driven scheduler exploration inspired by AlphaEvolve
- iterative evaluation loop built around XLS `benchmark_main`
- automated benchmark execution and CSV feature extraction
- fixed IR benchmark set for controlled comparison
- structured agent prompts and scheduling notes for reproducible runs

## Repository Structure

```text
Alpha_Evolve_XLS_Agent/
  README.md
  .gitignore
  docs/
    workflow.md
    baseline_report.md
  scripts/
    run_agent.py
    PPA_script.sh
    extract_ppa_features.sh
    run_baseline_ppa.sh
  agent_docs/
    Agents.md
    scheduling.md
    alpha_evolve_note.md
    README.md
  datasets/
    ir/
  xls_patch/
  knowledge/
```

## What This Repository Includes

- `docs/`: project-facing documentation, including the workflow description and baseline notes
- `scripts/`: runnable scripts for agent orchestration, benchmark execution, and feature extraction
- `agent_docs/`: prompt context and scheduling instructions used by the AI agent
- `datasets/ir/`: XLS IR benchmarks used in the experiments
- `xls_patch/`: intended location for modified XLS source files or patch files

## Dependency on Google XLS

This repository is **not** a standalone replacement for Google XLS.

It is built on top of the upstream XLS codebase and is meant to be used together with a compatible checkout of:

- `https://github.com/google/xls`

In other words, this repository provides the research workflow and project-specific assets, while Google XLS provides the underlying compiler, scheduler, and build system.

## Reproducing the Workflow

To reproduce the experiments, a user should:

1. Clone this repository.
2. Clone the upstream Google XLS repository.
3. Apply the modified XLS scheduling files or patches from `xls_patch/` to the XLS checkout.
4. Build the required XLS target:

```bash
bazel --batch build --jobs=16 -c opt //xls/dev_tools:benchmark_main
```

5. Run the project scripts from the XLS workspace together with the provided dataset and agent documentation.

The detailed workflow is described in:

- `docs/workflow.md`
- `docs/reproduction_guide.md`

If you want the shortest setup path for running the workflow on top of a fresh
Google XLS checkout, see:

- `docs/quick_start.md`

## Main Experimental Finding

The project produced two main observations:

- the AI agent did not discover a scheduler that clearly outperformed the baseline XLS scheduler
- the agent was still useful for exploring pipeline-stage configurations and guiding the search process based on design needs

This makes the project valuable both as a scheduling study and as an example of how AI-agent loops can be integrated with a real compiler framework.

## Why This Project Matters

This repository demonstrates:

- compiler-oriented experimentation on top of a large open-source codebase
- integration of LLM-guided search with systems-level evaluation
- reproducible workflow design for hardware-synthesis research
- practical analysis of the limits of scheduler-only optimization

## Notes

This repository intentionally excludes Bazel build outputs, generated logs, and large experiment-result directories. It is meant to be a clean, project repository suitable for GitHub sharing use.
