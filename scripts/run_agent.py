#!/usr/bin/env python3

import csv
import os
import subprocess
from pathlib import Path
import sys
import json
import matplotlib.pyplot as plt


THIS_FILE = Path(__file__).resolve()
REPO = THIS_FILE.parent.parent
PROMPT_DIR = THIS_FILE.parent / "generated_prompts"
PROMPT_DIR.mkdir(exist_ok=True)
LOG_DIR = THIS_FILE.parent / "codex_logs"
LOG_DIR.mkdir(exist_ok=True)

DOC_LINES = [
    "Read these files first:",
    "- xls/Agent_docs/Agents.md",
    "- xls/Agent_docs/README.md",
    "- xls/Agent_docs/alpha_evolve_note.md",
    "- xls/Agent_docs/scheduling.md",
    "",
    "Use exactly these relative paths from the repository root.",
    "",
    "Do not ask for confirmation.",
    "Do not stop after summarizing the instructions.",
    "Begin execution immediately.",
    "",
    "Important execution rule:",
    "- Do not run bazel yourself.",
    "- Do not run xls/PPA_script.sh yourself.",
    "- Do not run xls/extract_ppa_features.sh yourself.",
    "- Those commands will be executed externally by the local Python orchestrator.",
    "- Your job is to modify code, choose the next pipeline_stages values, and write notes based on prior CSV evidence.",
    "- When creating a new scheduling strategy, do not just tweak or lightly wrap the existing SDC scheduler.",
    "- Prefer non-SDC scheduling ideas such as list scheduling, critical-path-first ranking, mobility/force-directed heuristics, cut/partition-based methods, register-pressure-aware heuristics, chaining-aware priorities, or ASAP/ALAP-derived urgency metrics.",
    "- If inspired by scheduling literature, adapt the idea into a practical heuristic for this codebase and name that inspiration briefly in strategy_notes.txt.",
    "",
]

def run_stream(cmd, cwd):
    print(f"\n[RUN] {' '.join(cmd)}\n")
    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        env=os.environ.copy(),
    )
    assert proc.stdout is not None
    output = []
    for line in proc.stdout:
        print(line, end="")
        output.append(line)
    rc = proc.wait()
    return rc, "".join(output)

def load_csv_rows(csv_path: Path):
    if not csv_path.exists():
        return []
    with csv_path.open("r", encoding="utf-8") as f:
        return list(csv.DictReader(f))

def summarize_rows(rows):
    if not rows:
        return "No CSV rows found."
    out = []
    for row in rows:
        out.append(
            f"{row.get('benchmark','?')}: "
            f"status={row.get('status','')}, "
            f"min_clock_period_ps={row.get('min_clock_period_ps','')}, "
            f"total_area_um2={row.get('total_area_um2','')}, "
            f"total_pipeline_flops={row.get('total_pipeline_flops','')}, "
            f"duplicate_flops={row.get('duplicate_flops','')}, "
            f"min_stage_slack={row.get('min_stage_slack','')}"
        )
    return "\n".join(out)

def collect_history_text(iteration: int, completed_attempts: int) -> str:
    chunks = []

    baseline_csv = REPO / "PPA_report_baseline" / "summary_metrics.csv"
    baseline_rows = load_csv_rows(baseline_csv)
    chunks.append("[baseline]")
    chunks.append(summarize_rows(baseline_rows))
    chunks.append("")

    # 前面 iteration 的全部結果
    for i in range(1, iteration):
        for attempt in range(1, 6):
            csv_path = REPO / f"iteration_{i}" / f"try_{attempt}" / "summary_metrics.csv"
            if csv_path.exists():
                rows = load_csv_rows(csv_path)
                chunks.append(f"[iteration_{i}/try_{attempt}]")
                chunks.append(summarize_rows(rows))
                chunks.append("")

    # 本 iteration 已完成的 attempts
    for attempt in range(1, completed_attempts + 1):
        csv_path = REPO / f"iteration_{iteration}" / f"try_{attempt}" / "summary_metrics.csv"
        if csv_path.exists():
            rows = load_csv_rows(csv_path)
            chunks.append(f"[iteration_{iteration}/try_{attempt}]")
            chunks.append(summarize_rows(rows))
            chunks.append("")

    return "\n".join(chunks).strip()

