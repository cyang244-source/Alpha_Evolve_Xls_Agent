#!/usr/bin/env bash

set -euo pipefail

INPUT_DIR="${1:-my_ir_dataset}"
OUTPUT_DIR="${2:-PPA_report_3rd}"
SCHEDULING_STRATEGY="${3:-heuristic}"
PIPELINE_STAGES="${4:-5}"

BENCHMARK_BIN="./bazel-bin/xls/dev_tools/benchmark_main"
DELAY_MODEL="sky130"

mkdir -p "${OUTPUT_DIR}"

for file in "${INPUT_DIR}"/*.ir; do
  filename=$(basename "${file}" .ir)
  output_path="${OUTPUT_DIR}/${filename}_${SCHEDULING_STRATEGY}_s${PIPELINE_STAGES}_sched_report.txt"

  cmd=(
    "${BENCHMARK_BIN}"
    "--delay_model=${DELAY_MODEL}"
    "--generator=pipeline"
    "--pipeline_stages=${PIPELINE_STAGES}"
    "--scheduling_strategy=${SCHEDULING_STRATEGY}"
    "${file}"
  )

  if "${cmd[@]}" > "${output_path}" 2>&1; then
    echo "Completed ${filename} with ${SCHEDULING_STRATEGY} (stages=${PIPELINE_STAGES})"
  else
    echo "FAILED ${filename} with ${SCHEDULING_STRATEGY}; see ${output_path}"
  fi
done