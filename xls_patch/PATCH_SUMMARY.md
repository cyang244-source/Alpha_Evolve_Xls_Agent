# Patch Summary

The following XLS-facing files were identified as project-specific modifications
 relative to the base branch used in this workspace:

## Scheduling core

- `xls/scheduling/BUILD`
- `xls/scheduling/heuristic_scheduler.cc`
- `xls/scheduling/heuristic_scheduler.h`
- `xls/scheduling/run_pipeline_schedule.cc`
- `xls/scheduling/scheduling_options.cc`
- `xls/scheduling/scheduling_options.h`

## Tooling / flags

- `xls/tools/scheduling_options_flags.cc`
- `xls/tools/scheduling_options_flags.proto`

## What these changes cover

- registration of additional scheduling strategies such as `iter_1` to `iter_5`
- heuristic scheduler implementation updates
- pipeline scheduling flow changes for iteration-specific heuristics
- command-line flag and proto support for the added scheduling strategies

If you want to reproduce the scheduler behavior from this project, these are the
first XLS files to compare or copy into a Google XLS checkout.