def make_prompt(iteration: int, attempt: int, history_text: str) -> Path:
    prompt_text = f"""
Read these files first:
- xls/Agent_docs/Agents.md
- xls/Agent_docs/README.md
- xls/Agent_docs/alpha_evolve_note.md
- xls/Agent_docs/scheduling.md

Use exactly these relative paths from the repository root.

Do not ask for confirmation.
Do not stop after summarizing the instructions.
Begin execution immediately.

Do not run bazel yourself.
Do not run xls/PPA_script.sh yourself.
Do not run xls/extract_ppa_features.sh yourself.
Those commands will be executed externally by the local Python orchestrator.

For this run, only work on iteration {iteration} and attempt {attempt}.

Use scheduling strategy iter_{iteration}.
If this is attempt 1, create or update scheduling strategy iter_{iteration} and implement one new heuristic idea intended to improve PPA-related behavior, especially performance.
If this is not attempt 1, do not create a new strategy; only choose the next pipeline stage and update notes.

Strategy-generation guidance for attempt 1:
- do not make iter_{iteration} just a light variation of the existing SDC scheduler
- prefer a non-SDC-based scheduling heuristic or priority rule
- acceptable inspiration sources include classic scheduling algorithms or papers, but translate them into a practical heuristic that fits this repository
- good directions include:
  - list scheduling with custom priorities
  - mobility / force-directed style heuristics
  - critical-path-first or fanout-aware ranking
  - cut / partition / clustering driven stage assignment
  - register-pressure-aware or duplication-aware scheduling
  - chaining-aware heuristics
  - ASAP/ALAP-derived urgency metrics
- avoid relying on SDC as the main decision engine; the strategy should be meaningfully distinct from "SDC with tweaks"
- mention the heuristic family or literature inspiration briefly in iteration_{iteration}/strategy_notes.txt

Choose exactly one pipeline_stages value for this attempt.

Rules:
- if attempt = 1, stage must be 5
- if attempt > 1, choose the next stage based on the CSV evidence below
- stage must be an integer from 3 through 20 inclusive
- do not choose a stage that has already been used in this iteration
- search for a better trade-off between:
  - min_clock_period_ps
  - duplicate_flops
- also consider:
  - total_pipeline_flops
  - min_stage_slack
  - approximate_time_latency_ps = pipeline_stages * min_clock_period_ps
- do not choose randomly
- use local exploration when appropriate

Use the following CSV evidence:
{history_text}

Write exactly this file:
iteration_{iteration}/next_attempt.json

The file must be:
{{
  "iteration": {iteration},
  "attempt": {attempt},
  "strategy": "iter_{iteration}",
  "stage": N,
  "reasoning": "why this stage was chosen based on previous attempt results"
}}

Also update:
iteration_{iteration}/strategy_notes.txt

Do not choose future attempts in advance.
Only decide the current attempt.
Stop after writing next_attempt.json and updating strategy_notes.txt.
""".strip()

    prompt_path = PROMPT_DIR / f"prompt_iter_{iteration}_try_{attempt}.txt"
    prompt_path.write_text(prompt_text, encoding="utf-8")
    return prompt_path

def run_codex(prompt_file: Path, iteration: int):
    cmd = [
        "codex",
        "exec",
        "-C", str(REPO),
        "--dangerously-bypass-approvals-and-sandbox",
        str(prompt_file),
    ]
    rc, out = run_stream(cmd, REPO)
    (LOG_DIR / f"codex_iter_{iteration}.log").write_text(out, encoding="utf-8")
    return rc

def run_build():
    cmd = [
        "/usr/local/bin/bazel",
        "--batch",
        "build",
        "--jobs=16",
        "-c",
        "opt",
        "//xls/dev_tools:benchmark_main",
    ]
    return run_stream(cmd, REPO)

def run_attempt(iteration: int, attempt: int, stage: int):
    outdir = f"iteration_{iteration}/try_{attempt}"
    strategy = f"iter_{iteration}"

    rc1, out1 = run_stream(
        ["bash", "xls/PPA_script.sh", "my_ir_dataset", outdir, strategy, str(stage)],
        REPO,
    )
    if rc1 != 0:
        return rc1, out1

    rc2, out2 = run_stream(
        ["bash", "xls/extract_ppa_features.sh", outdir, f"{outdir}/summary_metrics.csv"],
        REPO,
    )
    return rc2, out1 + "\n" + out2

def choose_stage_for_attempt(iteration: int, attempt: int):
    plan_path = REPO / f"iteration_{iteration}" / "next_attempt.json"
    if not plan_path.exists():
        raise FileNotFoundError(f"Missing next attempt plan: {plan_path}")

    data = json.loads(plan_path.read_text(encoding="utf-8"))
    stage = data.get("stage")

    if not isinstance(stage, int) or not (3 <= stage <= 20):
        raise ValueError(f"Invalid stage value in {plan_path}: {stage}")

    if attempt == 1 and stage != 5:
        raise ValueError(f"Attempt 1 must use stage 5, got {stage}")

    # 避免重複
    used = []
    for prev in range(1, attempt):
        prev_path = REPO / f"iteration_{iteration}" / f"try_{prev}" / "used_stage.json"
        if prev_path.exists():
            prev_data = json.loads(prev_path.read_text(encoding="utf-8"))
            used.append(prev_data["stage"])

    if stage in used:
        raise ValueError(f"Stage {stage} already used earlier in iteration {iteration}")

    return stage

