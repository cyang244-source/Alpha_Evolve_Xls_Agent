# Agent: XLS Scheduler Explorer

## Goal
Explore scheduling heuristics in the XLS repository to improve PPA metrics for 5 iterations.

## Context files
Before making changes, read:
xls/Agent_docs/goals.md
xls/Agent_docs/alphaevolve_notes.md
xls/Agent_docs/xls_scheduler_notes.md
xls/Agent_docs/benchmark_workflow.md

## Naming convention
For iteration i, the scheduling strategy name must be:
- iter_1 for iteration 1
- iter_2 for iteration 2
- iter_3 for iteration 3
- iter_4 for iteration 4
- iter_5 for iteration 5

For iteration i, the output directory must be exactly:
- iteration_1 for iteration 1
- iteration_2 for iteration 2
- iteration_3 for iteration 3
- iteration_4 for iteration 4
- iteration_5 for iteration 5

Each iteration-specific strategy name must correspond to the implementation created or modified in that iteration.
Do not reuse the same strategy name across different iterations.

## Behavior rules
1. Focus only on scheduling-related algorithm code and the evaluation scripts.
2. For each iteration, propose one distinct scheduling heuristic idea.
    The implementation may introduce a new heuristic rather than only a small variation of an existing one. Prefer heuristics that directly target register pressure, duplicate flops, stage assignment quality, or timing slack.
    However, keep the implementation remain explainable, buildable, and evaluable within the current workflow.
3. For iteration i, create or update a scheduling strategy whose name is exactly iter_i, and use a dedicated output directory named iteration_i/.
4. For each iteration, explore a heuristic scheduling algorithm that potentially improve power, performance and area. 
5. After each change, run:
   bazel --batch build --jobs=4 -c opt //xls/dev_tools:benchmark_main
6. When running the evaluation script, always pass the input directory, iteration-specific output directory, and iteration-specific scheduling strategy explicitly using the current script interface:
   bash xls/PPA_script.sh my_ir_dataset iteration_i iter_i
7. Then extract features from that same iteration directory using:
   bash xls/extract_ppa_features.sh iteration_i iteration_i/summary_metrics.csv
8. Save all logs, outputs, reports, extracted features, and summaries under iteration_i/. Never overwrite previous iteration results.
9. For each iteration, save a file iteration_i/strategy_notes.txt that explains:
   - the strategy name
   - which source files were changed
   - the heuristic idea
   - how it differs from the previous iteration
10. If build or evaluation fails, first inspect the error and attempt one small fix.
11. Retry the failed step once.
12. If the build or evaluation still fails after one retry, revert that iteration and mark iter_i as failed.
13. Compare each iteration against the baseline and the current best result. The baseline PPA result is located at:
    PPA_report_baseline/summary_metrics.csv
14. At the end, summarize all 5 iterations and identify the best heuristic.

## Safety / scope
- Do not modify unrelated parts of the repository.
- Do not make large refactors.
- Do not remove files.
- Keep every change explainable.

## Output style
For each iteration, report:
- strategy name
- what changed
- why it may help
- whether build passed
- whether evaluation passed
- extracted PPA metrics
- whether it beat the current best