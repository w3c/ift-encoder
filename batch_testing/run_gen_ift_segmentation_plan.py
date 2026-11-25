#!/usr/bin/env python3
"""Coordinates running gen_ift_segmentation_plan across a collection of fonts.

Usage:
  bazel run -c opt //batch_testing:run_gen_ift_segmentation_plan -- \\
    --input_file=fonts.txt --output_directory=/tmp/out \\
    --trial=control --trial='cand:--experimental_flag'
"""

from __future__ import annotations

import concurrent.futures
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import sys
import traceback
from typing import Optional, Sequence, Tuple

from absl import app
from absl import flags
from python.runfiles import Runfiles


DEFAULT_QUALITY_LEVELS = [1, 2, 3, 4, 5, 6, 7]
DEFAULT_QUALITIES_SPEC = "1,2,3,4,5,6,7"

SCSV_HEADER = (
    "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; "
    "non_ift_total_cost; uncovered_ift_patch_cost; total_time_s; codepoint_count"
)

TIME_PATTERN = re.compile(
    r"^CodepointToGlyphSegments took: ([0-9.]+) seconds$", re.MULTILINE
)
CODEPOINT_COUNT_PATTERN = re.compile(
    r"^number_of_codepoints = ([0-9]+)$", re.MULTILINE
)
IDEAL_COST_PATTERN = re.compile(
    r"^ideal_total_cost = ([0-9]+)$", re.MULTILINE
)
IFT_COST_PATTERN = re.compile(r"^ift_total_cost = ([0-9]+)$", re.MULTILINE)
NON_IFT_COST_PATTERN = re.compile(
    r"^non_ift_total_cost = ([0-9]+)$", re.MULTILINE
)
UNCOVERED_IFT_PATCH_COST_PATTERN = re.compile(
    r"^uncovered_ift_patch_cost = ([0-9]+)$", re.MULTILINE
)

TIMEOUT_PATTERN = re.compile(
    r"^Timeout occurred for font:\s*(.+?),\s*trial:\s*(\S+)\s+at quality"
    r" level:\s*(\d+)$"
)

META_FLAGS = frozenset({
    "--noauto_config_primary_script",
    "--cjk_only",
    "--auto_config_primary_script=Script_CJK",
})


def resolve_cli_path(
    path_str: str, build_working_dir: Optional[str] = None
) -> str:
  """Resolves relative CLI paths against BUILD_WORKING_DIRECTORY if set."""
  if build_working_dir and not os.path.isabs(path_str):
    return os.path.normpath(os.path.join(build_working_dir, path_str))
  return path_str


def parse_trials(trial_specs: Sequence[str]) -> dict[str, list[str]]:
  """Parses repeatable --trial NAME[:FLAGS] specifications into a trial map.

  When no --trial specs are given, defaults to {"control": []}.
  """
  if not trial_specs:
    return {"control": []}

  trials: dict[str, list[str]] = {}
  for spec in trial_specs:
    colon_idx = spec.find(":")
    eq_idx = spec.find("=")
    sep_indices = [idx for idx in (colon_idx, eq_idx) if idx != -1]
    if sep_indices:
      sep_idx = min(sep_indices)
      name = spec[:sep_idx]
      flags_str = spec[sep_idx + 1 :]
    else:
      name = spec
      flags_str = ""

    if not name:
      raise ValueError(f"Invalid --trial specification '{spec}': trial name cannot be empty.")
    if any(ch.isspace() for ch in name) or ";" in name:
      raise ValueError(
          f"Invalid trial name '{name}': must not contain whitespace or ';'."
      )

    try:
      tokens = shlex.split(flags_str) if flags_str.strip() else []
    except ValueError as e:
      raise ValueError(
          f"Invalid flags in --trial '{spec}': {e}"
      ) from e

    for token in tokens:
      if not token.startswith("-"):
        raise ValueError(
            f"Invalid flag token '{token}' in --trial '{spec}': "
            "each flag must start with '-' (use --flag=value for flag arguments)."
        )

    if name in trials:
      trials[name].extend(tokens)
    else:
      trials[name] = tokens

  return trials


