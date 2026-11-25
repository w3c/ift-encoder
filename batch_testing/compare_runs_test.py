#!/usr/bin/env python3
"""Unit tests for compare_runs.py."""

import json
import os
from pathlib import Path
import tempfile

from absl.testing import absltest

from compare_runs import (
    ComparisonReport,
    COST_HIST_BINS,
    COST_HIST_COLORS,
    COST_HIST_LABELS,
    ErrorRecord,
    MatchedFontRecord,
    MetricAggregate,
    QuadrantSummary,
    RunRecord,
    TIME_HIST_BINS,
    TIME_HIST_COLORS,
    TIME_HIST_LABELS,
    UnmatchedFontRecord,
    analyze_timeouts,
    build_report,
    build_report_from_specs,
    compute_histogram,
    compute_mean,
    compute_median,
    compute_percentile,
    load_scsv,
    main,
    parse_error_log,
    parse_target_spec,
    render_html_report,
    render_text_report,
    resolve_dataset,
    select_records,
)


class StatsHelperTest(absltest.TestCase):
    def test_compute_percentile(self):
        # Empty
        self.assertEqual(compute_percentile([], 50), 0.0)
        # Single value
        self.assertEqual(compute_percentile([42.0], 0), 42.0)
        self.assertEqual(compute_percentile([42.0], 50), 42.0)
        self.assertEqual(compute_percentile([42.0], 100), 42.0)

        # Standard linear interpolation [10, 20, 30, 40, 50]
        # P00 = 10, P50 = 30, P100 = 50, P25 = 20, P75 = 40
        vals = [10.0, 20.0, 30.0, 40.0, 50.0]
        self.assertAlmostEqual(compute_percentile(vals, 0), 10.0)
        self.assertAlmostEqual(compute_percentile(vals, 25), 20.0)
        self.assertAlmostEqual(compute_percentile(vals, 50), 30.0)
        self.assertAlmostEqual(compute_percentile(vals, 75), 40.0)
        self.assertAlmostEqual(compute_percentile(vals, 100), 50.0)

        # Unsorted input
        unsorted_vals = [50.0, 10.0, 40.0, 20.0, 30.0]
        self.assertAlmostEqual(compute_percentile(unsorted_vals, 50), 30.0)

        # Pre-sorted flag
        sorted_vals = [10.0, 20.0, 30.0, 40.0, 50.0]
        self.assertAlmostEqual(compute_percentile(sorted_vals, 50, presorted=True), 30.0)

    def test_compute_median_and_mean(self):
        vals = [1.0, 2.0, 3.0, 4.0, 10.0]
        self.assertAlmostEqual(compute_median(vals), 3.0)
        self.assertAlmostEqual(compute_mean(vals), 4.0)

        self.assertEqual(compute_mean([]), 0.0)
        self.assertEqual(compute_median([]), 0.0)

    def test_compute_histogram(self):
        bins = [-10.0, 0.0, 10.0, 20.0]
        labels = ["negative", "low", "high"]
        colors = ["red", "yellow", "green"]

        # -5 in bin 0, 0 in bin 1, 5 in bin 1, 10 in bin 2, 20 in bin 2 (right closed)
        vals = [-5.0, 0.0, 5.0, 10.0, 20.0, -100.0]  # -100 is outside
        hist = compute_histogram(vals, bins, labels, colors)
        self.assertEqual(len(hist), 3)

        self.assertEqual(hist[0].label, "negative")
        self.assertEqual(hist[0].count, 1)  # -5.0
        self.assertAlmostEqual(hist[0].pct, 1 / 6 * 100.0, places=2)

        self.assertEqual(hist[1].label, "low")
        self.assertEqual(hist[1].count, 2)  # 0.0, 5.0

        self.assertEqual(hist[2].label, "high")
        self.assertEqual(hist[2].count, 2)  # 10.0, 20.0


