# XLS Patch

This directory contains the XLS-specific source changes used by the project.

The files are organized to mirror the upstream `google/xls` repository layout so
that they can be compared against or copied into a compatible XLS checkout.

## Included file groups

- `xls/scheduling/`: scheduler implementation and scheduling-strategy plumbing
- `xls/tools/`: command-line flag support for the added scheduling strategies

## Current patch scope

The project-specific scheduling changes are centered on:

- adding iteration-specific scheduling strategies such as `iter_1` through `iter_5`
- extending scheduler option parsing and flag exposure
- modifying pipeline scheduling behavior in the XLS scheduling flow

## How to use

1. Clone a compatible checkout of the upstream Google XLS repository.
2. Compare the files in this directory against the corresponding upstream paths.
3. Copy the modified files into the XLS checkout or apply an equivalent patch.

This repository is intended for project presentation and reproducibility support;
it is not a full standalone fork of Google XLS.
