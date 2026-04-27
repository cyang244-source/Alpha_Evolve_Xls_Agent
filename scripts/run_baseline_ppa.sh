#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLS_DIR="${ROOT_DIR}/bazel-bin/xls"
XLSCC="${TOOLS_DIR}/contrib/xlscc/xlscc"
OPT_MAIN="${TOOLS_DIR}/tools/opt_main"
BENCHMARK_MAIN="${TOOLS_DIR}/dev_tools/benchmark_main"

OUT_DIR="${ROOT_DIR}/baseline_results"
RAW_DIR="${OUT_DIR}/raw_ir"
OPT_DIR="${OUT_DIR}/opt_ir"
LOG_DIR="${OUT_DIR}/logs"
SUMMARY_CSV="${OUT_DIR}/summary.csv"

mkdir -p "${RAW_DIR}" "${OPT_DIR}" "${LOG_DIR}"

cat > "${SUMMARY_CSV}" <<'EOF'
benchmark,source_kind,input_path,top,strategy,status,scheduling_time_ms,total_delay_ps,total_area_um2,min_clock_period_ps,notes,log_path
EOF

append_summary() {
  local benchmark="$1"
  local source_kind="$2"
  local input_path="$3"
  local top="$4"
  local strategy="$5"
  local status="$6"
  local scheduling_time_ms="$7"
  local total_delay_ps="$8"
  local total_area_um2="$9"
  local min_clock_period_ps="${10}"
  local notes="${11}"
  local log_path="${12}"

  notes="${notes//,/;}"
  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "${benchmark}" "${source_kind}" "${input_path}" "${top}" "${strategy}" \
    "${status}" "${scheduling_time_ms}" "${total_delay_ps}" "${total_area_um2}" \
    "${min_clock_period_ps}" "${notes}" "${log_path}" >> "${SUMMARY_CSV}"
}

extract_metric() {
  local pattern="$1"
  local log_path="$2"
  local value
  value="$(grep -E "${pattern}" "${log_path}" | head -n1 | sed -E "s/.*: ([0-9.]+).*/\\1/" || true)"
  echo "${value}"
}

extract_failure_note() {
  local log_path="$1"
  grep -E "Error:|Check failed:|fatal error:" "${log_path}" | head -n1 | sed 's/^[[:space:]]*//' || true
}

run_benchmark() {
  local benchmark="$1"
  local source_kind="$2"
  local input_path="$3"
  local top="$4"
  local strategy="$5"

  local safe_name="${benchmark}_${strategy}"
  local log_path="${LOG_DIR}/${safe_name}.log"

  if "${BENCHMARK_MAIN}" "${input_path}" \
    --delay_model=asap7 \
    --generator=pipeline \
    --pipeline_stages=2 \
    --top="${top}" \
    --scheduling_strategy="${strategy}" \
    --run_evaluators=false > "${log_path}" 2>&1; then
    append_summary \
      "${benchmark}" \
      "${source_kind}" \
      "${input_path}" \
      "${top}" \
      "${strategy}" \
      "ok" \
      "$(extract_metric 'Scheduling time:' "${log_path}")" \
      "$(extract_metric 'Total delay:' "${log_path}")" \
      "$(extract_metric 'Total area:' "${log_path}")" \
      "$(extract_metric 'Min clock period ps:' "${log_path}")" \
      "" \
      "${log_path}"
  else
    append_summary \
      "${benchmark}" \
      "${source_kind}" \
      "${input_path}" \
      "${top}" \
      "${strategy}" \
      "benchmark_failed" \
      "" \
      "" \
      "" \
      "" \
      "$(extract_failure_note "${log_path}")" \
      "${log_path}"
  fi
}

run_strategy_sweep() {
  local benchmark="$1"
  local source_kind="$2"
  local input_path="$3"
  local top="$4"

  for strategy in sdc min_cut asap random; do
    run_benchmark "${benchmark}" "${source_kind}" "${input_path}" "${top}" "${strategy}"
  done
}

run_xlscc_example() {
  local benchmark="$1"
  local source_path="$2"
  local block_class="$3"
  local top="$4"

  local raw_ir="${RAW_DIR}/${benchmark}.ir"
  local opt_ir="${OPT_DIR}/${benchmark}.opt.ir"
  local translate_log="${LOG_DIR}/${benchmark}_translate.log"
  local opt_log="${LOG_DIR}/${benchmark}_opt.log"

  if "${XLSCC}" "${source_path}" \
    --block_from_class "${block_class}" \
    --defines=__SYNTHESIS__,__xlscc__,__AC_OVERRIDE_OVF_UPDATE_BODY=,__AC_OVERRIDE_OVF_UPDATE2_BODY= \
    --include_dirs=xls/contrib/xlscc/synth_only,xls/contrib/xlscc/synth_only/ac_compat,.,${ROOT_DIR} \
    > "${raw_ir}" 2> "${translate_log}"; then
    if "${OPT_MAIN}" "${raw_ir}" > "${opt_ir}" 2> "${opt_log}"; then
      run_strategy_sweep "${benchmark}" "xlscc_example" "${opt_ir}" "${top}"
    else
      append_summary \
        "${benchmark}" \
        "xlscc_example" \
        "${raw_ir}" \
        "${top}" \
        "opt" \
        "opt_failed" \
        "" \
        "" \
        "" \
        "" \
        "$(extract_failure_note "${opt_log}")" \
        "${opt_log}"
    fi
  else
    append_summary \
      "${benchmark}" \
      "xlscc_example" \
      "${source_path}" \
      "${top}" \
      "translate" \
      "translate_failed" \
      "" \
      "" \
      "" \
      "" \
      "$(extract_failure_note "${translate_log}")" \
      "${translate_log}"
  fi
}

main() {
  echo "Writing baseline results into ${OUT_DIR}"

  run_xlscc_example \
    "simple_pipelined_loop" \
    "${ROOT_DIR}/xls/contrib/xlscc/examples/simple_pipelined_loop.cc" \
    "MyBlock" \
    "MyBlock_proc"
  run_xlscc_example \
    "simple_backwards_pipelined_loop" \
    "${ROOT_DIR}/xls/contrib/xlscc/examples/simple_backwards_pipelined_loop.cc" \
    "MyBlock" \
    "MyBlock_proc"
  run_xlscc_example \
    "simple_nested_pipelined_loop" \
    "${ROOT_DIR}/xls/contrib/xlscc/examples/simple_nested_pipelined_loop.cc" \
    "MyBlock" \
    "MyBlock_proc"
  run_xlscc_example \
    "simple_unsigned_pipelined_loop" \
    "${ROOT_DIR}/xls/contrib/xlscc/examples/simple_unsigned_pipelined_loop.cc" \
    "MyBlock" \
    "MyBlock_proc"

  if [[ -f "${ROOT_DIR}/opt.ir" ]]; then
    run_strategy_sweep "sanity_opt_ir" "existing_ir" "${ROOT_DIR}/opt.ir" "test_opt"
  fi

  echo
  echo "Summary written to ${SUMMARY_CSV}"
  cat "${SUMMARY_CSV}"
}

main "$@"
