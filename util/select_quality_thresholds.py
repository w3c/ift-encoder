#!/usr/bin/env python3
"""Analyzes segmenter runtime data and selects codepoint thresholds for AutoPickQuality().

Given an out.scsv file and an error_log.txt file from a batch segmenter run across
quality levels 1..7, this utility uses dynamic programming to find monotonic
codepoint count thresholds for quality levels 7 down to 1 that maximize the
average quality level across the font corpus while ensuring that at least 99.5%
(configurable via --min-completion-rate) of the fonts in each bucket complete
segmentation within the timeout (default 15 minutes / 900 seconds).

Data files can be generated via: //batch_testing:run_gen_ift_segmentation_plan

Usage:
  ./util/select_quality_thresholds.py /path/to/dataset_dir_or_scsv [options]
"""

import argparse
from collections import defaultdict
import csv
import math
import os
from pathlib import Path
import re
import sys

QUALITY_NAMES = {
    1: "ONE",
    2: "TWO",
    3: "THREE",
    4: "FOUR",
    5: "FIVE",
    6: "SIX",
    7: "SEVEN",
}

def resolve_dataset_paths(
    dataset_arg: str | None, scsv_arg: str | None, error_log_arg: str | None
) -> tuple[Path, Path | None]:
  """Resolves the out.scsv and error_log.txt paths from CLI arguments."""
  scsv_path = None
  error_log_path = None

  if scsv_arg:
    scsv_path = Path(scsv_arg)
  elif dataset_arg:
    p = Path(dataset_arg)
    if p.is_dir():
      scsv_path = p / "out.scsv"
      if not error_log_arg and (p / "error_log.txt").exists():
        error_log_path = p / "error_log.txt"
    else:
      scsv_path = p
      if not error_log_arg and (p.parent / "error_log.txt").exists():
        error_log_path = p.parent / "error_log.txt"

  if error_log_arg:
    error_log_path = Path(error_log_arg)

  if scsv_path is None or not scsv_path.is_file():
    sys.exit(f"Error: Could not find SCSV data file at '{scsv_path}'.")

  if error_log_path is not None and not error_log_path.is_file():
    sys.exit(f"Error: Could not find error log file at '{error_log_path}'.")

  return scsv_path, error_log_path


def load_dataset(
    scsv_path: Path,
    error_log_path: Path | None,
    trial_filter: str | None = None,
):
  """Loads runtime records from out.scsv and timeout records from error_log.txt."""
  fonts_cp: dict[str, int] = {}
  runs: dict[str, dict[int, float]] = defaultdict(dict)
  trials_seen: set[str] = set()

  with scsv_path.open("r", encoding="utf-8") as f:
    reader = csv.DictReader(f, delimiter=";", skipinitialspace=True)
    for row in reader:
      if not row or not row.get("font_path"):
        continue
      font_path = row["font_path"].strip()
      if font_path == "font_path":
        continue
      trial = row.get("trial", "").strip()
      if trial_filter and trial != trial_filter:
        continue
      trials_seen.add(trial)

      q = int(row["quality_level"].strip())
      cp = int(row["codepoint_count"].strip())
      total_time_s = float(row["total_time_s"].strip())

      fonts_cp[font_path] = cp
      runs[font_path][q] = total_time_s

  timeouts: dict[str, set[int]] = defaultdict(set)
  if error_log_path and error_log_path.is_file():
    timeout_re = re.compile(
        r"Timeout occurred for font:\s*(.+?)(?:,\s*trial:\s*(\S+))?\s+at"
        r" quality level:\s*(\d+)"
    )
    with error_log_path.open("r", encoding="utf-8", errors="replace") as f:
      for line in f:
        match = timeout_re.search(line)
        if not match:
          continue
        font_path = match.group(1).strip()
        trial = (match.group(2) or "").strip()
        q = int(match.group(3).strip())
        if trial_filter and trial and trial != trial_filter:
          continue
        if trial:
          trials_seen.add(trial)
        timeouts[font_path].add(q)

  if not trial_filter and len(trials_seen) > 1:
    sys.exit(
        f"Error: Multiple trials found in dataset ({sorted(trials_seen)})."
        " Please specify one with --trial."
    )

  # Remove fonts that timed out at all quality levels
  # and therefore do not appear in out.scsv.
  missing_cp_fonts = sorted(set(timeouts.keys()) - set(fonts_cp.keys()))
  for font_path in missing_cp_fonts:
    del timeouts[font_path]
    continue

  return fonts_cp, runs, timeouts