def record_used_stage(iteration: int, attempt: int, stage: int):
    out = REPO / f"iteration_{iteration}" / f"try_{attempt}" / "used_stage.json"
    out.write_text(json.dumps({"stage": stage}, indent=2), encoding="utf-8")

def plot_iteration_tradeoff(iteration: int):
    records = []

    for attempt in range(1, 6):
        csv_path = REPO / f"iteration_{iteration}" / f"try_{attempt}" / "summary_metrics.csv"
        rows = load_csv_rows(csv_path)
        if not rows:
            continue

        # 你可以選 sha256 當代表，也可以之後改成 aggregate
        for row in rows:
            if row.get("benchmark") == "sha256" and row.get("status") == "success":
                stage = None
                plan_path = REPO / f"iteration_{iteration}" / "stage_plan.json"
                if plan_path.exists():
                    data = json.loads(plan_path.read_text(encoding="utf-8"))
                    stages = data.get("stages", [])
                    if len(stages) >= attempt:
                        stage = stages[attempt - 1]

                min_clk = float(row["min_clock_period_ps"])
                dup = float(row["duplicate_flops"]) if row["duplicate_flops"] else 0.0
                approx_latency = stage * min_clk if stage is not None else None

                records.append({
                    "attempt": attempt,
                    "stage": stage,
                    "min_clk": min_clk,
                    "dup": dup,
                    "approx_latency": approx_latency,
                })

    if not records:
        print(f"No plottable records for iteration {iteration}")
        return

    outdir = REPO / f"iteration_{iteration}"

    # Plot 1: duplicate_flops vs min_clock_period_ps
    plt.figure()
    for r in records:
        plt.scatter(r["dup"], r["min_clk"])
        plt.annotate(f"s{r['stage']}/t{r['attempt']}", (r["dup"], r["min_clk"]))
    plt.xlabel("duplicate_flops")
    plt.ylabel("min_clock_period_ps")
    plt.title(f"Iteration {iteration}: trade-off")
    plt.tight_layout()
    plt.savefig(outdir / "tradeoff_duplicate_vs_clock.png")
    plt.close()

    # Plot 2: pipeline stage vs min_clock_period_ps
    plt.figure()
    xs = [r["stage"] for r in records]
    ys = [r["min_clk"] for r in records]
    plt.plot(xs, ys, marker="o")
    for r in records:
        plt.annotate(f"t{r['attempt']}", (r["stage"], r["min_clk"]))
    plt.xlabel("pipeline_stages")
    plt.ylabel("min_clock_period_ps")
    plt.title(f"Iteration {iteration}: stage vs clock")
    plt.tight_layout()
    plt.savefig(outdir / "tradeoff_stage_vs_clock.png")
    plt.close()

    # Plot 3: pipeline stage vs approximate latency
    plt.figure()
    xs = [r["stage"] for r in records if r["approx_latency"] is not None]
    ys = [r["approx_latency"] for r in records if r["approx_latency"] is not None]
    plt.plot(xs, ys, marker="o")
    for r in records:
        if r["approx_latency"] is not None:
            plt.annotate(f"t{r['attempt']}", (r["stage"], r["approx_latency"]))
    plt.xlabel("pipeline_stages")
    plt.ylabel("approx_time_latency_ps")
    plt.title(f"Iteration {iteration}: stage vs approximate latency")
    plt.tight_layout()
    plt.savefig(outdir / "tradeoff_stage_vs_latency.png")
    plt.close()

def main():
    print("REPO =", REPO)

    for iteration in range(1, 6):
        print(f"\n========== ITERATION {iteration} ==========")

        build_done = False

        for attempt in range(1, 6):
            print(f"\n------ ITERATION {iteration} / ATTEMPT {attempt} ------")

            history_text = collect_history_text(iteration, attempt - 1)
            prompt_file = make_prompt(iteration, attempt, history_text)

            rc = run_codex(prompt_file, iteration)
            if rc != 0:
                print(f"Codex step failed at iteration {iteration}, attempt {attempt}.")
                return rc

            stage = choose_stage_for_attempt(iteration, attempt)
            print(f"Chosen stage for iteration {iteration}, attempt {attempt}: {stage}")

            if not build_done:
                rc, _ = run_build()
                if rc != 0:
                    print(f"Local build failed at iteration {iteration}.")
                    return rc
                build_done = True

            rc, _ = run_attempt(iteration=iteration, attempt=attempt, stage=stage)
            if rc != 0:
                print(f"Attempt {attempt} failed at iteration {iteration}.")
                return rc

            record_used_stage(iteration, attempt, stage)

        # plot_iteration_tradeoff(iteration)

    print("\nAll iterations completed.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