def parse_qualities(spec: str) -> list[int]:
  """Parses a comma-separated quality spec (e.g. '1,2,3' or '1-7') into ints."""
  if not spec or not spec.strip():
    raise ValueError("Quality levels specification cannot be empty.")

  qualities: list[int] = []
  seen: set[int] = set()
  for raw_part in spec.split(","):
    part = raw_part.strip()
    if not part:
      raise ValueError(f"Empty item in quality specification '{spec}'.")
    if "-" in part:
      pieces = part.split("-")
      if len(pieces) != 2 or not pieces[0].strip() or not pieces[1].strip():
        raise ValueError(f"Invalid quality range '{part}' in '{spec}'.")
      try:
        start = int(pieces[0].strip())
        end = int(pieces[1].strip())
      except ValueError as e:
        raise ValueError(f"Invalid quality range '{part}' in '{spec}': {e}") from e
      if start < 0 or end < start:
        raise ValueError(
            f"Invalid quality range '{part}' in '{spec}': start must be >= 0 and <= end."
        )
      vals = range(start, end + 1)
    else:
      try:
        q = int(part)
      except ValueError as e:
        raise ValueError(f"Invalid quality level '{part}' in '{spec}': {e}") from e
      if q < 0:
        raise ValueError(f"Quality level must be >= 0, got {q}.")
      vals = (q,)

    for q in vals:
      if q not in seen:
        seen.add(q)
        qualities.append(q)

  if not qualities:
    raise ValueError(f"No quality levels parsed from '{spec}'.")
  return qualities


def find_segmenter_binary(runfiles: Optional[Runfiles] = None) -> str:
  """Locates the gen_ift_segmentation_plan binary via Bazel runfiles."""
  r = runfiles or Runfiles.Create()
  resolved = (
      r.Rlocation("ift_encoder/util/gen_ift_segmentation_plan")
      if r is not None
      else None
  )
  if resolved and os.path.isfile(resolved):
    real_resolved = os.path.realpath(resolved)
    if "-fastbuild" in real_resolved or "-dbg" in real_resolved:
      print(
          f"WARNING: Segmenter binary ({real_resolved}) appears to be a "
          "non-optimized build. Pass `-c opt` to `bazel run` for accurate "
          "runtime measurements.",
          file=sys.stderr,
      )
    return resolved

  raise FileNotFoundError(
      "Could not locate 'util/gen_ift_segmentation_plan' via Bazel runfiles "
      f"(runfiles={r is not None}, resolved={resolved!r}). "
      "Run via `bazel run -c opt //batch_testing:run_gen_ift_segmentation_plan`."
  )


def build_subprocess_env(runfiles: Optional[Runfiles] = None) -> dict[str, str]:
  """Builds environment variables so gen_ift_segmentation_plan resolves runfiles."""
  env = os.environ.copy()
  r = runfiles or Runfiles.Create()
  if r is not None:
    env.update(r.EnvVars())
  return env


