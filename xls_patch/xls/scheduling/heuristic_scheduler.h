// Copyright 2026 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef XLS_SCHEDULING_HEURISTIC_SCHEDULER_H_
#define XLS_SCHEDULING_HEURISTIC_SCHEDULER_H_

#include <cstdint>
#include <optional>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "xls/estimators/delay_model/delay_estimator.h"
#include "xls/ir/function_base.h"
#include "xls/scheduling/pipeline_schedule.h"
#include "xls/scheduling/scheduling_options.h"

namespace xls {

struct HeuristicRefinementOptions {
  bool prefer_later_moves = false;
  bool prioritize_live_range_cost = false;
  bool prioritize_stage_delay = false;
  bool prioritize_cross_stage_users = false;
  bool prioritize_total_user_count = false;
  bool prioritize_operand_carry_cost = false;
  bool prioritize_delay_carry_efficiency = false;
  bool prioritize_downstream_concentration = false;
  bool prioritize_downstream_distance_balance = false;
  bool prioritize_tail_relief_density = false;
  bool prioritize_delay_fanout_leverage = false;
  bool prioritize_boundary_leverage_density = false;
  bool prioritize_boundary_readiness = false;
  bool prioritize_boundary_containment_density = false;
  bool prioritize_next_stage_affinity = false;
  bool prefer_fewer_cross_stage_users = false;
  bool prefer_fewer_total_user_count = false;
  bool stage_tail_only = false;
  bool slowest_stage_only = false;
  bool delay_first_objective = false;
  bool critical_stage_only = false;
  bool critical_path_only = false;
  bool best_improvement = false;
  int64_t max_move_distance = 1;
};

// Refines a legal seed schedule using a simple register-pressure-aware local
// search. The heuristic considers adjacent stage moves which preserve timing
// and constraints, accepting the first move that improves the objective.
absl::StatusOr<PipelineSchedule> HeuristicRefineSchedule(
    FunctionBase* f, const DelayEstimator& delay_estimator,
    const PipelineSchedule& seed_schedule, int64_t clock_period_ps,
    absl::Span<const SchedulingConstraint> constraints,
    std::optional<int64_t> worst_case_throughput,
    HeuristicRefinementOptions options = {});

}  // namespace xls

#endif  // XLS_SCHEDULING_HEURISTIC_SCHEDULER_H_
