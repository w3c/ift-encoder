# Batch Testing Tools

Tools for running `//util:gen_ift_segmentation_plan` across a corpus of fonts
and comparing results across trials or separate output directories. Pass
`--helpshort` to inspect available flags).

## Collecting a list of fonts

For example using the Google Fonts repo:

```sh
find "<abslolute path to repo>/fonts/{ofl,apache,ufl}/" -iname "*.[ot]tf" > fonts.txt
```

## Running a batch segmentation job (`//batch_testing:run_gen_ift_segmentation_plan`)

Build and run directly against the current repository state via `bazel run -c opt`:

```sh
bazel run -c opt //batch_testing:run_gen_ift_segmentation_plan -- \
  --input_file=fonts.txt \
  --output_directory=/tmp/ift_batch/my_run \
  --jobs=40 \
  --timeout=2700 \
  --trial=control \
  --trial="candidate:--experimental_flag"
```

### Key options

- `--input_file` (or positional `input_file`): Path to a text file containing one
  font file path per line.
- `-o`, `--output_directory`: Directory (created automatically) where `out.scsv`,
  `error_log.txt`, and `NOTES.txt` are written.
- `--trial=NAME[:FLAGS]`: Repeatable flag defining each trial and any extra flags
  forwarded to `gen_ift_segmentation_plan` (use `--flag=value` for flag values).
  Defaults to a single `control` trial with no extra flags when omitted.
  - Runner meta-flags handled by the script:
    - `--noauto_config_primary_script`: Disables automatic CJK script selection
      from `METADATA.pb`.
    - `--cjk_only`: Skips non-CJK fonts for that trial.
    - `--auto_config_primary_script=Script_CJK`: Forces `Script_CJK` on CJK fonts.
- `-q`, `--qualities`: Comma-separated quality levels or ranges (default: `1,2,3,4,5,6,7`).
- `-j`, `--jobs`: Number of concurrent segmenter executions (default: CPU count).
- `-t`, `--timeout`: Per-execution timeout in seconds. If a `(font, trial)` times out
  at quality `q`, remaining higher quality levels for that `(font, trial)` are
  automatically recorded as timeouts.
- `-r`, `--resume`: Resume an interrupted run in `--output_directory`, skipping
  already completed rows in `out.scsv` and timeouts in `error_log.txt`.
- `--overwrite`: Allow overwriting an existing non-empty `out.scsv` when starting fresh.
- `--notes`: Optional extra note line to record in `NOTES.txt`.

## Comparing runs (`//batch_testing:compare_runs`)

### Usage (single dataset directory)

Generate a text report to stdout:

```sh
bazel run -c opt //batch_testing:compare_runs -- \
  /tmp/ift_batch/my_run control candidate
```

Generate an interactive HTML dashboard:

```sh
bazel run -c opt //batch_testing:compare_runs -- \
  /tmp/ift_batch/my_run control candidate --html --output=comparison.html
```

### Usage (two separate directories, e.g., different encoder commits)

When comparing runs executed in two separate output directories, pass two
`<dir>[:<trial>]` target specifications. When a directory contains only a single
trial (such as the default `control`), `:<trial>` may be omitted:

```sh
# Compare single-trial runs from two directories:
bazel run -c opt //batch_testing:compare_runs -- \
  /tmp/ift_batch/commit_a /tmp/ift_batch/commit_b

# Compare specific trials from multi-trial directories:
bazel run -c opt //batch_testing:compare_runs -- \
  /tmp/ift_batch/commit_a:control /tmp/ift_batch/commit_b:candidate --html --output=comparison.html
```

### Key `compare_runs` options

- `--quality` / `-q`: Restrict comparison to a single quality level (e.g. `--quality=7`).
- `--quality_mode`: When `--quality` is omitted, `common` (default) picks the highest
  quality completed in both trials per font, while `candidate` picks each font's
  highest completed quality in the candidate trial.
- `--format` (`text` or `html`), `--text`, `--html`: Output terminal text (default)
  or standalone HTML dashboard.
- `-o`, `--output`: Output file path.
- `--top_n`: Number of outlier fonts to display per category in text mode (default: 10).