def get_encoder_commit_info(repo_dir: Optional[str] = None) -> str:
  """Returns '<commit_sha>' or '<commit_sha> (dirty)' for the repository."""
  if repo_dir is None:
    repo_dir = os.environ.get("BUILD_WORKSPACE_DIRECTORY") or str(
        Path(__file__).resolve().parent.parent
    )
  try:
    rev = subprocess.run(
        ["git", "-C", repo_dir, "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
        timeout=5,
    )
    if rev.returncode != 0:
      return "unknown"
    commit = rev.stdout.strip() or "unknown"
    status = subprocess.run(
        ["git", "-C", repo_dir, "status", "--porcelain"],
        capture_output=True,
        text=True,
        timeout=5,
    )
    if status.returncode == 0 and status.stdout.strip():
      return f"{commit} (dirty)"
    return commit
  except (OSError, subprocess.SubprocessError):
    return "unknown"


def format_notes(
    *,
    commit_info: str,
    input_file: str,
    font_count: int,
    quality_levels: Sequence[int],
    timeout: Optional[float],
    jobs: int,
    trials: dict[str, list[str]],
    extra_notes: Optional[str] = None,
    timestamp: Optional[str] = None,
) -> str:
  """Formats the NOTES.txt content for a batch evaluation run."""
  ts = timestamp or datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
  timeout_str = f"{timeout:g}s" if timeout is not None else "None"
  lines = [
      f"Encoder Commit: {commit_info}",
      f"Timestamp: {ts}",
      f"Input File: {input_file} ({font_count} fonts)",
      f"Quality Levels Tested: {', '.join(str(q) for q in quality_levels)}",
      f"Timeout: {timeout_str}",
      f"Jobs: {jobs}",
  ]
  if extra_notes:
    lines.append(f"Notes: {extra_notes}")
  lines.append("")
  lines.append(f"TRIALS = {json.dumps(trials, indent=2)}")
  lines.append("")
  return "\n".join(lines)


def get_cjk_script_flag(font_path: str, trial_flags: Sequence[str]) -> list[str]:
  """Checks METADATA.pb for CJK subsets and returns the appropriate flag if applicable."""
  font_dir = os.path.dirname(font_path)
  metadata_path = os.path.join(font_dir, "METADATA.pb")

  if not os.path.isfile(metadata_path):
    return []

  subsets = []
  try:
    with open(metadata_path, "r", encoding="utf-8") as f:
      for line in f:
        line = line.strip()
        if line.startswith("subsets:"):
          match = re.search(r'subsets:\s*"([^"]+)"', line)
          if match:
            subsets.append(match.group(1))
  except Exception as e:
    print(f"Error reading {metadata_path}: {e}", file=sys.stderr)
    return []

  target_subsets = {
      "chinese-simplified",
      "chinese-hongkong",
      "chinese-traditional",
      "japanese",
      "korean",
  }
  found_target_subsets = [s for s in subsets if s in target_subsets]

  if len(found_target_subsets) == 0 and "--cjk_only" in trial_flags:
    return ["SKIP"]

  if "--auto_config_primary_script=Script_CJK" in trial_flags:
    if len(found_target_subsets) == 0:
      return []
    return ["--auto_config_primary_script=Script_CJK"]

  if "--noauto_config_primary_script" in trial_flags:
    return []

  if len(found_target_subsets) == 1:
    subset = found_target_subsets[0]
    script_map = {
        "chinese-simplified": "Script_chinese-simplified",
        "chinese-hongkong": "Script_chinese-traditional",
        "chinese-traditional": "Script_chinese-traditional",
        "japanese": "Script_japanese",
        "korean": "Script_korean",
    }
    script_value = script_map.get(subset)
    if script_value:
      return [f"--auto_config_primary_script={script_value}"]

  return []


def check_font(
    font_path: str,
    quality_level: int,
    trial_name: str,
    trial_flags: Sequence[str],
    segmenter_binary: str,
    timeout: Optional[float] = None,
    resolved_font_path: Optional[str] = None,
    subprocess_env: Optional[dict[str, str]] = None,
) -> Tuple[
    Optional[Tuple[str, int, str, int, int, int, int, float, int]],
    list[str],
]:
  """Runs gen_ift_segmentation_plan on a single font for a given quality and trial.

  Returns ((font_path, quality_level, trial_name, ideal_total_cost,
  ift_total_cost, non_ift_total_cost, uncovered_ift_patch_cost, total_time,
  codepoint_count) or None, error_lines).
  """
  actual_font_path = resolved_font_path or font_path
  extra_args = get_cjk_script_flag(actual_font_path, trial_flags) + [
      flag for flag in trial_flags if flag not in META_FLAGS
  ]

  cmd = [
      segmenter_binary,
      f"--input_font={actual_font_path}",
      f"--auto_config_quality={quality_level}",
      "--nooutput_segmentation_plan",
      "--output_segmentation_analysis",
  ] + extra_args

  errors: list[str] = []

  try:
    with subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
        env=subprocess_env,
    ) as proc:
      try:
        _, stderr_output = proc.communicate(timeout=timeout)
      except subprocess.TimeoutExpired:
        try:
          os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
          pass
        proc.communicate()
        raise

      if proc.returncode != 0:
        detail = f"command line return code {proc.returncode}"
        stderr_lines = [
            ln.strip() for ln in stderr_output.splitlines() if ln.strip()
        ]
        if stderr_lines and proc.returncode not in (143, -15):
          tail = stderr_lines[-1]
          if len(tail) > 200:
            tail = tail[:197] + "..."
          detail = f"{detail} ({tail})"
        errors.append(
            f"Error processing {font_path}, quality {quality_level}, "
            f"trial {trial_name}: {detail}"
        )
        return None, errors

    metric_specs = (
        ("codepoint_count", CODEPOINT_COUNT_PATTERN, int, "codepoint count"),
        ("ideal_total_cost", IDEAL_COST_PATTERN, int, "ideal total cost"),
        ("ift_total_cost", IFT_COST_PATTERN, int, "ift total cost"),
        ("non_ift_total_cost", NON_IFT_COST_PATTERN, int, "non-ift total cost"),
        (
            "uncovered_ift_patch_cost",
            UNCOVERED_IFT_PATCH_COST_PATTERN,
            int,
            "uncovered ift patch cost",
        ),
        ("total_time", TIME_PATTERN, float, "total time"),
    )
    parsed: dict[str, int | float] = {}
    for key, pattern, caster, label in metric_specs:
      match = pattern.search(stderr_output)
      if match:
        parsed[key] = caster(match.group(1))
      else:
        errors.append(
            f"Error processing {font_path}, quality {quality_level}, "
            f"trial {trial_name}: {label} not found in output"
        )

    if errors:
      return None, errors

  except subprocess.TimeoutExpired:
    raise
  except FileNotFoundError:
    raise
  except Exception as e:
    errors.append(
        f"Error processing {font_path}, quality {quality_level}, "
        f"trial {trial_name}: {e} {traceback.format_exc().strip()}"
    )
    return None, errors

  return (
      font_path,
      quality_level,
      trial_name,
      int(parsed["ideal_total_cost"]),
      int(parsed["ift_total_cost"]),
      int(parsed["non_ift_total_cost"]),
      int(parsed["uncovered_ift_patch_cost"]),
      float(parsed["total_time"]),
      int(parsed["codepoint_count"]),
  ), errors


