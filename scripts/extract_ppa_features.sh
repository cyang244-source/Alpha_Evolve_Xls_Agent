#!/usr/bin/env bash

set -euo pipefail

INPUT_DIR="${1:-PPA_report_baseline}"
OUTPUT_CSV="${2:-${INPUT_DIR}/summary_metrics.csv}"

mkdir -p "$(dirname "${OUTPUT_CSV}")"

extract_first_match() {
  local pattern="$1"
  local file="$2"
  grep -m1 -E "${pattern}" "${file}" || true
}

extract_numeric_suffix() {
  local pattern="$1"
  local file="$2"
  local line
  line="$(extract_first_match "${pattern}" "${file}")"
  if [[ -z "${line}" ]]; then
    echo ""
    return
  fi
  echo "${line}" | sed -E 's/.*: *([-0-9.]+).*/\1/'
}

extract_pipeline_flops() {
  local file="$1"
  local line
  line="$(extract_first_match 'Total pipeline flops:' "${file}")"
  if [[ -z "${line}" ]]; then
    echo ""
    return
  fi
  echo "${line}" | sed -E 's/.*Total pipeline flops: *([0-9]+).*/\1/'
}

extract_duplicate_flops() {
  local file="$1"
  local line
  line="$(extract_first_match 'Total pipeline flops:' "${file}")"
  if [[ -z "${line}" ]]; then
    echo ""
    return
  fi
  echo "${line}" | sed -E 's/.*\(([0-9]+) dups,.*/\1/'
}

extract_status() {
  local file="$1"
  local has_error=0
  local has_schedule=0
  local has_verilog=0

  grep -q 'Error:' "${file}" && has_error=1 || true
  grep -q 'Scheduling time:' "${file}" && has_schedule=1 || true
  grep -q 'Lines of Verilog:' "${file}" && has_verilog=1 || true

  if [[ "${has_error}" -eq 0 && "${has_schedule}" -eq 1 && "${has_verilog}" -eq 1 ]]; then
    echo "success"
  elif [[ "${has_error}" -eq 1 && "${has_schedule}" -eq 1 ]]; then
    echo "schedule_ok_codegen_failed"
  elif [[ "${has_error}" -eq 1 ]]; then
    echo "failed"
  else
    echo "unknown"
  fi
}

extract_error_note() {
  local file="$1"
  local line
  line="$(extract_first_match 'Error:' "${file}")"
  if [[ -z "${line}" ]]; then
    echo ""
    return
  fi
  echo "${line#Error: }" | sed 's/,/;/g'
}

detect_report_metadata() {
  local trimmed="$1"
  local scheduler=""
  local benchmark=""
  local pipeline_stages=""
  local stem="${trimmed}"

  if [[ "${stem}" =~ ^(.+)_s([0-9]+)$ ]]; then
    stem="${BASH_REMATCH[1]}"
    pipeline_stages="${BASH_REMATCH[2]}"
  fi

  # First try legacy scheduler names.
  for candidate_scheduler in heuristic hybrid min_cut sdc asap random; do
    if [[ "${stem}" == *"_${candidate_scheduler}" ]]; then
      scheduler="${candidate_scheduler}"
      benchmark="${stem%_"${candidate_scheduler}"}"
      echo "${benchmark},${scheduler},${pipeline_stages}"
      return
    fi
  done

  # Then try iteration-based scheduler names like iter_1, iter_2, ...
  if [[ "${stem}" =~ ^(.+)_((iter_[0-9]+))$ ]]; then
    benchmark="${BASH_REMATCH[1]}"
    scheduler="${BASH_REMATCH[2]}"
    echo "${benchmark},${scheduler},${pipeline_stages}"
    return
  fi

  # Fallback
  scheduler="unknown"
  benchmark="${stem}"
  echo "${benchmark},${scheduler},${pipeline_stages}"
}

{
  echo "benchmark,scheduler,pipeline_stages,status,scheduling_time_ms,min_clock_period_ps,total_pipeline_flops,duplicate_flops,min_stage_slack,lines_of_verilog,total_delay_ps,total_area_um2,error_note,report_path"

  shopt -s nullglob
  for report in "${INPUT_DIR}"/*_sched_report.txt; do
    base="$(basename "${report}")"
    trimmed="${base%_sched_report.txt}"

    detected="$(detect_report_metadata "${trimmed}")"
    IFS=',' read -r benchmark scheduler pipeline_stages <<< "${detected}"

    status="$(extract_status "${report}")"
    scheduling_time_ms="$(extract_numeric_suffix 'Scheduling time:' "${report}")"
    min_clock_period_ps="$(extract_numeric_suffix 'Min clock period ps:' "${report}")"
    total_pipeline_flops="$(extract_pipeline_flops "${report}")"
    duplicate_flops="$(extract_duplicate_flops "${report}")"
    min_stage_slack="$(extract_numeric_suffix 'Min stage slack:' "${report}")"
    lines_of_verilog="$(extract_numeric_suffix 'Lines of Verilog:' "${report}")"
    total_delay_ps="$(extract_numeric_suffix 'Total delay:' "${report}")"
    total_area_um2="$(extract_numeric_suffix 'Total area:' "${report}")"
    error_note="$(extract_error_note "${report}")"

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
      "${benchmark}" \
      "${scheduler}" \
      "${pipeline_stages}" \
      "${status}" \
      "${scheduling_time_ms}" \
      "${min_clock_period_ps}" \
      "${total_pipeline_flops}" \
      "${duplicate_flops}" \
      "${min_stage_slack}" \
      "${lines_of_verilog}" \
      "${total_delay_ps}" \
      "${total_area_um2}" \
      "${error_note}" \
      "${report}"
  done
} > "${OUTPUT_CSV}"

echo "Wrote ${OUTPUT_CSV}"
