# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Execute actual diagnostic wrappers with deterministic stage timing."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STUB = r"""
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef int status_t;
struct pbl_settings_raw_iter {int unused;};
struct pbl_settings_record_hdr {unsigned flags,key_len,val_len;};
#define SETTINGS_FLAG_WRITE_COMPLETE 1
struct pbl_settings_file {char *name;struct pbl_settings_raw_iter iter;};
struct pbl_thread { uint32_t id; };
struct pbl_thread *pbl_thread_current(void);
uint32_t pbl_uptime_ticks(void);
void pbl_irq_lock(void);
void pbl_irq_unlock(void);
void test_log(const char *, ...);
#define PBL_LOG_MODULE_DEFINE(...)
#define LOG_LEVEL_DEBUG 4
#define PBL_LOG_SYNC_WRN test_log
"""
HARNESS = r"""
#include <assert.h>
#include SOURCE
void t5ai_flash_phase_trace_dump(void) {}
static uint32_t now;
static int locked, logs;
static status_t result;
static struct pbl_thread thread = {7};
struct pbl_thread *pbl_thread_current(void) { return &thread; }
uint32_t pbl_uptime_ticks(void) { return now; }
void pbl_irq_lock(void) { ++locked; }
void pbl_irq_unlock(void) { assert(locked); --locked; }
void test_log(const char *fmt, ...) { assert(!locked); ++logs; }
void __real_pbl_settings__raw_iter_write_header(struct pbl_settings_raw_iter *i, struct pbl_settings_record_hdr *h) { now += 2; }
void __real_pbl_settings__raw_iter_write_key(struct pbl_settings_raw_iter *i, const uint8_t *d) { now += 3; }
void __real_pbl_settings__raw_iter_write_val(struct pbl_settings_raw_iter *i, const uint8_t *d) { now += 4; }
status_t write_record(struct pbl_settings_file *f,const void *k,size_t kl,
                                                const void *v,size_t vl,uint32_t ts) {
  assert(ts == 42); now += 5;
  struct pbl_settings_record_hdr h = {.flags=1,.key_len=kl,.val_len=vl};
  __wrap_pbl_settings__raw_iter_write_header(&f->iter,&h);
  __wrap_pbl_settings__raw_iter_write_key(&f->iter,k);
  __wrap_pbl_settings__raw_iter_write_val(&f->iter,v);
  h.flags = 0;
  if (result >= 0) { __wrap_pbl_settings__raw_iter_write_header(&f->iter,&h); }
  return result;
}
status_t __real_pbl_settings_mark_all_dirty(struct pbl_settings_file *f) {
  assert(prv_ours());
  unsigned before = logs;
  assert(write_record(f,"k",1,"val",3,42) == result);
  assert(logs == before); // No logging in a record or I/O measurement.
  return result;
}
int main(void) {
  struct pbl_settings_file f = {.name="testprefs"};
  now = UINT32_MAX - 8;
  assert(__wrap_pbl_settings_mark_all_dirty(&f) == 0);
  assert(!s_owner && s_records == 1 && s_bytes == 4 && s_file == 1);
  assert(s_record[0].count == 1 && s_record[0].total == 11 && s_record[0].max == 11);
  assert(s_record[1].count == 2 && s_record[1].total == 4);
  assert(s_record[4].total == 5);
  assert(s_record[2].total == 3 && s_record[3].total == 4);
  unsigned before = s_record[0].count;
  write_record(&f,"k",1,"val",3,42);
  assert(s_record[0].count == before); // Outside rewrite excluded.
  result = -7; assert(__wrap_pbl_settings_mark_all_dirty(&f) == -7);
  assert(!s_owner && !s_records && !s_bytes && s_file == 2);
  uint32_t samples[] = {0,1,4,9,19,49,50};
  for (unsigned i=0;i<7;++i) { t5ai_io_trace_sample(T5IO_WRITE_TOTAL,samples[i]); }
  assert(s_io[T5IO_WRITE_TOTAL].count == 7 && s_io[T5IO_WRITE_TOTAL].total == 132);
  assert(s_io[T5IO_WRITE_TOTAL].max == 50);
  for (unsigned i=0;i<7;++i) { assert(s_io[T5IO_WRITE_TOTAL].bins[i] == 1); }
  t5ai_io_trace_sample(T5IO_COUNT, 123); // Invalid stage ignored.
  t5ai_io_trace_dump();
  assert(!locked && logs > 0);
}
"""


class SettingsSyncTraceTest(unittest.TestCase):
    def test_mailbox_write_filter_matches_flash_protocol(self):
        flash = (ROOT / "drivers/flash/t5ai_flash_ipc.c").read_text()
        mailbox = (ROOT / "drivers/t5ai/mb_ipc.c").read_text()
        command = int(re.search(r"FLASH_WRITE = (\d+)", flash).group(1))
        filter_command = int(
            re.search(
                r"const bool trace_write =.*?MB_IPC_USER_COMMAND_SHIFT\) == (\d+)u;",
                mailbox,
                re.DOTALL,
            ).group(1)
        )
        self.assertEqual(filter_command, command)

    def test_stages_wraparound_failure_and_histogram(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            headers = [
                "pbl/kernel/irq.h",
                "pbl/kernel/time.h",
                "pbl/kernel/sched.h",
                "pbl/kernel/thread.h",
                "pbl/logging/logging.h",
                "pbl/settings/settings.h",
                "raw_iter.h",
            ]
            for header in headers:
                p = root / header
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text('#include "stub.h"\n')
            p = root / "pbl/soc/t5ai/io_trace.h"
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text((ROOT / "include/pbl/soc/t5ai/io_trace.h").read_text())
            (root / "stub.h").write_text(STUB)
            (root / "test.c").write_text(HARNESS)
            exe = root / "test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-I" + str(root),
                    '-DSOURCE="' + str(ROOT / "soc/t5ai/settings_sync_trace.c") + '"',
                    str(root / "test.c"),
                    "-o",
                    str(exe),
                ],
                check=True,
                capture_output=True,
            )
            subprocess.run([str(exe)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