def load_skip_set(
    out_scsv_path: str, error_log_path: str
) -> set[Tuple[str, int, str]]:
  """Loads previously completed or timed-out (font_path, quality, trial) tuples."""
  skip_set: set[Tuple[str, int, str]] = set()

  if os.path.isfile(out_scsv_path):
    with open(out_scsv_path, "r", encoding="utf-8") as f:
      for line in f:
        parts = [p.strip() for p in line.split(";")]
        if len(parts) >= 3 and parts[0] != "font_path":
          try:
            font_path = parts[0]
            quality_level = int(parts[1])
            trial_name = parts[2]
            skip_set.add((font_path, quality_level, trial_name))
          except ValueError:
            pass
  else:
    print(
        f"Warning: Resume file '{out_scsv_path}' not found. Starting from scratch.",
        file=sys.stderr,
    )

  if os.path.isfile(error_log_path):
    with open(error_log_path, "r", encoding="utf-8", errors="replace") as f:
      for line in f:
        match = TIMEOUT_PATTERN.match(line.strip())
        if match:
          font_path = match.group(1).strip()
          trial_name = match.group(2).strip()
          quality_level = int(match.group(3))
          skip_set.add((font_path, quality_level, trial_name))
  else:
    print(
        f"Warning: Resume error log file '{error_log_path}' not found.",
        file=sys.stderr,
    )

  return skip_set


def define_flags(fv: flags.FlagValues) -> None:
  """Registers command-line flags on the given FlagValues instance."""
  flags.DEFINE_string(
      "input_file",
      None,
      "Text file with one font file path per line (may also be passed as a "
      "positional argument).",
      flag_values=fv,
  )
  flags.DEFINE_string(
      "output_directory",
      None,
      "Output directory (created if needed) where out.scsv, error_log.txt, "
      "and NOTES.txt are written.",
      short_name="o",
      required=True,
      flag_values=fv,
  )
  flags.DEFINE_alias("output-directory", "output_directory", flag_values=fv)
  flags.DEFINE_multi_string(
      "trial",
      [],
      "Repeatable trial specification in the form NAME or 'NAME:FLAGS' "
      "(e.g. --trial=control --trial='cand:--experimental_flag'). "
      "Defaults to a single 'control' trial with no extra flags.",
      flag_values=fv,
  )
  flags.DEFINE_string(
      "qualities",
      DEFAULT_QUALITIES_SPEC,
      "Comma-separated quality levels or ranges to run (e.g. "
      "'1,2,3,4,5,6,7' or '1-7' or '7').",
      short_name="q",
      flag_values=fv,
  )
  flags.DEFINE_integer(
      "jobs",
      os.cpu_count() or 4,
      "Number of concurrent executions (default: number of CPUs).",
      short_name="j",
      lower_bound=1,
      flag_values=fv,
  )
  flags.DEFINE_float(
      "timeout",
      2700,
      "Timeout in seconds for each execution.",
      short_name="t",
      flag_values=fv,
  )
  flags.DEFINE_bool(
      "resume",
      False,
      "Resume from existing out.scsv and error_log.txt in "
      "--output_directory, skipping completed and timed-out tuples.",
      short_name="r",
      flag_values=fv,
  )
  flags.DEFINE_bool(
      "overwrite",
      False,
      "Overwrite existing out.scsv and error_log.txt in --output_directory "
      "if they already exist.",
      flag_values=fv,
  )
  flags.DEFINE_string(
      "notes",
      None,
      "Optional extra note line to record in NOTES.txt.",
      flag_values=fv,
  )


