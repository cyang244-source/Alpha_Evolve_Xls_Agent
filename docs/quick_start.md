# Quick Start

This is the shortest setup flow for running the AI-agent-driven XLS workflow from scratch on top of a fresh Google XLS checkout.

## Commands

```bash
git clone https://github.com/google/xls.git
git clone https://github.com/ChiehCheng-Yang/Alpha_Evolve_XLS_Agent.git
cd xls

cp ../Alpha_Evolve_XLS_Agent/scripts/run_agent.py xls/
cp ../Alpha_Evolve_XLS_Agent/scripts/PPA_script.sh xls/
cp ../Alpha_Evolve_XLS_Agent/scripts/extract_ppa_features.sh xls/
cp ../Alpha_Evolve_XLS_Agent/scripts/run_baseline_ppa.sh xls/

mkdir -p xls/Agent_docs
cp ../Alpha_Evolve_XLS_Agent/agent_docs/* xls/Agent_docs/

mkdir -p my_ir_dataset
cp ../Alpha_Evolve_XLS_Agent/datasets/ir/*.ir my_ir_dataset/

bazel --batch build --jobs=16 -c opt //xls/dev_tools:benchmark_main
python3 xls/run_agent.py
```

## What this does

1. Clones the upstream Google XLS repository.
2. Clones this project repository.
3. Copies the workflow scripts into the XLS workspace.
4. Copies the agent prompt documents into `xls/Agent_docs/`.
5. Copies the IR benchmark dataset into `my_ir_dataset/`.
6. Builds `benchmark_main`.
7. Runs the AI-agent workflow.

## Notes

- This quick-start flow is intended for rerunning the AI-agent workflow, not for reusing pre-generated results.
- The workflow depends primarily on `//xls/dev_tools:benchmark_main`.
- If your environment does not already provide the required agent runtime or Python packages, you may need to install them before running `python3 xls/run_agent.py`.
