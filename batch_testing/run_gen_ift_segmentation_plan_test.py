#!/usr/bin/env python3
"""Unit and integration tests for run_gen_ift_segmentation_plan.py."""

from pathlib import Path
import tempfile

from absl.testing import absltest
from python.runfiles import Runfiles

import compare_runs
import run_gen_ift_segmentation_plan as runner


def _find_test_font() -> Path:
  r = Runfiles.Create()
  if r is not None:
    resolved = r.Rlocation("ift_encoder/ift/common/testdata/Roboto-Regular.abcd.ttf")
    if resolved and Path(resolved).is_file():
      return Path(resolved)
  raise FileNotFoundError(
      "Could not locate Roboto-Regular.abcd.ttf via Bazel runfiles."
  )


class RunnerParsingTest(absltest.TestCase):

  def test_parse_trials_defaults_to_control(self):
    self.assertEqual(runner.parse_trials([]), {"control": []})

  def test_parse_trials_valid_specs(self):
    trials = runner.parse_trials([
        "control",
        "cand_colon:--flag_a --flag_b=42",
        "cand_eq=--flag_c=val",
        "cand_colon:--flag_d",
        "quoted:--msg='hello world'",
    ])
    self.assertEqual(
        trials,
        {
            "control": [],
            "cand_colon": ["--flag_a", "--flag_b=42", "--flag_d"],
            "cand_eq": ["--flag_c=val"],
            "quoted": ["--msg=hello world"],
        },
    )

  def test_parse_trials_invalid_specs(self):
    for bad in ("", ":--flag", "has space:--flag", "semi;colon:--flag", "cand:not_a_flag", "cand:--unclosed'"):
      with self.subTest(bad=bad):
        with self.assertRaises(ValueError):
          runner.parse_trials([bad])

  def test_parse_qualities(self):
    self.assertEqual(runner.parse_qualities("1,2,3"), [1, 2, 3])
    self.assertEqual(runner.parse_qualities("1-4,2,7"), [1, 2, 3, 4, 7])
    self.assertEqual(runner.parse_qualities("7"), [7])
    self.assertEqual(runner.parse_qualities("0,1-2"), [0, 1, 2])

    for bad in ("", "   ", "5-2", "-1", "1,,2", "abc", "1-"):
      with self.subTest(bad=bad):
        with self.assertRaises(ValueError):
          runner.parse_qualities(bad)

  def test_cjk_script_flag(self):
    with tempfile.TemporaryDirectory() as tmp:
      cjk_dir = Path(tmp) / "cjk"
      cjk_dir.mkdir()
      (cjk_dir / "METADATA.pb").write_text(
          'name: "Noto Sans JP"\nsubsets: "latin"\nsubsets: "japanese"\n',
          encoding="utf-8",
      )
      cjk_font = str(cjk_dir / "Font.ttf")

      self.assertEqual(
          runner.get_cjk_script_flag(cjk_font, []),
          ["--auto_config_primary_script=Script_japanese"],
      )
      self.assertEqual(
          runner.get_cjk_script_flag(cjk_font, ["--noauto_config_primary_script"]),
          [],
      )
      self.assertEqual(
          runner.get_cjk_script_flag(cjk_font, ["--auto_config_primary_script=Script_CJK"]),
          ["--auto_config_primary_script=Script_CJK"],
      )

      non_cjk_dir = Path(tmp) / "latin"
      non_cjk_dir.mkdir()
      (non_cjk_dir / "METADATA.pb").write_text(
          'name: "Roboto"\nsubsets: "latin"\n',
          encoding="utf-8",
      )
      non_cjk_font = str(non_cjk_dir / "Roboto.ttf")

      self.assertEqual(runner.get_cjk_script_flag(non_cjk_font, []), [])
      self.assertEqual(
          runner.get_cjk_script_flag(non_cjk_font, ["--cjk_only"]),
          ["SKIP"],
      )
      self.assertEqual(
          runner.get_cjk_script_flag(non_cjk_font, ["--auto_config_primary_script=Script_CJK"]),
          [],
      )

  def test_format_notes(self):
    notes = runner.format_notes(
        commit_info="deadbeef (dirty)",
        input_file="/tmp/fonts.txt",
        font_count=10,
        quality_levels=[1, 2],
        timeout=2700.0,
        jobs=40,
        trials={"control": [], "cand": ["--foo"]},
        extra_notes="quick benchmark",
        timestamp="2026-11-25 00:00:00 UTC",
    )
    first_line = notes.splitlines()[0]
    self.assertEqual(first_line, "Encoder Commit: deadbeef (dirty)")
    self.assertIn("Quality Levels Tested: 1, 2", notes)
    self.assertIn("Timeout: 2700s", notes)
    self.assertIn("Notes: quick benchmark", notes)
    self.assertIn('"cand"', notes)