def _run_with_flags(
    positional_args: Sequence[str], fv: flags.FlagValues
) -> int:
  if fv.input_file and positional_args:
    print(
        "Error: Specify input file via --input_file or as a positional "
        "argument, not both.",
        file=sys.stderr,
    )
    return 1
  if len(positional_args) > 1:
    print(
        "Error: Expected at most 1 positional argument (input_file), got "
        f"{len(positional_args)}.",
        file=sys.stderr,
    )
    return 1
  raw_input_file = fv.input_file or (
      positional_args[0] if positional_args else None
  )
  if not raw_input_file:
    print(
        "Error: Input file is required (pass via --input_file=<path> or as "
        "a positional argument).",
        file=sys.stderr,
    )
    return 1

  if fv.jobs < 1:
    print("Error: --jobs must be at least 1.", file=sys.stderr)
    return 1
  if fv.timeout is not None and fv.timeout <= 0:
    print("Error: --timeout must be > 0.", file=sys.stderr)
    return 1

  try:
    trials = parse_trials(fv.trial)
    quality_levels = parse_qualities(fv.qualities)
  except ValueError as e:
    print(f"Error: {e}", file=sys.stderr)
    return 1

  build_working_dir = os.environ.get("BUILD_WORKING_DIRECTORY")
  input_file = resolve_cli_path(raw_input_file, build_working_dir)
  output_dir = resolve_cli_path(fv.output_directory, build_working_dir)

  if not os.path.isfile(input_file):
    print(f"Error: Input file '{input_file}' not found.", file=sys.stderr)
    return 1

  with open(input_file, "r", encoding="utf-8") as f:
    font_paths = [line.strip() for line in f if line.strip()]

  if not font_paths:
    print("No font paths found in the input file.", file=sys.stderr)
    return 0

  runfiles = Runfiles.Create()

  try:
    segmenter_binary = find_segmenter_binary(runfiles=runfiles)
  except FileNotFoundError as e:
    print(f"Error: {e}", file=sys.stderr)
    return 1

  out_scsv_path = os.path.join(output_dir, "out.scsv")
  error_log_path = os.path.join(output_dir, "error_log.txt")
  notes_path = os.path.join(output_dir, "NOTES.txt")

  if (
      not fv.resume
      and not fv.overwrite
      and os.path.exists(out_scsv_path)
      and os.path.getsize(out_scsv_path) > 0
  ):
    print(
        f"Error: Output file '{out_scsv_path}' already exists and is non-empty. "
        "Pass --resume to continue or --overwrite to replace it.",
        file=sys.stderr,
    )
    return 1

  os.makedirs(output_dir, exist_ok=True)

  skip_set: set[Tuple[str, int, str]] = set()
  if fv.resume:
    skip_set = load_skip_set(out_scsv_path, error_log_path)

  commit_info = get_encoder_commit_info()
  notes_content = format_notes(
      commit_info=commit_info,
      input_file=input_file,
      font_count=len(font_paths),
      quality_levels=quality_levels,
      timeout=fv.timeout,
      jobs=fv.jobs,
      trials=trials,
      extra_notes=fv.notes,
  )
  if fv.resume and os.path.isfile(notes_path) and os.path.getsize(notes_path) > 0:
    with open(notes_path, "a", encoding="utf-8") as f:
      f.write("\n--- Resumed ---\n")
      f.write(notes_content)
  else:
    with open(notes_path, "w", encoding="utf-8") as f:
      f.write(notes_content)

  subprocess_env = build_subprocess_env(runfiles=runfiles)

  font_task_chains = []
  total_fonts = 0
  for path in font_paths:
    resolved_path = resolve_cli_path(path, build_working_dir)
    for trial_name, trial_flags in trials.items():
      if "SKIP" in get_cjk_script_flag(resolved_path, trial_flags):
        continue
      qualities = [
          q for q in quality_levels if (path, q, trial_name) not in skip_set
      ]
      if qualities:
        font_task_chains.append(
            (path, resolved_path, trial_name, trial_flags, qualities)
        )
        total_fonts += len(qualities)

  append_scsv = (
      fv.resume
      and os.path.isfile(out_scsv_path)
      and os.path.getsize(out_scsv_path) > 0
  )
  scsv_mode = "a" if append_scsv else "w"
  err_mode = "a" if (fv.resume and os.path.isfile(error_log_path)) else "w"

  def log_error(msg: str, err_file) -> None:
    print(msg, file=sys.stderr)
    err_file.write(msg + "\n")
    err_file.flush()

  with (
      open(out_scsv_path, scsv_mode, encoding="utf-8") as scsv_f,
      open(error_log_path, err_mode, encoding="utf-8") as err_f,
  ):
    if not append_scsv:
      scsv_f.write(SCSV_HEADER + "\n")
      scsv_f.flush()

    with concurrent.futures.ThreadPoolExecutor(
        max_workers=fv.jobs
    ) as executor:
      active_futures = {}
      for (
          path,
          resolved_path,
          trial_name,
          trial_flags,
          qualities,
      ) in font_task_chains:
        first_q = qualities[0]
        remaining_q = qualities[1:]
        future = executor.submit(
            check_font,
            path,
            first_q,
            trial_name,
            trial_flags,
            segmenter_binary,
            fv.timeout,
            resolved_path,
            subprocess_env,
        )
        active_futures[future] = (
            path,
            resolved_path,
            first_q,
            trial_name,
            trial_flags,
            remaining_q,
        )

      processed_count = 0
      while active_futures:
        done, _ = concurrent.futures.wait(
            active_futures,
            return_when=concurrent.futures.FIRST_COMPLETED,
        )
        for future in done:
          (
              path,
              resolved_path,
              quality,
              trial_name,
              trial_flags,
              remaining_q,
          ) = active_futures.pop(future)

          try:
            result, err_lines = future.result()
            for err_line in err_lines:
              log_error(err_line, err_f)
          except subprocess.TimeoutExpired:
            for q in [quality] + remaining_q:
              log_error(
                  f"Timeout occurred for font: {path}, trial: {trial_name} "
                  f"at quality level: {q}",
                  err_f,
              )
              processed_count += 1
              if processed_count % 100 == 0:
                log_error(
                    f"Processed {processed_count}/{total_fonts} fonts...",
                    err_f,
                )
            continue
          except FileNotFoundError:
            log_error(
                f"Error: segmenter executable not found at '{segmenter_binary}'.",
                err_f,
            )
            return 1
          except Exception as exc:
            log_error(
                f"{path}, quality {quality}, trial {trial_name} "
                f"generated an exception: {exc}",
                err_f,
            )
            result = None

          processed_count += 1
          if processed_count % 100 == 0:
            log_error(
                f"Processed {processed_count}/{total_fonts} fonts...", err_f
            )

          if result is not None:
            (
                font_path,
                quality_level,
                trial_name,
                ideal_total_cost,
                ift_total_cost,
                non_ift_total_cost,
                uncovered_ift_patch_cost,
                total_time,
                codepoint_count,
            ) = result
            scsv_f.write(
                f"{font_path}; {quality_level}; {trial_name}; "
                f"{ideal_total_cost}; {ift_total_cost}; {non_ift_total_cost}; "
                f"{uncovered_ift_patch_cost}; {total_time}; {codepoint_count}\n"
            )
            scsv_f.flush()

          if remaining_q:
            next_q = remaining_q[0]
            next_future = executor.submit(
                check_font,
                path,
                next_q,
                trial_name,
                trial_flags,
                segmenter_binary,
                fv.timeout,
                resolved_path,
                subprocess_env,
            )
            active_futures[next_future] = (
                path,
                resolved_path,
                next_q,
                trial_name,
                trial_flags,
                remaining_q[1:],
            )

  return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
  fv = flags.FlagValues()
  define_flags(fv)
  raw_args = list(sys.argv[1:] if argv is None else argv)
  try:
    remaining = fv(["run_gen_ift_segmentation_plan", *raw_args])
  except (flags.Error, ValueError) as e:
    print(f"Error: {e}", file=sys.stderr)
    return 1
  return _run_with_flags(remaining[1:], fv)


if __name__ == "__main__":
  define_flags(flags.FLAGS)
  app.run(lambda argv: _run_with_flags(argv[1:], flags.FLAGS))
