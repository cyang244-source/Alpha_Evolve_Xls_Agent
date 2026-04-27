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

#include "xls/scheduling/heuristic_scheduler.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xls/common/status/status_macros.h"
#include "xls/ir/function.h"
#include "xls/ir/function_base.h"
#include "xls/ir/node.h"
#include "xls/ir/nodes.h"
#include "xls/ir/topo_sort.h"
#include "xls/scheduling/pipeline_schedule.h"
#include "xls/scheduling/schedule_graph.h"
#include "xls/scheduling/schedule_util.h"
#include "xls/scheduling/scheduling_options.h"

namespace xls {

namespace {

absl::StatusOr<int64_t> ComputeMaxStageDelayPs(
    FunctionBase* f, const PipelineSchedule& schedule,
    const DelayEstimator& delay_estimator) {
  int64_t max_stage_delay_ps = 0;
  absl::flat_hash_map<Node*, int64_t> completion_time_ps;
  for (Node* node : TopoSort(f)) {
    if (IsUntimed(node)) {
      completion_time_ps[node] = 0;
      continue;
    }
    int64_t start_time_ps = 0;
    for (Node* operand : node->operands()) {
      if (IsUntimed(operand)) {
        continue;
      }
      if (schedule.cycle(operand) == schedule.cycle(node)) {
        start_time_ps = std::max(start_time_ps, completion_time_ps[operand]);
      }
    }
    XLS_ASSIGN_OR_RETURN(int64_t node_delay_ps,
                         delay_estimator.GetOperationDelayInPs(node));
    completion_time_ps[node] = start_time_ps + node_delay_ps;
    max_stage_delay_ps =
        std::max(max_stage_delay_ps, completion_time_ps[node]);
  }
  return max_stage_delay_ps;
}

struct Objective {
  int64_t register_bits;
  int64_t max_stage_delay_ps;
};

struct NodeTimingData {
  absl::flat_hash_map<Node*, int64_t> completion_time_ps;
  absl::flat_hash_map<Node*, int64_t> node_delay_ps;
  std::vector<int64_t> stage_delay_ps;
};

absl::flat_hash_set<Node*> CollectFrozenNodes(
    FunctionBase* f, absl::Span<const SchedulingConstraint> constraints) {
  absl::flat_hash_set<Node*> frozen_nodes;
  for (const SchedulingConstraint& constraint : constraints) {
    if (std::holds_alternative<NodeInCycleConstraint>(constraint)) {
      frozen_nodes.insert(std::get<NodeInCycleConstraint>(constraint).GetNode());
    }
  }
  if (Function* function = dynamic_cast<Function*>(f)) {
    frozen_nodes.insert(function->params().begin(), function->params().end());
    if (!function->return_value()->Is<Param>()) {
      frozen_nodes.insert(function->return_value());
    }
  }
  return frozen_nodes;
}

absl::StatusOr<NodeTimingData> ComputeNodeTimingData(
    FunctionBase* f, const PipelineSchedule& schedule,
    const DelayEstimator& delay_estimator) {
  NodeTimingData timing_data;
  timing_data.stage_delay_ps.resize(schedule.length(), 0);
  for (Node* node : TopoSort(f)) {
    if (IsUntimed(node)) {
      timing_data.completion_time_ps[node] = 0;
      continue;
    }
    int64_t start_time_ps = 0;
    for (Node* operand : node->operands()) {
      if (IsUntimed(operand)) {
        continue;
      }
      if (schedule.cycle(operand) == schedule.cycle(node)) {
        start_time_ps =
            std::max(start_time_ps, timing_data.completion_time_ps[operand]);
      }
    }
    XLS_ASSIGN_OR_RETURN(int64_t node_delay_ps,
                         delay_estimator.GetOperationDelayInPs(node));
    int64_t completion_time_ps = start_time_ps + node_delay_ps;
    timing_data.completion_time_ps[node] = completion_time_ps;
    timing_data.node_delay_ps[node] = node_delay_ps;
    timing_data.stage_delay_ps[schedule.cycle(node)] =
        std::max(timing_data.stage_delay_ps[schedule.cycle(node)],
                 completion_time_ps);
  }
  return timing_data;
}

int64_t ComputeLiveRangeCost(const PipelineSchedule& schedule, Node* node) {
  if (IsUntimed(node)) {
    return 0;
  }
  int64_t live_range_stages = 0;
  for (int64_t stage = schedule.cycle(node); stage < schedule.length();
       ++stage) {
    if (schedule.IsLiveOutOfCycle(node, stage)) {
      ++live_range_stages;
    }
  }
  return live_range_stages * node->GetType()->GetFlatBitCount();
}

int64_t CountCrossStageUsers(const PipelineSchedule& schedule, Node* node) {
  if (IsUntimed(node)) {
    return 0;
  }
  int64_t cross_stage_uses = 0;
  for (Node* user : node->users()) {
    if (IsUntimed(user)) {
      continue;
    }
    if (schedule.cycle(user) > schedule.cycle(node)) {
      ++cross_stage_uses;
    }
  }
  return cross_stage_uses;
}

int64_t CountUsersInCycle(const PipelineSchedule& schedule, Node* node,
                          int64_t cycle) {
  if (IsUntimed(node)) {
    return 0;
  }
  int64_t users_in_cycle = 0;
  for (Node* user : node->users()) {
    if (IsUntimed(user)) {
      continue;
    }
    if (schedule.cycle(user) == cycle) {
      ++users_in_cycle;
    }
  }
  return users_in_cycle;
}

int64_t CountTimedUsers(Node* node) {
  int64_t timed_users = 0;
  for (Node* user : node->users()) {
    if (!IsUntimed(user)) {
      ++timed_users;
    }
  }
  return timed_users;
}

int64_t CountUsersBeyondCycle(const PipelineSchedule& schedule, Node* node,
                              int64_t cycle) {
  if (IsUntimed(node)) {
    return 0;
  }
  int64_t users_beyond_cycle = 0;
  for (Node* user : node->users()) {
    if (IsUntimed(user)) {
      continue;
    }
    if (schedule.cycle(user) > cycle) {
      ++users_beyond_cycle;
    }
  }
  return users_beyond_cycle;
}

int64_t SumUserDistanceBeyondCycle(const PipelineSchedule& schedule, Node* node,
                                   int64_t cycle) {
  if (IsUntimed(node)) {
    return 0;
  }
  int64_t total_distance = 0;
  for (Node* user : node->users()) {
    if (IsUntimed(user)) {
      continue;
    }
    if (schedule.cycle(user) > cycle) {
      total_distance += schedule.cycle(user) - cycle;
    }
  }
  return total_distance;
}

int64_t ComputeOperandCarryCost(const PipelineSchedule& schedule, Node* node,
                                int64_t target_cycle) {
  if (IsUntimed(node)) {
    return 0;
  }
  int64_t operand_carry_bits = 0;
  for (Node* operand : node->operands()) {
    if (IsUntimed(operand)) {
      continue;
    }
    if (schedule.cycle(operand) < target_cycle) {
      operand_carry_bits += operand->GetType()->GetFlatBitCount();
    }
  }
  return operand_carry_bits;
}

int64_t OperandCarryPriority(const PipelineSchedule& schedule, Node* node,
                             const HeuristicRefinementOptions& options) {
  if (!options.prioritize_operand_carry_cost || IsUntimed(node)) {
    return 0;
  }
  const int64_t current_cycle = schedule.cycle(node);
  const int64_t target_cycle =
      options.prefer_later_moves ? current_cycle + 1 : current_cycle - 1;
  if (target_cycle < 0 || target_cycle >= schedule.length()) {
    return std::numeric_limits<int64_t>::min();
  }
  // Prefer boundary splits that only drag a small operand footprint across the
  // new stage boundary. These moves can still relieve the tail of a slow stage
  // while avoiding the larger register-pressure spikes seen in iteration 1.
  return -ComputeOperandCarryCost(schedule, node, target_cycle);
}

int64_t DelayCarryEfficiencyPriority(
    const PipelineSchedule& schedule, Node* node,
    const NodeTimingData& timing_data,
    const HeuristicRefinementOptions& options) {
  if (!options.prioritize_delay_carry_efficiency || IsUntimed(node)) {
    return 0;
  }
  const int64_t current_cycle = schedule.cycle(node);
  const int64_t target_cycle =
      options.prefer_later_moves ? current_cycle + 1 : current_cycle - 1;
  if (target_cycle < 0 || target_cycle >= schedule.length()) {
    return std::numeric_limits<int64_t>::min();
  }
  const int64_t intrinsic_delay_ps = timing_data.node_delay_ps.at(node);
  const int64_t carry_bits =
      std::max<int64_t>(1, ComputeOperandCarryCost(schedule, node, target_cycle));
  // Favor boundary moves that remove a relatively large amount of local delay
  // per operand bit that must now cross the boundary. This directly targets
  // timing relief while screening out moves that would likely explode flops.
  return intrinsic_delay_ps * 1024 / carry_bits;
}

int64_t DownstreamConcentrationPriority(
    const PipelineSchedule& schedule, Node* node,
    const NodeTimingData& timing_data,
    const HeuristicRefinementOptions& options) {
  if (!options.prioritize_downstream_concentration || IsUntimed(node)) {
    return 0;
  }
  const int64_t current_cycle = schedule.cycle(node);
  const int64_t target_cycle =
      options.prefer_later_moves ? current_cycle + 1 : current_cycle - 1;
  if (target_cycle < 0 || target_cycle >= schedule.length()) {
    return std::numeric_limits<int64_t>::min();
  }
  const int64_t intrinsic_delay_ps = timing_data.node_delay_ps.at(node);
  const int64_t carry_bits =
      std::max<int64_t>(1, ComputeOperandCarryCost(schedule, node, target_cycle));
  const int64_t target_cycle_users =
      CountUsersInCycle(schedule, node, target_cycle);
  const int64_t farther_users =
      CountUsersBeyondCycle(schedule, node, target_cycle);
  const int64_t same_stage_users =
      CountUsersInCycle(schedule, node, current_cycle);
  // Favor moves where the immediate downstream stage already contains most
  // consumers. That keeps the timing benefit of splitting a critical tail node
  // while reducing the risk of values staying live across many later stages.
  return intrinsic_delay_ps * 2048 * (target_cycle_users + 1) /
         (carry_bits * (farther_users + same_stage_users + 1));
}

int64_t DownstreamDistanceBalancePriority(
    const PipelineSchedule& schedule, Node* node,
    const NodeTimingData& timing_data,
    const HeuristicRefinementOptions& options) {
  if (!options.prioritize_downstream_distance_balance || IsUntimed(node)) {
    return 0;
  }
  const int64_t current_cycle = schedule.cycle(node);
  const int64_t target_cycle =
      options.prefer_later_moves ? current_cycle + 1 : current_cycle - 1;
  if (target_cycle < 0 || target_cycle >= schedule.length()) {
    return std::numeric_limits<int64_t>::min();
  }
  const int64_t intrinsic_delay_ps = timing_data.node_delay_ps.at(node);
  const int64_t carry_bits =
      std::max<int64_t>(1, ComputeOperandCarryCost(schedule, node, target_cycle));
  const int64_t target_cycle_users =
      CountUsersInCycle(schedule, node, target_cycle);
  const int64_t same_stage_users =
      CountUsersInCycle(schedule, node, current_cycle);
  const int64_t farther_user_distance =
      SumUserDistanceBeyondCycle(schedule, node, target_cycle);
  // Favor timing-critical boundary splits whose fanout already sits near the
  // immediate downstream stage. Penalizing total distance beyond the target
  // stage screens out moves that would leave the value live across many later
  // stages and inflate duplicated storage.
  return intrinsic_delay_ps * 4096 * (target_cycle_users + 1) /
         (carry_bits * (same_stage_users + farther_user_distance + 1));
}

int64_t TailReliefDensityPriority(const PipelineSchedule& schedule, Node* node,
                                  const NodeTimingData& timing_data,
                                  const HeuristicRefinementOptions& options) {
  if (!options.prioritize_tail_relief_density || IsUntimed(node)) {
    return 0;
  }
  const int64_t current_cycle = schedule.cycle(node);
  const int64_t target_cycle =
      options.prefer_later_moves ? current_cycle + 1 : current_cycle - 1;
  if (target_cycle < 0 || target_cycle >= schedule.length()) {
    return std::numeric_limits<int64_t>::min();
  }
  const int64_t node_delay_ps = timing_data.node_delay_ps.at(node);
  const int64_t node_start_time_ps =
      timing_data.completion_time_ps.at(node) - node_delay_ps;
  const int64_t estimated_relief_ps = std::max<int64_t>(
      1, timing_data.stage_delay_ps[current_cycle] - node_start_time_ps);
  const int64_t carry_bits =
      std::max<int64_t>(1, ComputeOperandCarryCost(schedule, node, target_cycle));
  const int64_t next_stage_users =
      CountUsersInCycle(schedule, node, target_cycle);
  const int64_t farther_user_distance =
      SumUserDistanceBeyondCycle(schedule, node, target_cycle);
  // Favor moves that peel a large amount of delay off the current stage tail
  // per carried operand bit, but only when the moved value is mostly consumed
  // right across the boundary instead of staying live across many later stages.
  return estimated_relief_ps * 4096 * (next_stage_users + 1) /
         (carry_bits * (farther_user_distance + 1));
}

int64_t CrossStageUserPriority(const PipelineSchedule& schedule, Node* node,
                               const HeuristicRefinementOptions& options) {
  if (!options.prioritize_cross_stage_users) {
    return 0;
  }
  const int64_t cross_stage_users = CountCrossStageUsers(schedule, node);
  return options.prefer_fewer_cross_stage_users ? -cross_stage_users
                                                : cross_stage_users;
}

int64_t TotalUserPriority(Node* node,
                          const HeuristicRefinementOptions& options) {
  if (!options.prioritize_total_user_count) {
    return 0;
  }
  const int64_t timed_users = CountTimedUsers(node);
  return options.prefer_fewer_total_user_count ? -timed_users : timed_users;
}

int64_t DelayFanoutLeveragePriority(const PipelineSchedule& schedule, Node* node,
                                    const NodeTimingData& timing_data,
                                    const HeuristicRefinementOptions& options) {
  if (!options.prioritize_delay_fanout_leverage || IsUntimed(node)) {
    return 0;
  }
  const int64_t intrinsic_delay_ps = timing_data.node_delay_ps.at(node);
  const int64_t boundary_fanout =
      std::max<int64_t>(1, CountCrossStageUsers(schedule, node));
  // Favor critical-path nodes that both consume significant delay budget and
  // already feed across a stage boundary. These are the moves most likely to
  // relieve a slow stage without dragging a large cone of dependents along.
  return intrinsic_delay_ps * boundary_fanout;
}

int64_t BoundaryLeverageDensityPriority(
    const PipelineSchedule& schedule, Node* node,
    const NodeTimingData& timing_data,
    const HeuristicRefinementOptions& options) {
  if (!options.prioritize_boundary_leverage_density || IsUntimed(node)) {
    return 0;
  }
  const int64_t current_cycle = schedule.cycle(node);
  const int64_t target_cycle =
      options.prefer_later_moves ? current_cycle + 1 : current_cycle - 1;
  if (target_cycle < 0 || target_cycle >= schedule.length()) {
    return std::numeric_limits<int64_t>::min();
  }
  const int64_t intrinsic_delay_ps = timing_data.node_delay_ps.at(node);
  const int64_t carry_bits =
      std::max<int64_t>(1, ComputeOperandCarryCost(schedule, node, target_cycle));
  const int64_t boundary_users =
      std::max<int64_t>(1, CountCrossStageUsers(schedule, node));
  const int64_t next_stage_users =
      CountUsersInCycle(schedule, node, target_cycle);
  const int64_t farther_users =
      CountUsersBeyondCycle(schedule, node, target_cycle);
  // Prefer critical boundary-splitting moves whose existing downstream fanout
  // is already concentrated near the target stage. This keeps iter_3 focused
  // on high-leverage timing bottlenecks while screening out moves that would
  // extend live ranges far beyond the new boundary.
  return intrinsic_delay_ps * 4096 * boundary_users * (next_stage_users + 1) /
         (carry_bits * (farther_users + 1));
}

int64_t BoundaryReadinessPriority(const PipelineSchedule& schedule, Node* node,
                                  const NodeTimingData& timing_data,
                                  const HeuristicRefinementOptions& options) {
  if (!options.prioritize_boundary_readiness || IsUntimed(node)) {
    return 0;
  }
  const int64_t current_cycle = schedule.cycle(node);
  const int64_t target_cycle =
      options.prefer_later_moves ? current_cycle + 1 : current_cycle - 1;
  if (target_cycle < 0 || target_cycle >= schedule.length()) {
    return std::numeric_limits<int64_t>::min();
  }
  const int64_t same_stage_users =
      CountUsersInCycle(schedule, node, current_cycle);
  const int64_t next_stage_users =
      CountUsersInCycle(schedule, node, target_cycle);
  const int64_t downstream_users = CountCrossStageUsers(schedule, node);
  const int64_t farther_stage_users =
      CountUsersBeyondCycle(schedule, node, target_cycle);
  const int64_t carry_bits =
      std::max<int64_t>(1, ComputeOperandCarryCost(schedule, node, target_cycle));
  // Favor high-delay tail nodes that are already "pulled" toward the target
  // boundary: most of their fanout is already downstream, especially in the
  // immediate target stage, while same-stage consumers, farther-stage users,
  // and wide carried operands reduce the score.
  return timing_data.node_delay_ps.at(node) * 4096 * (downstream_users + 1) *
         (next_stage_users + 1) /
         (carry_bits * (same_stage_users + 1) * (farther_stage_users + 1));
}

int64_t NextStageAffinityPriority(const PipelineSchedule& schedule, Node* node,
                                  const NodeTimingData& timing_data,
                                  const HeuristicRefinementOptions& options) {
  if (!options.prioritize_next_stage_affinity || IsUntimed(node)) {
    return 0;
  }
  const int64_t current_cycle = schedule.cycle(node);
  const int64_t target_cycle =
      options.prefer_later_moves ? current_cycle + 1 : current_cycle - 1;
  if (target_cycle < 0 || target_cycle >= schedule.length()) {
    return std::numeric_limits<int64_t>::min();
  }
  const int64_t next_stage_users =
      CountUsersInCycle(schedule, node, target_cycle);
  const int64_t farther_stage_users =
      CountUsersBeyondCycle(schedule, node, target_cycle);
  const int64_t same_stage_users =
      CountUsersInCycle(schedule, node, current_cycle);
  const int64_t intrinsic_delay_ps = timing_data.node_delay_ps.at(node);
  // Prefer critical nodes whose fanout is already concentrated in the
  // immediately downstream stage. Those moves can still cut the slow-stage tail
  // but are less likely to create the deeper duplicated live ranges seen when
  // the fanout is spread across many later stages.
  return intrinsic_delay_ps * (next_stage_users + 1) -
         intrinsic_delay_ps * farther_stage_users - same_stage_users;
}

int64_t BoundaryContainmentDensityPriority(
    const PipelineSchedule& schedule, Node* node,
    const NodeTimingData& timing_data,
    const HeuristicRefinementOptions& options) {
  if (!options.prioritize_boundary_containment_density || IsUntimed(node)) {
    return 0;
  }
  const int64_t current_cycle = schedule.cycle(node);
  const int64_t target_cycle =
      options.prefer_later_moves ? current_cycle + 1 : current_cycle - 1;
  if (target_cycle < 0 || target_cycle >= schedule.length()) {
    return std::numeric_limits<int64_t>::min();
  }
  const int64_t next_stage_users =
      CountUsersInCycle(schedule, node, target_cycle);
  const int64_t same_stage_users =
      CountUsersInCycle(schedule, node, current_cycle);
  const int64_t farther_user_distance =
      SumUserDistanceBeyondCycle(schedule, node, target_cycle);
  const int64_t carry_bits =
      std::max<int64_t>(1, ComputeOperandCarryCost(schedule, node, target_cycle));
  const int64_t intrinsic_delay_ps = timing_data.node_delay_ps.at(node);
  // Favor delay-heavy boundary splits whose consumers cluster immediately
  // downstream and do not stay live far beyond the new boundary. Weighting by
  // farther-stage distance instead of only user count gives iter_5 a stronger
  // bias against moves that tend to create deep duplicated live ranges.
  return intrinsic_delay_ps * 4096 * (next_stage_users + 1) *
         (next_stage_users + 1) /
         (carry_bits * (same_stage_users + 1) * (farther_user_distance + 1));
}

absl::StatusOr<Objective> EvaluateObjective(
    FunctionBase* f, const DelayEstimator& delay_estimator,
    const PipelineSchedule& schedule) {
  XLS_ASSIGN_OR_RETURN(
      int64_t max_stage_delay_ps,
      ComputeMaxStageDelayPs(f, schedule, delay_estimator));
  // Use a schedule-safe register-pressure proxy here. The built-in
  // CountFinalInteriorPipelineRegisters() assumes every node has a scheduled
  // cycle, which is not true for untimed nodes like literals.
  int64_t register_bits = 0;
  for (int64_t stage = 0; stage < schedule.length(); ++stage) {
    for (Node* node : f->nodes()) {
      if (IsUntimed(node)) {
        continue;
      }
      if (schedule.cycle(node) > stage) {
        continue;
      }
      if (schedule.IsLiveOutOfCycle(node, stage)) {
        register_bits += node->GetType()->GetFlatBitCount();
      }
    }
  }
  return Objective{.register_bits = register_bits,
                   .max_stage_delay_ps = max_stage_delay_ps};
}

bool IsBetter(const Objective& candidate, const Objective& incumbent) {
  if (candidate.register_bits != incumbent.register_bits) {
    return candidate.register_bits < incumbent.register_bits;
  }
  return candidate.max_stage_delay_ps < incumbent.max_stage_delay_ps;
}

bool IsBetterDelayFirst(const Objective& candidate,
                        const Objective& incumbent) {
  if (candidate.max_stage_delay_ps != incumbent.max_stage_delay_ps) {
    return candidate.max_stage_delay_ps < incumbent.max_stage_delay_ps;
  }
  return candidate.register_bits < incumbent.register_bits;
}

bool PreservesTopologicalOrder(const PipelineSchedule& schedule, Node* node,
                               int64_t target_cycle) {
  for (Node* operand : node->operands()) {
    if (IsUntimed(operand)) {
      continue;
    }
    if (schedule.cycle(operand) > target_cycle) {
      return false;
    }
  }
  for (Node* user : node->users()) {
    if (IsUntimed(user)) {
      continue;
    }
    if (target_cycle > schedule.cycle(user)) {
      return false;
    }
  }
  return true;
}

bool IsCriticalStageNode(const PipelineSchedule& schedule, Node* node,
                         const NodeTimingData& timing_data) {
  if (IsUntimed(node)) {
    return false;
  }
  const int64_t stage = schedule.cycle(node);
  const int64_t node_stage_delay = timing_data.stage_delay_ps[stage];
  const int64_t max_stage_delay =
      *std::max_element(timing_data.stage_delay_ps.begin(),
                        timing_data.stage_delay_ps.end());
  return node_stage_delay * 10 >= max_stage_delay * 9;
}

bool IsCriticalPathNode(const PipelineSchedule& schedule, Node* node,
                        const NodeTimingData& timing_data) {
  if (IsUntimed(node)) {
    return false;
  }
  const int64_t stage = schedule.cycle(node);
  const int64_t stage_delay = timing_data.stage_delay_ps[stage];
  if (stage_delay == 0) {
    return false;
  }
  return timing_data.completion_time_ps.at(node) * 10 >= stage_delay * 9;
}

bool IsStageTailNode(const PipelineSchedule& schedule, Node* node,
                     const NodeTimingData& timing_data) {
  if (IsUntimed(node)) {
    return false;
  }
  const int64_t stage = schedule.cycle(node);
  const int64_t stage_delay = timing_data.stage_delay_ps[stage];
  if (stage_delay == 0) {
    return false;
  }
  // Focus later-move exploration on nodes that already sit near the end of the
  // stage timing chain, since splitting these nodes across a boundary is more
  // likely to improve clock period than moving short, non-critical work.
  return timing_data.completion_time_ps.at(node) * 10 >= stage_delay * 8;
}

bool IsSlowestStageNode(const PipelineSchedule& schedule, Node* node,
                        const NodeTimingData& timing_data) {
  if (IsUntimed(node)) {
    return false;
  }
  const int64_t max_stage_delay =
      *std::max_element(timing_data.stage_delay_ps.begin(),
                        timing_data.stage_delay_ps.end());
  return timing_data.stage_delay_ps[schedule.cycle(node)] == max_stage_delay;
}

std::vector<int64_t> GetMoveDeltas(const HeuristicRefinementOptions& options) {
  std::vector<int64_t> deltas;
  deltas.reserve(options.max_move_distance * 2);
  for (int64_t distance = 1; distance <= options.max_move_distance; ++distance) {
    if (options.prefer_later_moves) {
      deltas.push_back(distance);
      deltas.push_back(-distance);
    } else {
      deltas.push_back(-distance);
      deltas.push_back(distance);
    }
  }
  return deltas;
}

absl::StatusOr<bool> TryAdjacentMove(
    FunctionBase* f, const DelayEstimator& delay_estimator,
    const PipelineSchedule& current_schedule, Node* node, int64_t target_cycle,
    int64_t clock_period_ps, absl::Span<const SchedulingConstraint> constraints,
    std::optional<int64_t> worst_case_throughput,
    const HeuristicRefinementOptions& options,
    PipelineSchedule* improved_schedule, Objective* improved_objective) {
  ScheduleCycleMap candidate_cycle_map = current_schedule.GetCycleMap();
  candidate_cycle_map[node] = target_cycle;
  PipelineSchedule candidate_schedule(f, std::move(candidate_cycle_map),
                                      current_schedule.length(),
                                      current_schedule.min_clock_period_ps());
  absl::Status verify_status = candidate_schedule.Verify();
  if (!verify_status.ok()) {
    return false;
  }
  verify_status =
      candidate_schedule.VerifyTiming(clock_period_ps, delay_estimator);
  if (!verify_status.ok()) {
    return false;
  }
  verify_status = candidate_schedule.VerifyConstraints(constraints,
                                                       worst_case_throughput);
  if (!verify_status.ok()) {
    return false;
  }

  XLS_ASSIGN_OR_RETURN(Objective candidate_objective,
                       EvaluateObjective(f, delay_estimator, candidate_schedule));
  const bool improved =
      options.delay_first_objective
          ? IsBetterDelayFirst(candidate_objective, *improved_objective)
          : IsBetter(candidate_objective, *improved_objective);
  if (improved) {
    *improved_schedule = std::move(candidate_schedule);
    *improved_objective = candidate_objective;
    return true;
  }
  return false;
}

}  // namespace

absl::StatusOr<PipelineSchedule> HeuristicRefineSchedule(
    FunctionBase* f, const DelayEstimator& delay_estimator,
    const PipelineSchedule& seed_schedule, int64_t clock_period_ps,
    absl::Span<const SchedulingConstraint> constraints,
    std::optional<int64_t> worst_case_throughput,
    HeuristicRefinementOptions options) {
  PipelineSchedule current_schedule = seed_schedule;
  XLS_ASSIGN_OR_RETURN(
      Objective current_objective,
      EvaluateObjective(f, delay_estimator, current_schedule));
  const absl::flat_hash_set<Node*> frozen_nodes =
      CollectFrozenNodes(f, constraints);

  bool improved = true;
  int64_t iteration_count = 0;
  while (improved && iteration_count < 50) {
    improved = false;
    ++iteration_count;
    std::vector<Node*> candidate_nodes = TopoSort(f);
    std::optional<NodeTimingData> timing_data;
    if (options.prioritize_stage_delay) {
      XLS_ASSIGN_OR_RETURN(
          timing_data,
          ComputeNodeTimingData(f, current_schedule, delay_estimator));
      absl::c_stable_sort(candidate_nodes, [&](Node* lhs, Node* rhs) {
        const auto lhs_score = std::make_tuple(
            IsUntimed(lhs) ? -1
                           : timing_data->stage_delay_ps
                                 [current_schedule.cycle(lhs)],
            IsUntimed(lhs) ? -1 : timing_data->completion_time_ps[lhs],
            IsUntimed(lhs) ? std::numeric_limits<int64_t>::min()
                           : DelayCarryEfficiencyPriority(
                                 current_schedule, lhs, *timing_data, options),
            IsUntimed(lhs) ? std::numeric_limits<int64_t>::min()
                           : DownstreamConcentrationPriority(
                                 current_schedule, lhs, *timing_data, options),
            IsUntimed(lhs) ? std::numeric_limits<int64_t>::min()
                           : DownstreamDistanceBalancePriority(
                                 current_schedule, lhs, *timing_data, options),
            IsUntimed(lhs) ? std::numeric_limits<int64_t>::min()
                           : TailReliefDensityPriority(current_schedule, lhs,
                                                       *timing_data, options),
            IsUntimed(lhs) ? std::numeric_limits<int64_t>::min()
                           : BoundaryContainmentDensityPriority(
                                 current_schedule, lhs, *timing_data, options),
            IsUntimed(lhs) ? std::numeric_limits<int64_t>::min()
                           : NextStageAffinityPriority(current_schedule, lhs,
                                                       *timing_data, options),
            IsUntimed(lhs) ? std::numeric_limits<int64_t>::min()
                           : BoundaryReadinessPriority(current_schedule, lhs,
                                                       *timing_data, options),
            IsUntimed(lhs) ? std::numeric_limits<int64_t>::min()
                           : BoundaryLeverageDensityPriority(
                                 current_schedule, lhs, *timing_data, options),
            IsUntimed(lhs) ? -1
                           : DelayFanoutLeveragePriority(current_schedule, lhs,
                                                         *timing_data, options),
            TotalUserPriority(lhs, options),
            CrossStageUserPriority(current_schedule, lhs, options),
            OperandCarryPriority(current_schedule, lhs, options),
            options.prioritize_live_range_cost
                ? ComputeLiveRangeCost(current_schedule, lhs)
                : 0);
        const auto rhs_score = std::make_tuple(
            IsUntimed(rhs) ? -1
                           : timing_data->stage_delay_ps
                                 [current_schedule.cycle(rhs)],
            IsUntimed(rhs) ? -1 : timing_data->completion_time_ps[rhs],
            IsUntimed(rhs) ? std::numeric_limits<int64_t>::min()
                           : DelayCarryEfficiencyPriority(
                                 current_schedule, rhs, *timing_data, options),
            IsUntimed(rhs) ? std::numeric_limits<int64_t>::min()
                           : DownstreamConcentrationPriority(
                                 current_schedule, rhs, *timing_data, options),
            IsUntimed(rhs) ? std::numeric_limits<int64_t>::min()
                           : DownstreamDistanceBalancePriority(
                                 current_schedule, rhs, *timing_data, options),
            IsUntimed(rhs) ? std::numeric_limits<int64_t>::min()
                           : TailReliefDensityPriority(current_schedule, rhs,
                                                       *timing_data, options),
            IsUntimed(rhs) ? std::numeric_limits<int64_t>::min()
                           : BoundaryContainmentDensityPriority(
                                 current_schedule, rhs, *timing_data, options),
            IsUntimed(rhs) ? std::numeric_limits<int64_t>::min()
                           : NextStageAffinityPriority(current_schedule, rhs,
                                                       *timing_data, options),
            IsUntimed(rhs) ? std::numeric_limits<int64_t>::min()
                           : BoundaryReadinessPriority(current_schedule, rhs,
                                                       *timing_data, options),
            IsUntimed(rhs) ? std::numeric_limits<int64_t>::min()
                           : BoundaryLeverageDensityPriority(
                                 current_schedule, rhs, *timing_data, options),
            IsUntimed(rhs) ? -1
                           : DelayFanoutLeveragePriority(current_schedule, rhs,
                                                         *timing_data, options),
            TotalUserPriority(rhs, options),
            CrossStageUserPriority(current_schedule, rhs, options),
            OperandCarryPriority(current_schedule, rhs, options),
            options.prioritize_live_range_cost
                ? ComputeLiveRangeCost(current_schedule, rhs)
                : 0);
        return lhs_score > rhs_score;
      });
    } else if (options.prioritize_live_range_cost ||
               options.prioritize_cross_stage_users ||
               options.prioritize_operand_carry_cost) {
      absl::c_stable_sort(candidate_nodes, [&](Node* lhs, Node* rhs) {
        const auto lhs_score = std::make_tuple(
            CrossStageUserPriority(current_schedule, lhs, options),
            OperandCarryPriority(current_schedule, lhs, options),
            options.prioritize_live_range_cost
                ? ComputeLiveRangeCost(current_schedule, lhs)
                : 0);
        const auto rhs_score = std::make_tuple(
            CrossStageUserPriority(current_schedule, rhs, options),
            OperandCarryPriority(current_schedule, rhs, options),
            options.prioritize_live_range_cost
                ? ComputeLiveRangeCost(current_schedule, rhs)
                : 0);
        return lhs_score > rhs_score;
      });
    }
    PipelineSchedule best_schedule = current_schedule;
    Objective best_objective = current_objective;
    for (Node* node : candidate_nodes) {
      if (IsUntimed(node) || frozen_nodes.contains(node)) {
        continue;
      }
      if (options.critical_stage_only && timing_data.has_value() &&
          !IsCriticalStageNode(current_schedule, node, *timing_data)) {
        continue;
      }
      if (options.critical_path_only && timing_data.has_value() &&
          !IsCriticalPathNode(current_schedule, node, *timing_data)) {
        continue;
      }
      if (options.stage_tail_only && timing_data.has_value() &&
          !IsStageTailNode(current_schedule, node, *timing_data)) {
        continue;
      }
      if (options.slowest_stage_only && timing_data.has_value() &&
          !IsSlowestStageNode(current_schedule, node, *timing_data)) {
        continue;
      }
      if (options.prioritize_cross_stage_users &&
          CountCrossStageUsers(current_schedule, node) == 0) {
        continue;
      }
      const int64_t current_cycle = current_schedule.cycle(node);
      for (int64_t delta : GetMoveDeltas(options)) {
        const int64_t target_cycle = current_cycle + delta;
        if (target_cycle < 0 || target_cycle >= current_schedule.length()) {
          continue;
        }
        if (!PreservesTopologicalOrder(current_schedule, node, target_cycle)) {
          continue;
        }
        PipelineSchedule candidate_schedule = current_schedule;
        Objective candidate_objective = current_objective;
        XLS_ASSIGN_OR_RETURN(
            bool accepted,
            TryAdjacentMove(f, delay_estimator, current_schedule, node,
                            target_cycle, clock_period_ps, constraints,
                            worst_case_throughput, options,
                            &candidate_schedule, &candidate_objective));
        if (accepted) {
          if (options.best_improvement) {
            best_schedule = std::move(candidate_schedule);
            best_objective = candidate_objective;
          } else {
            current_schedule = std::move(candidate_schedule);
            current_objective = candidate_objective;
            improved = true;
            break;
          }
        }
      }
      if (improved) {
        break;
      }
    }
    if (!improved && options.best_improvement &&
        (best_objective.register_bits != current_objective.register_bits ||
         best_objective.max_stage_delay_ps !=
             current_objective.max_stage_delay_ps)) {
      current_schedule = std::move(best_schedule);
      current_objective = best_objective;
      improved = true;
    }
  }

  return current_schedule;
}

}  // namespace xls