def select_buckets_dp(
    fonts_cp: dict[str, int],
    runs: dict[str, dict[int, float]],
    timeouts: dict[str, set[int]],
    timeout_s: float = 900.0,
    min_completion_rate: float = 0.98,
    step: int = 100,
    min_bucket_fonts: int = 1,
    strict_tail: bool = False,
):
  """Finds monotonic thresholds across quality levels 7..1 maximizing total quality."""
  max_timeout_rate = 1.0 - min_completion_rate + 1e-12
  max_cp = max(fonts_cp.values())

  if step > 1:
    candidates = list(range(step, ((max_cp // step) + 2) * step, step))
  else:
    candidates = sorted(set(fonts_cp.values()))

  pts = [0] + candidates
  num_intervals = len(pts) - 1

  prefix_count = [0] * (num_intervals + 1)
  prefix_timeouts = {q: [0] * (num_intervals + 1) for q in range(1, 8)}

  for i in range(1, num_intervals + 1):
    lo, hi = pts[i - 1], pts[i]
    bucket_fonts = [fp for fp, cp in fonts_cp.items() if lo < cp <= hi]
    prefix_count[i] = prefix_count[i - 1] + len(bucket_fonts)
    for q in range(1, 8):
      timeout_count = sum(
          1
          for fp in bucket_fonts
          if q in timeouts.get(fp, set())
          or runs.get(fp, {}).get(q, math.inf) >= timeout_s
      )
      prefix_timeouts[q][i] = prefix_timeouts[q][i - 1] + timeout_count

  # DP state: dp[q][i] = best (total_quality_sum, -total_timeouts, -num_buckets)
  # covering prefix of intervals [0..i) using quality levels in [q..7].
  neg_inf = (-10**18, -10**18, -10**18)
  dp = {q: [neg_inf] * (num_intervals + 1) for q in range(1, 9)}
  parent = {q: [None] * (num_intervals + 1) for q in range(1, 9)}
  dp[8][0] = (0, 0, 0)

  for q in range(7, 0, -1):
    # Option 1: do not use quality level q
    for i in range(num_intervals + 1):
      dp[q][i] = dp[q + 1][i]
      parent[q][i] = (q + 1, i, False)

    # Option 2: use quality level q on interval (pts[prev_i], pts[i]]
    for prev_i in range(num_intervals):
      if dp[q + 1][prev_i] == neg_inf:
        continue
      for i in range(prev_i + 1, num_intervals + 1):
        # Skip candidate upper bounds that add no fonts over i - 1 (unless i is
        # the final catch-all interval) so thresholds stay tight to the data.
        if i < num_intervals and prefix_count[i] == prefix_count[i - 1]:
          continue

        n = prefix_count[i] - prefix_count[prev_i]
        if n < min_bucket_fonts:
          continue

        timeouts = prefix_timeouts[q][i] - prefix_timeouts[q][prev_i]
        is_fallback_tail = (
            q == 1 and i == num_intervals and not strict_tail
        )
        if (timeouts / n) <= max_timeout_rate or is_fallback_tail:
          prev_q_sum, prev_neg_timeouts, prev_neg_b = dp[q + 1][prev_i]
          cand = (prev_q_sum + q * n, prev_neg_timeouts - timeouts, prev_neg_b - 1)
          if cand > dp[q][i]:
            dp[q][i] = cand
            parent[q][i] = (q + 1, prev_i, True)

  if dp[1][num_intervals] == neg_inf:
    return None

  # Reconstruct buckets
  buckets = []
  cur_q, cur_i = 1, num_intervals
  while cur_q <= 7:
    prev_q, prev_i, used = parent[cur_q][cur_i]
    if used:
      n = prefix_count[cur_i] - prefix_count[prev_i]
      to = prefix_timeouts[cur_q][cur_i] - prefix_timeouts[cur_q][prev_i]
      lo_cp = 1 if prev_i == 0 else pts[prev_i]
      hi_cp = None if cur_i == num_intervals else pts[cur_i]
      buckets.append({
          "quality": cur_q,
          "lo": lo_cp,
          "hi": hi_cp,
          "is_first": prev_i == 0,
          "timeouts": to,
          "total": n,
          "timeout_pct": (to / n) * 100.0,
          "completion_pct": ((n - to) / n) * 100.0,
      })
    cur_q, cur_i = prev_q, prev_i

  buckets.reverse()
  # The last bucket always extends to infinity in AutoPickQuality().
  if buckets:
    buckets[-1]["hi"] = None

  return buckets


def format_bin_label(bucket: dict[str, int | float | bool | None]) -> str:
  bracket = "[" if bucket["is_first"] else "("
  lo_part = f"{bracket}{bucket['lo']},"
  hi_str = "inf" if bucket["hi"] is None else f"{bucket['hi']}"
  return f"{lo_part:<7} {hi_str:>5}]"


def format_cpp_snippet(
    buckets: list[dict[str, int | float | bool | None]],
    timeout_min: float,
    min_completion_rate: float,
) -> str:
  """Generates the C++ AutoPickQuality() function with updated thresholds."""
  min_label = (
      f"{int(round(timeout_min))}"
      if abs(timeout_min - round(timeout_min)) < 1e-6
      else f"{round(timeout_min, 2):g}"
  )
  max_timeout_pct = (1.0 - min_completion_rate) * 100.0
  if abs(max_timeout_pct - round(max_timeout_pct, 1)) < 1e-6:
    pct_label = f"{max_timeout_pct:.1f}%"
  else:
    pct_label = f"{round(max_timeout_pct, 4):g}%"

  lines = [
      "static Quality AutoPickQuality(uint32_t codepoint_count) {",
      "  // These values were picked by looking at segmenter run times by"
      " quality level",
      "  // across the font collection from https://github.com/google/fonts/.",
      "  // Roughly they aim to keep the number of segmentations that take"
      f" >= {min_label} minutes",
      f"  // for a particular bucket at or below {pct_label}.",
      "  //",
      "  // Generated by: `bazel run util:select_quality_thresholds -- <data directory> --step=100`",
      "  //",
      f"  // {'Bin':<14}  {'Quality':<9} % >= {min_label} minutes",
  ]
  for b in buckets:
    bin_str = format_bin_label(b)
    lines.append(
        f"  // {bin_str:<14}  {b['quality']:<9d} {b['timeout_pct']:4.1f}"
    )

  for b in buckets[:-1]:
    q_enum = QUALITY_NAMES[int(b["quality"])]
    lines.append(f"  if (codepoint_count <= {b['hi']}) {{")
    lines.append(f"    return {q_enum};")
    lines.append("  }")

  last_q_enum = QUALITY_NAMES[int(buckets[-1]["quality"])]
  lines.append(f"  return {last_q_enum};")
  lines.append("}")
  return "\n".join(lines)


def main():
  parser = argparse.ArgumentParser(
      description=(
          "Select codepoint quality thresholds for"
          " AutoSegmenterConfig::AutoPickQuality()."
      )
  )
  parser.add_argument(
      "dataset",
      nargs="?",
      help="Path to dataset directory (containing out.scsv and error_log.txt) or out.scsv file.",
  )
  parser.add_argument(
      "--scsv",
      help="Explicit path to out.scsv file.",
  )
  parser.add_argument(
      "--error-log",
      help="Explicit path to error_log.txt file.",
  )
  parser.add_argument(
      "--trial",
      help="Filter dataset to a specific trial name.",
  )
  parser.add_argument(
      "-t",
      "--timeout",
      type=float,
      default=900.0,
      help="Timeout threshold in seconds (default: 900.0 = 15 minutes).",
  )
  parser.add_argument(
      "--min-completion-rate",
      type=float,
      default=0.995,
      help=(
          "Minimum fraction of fonts in each bucket that must complete within"
          " timeout (default: 0.995)."
      ),
  )
  parser.add_argument(
      "--step",
      type=int,
      default=100,
      help=(
          "Codepoint threshold rounding step size (default: 100; use 1 for"
          " exact codepoint counts)."
      ),
  )
  parser.add_argument(
      "--min-bucket-fonts",
      type=int,
      default=1,
      help="Minimum number of fonts required in a bucket (default: 1).",
  )
  parser.add_argument(
      "--strict-tail",
      action="store_true",
      help=(
          "Require even the final highest-codepoint fallback bucket to meet"
          " --min-completion-rate."
      ),
  )

  args = parser.parse_args()
  if not args.dataset and not args.scsv:
    parser.error("Must provide either a dataset path or --scsv.")

  scsv_path, error_log_path = resolve_dataset_paths(
      args.dataset, args.scsv, args.error_log
  )
  fonts_cp, runs, timeouts = load_dataset(
      scsv_path,
      error_log_path,
      trial_filter=args.trial,
  )

  if not fonts_cp:
    sys.exit("Error: No fonts loaded from dataset.")

  buckets = select_buckets_dp(
      fonts_cp,
      runs,
      timeouts,
      timeout_s=args.timeout,
      min_completion_rate=args.min_completion_rate,
      step=args.step,
      min_bucket_fonts=args.min_bucket_fonts,
      strict_tail=args.strict_tail,
  )

  if not buckets:
    sys.exit(
        "Error: Could not find any valid bucket assignment satisfying the"
        " constraints."
    )

  total_fonts = len(fonts_cp)
  total_timeouts = sum(int(b["timeouts"]) for b in buckets)
  avg_quality = (
      sum(int(b["quality"]) * int(b["total"]) for b in buckets) / total_fonts
  )
  timeout_min = args.timeout / 60.0

  print(f"Dataset SCSV:      {scsv_path}")
  print(f"Dataset Error Log: {error_log_path or 'None'}")
  print(
      f"Total Fonts:       {total_fonts}"
  )
  print(
      f"Target Criterion:  >= {args.min_completion_rate * 100:.1f}% completion "
      f"(< {timeout_min:.1f} min) per bucket (step={args.step})"
  )
  print(
      f"Overall Summary:   avg quality = {avg_quality:.2f}, "
      f"total timeouts = {total_timeouts}/{total_fonts} "
      f"({(total_timeouts / total_fonts) * 100:.2f}%)"
  )
  print()

  header = (
      f"{'Bin':<16}  {'Quality':>7}  {'Fonts':>7}  {'Timeouts':>8}  "
      f"{'% < Timeout':>11}  {'% >= Timeout':>12}"
  )
  print(header)
  print("-" * len(header))
  for b in buckets:
    bin_str = format_bin_label(b)
    print(
        f"{bin_str:<16}  {b['quality']:>7d}  {b['total']:>7d}  "
        f"{b['timeouts']:>8d}  {b['completion_pct']:>10.2f}%  "
        f"{b['timeout_pct']:>11.2f}%"
    )

  print("\nSuggested AutoPickQuality() implementation:\n")
  print(format_cpp_snippet(buckets, timeout_min, args.min_completion_rate))


if __name__ == "__main__":
  main()
