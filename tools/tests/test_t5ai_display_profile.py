# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise real display instrumentation and its decoded-log report together."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.unhygienix_display_profile import (
    read_cadence,
    read_peaks,
    read_transfer_windows,
    read_windows,
    summarize,
    summarize_cadence,
    summarize_peaks,
    summarize_transfer,
)

ROOT = Path(__file__).resolve().parents[2]


class DisplayProfileTest(unittest.TestCase):
    def test_real_profile_windows_wrap_and_clock_change(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            headers = {
                "pbl/logging/logging.h": '#include <stdio.h>\n#define LOG_LEVEL_DEBUG 0\n#define PBL_LOG_MODULE_DEFINE(...)\n#define PBL_LOG_DBG(...) do { printf(__VA_ARGS__); puts(""); } while (0)\n',
                "system/profiler.h": "#define PROFILER_NODE_GET_COUNT(n) count_##n\n#define PROFILER_NODE_GET_TOTAL_CYCLES(n) cycles_##n\n",
            }
            for name, source in headers.items():
                p = d / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(source)
            (d / "test.c").write_text(r"""
#include <stdint.h>
static uint32_t count_render_app, cycles_render_app;
static uint32_t count_compositor, cycles_compositor;
static uint32_t count_text_render_flash, cycles_text_render_flash;
#include "soc/t5ai/display_profile.c"
int main(void) {
  T5AITransferProfile transfer = {.rows=260, .panel_rows=466, .bytes=434312,
      .dma_submits=260, .dma_frames=1, .convert_cycles=500000,
      .wait_cycles=1000000, .observed_cycles=1800000, .wait_max_cycles=20000};
  uint32_t start = UINT32_MAX - 1000u;
  for (unsigned i = 0; i < 64; ++i) {
    if (i == 16) {
      // Force a profiler cumulative-cycle wrap without changing its delta.
      s_render.previous_cycles = cycles_render_app = UINT32_MAX - 100u;
    }
    ++count_render_app; cycles_render_app += 2000000;
    ++count_compositor; cycles_compositor += 400000;
    ++count_text_render_flash; cycles_text_render_flash += 100000;
    t5ai_display_profile_frame(start, 640u, i == 35 ? 50000000u : 100000000u, 0, &transfer);
    start += 1536u;
  }
  // Long idle intervals require a 64-bit span across sixteen frames.
  for (unsigned i = 0; i < 16; ++i) {
    t5ai_display_profile_frame(start, 640u, 100000000u, 0, &transfer);
    start += 0x20000000u;
  }
  s_history_count = 0;
  s_peak_started = false;
  s_peak250 = s_peak1000 = s_peak_interval = 0;
  start = UINT32_MAX - 2000u;
  for (unsigned i = 0; i < 64; ++i) {
    t5ai_display_profile_frame(start, 640u, 100000000u, 0, &transfer);
    start += 1000u;
  }
}
""")
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DCONFIG_T5AI_DISPLAY_PROFILE",
                    "-I",
                    str(d),
                    "-I",
                    str(ROOT),
                    "-I",
                    str(ROOT / "include"),
                    str(d / "test.c"),
                    "-o",
                    str(d / "test"),
                ],
                check=True,
            )
            output = subprocess.check_output([str(d / "test")], text=True)
        transfer_rows, _ = read_transfer_windows(output.splitlines())
        transfer_report = summarize_transfer(transfer_rows[:2])
        self.assertEqual(transfer_report["dma_submissions_per_frame"], 260)
        self.assertEqual(transfer_report["bytes_per_frame"], 434312)
        self.assertEqual(transfer_report["dma_frames"], 32)
        self.assertEqual(transfer_report["stages_per_frame_ms"]["convert_us"], 5)
        self.assertEqual(transfer_report["stages_per_frame_ms"]["wait_us"], 10)
        self.assertEqual(transfer_report["observed_dma_per_frame_ms"], 18)
        lost_stage = [
            line for line in output.splitlines() if not line.startswith("T5DMA seq=2 ")
        ]
        lost_rows, lost_rejected = read_transfer_windows(lost_stage)
        self.assertNotIn(2, [w["seq"] for w in lost_rows])
        self.assertIn(
            {"session": 1, "seq": 2, "reason": "incomplete records"}, lost_rejected
        )
        self.assertEqual(
            read_windows(lost_stage)[0], read_windows(output.splitlines())[0]
        )
        device = read_peaks(output.splitlines())
        # Even if all earlier peak records are lost, the final device high-water
        # counters retain the true 32 FPS burst through a raw timer wrap.
        peak = summarize_peaks(device[-1:])
        self.assertEqual(peak["since_boot_peak_250ms_fps"], 32)
        self.assertEqual(peak["since_boot_peak_1s_fps"], 32)
        self.assertEqual(peak["since_boot_peak_single_interval_fps"], 32)
        self.assertFalse(peak["history_overflow"])
        rows, rejected = read_windows(output.splitlines())
        self.assertEqual([w["seq"] for w in rows[:4]], [1, 2, 4, 5])
        self.assertEqual(
            rejected, [{"session": 1, "seq": 3, "reason": "invalid timing window"}]
        )
        report = summarize(rows[:2])
        self.assertEqual(report["frames"], 32)
        self.assertEqual(report["intervals"], 31)
        self.assertAlmostEqual(report["fps"], 1000 / 48)
        self.assertEqual(report["interval_min_ms"], 48)
        self.assertEqual(report["interval_max_ms"], 48)
        self.assertEqual(report["intervals_over_30fps_budget"], 31)
        self.assertEqual(report["transfer_mean_ms"], 20)
        self.assertEqual(report["render_mean_ms"], 20)
        self.assertEqual(report["comp_mean_ms"], 4)
        self.assertEqual(report["font_mean_ms"], 1)
        self.assertEqual(rows[3]["span"], 1536 + 15 * 0x20000000)
        self.assertEqual(rows[3]["render_n"], 0)  # Never reuse stale render timings.

    def test_dynamic_peak_idle_wrap_loss_and_host_clock(self):
        lines = []
        ticks = 0xFFFFFF00
        elapsed = 0
        # Two seconds at 32 FPS, a two-second pause, then eight FPS.
        for frame in range(1, 98):
            delta = 1000 if frame <= 65 else (64000 if frame == 66 else 4000)
            elapsed += delta
            ticks = (ticks + delta) & 0xFFFFFFFF
            host = 1000000000 + elapsed * 31250
            lines.append(
                f"[host_mono_ns={host}] T5CADENCE frame={frame} tick={ticks} elapsed=640 hz=240000000 dma_fail=0"
            )
        rows = read_cadence(lines)
        report = summarize_cadence(rows)
        self.assertEqual(report["peak_single_interval_fps"], 32)
        self.assertEqual(report["peak_250ms_fps"], 32)
        self.assertEqual(report["peak_1s_fps"], 32)
        self.assertEqual(report["peak_16_interval_fps"], 32)
        self.assertEqual(report["host_crosscheck"]["rtc_to_host_ratio"], 1)
        self.assertIn(0, [point["fps"] for point in report["timeline_250ms"]])
        self.assertIn(8, [point["fps"] for point in report["timeline_250ms"]])
        lost = read_cadence(lines[:20] + lines[22:])
        self.assertEqual(summarize_cadence(lost)["missing_update_records"], 2)
        self.assertNotEqual(lost[19]["segment"], lost[20]["segment"])
        reboot = read_cadence(lines + lines[:3])
        self.assertEqual(reboot[-1]["session"], 2)
        self.assertEqual(reboot[-1]["seconds"], 0.0625)

    def test_missing_records_and_reboot_are_separate(self):
        lines = [
            "T5FRAME seq=5 frames=16 intervals=16 span_lo=24576 span_hi=0 min=1536 max=1536",
            "T5DRAW seq=5 update_us=320000 update_max_us=20000 over30=16 dma_fail=0 invalid=0 hz=100000000",
            "T5FRAME seq=1 frames=16 intervals=15 span_lo=23040 span_hi=0 min=1536 max=1536",
            "T5DRAW seq=1 update_us=320000 update_max_us=20000 over30=15 dma_fail=0 invalid=0 hz=100000000",
            "T5PHASE seq=1 render_n=16 render_us=320000 comp_n=16 comp_us=64000 font_n=0 font_us=0",
        ]
        rows, rejected = read_windows(lines)
        self.assertEqual(rows[0]["session"], 2)
        self.assertEqual(rejected[0]["reason"], "incomplete records")
        self.assertIsNone(summarize(rows)["font_mean_ms"])
        signed = [line.replace("span_lo=23040", "span_lo=-1") for line in lines[2:]]
        self.assertEqual(read_windows(signed)[0][0]["span"], 0xFFFFFFFF)


if __name__ == "__main__":
    unittest.main()