class RunnerResumeAndEndToEndTest(absltest.TestCase):

  def setUp(self):
    self.temp_dir = tempfile.TemporaryDirectory()
    self.root = Path(self.temp_dir.name)
    self.font_path = _find_test_font()
    self.fonts_txt = self.root / "fonts.txt"
    self.fonts_txt.write_text(f"{self.font_path}\n", encoding="utf-8")

  def tearDown(self):
    self.temp_dir.cleanup()

  def test_load_skip_set_and_overwrite_protection(self):
    out_dir = self.root / "run_existing"
    out_dir.mkdir()
    scsv_path = out_dir / "out.scsv"
    err_path = out_dir / "error_log.txt"
    scsv_content = (
        f"{runner.SCSV_HEADER}\n"
        "/fonts/A.ttf; 1; control; 10; 20; 30; 0; 1.0; 5\n"
    )
    scsv_path.write_text(scsv_content, encoding="utf-8")
    err_path.write_text(
        "Timeout occurred for font: /fonts/B.ttf, trial: cand at quality level: 2\n",
        encoding="utf-8",
    )

    skip_set = runner.load_skip_set(str(scsv_path), str(err_path))
    self.assertEqual(skip_set, {("/fonts/A.ttf", 1, "control"), ("/fonts/B.ttf", 2, "cand")})

    # Running without --resume or --overwrite should refuse to clobber non-empty out.scsv.
    rc = runner.main([str(self.fonts_txt), "-o", str(out_dir)])
    self.assertEqual(rc, 1)
    self.assertEqual(scsv_path.read_text(encoding="utf-8"), scsv_content)

  def test_end_to_end_run_resume_and_compare(self):
    out_dir = self.root / "nested" / "batch_out"
    rc = runner.main([
        str(self.fonts_txt),
        "-o",
        str(out_dir),
        "-q",
        "1,2",
        "-j",
        "2",
        "--trial",
        "control",
        "--trial",
        "no_cjk:--noauto_config_primary_script",
        "--notes",
        "integration smoke",
    ])
    self.assertEqual(rc, 0)

    scsv_path = out_dir / "out.scsv"
    err_path = out_dir / "error_log.txt"
    notes_path = out_dir / "NOTES.txt"
    self.assertTrue(scsv_path.is_file())
    self.assertTrue(err_path.is_file())
    self.assertTrue(notes_path.is_file())
    self.assertEqual(err_path.read_text(encoding="utf-8").strip(), "")

    notes_text = notes_path.read_text(encoding="utf-8")
    self.assertTrue(notes_text.startswith("Encoder Commit: "))
    self.assertIn("Quality Levels Tested: 1, 2", notes_text)
    self.assertIn("Notes: integration smoke", notes_text)

    data, trials, qualities = compare_runs.load_scsv(scsv_path)
    self.assertEqual(trials, ["control", "no_cjk"])
    self.assertEqual(qualities, [1, 2])
    self.assertEqual(sorted(data["control"][str(self.font_path)].keys()), [1, 2])
    self.assertEqual(sorted(data["no_cjk"][str(self.font_path)].keys()), [1, 2])

    # Verify compare_runs works directly on the generated directory.
    self.assertEqual(
        compare_runs.main([str(out_dir), "control", "no_cjk", "--text"]),
        0,
    )

    # Verify --resume runs only newly requested quality 3 without duplicating Q1/Q2.
    rc_resume = runner.main([
        str(self.fonts_txt),
        "-o",
        str(out_dir),
        "-q",
        "1,2,3",
        "-j",
        "2",
        "--trial",
        "control",
        "--trial",
        "no_cjk:--noauto_config_primary_script",
        "--resume",
    ])
    self.assertEqual(rc_resume, 0)
    non_header_lines = [
        ln
        for ln in scsv_path.read_text(encoding="utf-8").splitlines()
        if ln.strip() and not ln.startswith("font_path;")
    ]
    self.assertEqual(len(non_header_lines), 6)
    _, _, resumed_qualities = compare_runs.load_scsv(scsv_path)
    self.assertEqual(resumed_qualities, [1, 2, 3])
    self.assertIn("--- Resumed ---", notes_path.read_text(encoding="utf-8"))

  def test_absl_flag_styles_and_validation(self):
    out_dir = self.root / "absl_flags_out"
    rc = runner.main([
        f"--input_file={self.fonts_txt}",
        f"--output_directory={out_dir}",
        "--qualities=1",
        "--jobs=1",
        "--trial=control",
        "--nooverwrite",
    ])
    self.assertEqual(rc, 0)
    self.assertTrue((out_dir / "out.scsv").is_file())

    # Missing required --output_directory
    self.assertEqual(runner.main([str(self.fonts_txt)]), 1)
    # Missing input file
    self.assertEqual(runner.main([f"--output_directory={out_dir}"]), 1)
    # Specifying both --input_file and positional input_file
    self.assertEqual(
        runner.main([
            str(self.fonts_txt),
            f"--input_file={self.fonts_txt}",
            f"--output_directory={out_dir}",
        ]),
        1,
    )
    # Out-of-bounds --jobs
    self.assertEqual(
        runner.main([
            str(self.fonts_txt),
            f"--output_directory={out_dir}",
            "--jobs=0",
        ]),
        1,
    )


if __name__ == "__main__":
  absltest.main()