class DatasetParsingTest(absltest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.dir_path = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_resolve_dataset_from_dir(self):
        scsv = self.dir_path / "out.scsv"
        scsv.write_text("dummy", encoding="utf-8")
        notes = self.dir_path / "NOTES.txt"
        notes.write_text("Test notes", encoding="utf-8")
        err = self.dir_path / "error_log.txt"
        err.write_text("Error log", encoding="utf-8")

        res = resolve_dataset(self.dir_path)
        self.assertEqual(res.scsv_path, scsv)
        self.assertEqual(res.dataset_name, self.dir_path.name)
        self.assertEqual(res.notes_text, "Test notes")
        self.assertEqual(res.error_log_path, err)

    def test_resolve_dataset_from_file(self):
        scsv = self.dir_path / "out.scsv"
        scsv.write_text("dummy", encoding="utf-8")
        res = resolve_dataset(scsv)
        self.assertEqual(res.scsv_path, scsv)
        self.assertEqual(res.dataset_name, self.dir_path.name)
        self.assertEqual(res.notes_text, "")
        self.assertIsNone(res.error_log_path)

    def test_resolve_dataset_missing(self):
        with self.assertRaises(FileNotFoundError):
            resolve_dataset(self.dir_path / "nonexistent")

    def test_load_scsv_valid(self):
        scsv = self.dir_path / "out.scsv"
        content = (
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; uncovered_ift_patch_cost; total_time_s; codepoint_count\n"
            "/fonts/A.ttf; 1; control; 100; 200; 300; 10; 1.5; 50\n"
            "/fonts/A.ttf; 1; candidate; 100; 180; 300; 8; 1.2; 50\n"
            "/fonts/A.ttf; 2; candidate; 100; 160; 300; 5; 2.0; 50\n"
            # Repeated header should be skipped
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; uncovered_ift_patch_cost; total_time_s; codepoint_count\n"
            "/fonts/B.ttf; 1; control; 200; 400; 600; 20; 5.0; 120\n"
        )
        scsv.write_text(content, encoding="utf-8")

        data, trials, qualities = load_scsv(scsv)
        self.assertEqual(trials, ["candidate", "control"])
        self.assertEqual(qualities, [1, 2])
        self.assertIn("/fonts/A.ttf", data["control"])
        self.assertIn("/fonts/A.ttf", data["candidate"])
        self.assertIn(1, data["control"]["/fonts/A.ttf"])
        self.assertIn(2, data["candidate"]["/fonts/A.ttf"])

        rec = data["candidate"]["/fonts/A.ttf"][1]
        self.assertEqual(rec.font_name, "A.ttf")
        self.assertEqual(rec.ift_total_cost, 180.0)
        self.assertEqual(rec.total_time_s, 1.2)
        self.assertEqual(rec.uncovered_ift_patch_cost, 8.0)

    def test_load_scsv_missing_required_column(self):
        scsv = self.dir_path / "out.scsv"
        scsv.write_text("font_path; quality_level; trial\n", encoding="utf-8")
        with self.assertRaises(ValueError) as ctx:
            load_scsv(scsv)
        self.assertIn("Missing required column", str(ctx.exception))

    def test_load_scsv_empty(self):
        scsv = self.dir_path / "out.scsv"
        scsv.write_text("", encoding="utf-8")
        with self.assertRaises(ValueError) as ctx:
            load_scsv(scsv)
        self.assertIn("Empty dataset file", str(ctx.exception))

    def test_load_scsv_malformed_row(self):
        scsv = self.dir_path / "out.scsv"
        content = (
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; total_time_s; codepoint_count\n"
            "/fonts/A.ttf; invalid_int; control; 100; 200; 300; 1.5; 50\n"
        )
        scsv.write_text(content, encoding="utf-8")
        with self.assertRaises(ValueError) as ctx:
            load_scsv(scsv)
        self.assertIn("Error parsing row at line 2", str(ctx.exception))

    def test_load_scsv_short_row_with_reordered_columns(self):
        # Even if len(row) >= len(required), if required column index is beyond len(row), raises ValueError
        scsv = self.dir_path / "out.scsv"
        content = (
            "a; b; c; d; e; f; g; h; font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; total_time_s; codepoint_count\n"
            "1; 2; 3; 4; 5; 6; 7; 8; 9; 10\n"
        )
        scsv.write_text(content, encoding="utf-8")
        with self.assertRaises(ValueError) as ctx:
            load_scsv(scsv)
        self.assertIn("Malformed row at line 2", str(ctx.exception))

    def test_zero_time_or_cost_rejected(self):
        scsv = self.dir_path / "out.scsv"
        # Zero time
        content_zero_time = (
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; total_time_s; codepoint_count\n"
            "/fonts/A.ttf; 1; control; 100; 200; 300; 0.0; 50\n"
        )
        scsv.write_text(content_zero_time, encoding="utf-8")
        with self.assertRaises(ValueError) as ctx:
            load_scsv(scsv)
        self.assertIn("total_time_s must be > 0", str(ctx.exception))

        # Zero cost
        content_zero_cost = (
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; total_time_s; codepoint_count\n"
            "/fonts/A.ttf; 1; control; 100; 0; 300; 1.5; 50\n"
        )
        scsv.write_text(content_zero_cost, encoding="utf-8")
        with self.assertRaises(ValueError) as ctx:
            load_scsv(scsv)
        self.assertIn("ift_total_cost must be > 0", str(ctx.exception))

    def test_parse_error_log_sigterm_and_multiple_errors(self):
        err_file = self.dir_path / "error_log.txt"
        content = (
            "Processed 100/1000 fonts...\n"
            "Timeout occurred for font: /fonts/A.ttf, trial: control at quality level: 1\n"
            "Timeout occurred for font: /fonts/A.ttf, trial: control at quality level: 2\n"
            # Return code 143 (SIGTERM)
            "Error processing /fonts/B.ttf, quality 1, trial candidate: command line return code 143\n"
            # Return code -15 (SIGTERM)
            "Error processing /fonts/B.ttf, quality 2, trial candidate: command line return code -15\n"
            # Return code 1430 should NOT be SIGTERM
            "Error processing /fonts/C.ttf, quality 1, trial candidate: return code 1430 in some context\n"
            # Multiple errors for same font/trial/quality: first one must be kept!
            "Error processing /fonts/D.ttf, quality 1, trial control: total cost not found in output\n"
            "/fonts/D.ttf, quality 1, trial control generated an exception: UnboundLocalError\n"
        )
        err_file.write_text(content, encoding="utf-8")

        errors = parse_error_log(err_file)
        self.assertEqual(errors["candidate"]["/fonts/B.ttf"][1].error_type, "TIMEOUT (SIGTERM/143)")
        self.assertEqual(errors["candidate"]["/fonts/B.ttf"][2].error_type, "TIMEOUT (SIGTERM/143)")
        self.assertEqual(errors["candidate"]["/fonts/C.ttf"][1].error_type, "ERROR")

        # Verify first error is kept
        rec_d = errors["control"]["/fonts/D.ttf"][1]
        self.assertEqual(rec_d.error_type, "ERROR")
        self.assertIn("total cost not found", rec_d.detail)


class RecordSelectionAndReportTest(absltest.TestCase):
    def setUp(self):
        # Setup synthetic dataset with 4 fonts:
        # Font A: Q1 ctrl (time=2.0, cost=100), Q1 cand (time=1.0, cost=90)
        # Font B: Q1 ctrl (time=1.0, cost=100), Q1 cand (time=2.0, cost=110)
        #         Q2 ctrl (missing), Q2 cand (time=3.0, cost=80)
        # Font C: Q1 ctrl (time=1.0, cost=100), Q1 cand (missing)
        # Font D: Q0 ctrl (time=1.0, cost=50), Q1 cand (time=2.0, cost=60)
        self.data: dict[str, dict[str, dict[int, RunRecord]]] = {
            "control": {
                "/fonts/A.ttf": {
                    1: RunRecord("A.ttf", "/fonts/A.ttf", 1, "control", 50, 100, 150, 10, 2.0, 100)
                },
                "/fonts/B.ttf": {
                    1: RunRecord("B.ttf", "/fonts/B.ttf", 1, "control", 50, 100, 150, 20, 1.0, 100)
                },
                "/fonts/C.ttf": {
                    1: RunRecord("C.ttf", "/fonts/C.ttf", 1, "control", 50, 100, 150, 0, 1.0, 100)
                },
                "/fonts/D.ttf": {
                    0: RunRecord("D.ttf", "/fonts/D.ttf", 0, "control", 50, 50, 150, 0, 1.0, 100)
                },
            },
            "candidate": {
                "/fonts/A.ttf": {
                    1: RunRecord("A.ttf", "/fonts/A.ttf", 1, "candidate", 50, 90, 150, 5, 1.0, 100)
                },
                "/fonts/B.ttf": {
                    1: RunRecord("B.ttf", "/fonts/B.ttf", 1, "candidate", 50, 110, 150, 25, 2.0, 100),
                    2: RunRecord("B.ttf", "/fonts/B.ttf", 2, "candidate", 50, 80, 150, 15, 3.0, 100),
                },
                "/fonts/D.ttf": {
                    1: RunRecord("D.ttf", "/fonts/D.ttf", 1, "candidate", 50, 60, 150, 0, 2.0, 100)
                },
            },
        }
        self.errors: dict[str, dict[str, dict[int, ErrorRecord]]] = {
            "control": {
                "/fonts/B.ttf": {
                    2: ErrorRecord("/fonts/B.ttf", "B.ttf", "control", 2, "TIMEOUT", "Timeout", "raw")
                }
            },
            "candidate": {},
        }
        self.qualities = [0, 1, 2]

    def test_select_records_quality_modes(self):
        # Mode: "common" (default):
        # A matches at Q1
        # B matches at Q1 (since both completed Q1, even though candidate reached Q2)
        # C has only control Q1 -> unmatched_ctrl
        # D has control Q0 and candidate Q1 (no common quality) -> unmatched_cand (cand Q1, ctrl Q0)
        matched, un_cand, un_ctrl, counts = select_records(
            self.data, "control", "candidate", quality_filter=None, quality_mode="common"
        )
        self.assertEqual(len(matched), 2)  # A and B at Q1
        self.assertEqual(counts, {1: 2})
        self.assertEqual(len(un_cand), 1)  # D at Q1
        self.assertEqual(un_cand[0].font_path, "/fonts/D.ttf")
        self.assertEqual(un_cand[0].selected_quality, 1)
        self.assertEqual(un_cand[0].other_max_quality, 0)  # Q0 should be preserved!
        self.assertEqual(len(un_ctrl), 1)  # C at Q1

        # Mode: "candidate":
        # A matches at Q1
        # B candidate highest is Q2, but control lacks Q2 -> B goes to unmatched_cand!
        matched_c, un_cand_c, un_ctrl_c, counts_c = select_records(
            self.data, "control", "candidate", quality_filter=None, quality_mode="candidate"
        )
        self.assertEqual(len(matched_c), 1)  # Only A
        self.assertEqual(matched_c[0][0], "/fonts/A.ttf")
        self.assertEqual(len(un_cand_c), 2)  # B at Q2 and D at Q1

    def test_analyze_timeouts_missing_vs_resolved_and_new(self):
        # Control has error on /fonts/B.ttf at Q2. Candidate completed Q2 -> RESOLVED.
        # Control has error on /fonts/E.ttf at Q1. Candidate has NO data for E -> MISSING DATA.
        # Candidate has error on /fonts/F.ttf at Q1. Control has NO data for F -> MISSING DATA.
        # Candidate has error on /fonts/A.ttf at Q2. Control completed Q2 -> NEW.
        data = {
            "control": {
                "/fonts/A.ttf": {2: RunRecord("A.ttf", "/fonts/A.ttf", 2, "control", 50, 100, 150, 0, 1.0, 100)},
                "/fonts/B.ttf": {},
            },
            "candidate": {
                "/fonts/B.ttf": {2: RunRecord("B.ttf", "/fonts/B.ttf", 2, "candidate", 50, 100, 150, 0, 1.0, 100)},
                "/fonts/A.ttf": {},
            },
        }
        errors = {
            "control": {
                "/fonts/B.ttf": {2: ErrorRecord("/fonts/B.ttf", "B.ttf", "control", 2, "TIMEOUT", "Timeout", "raw")},
                "/fonts/E.ttf": {1: ErrorRecord("/fonts/E.ttf", "E.ttf", "control", 1, "TIMEOUT", "Timeout", "raw")},
            },
            "candidate": {
                "/fonts/A.ttf": {2: ErrorRecord("/fonts/A.ttf", "A.ttf", "candidate", 2, "TIMEOUT", "Timeout", "raw")},
                "/fonts/F.ttf": {1: ErrorRecord("/fonts/F.ttf", "F.ttf", "candidate", 1, "TIMEOUT", "Timeout", "raw")},
            },
        }
        to = analyze_timeouts(errors, data, "control", "candidate", [1, 2], quality_filter=None)
        self.assertEqual(to.resolved_count, 1)  # B at Q2
        self.assertEqual(to.new_count, 1)       # A at Q2
        self.assertEqual(to.missing_count, 2)   # E and F at Q1

        status_by_font = {r.font_path: r.status for r in to.rows}
        self.assertEqual(status_by_font["/fonts/B.ttf"], "RESOLVED")
        self.assertEqual(status_by_font["/fonts/A.ttf"], "NEW TIMEOUT")
        self.assertEqual(status_by_font["/fonts/E.ttf"], "MISSING DATA")
        self.assertEqual(status_by_font["/fonts/F.ttf"], "MISSING DATA")

    def test_separate_timeouts_and_errors_counts_and_columns(self):
        # Font T: timeout in control at Q1, candidate completes -> RESOLVED timeout
        # Font E: exit code 127 in control at Q1, candidate completes -> RESOLVED error
        # Font N: completes in control at Q1, candidate exit code 1 in Q1 -> NEW error
        data = {
            "control": {
                "/fonts/M.ttf": {1: RunRecord("M.ttf", "/fonts/M.ttf", 1, "control", 50, 100, 150, 0, 1.0, 100)},
                "/fonts/N.ttf": {1: RunRecord("N.ttf", "/fonts/N.ttf", 1, "control", 50, 100, 150, 0, 1.0, 100)},
                "/fonts/T.ttf": {},
                "/fonts/E.ttf": {},
            },
            "candidate": {
                "/fonts/M.ttf": {1: RunRecord("M.ttf", "/fonts/M.ttf", 1, "candidate", 50, 100, 150, 0, 1.0, 100)},
                "/fonts/T.ttf": {1: RunRecord("T.ttf", "/fonts/T.ttf", 1, "candidate", 50, 100, 150, 0, 1.0, 100)},
                "/fonts/E.ttf": {1: RunRecord("E.ttf", "/fonts/E.ttf", 1, "candidate", 50, 100, 150, 0, 1.0, 100)},
                "/fonts/N.ttf": {},
            },
        }
        errors = {
            "control": {
                "/fonts/T.ttf": {1: ErrorRecord("/fonts/T.ttf", "T.ttf", "control", 1, "TIMEOUT", "Timeout occurred", "raw")},
                "/fonts/E.ttf": {1: ErrorRecord("/fonts/E.ttf", "E.ttf", "control", 1, "ERROR", "command line return code 127", "raw")},
            },
            "candidate": {
                "/fonts/N.ttf": {1: ErrorRecord("/fonts/N.ttf", "N.ttf", "candidate", 1, "ERROR", "command line return code 1", "raw")},
            },
        }
        to = analyze_timeouts(errors, data, "control", "candidate", [1], quality_filter=1)
        # Timeouts: Control 1 (T), Candidate 0
        self.assertEqual(to.control_count, 1)
        self.assertEqual(to.candidate_count, 0)
        self.assertEqual(to.diff_count, -1)
        self.assertEqual(to.resolved_count, 1)  # T resolved
        self.assertEqual(to.new_count, 0)

        # Errors: Control 1 (E), Candidate 1 (N)
        self.assertEqual(to.control_error_count, 1)
        self.assertEqual(to.candidate_error_count, 1)
        self.assertEqual(to.diff_error_count, 0)
        self.assertEqual(to.resolved_error_count, 1)  # E resolved
        self.assertEqual(to.new_error_count, 1)       # N new

        # Check per-quality summary
        pq = to.per_quality[0]
        self.assertEqual(pq.control_count, 1)
        self.assertEqual(pq.candidate_count, 0)
        self.assertEqual(pq.control_error_count, 1)
        self.assertEqual(pq.candidate_error_count, 1)

        # Check rows
        status_map = {r.font_path: (r.category, r.status) for r in to.rows}
        self.assertEqual(status_map["/fonts/T.ttf"], ("TIMEOUT", "RESOLVED"))
        self.assertEqual(status_map["/fonts/E.ttf"], ("ERROR", "RESOLVED"))
        self.assertEqual(status_map["/fonts/N.ttf"], ("ERROR", "NEW ERROR"))

        # Build report and check rendering
        rep = build_report("test_err_sep", "", data, errors, [1], "control", "candidate", quality_filter=1)
        txt = render_text_report(rep)
        self.assertIn("Timeouts:                    1  ->        0", txt)
        self.assertIn("Errors:                      1  ->        1", txt)
        self.assertIn("[TIMEOUT - RESOLVED      ]", txt)
        self.assertIn("[ERROR - RESOLVED      ]", txt)
        self.assertIn("[ERROR - NEW ERROR     ]", txt)

        html = render_html_report(rep)
        self.assertIn("Ctrl T/O", html)
        self.assertIn("Ctrl Err", html)
        self.assertIn("Timeouts Only", html)
        self.assertIn("Errors Only", html)

    def test_uncovered_cost_zero_to_positive_and_all_zero_na(self):
        # Test font going from 0 uncovered to 50 uncovered
        data = {
            "control": {
                "/fonts/A.ttf": {1: RunRecord("A.ttf", "/fonts/A.ttf", 1, "control", 50, 100, 150, 0, 1.0, 100)},
                "/fonts/B.ttf": {1: RunRecord("B.ttf", "/fonts/B.ttf", 1, "control", 50, 100, 150, 10, 1.0, 100)},
            },
            "candidate": {
                "/fonts/A.ttf": {1: RunRecord("A.ttf", "/fonts/A.ttf", 1, "candidate", 50, 100, 150, 50, 1.0, 100)},
                "/fonts/B.ttf": {1: RunRecord("B.ttf", "/fonts/B.ttf", 1, "candidate", 50, 100, 150, 5, 1.0, 100)},
            },
        }
        report = build_report("test", "", data, {}, [1], "control", "candidate", quality_filter=1)
        # Both A (0 -> 50) and B (10 -> 5) should be active!
        self.assertEqual(report.uncov_stats.active_count, 2)
        # Font A delta should be +100.0%, Font B delta should be -50.0%
        self.assertAlmostEqual(report.uncov_stats.delta_pct_median, 25.0)

        txt = render_text_report(report)
        self.assertIn("active fonts: 2", txt)


class RenderingAndCliTest(absltest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.dir_path = Path(self.temp_dir.name)

        # Create dummy dataset on disk
        scsv = self.dir_path / "out.scsv"
        content = (
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; uncovered_ift_patch_cost; total_time_s; codepoint_count\n"
            "/fonts/A.ttf; 1; control; 100; 200; 300; 10; 2.5; 50\n"
            "/fonts/A.ttf; 1; candidate; 100; 180; 300; 8; 1.5; 50\n"
            "/fonts/B.ttf; 1; control; 200; 400; 600; 20; 15.0; 120\n"
            "/fonts/B.ttf; 1; candidate; 200; 420; 600; 25; 18.0; 120\n"
        )
        scsv.write_text(content, encoding="utf-8")

        notes = self.dir_path / "NOTES.txt"
        notes.write_text("Test run notes with </script> and <tag>", encoding="utf-8")

        err = self.dir_path / "error_log.txt"
        err.write_text(
            "Timeout occurred for font: /fonts/C.ttf, trial: control at quality level: 1\n"
            "Error processing /fonts/D.ttf, quality 1, trial control: <class 'Exception'> boom\n",
            encoding="utf-8",
        )

        data, trials, qualities = load_scsv(scsv)
        errors = parse_error_log(err)
        self.report = build_report(
            dataset_name=self.dir_path.name,
            notes_text=notes.read_text(encoding="utf-8"),
            data=data,
            errors=errors,
            available_qualities=qualities,
            control_trial="control",
            candidate_trial="candidate",
            quality_filter=1,
        )

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_render_text_report(self):
        txt = render_text_report(self.report, top_n=5)
        self.assertIn("IFT SEGMENTER COMPARISON REPORT", txt)
        self.assertIn("Control:   control", txt)
        self.assertIn("Candidate: candidate", txt)
        self.assertIn("SUMMARY METRICS", txt)
        self.assertIn("JOINT OUTCOME QUADRANTS", txt)
        self.assertIn("3. Slower/Same Time & <= Cost", txt)
        self.assertIn("4. Slower/Same Time & Higher Cost", txt)
        self.assertIn("QUANTILE DISTRIBUTION COMPARISON", txt)
        self.assertIn("BREAKDOWN BY CONTROL RUNTIME TIER", txt)
        self.assertIn("DISTRIBUTION OF SEGMENTATION TIME % CHANGE", txt)
        self.assertIn("TOP 1 LARGEST TIME SPEEDUPS", txt)
        self.assertIn("TOP 1 LARGEST TIME SLOWDOWNS", txt)
        self.assertIn("A.ttf", txt)
        self.assertIn("B.ttf", txt)

    def test_render_html_report(self):
        html_doc = render_html_report(self.report)
        self.assertIn("<!DOCTYPE html>", html_doc)
        self.assertIn("IFT Segmenter Comparison: control vs candidate", html_doc)

        # Ensure </script> in notes was escaped to <\/script> in inlined JSON blob
        self.assertNotIn("</script> and <tag>", html_doc[html_doc.find("const DATA = "):])
        self.assertIn("<\\/script>", html_doc)

        # Ensure DATA JSON blob is valid JSON
        marker = "const DATA = "
        self.assertIn(marker, html_doc)
        start = html_doc.find(marker) + len(marker)
        end = html_doc.find(";\n    let filteredRecords", start)
        self.assertNotEqual(end, -1)
        data_str = html_doc[start:end]
        data_obj = json.loads(data_str)
        self.assertEqual(data_obj["base_name"], "control")
        self.assertEqual(data_obj["cand_name"], "candidate")
        self.assertEqual(len(data_obj["records"]), 2)
        self.assertEqual(len(data_obj["timeout_rows"]), 2)

    def test_quality_zero_handling(self):
        # Create an unmatched record with other_max_quality == 0
        dummy_run = RunRecord("FontQ0.ttf", "/fonts/FontQ0.ttf", 1, "candidate", 10, 20, 30, 0, 1.0, 50)
        unmatched_cand = [
            UnmatchedFontRecord(
                font_name="FontQ0.ttf",
                font_path="/fonts/FontQ0.ttf",
                selected_quality=1,
                record=dummy_run,
                other_max_quality=0,  # Quality 0!
            )
        ]
        # Replace unmatched in report
        rep = build_report(
            dataset_name="test",
            notes_text="",
            data=self.report.to_js_dict()["records"],  # dummy
            errors={},
            available_qualities=[1],
            control_trial="control",
            candidate_trial="candidate",
            quality_filter=1,
        ) if False else self.report  # create custom report with dummy unmatched
        custom_report = ComparisonReport(
            dataset_name=self.report.dataset_name,
            notes_text=self.report.notes_text,
            control_trial=self.report.control_trial,
            candidate_trial=self.report.candidate_trial,
            quality_filter=self.report.quality_filter,
            quality_mode_desc=self.report.quality_mode_desc,
            quality_counts=self.report.quality_counts,
            time_stats=self.report.time_stats,
            cost_stats=self.report.cost_stats,
            uncov_stats=self.report.uncov_stats,
            quadrants=self.report.quadrants,
            time_histogram=self.report.time_histogram,
            cost_histogram=self.report.cost_histogram,
            uncov_histogram=self.report.uncov_histogram,
            quantiles=self.report.quantiles,
            runtime_tiers=self.report.runtime_tiers,
            matched_records=self.report.matched_records,
            unmatched_candidate_only=unmatched_cand,
            unmatched_control_only=[],
            timeouts=self.report.timeouts,
        )
        txt = render_text_report(custom_report)
        self.assertIn("Control: Q0", txt)
        self.assertNotIn("Control: timed out", txt)

        html_doc = render_html_report(custom_report)
        self.assertIn("Q0", html_doc)

    def test_unmatched_control_only_rendered(self):
        dummy_run = RunRecord("FontCtrl.ttf", "/fonts/FontCtrl.ttf", 1, "control", 10, 20, 30, 0, 1.0, 50)
        unmatched_ctrl = [
            UnmatchedFontRecord(
                font_name="FontCtrl.ttf",
                font_path="/fonts/FontCtrl.ttf",
                selected_quality=1,
                record=dummy_run,
                other_max_quality=None,
            )
        ]
        custom_report = ComparisonReport(
            dataset_name=self.report.dataset_name,
            notes_text=self.report.notes_text,
            control_trial=self.report.control_trial,
            candidate_trial=self.report.candidate_trial,
            quality_filter=self.report.quality_filter,
            quality_mode_desc=self.report.quality_mode_desc,
            quality_counts=self.report.quality_counts,
            time_stats=self.report.time_stats,
            cost_stats=self.report.cost_stats,
            uncov_stats=self.report.uncov_stats,
            quadrants=self.report.quadrants,
            time_histogram=self.report.time_histogram,
            cost_histogram=self.report.cost_histogram,
            uncov_histogram=self.report.uncov_histogram,
            quantiles=self.report.quantiles,
            runtime_tiers=self.report.runtime_tiers,
            matched_records=self.report.matched_records,
            unmatched_candidate_only=[],
            unmatched_control_only=unmatched_ctrl,
            timeouts=self.report.timeouts,
        )
        txt = render_text_report(custom_report)
        self.assertIn("font(s) completed in control at selected quality", txt)
        self.assertIn("FontCtrl.ttf", txt)

        html_doc = render_html_report(custom_report)
        self.assertIn("FontCtrl.ttf", html_doc)

    def test_render_reports_with_all_zero_uncovered_cost(self):
        # Create dataset where uncovered_ift_patch_cost column is omitted
        scsv_content = (
            "font_path;trial;quality_level;codepoint_count;total_time_s;ift_total_cost;ideal_total_cost;non_ift_total_cost\n"
            "/fonts/A.ttf;control;1;100;10.0;1000;900;1500\n"
            "/fonts/A.ttf;candidate;1;100;8.0;950;900;1500\n"
            "/fonts/B.ttf;control;1;200;20.0;2000;1800;2500\n"
            "/fonts/B.ttf;candidate;1;200;22.0;2100;1800;2500\n"
        )
        zero_scsv = self.dir_path / "zero_uncov.scsv"
        zero_scsv.write_text(scsv_content, encoding="utf-8")
        data, trials, qualities = load_scsv(zero_scsv)
        report = build_report(
            dataset_name="zero_uncov_test",
            notes_text="",
            data=data,
            errors={},
            available_qualities=qualities,
            control_trial="control",
            candidate_trial="candidate",
            quality_filter=1,
        )
        self.assertEqual(report.uncov_stats.active_count, 0)
        self.assertIsNone(report.uncov_stats.delta_pct_median)

        # Ensure text report renders with "n/a" and without ZeroDivisionError
        txt = render_text_report(report)
        self.assertIn("active fonts: 0", txt)
        self.assertIn("n/a", txt)

        # Ensure HTML report renders cleanly
        html_doc = render_html_report(report)
        self.assertIn("<!DOCTYPE html>", html_doc)

    def test_cli_text_stdout(self):
        ret = main([str(self.dir_path), "control", "candidate", "--text"])
        self.assertEqual(ret, 0)

    def test_cli_text_output_file(self):
        out_txt = self.dir_path / "report.txt"
        ret = main([str(self.dir_path), "control", "candidate", "-o", str(out_txt)])
        self.assertEqual(ret, 0)
        self.assertTrue(out_txt.exists())
        self.assertIn("IFT SEGMENTER COMPARISON REPORT", out_txt.read_text(encoding="utf-8"))

    def test_cli_html_output_file(self):
        out_html = self.dir_path / "report.html"
        ret = main([str(self.dir_path), "control", "candidate", "-o", str(out_html)])
        self.assertEqual(ret, 0)
        self.assertTrue(out_html.exists())
        self.assertIn("<!DOCTYPE html>", out_html.read_text(encoding="utf-8"))

    def test_cli_error_returns_nonzero(self):
        ret = main([str(self.dir_path), "control", "nonexistent_trial"])
        self.assertEqual(ret, 1)


class TwoDirectoryComparisonTest(absltest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.dir_a = self.root / "commit_a"
        self.dir_b = self.root / "commit_b"
        self.dir_a.mkdir()
        self.dir_b.mkdir()

        # Both directories only have a single "control" trial
        (self.dir_a / "out.scsv").write_text(
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; uncovered_ift_patch_cost; total_time_s; codepoint_count\n"
            "/fonts/A.ttf; 1; control; 100; 200; 300; 10; 4.0; 50\n"
            "/fonts/B.ttf; 1; control; 200; 400; 600; 20; 10.0; 120\n",
            encoding="utf-8",
        )
        (self.dir_a / "NOTES.txt").write_text("Encoder Commit: aaaa1111\n", encoding="utf-8")
        (self.dir_a / "error_log.txt").write_text(
            "Timeout occurred for font: /fonts/C.ttf, trial: control at quality level: 1\n",
            encoding="utf-8",
        )

        (self.dir_b / "out.scsv").write_text(
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; uncovered_ift_patch_cost; total_time_s; codepoint_count\n"
            "/fonts/A.ttf; 1; control; 100; 180; 300; 8; 2.0; 50\n"
            "/fonts/B.ttf; 1; control; 200; 410; 600; 18; 8.0; 120\n"
            "/fonts/C.ttf; 1; control; 150; 300; 450; 5; 5.0; 90\n",
            encoding="utf-8",
        )
        (self.dir_b / "NOTES.txt").write_text("Encoder Commit: bbbb2222\n", encoding="utf-8")
        (self.dir_b / "error_log.txt").write_text(
            "Error processing /fonts/D.ttf, quality 1, trial control: command line return code 1\n",
            encoding="utf-8",
        )

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_parse_target_spec(self):
        path_only, trial = parse_target_spec(str(self.dir_a))
        self.assertEqual(path_only, str(self.dir_a))
        self.assertIsNone(trial)

        path_with_trial, trial = parse_target_spec(f"{self.dir_a}:control")
        self.assertEqual(path_with_trial, str(self.dir_a))
        self.assertEqual(trial, "control")

        rel_path, rel_trial = parse_target_spec("commit_a:my_trial", str(self.root))
        self.assertEqual(rel_path, str(self.dir_a))
        self.assertEqual(rel_trial, "my_trial")

    def test_two_dirs_single_trial_auto_selected_even_with_identical_trial_name(self):
        report = build_report_from_specs(str(self.dir_a), str(self.dir_b))
        self.assertEqual(report.control_trial, "commit_a:control")
        self.assertEqual(report.candidate_trial, "commit_b:control")
        self.assertEqual(report.total_matched_fonts, 2)
        self.assertAlmostEqual(report.time_stats.control_sum, 14.0)
        self.assertAlmostEqual(report.time_stats.candidate_sum, 10.0)
        self.assertEqual(report.timeouts.resolved_count, 1)  # /fonts/C.ttf resolved
        self.assertEqual(report.timeouts.missing_error_count, 1)  # /fonts/D.ttf missing in control
        self.assertIn("aaaa1111", report.control_notes_text)
        self.assertIn("bbbb2222", report.candidate_notes_text)

        txt = render_text_report(report)
        self.assertIn("Control:   commit_a:control", txt)
        self.assertIn("Candidate: commit_b:control", txt)
        self.assertIn("Ctrl Note: Encoder Commit: aaaa1111", txt)
        self.assertIn("Cand Note: Encoder Commit: bbbb2222", txt)

        html_doc = render_html_report(report)
        self.assertIn("commit_a:control", html_doc)
        self.assertIn("commit_b:control", html_doc)
        self.assertIn("Control Notes", html_doc)
        self.assertIn("Candidate Notes", html_doc)

    def test_two_dirs_same_basename_disambiguated(self):
        nested_a = self.root / "v1" / "run"
        nested_b = self.root / "v2" / "run"
        nested_a.mkdir(parents=True)
        nested_b.mkdir(parents=True)
        (nested_a / "out.scsv").write_text((self.dir_a / "out.scsv").read_text(encoding="utf-8"), encoding="utf-8")
        (nested_b / "out.scsv").write_text((self.dir_b / "out.scsv").read_text(encoding="utf-8"), encoding="utf-8")

        report = build_report_from_specs(str(nested_a), str(nested_b))
        self.assertEqual(report.control_trial, "v1/run:control")
        self.assertEqual(report.candidate_trial, "v2/run:control")
        self.assertEqual(report.total_matched_fonts, 2)

    def test_two_dirs_explicit_trials_and_errors(self):
        multi_a = self.root / "multi_a"
        multi_b = self.root / "multi_b"
        multi_a.mkdir()
        multi_b.mkdir()
        (multi_a / "out.scsv").write_text(
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; uncovered_ift_patch_cost; total_time_s; codepoint_count\n"
            "/fonts/A.ttf; 1; t1; 100; 200; 300; 10; 4.0; 50\n"
            "/fonts/A.ttf; 1; t2; 100; 210; 300; 10; 5.0; 50\n",
            encoding="utf-8",
        )
        (multi_b / "out.scsv").write_text(
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; uncovered_ift_patch_cost; total_time_s; codepoint_count\n"
            "/fonts/A.ttf; 1; exp1; 100; 180; 300; 8; 2.0; 50\n"
            "/fonts/A.ttf; 1; exp2; 100; 190; 300; 8; 2.5; 50\n",
            encoding="utf-8",
        )

        # Missing trial suffix when multiple trials exist must raise ValueError
        with self.assertRaises(ValueError) as ctx:
            build_report_from_specs(str(multi_a), f"{multi_b}:exp1")
        self.assertIn("contains multiple trials", str(ctx.exception))

        # Nonexistent explicit trial must raise ValueError
        with self.assertRaises(ValueError) as ctx2:
            build_report_from_specs(f"{multi_a}:missing", f"{multi_b}:exp1")
        self.assertIn("not found", str(ctx2.exception))

        # Valid explicit specs
        report = build_report_from_specs(f"{multi_a}:t2", f"{multi_b}:exp1")
        self.assertEqual(report.control_trial, "multi_a:t2")
        self.assertEqual(report.candidate_trial, "multi_b:exp1")
        self.assertAlmostEqual(report.time_stats.control_sum, 5.0)
        self.assertAlmostEqual(report.time_stats.candidate_sum, 2.0)

    def test_two_arg_mode_same_directory_different_trials(self):
        same_dir = self.root / "same"
        same_dir.mkdir()
        (same_dir / "out.scsv").write_text(
            "font_path; quality_level; trial; ideal_total_cost; ift_total_cost; non_ift_total_cost; uncovered_ift_patch_cost; total_time_s; codepoint_count\n"
            "/fonts/A.ttf; 1; ctrl; 100; 200; 300; 10; 4.0; 50\n"
            "/fonts/A.ttf; 1; cand; 100; 180; 300; 8; 2.0; 50\n",
            encoding="utf-8",
        )
        report = build_report_from_specs(f"{same_dir}:ctrl", f"{same_dir}:cand")
        self.assertEqual(report.control_trial, "ctrl")
        self.assertEqual(report.candidate_trial, "cand")
        self.assertEqual(report.dataset_name, "same")

    def test_cli_two_directory_html_default_filename_sanitized(self):
        out_dir = self.root / "out_html"
        out_dir.mkdir()
        prev_bwd = os.environ.get("BUILD_WORKING_DIRECTORY")
        os.environ["BUILD_WORKING_DIRECTORY"] = str(out_dir)
        try:
            ret = main([str(self.dir_a), str(self.dir_b), "--html"])
        finally:
            if prev_bwd is None:
                os.environ.pop("BUILD_WORKING_DIRECTORY", None)
            else:
                os.environ["BUILD_WORKING_DIRECTORY"] = prev_bwd

        self.assertEqual(ret, 0)
        expected_html = out_dir / "comparison_commit_a_control_vs_commit_b_control.html"
        self.assertTrue(expected_html.exists())
        self.assertIn("commit_a:control", expected_html.read_text(encoding="utf-8"))

    def test_cli_absl_flags_and_validation(self):
        out_txt = self.root / "absl_flags.txt"
        ret = main([
            str(self.dir_a),
            str(self.dir_b),
            "--quality_mode=candidate",
            "--top_n=5",
            "--format=text",
            f"--output={out_txt}",
        ])
        self.assertEqual(ret, 0)
        self.assertTrue(out_txt.exists())
        self.assertIn("Highest Non-Timeout Candidate Quality", out_txt.read_text(encoding="utf-8"))

        # Invalid enum value and invalid positional arg count return nonzero
        self.assertEqual(main([str(self.dir_a), str(self.dir_b), "--quality_mode=bogus"]), 1)
        self.assertEqual(main([str(self.dir_a)]), 1)


if __name__ == "__main__":
    absltest.main()
