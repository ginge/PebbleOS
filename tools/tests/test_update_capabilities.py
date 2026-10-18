# SPDX-License-Identifier: Apache-2.0
"""Update admission, platform defaults and stock enabled-path equivalence."""
import os
import re
import subprocess
import tempfile
import unittest
from itertools import product
from pathlib import Path
from unittest.mock import patch

import kconfiglib

from tools.tests.test_getafix_boards import load_config as getafix_config
from tools.tests.test_unhygienix_boards import load_config as board_config

ROOT = Path(__file__).resolve().parents[2]
CAPABILITIES = ("FIRMWARE_UPDATE_SUPPORTED", "RECOVERY_UPDATE_SUPPORTED",
                "SYSTEM_RESOURCE_UPDATE_SUPPORTED")
SOURCES = ("fw/services/firmware_update/service.c", "fw/services/prf_update/service.c",
           "fw/services/put_bytes/put_bytes.c")
# Pin the pre-change implementation; do not silently move comparisons with HEAD.
BASELINE = "b694ad747c0fc317f61123cc5537daaaab02fc2d"


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HEADERS = {
    "pbl/logging/logging.h": """#pragma once
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_WRN(...) (++warnings)
#define PBL_LOG_ERR(...)
#define PBL_LOG_DBG(...)
extern unsigned warnings;
""",
    "pbl/kernel/sem.h": """#pragma once
#define PBL_SEM_DEFINE(name,a,b) int name
#define PBL_NO_WAIT 0
extern unsigned side_effects;
static inline int pbl_sem_take(int *s, int t) { (void)s; (void)t; ++side_effects; return 0; }
static inline void pbl_sem_give(int *s) { (void)s; ++side_effects; }
""",
    "pbl/services/runlevel.h": """#pragma once
#define RunLevel_Normal 0
static inline void services_set_runlevel(int level) { (void)level; ++side_effects; }
""",
    "pbl/util/math.h": "#define MIN(a,b) ((a)<(b)?(a):(b))\n",
    "kernel/system_message.h": """#pragma once
#include <assert.h>
extern unsigned replies;
static inline void system_message_send_firmware_start_response(FirmwareUpdateStatus s) {
  assert(s == FirmwareUpdateCancelled); ++replies;
}
""",
    "system/passert.h": "#include <assert.h>\n#define PBL_ASSERTN(x) assert(x)\n",
    "kernel/events.h": """#pragma once
#include <pbl/services/put_bytes/put_bytes.h>
typedef enum { PebbleSystemMessageFirmwareUpdateStartLegacy,
  PebbleSystemMessageFirmwareUpdateStart, PebbleSystemMessageFirmwareUpdateComplete,
  PebbleSystemMessageFirmwareUpdateFailed } PebbleSystemMessageEventType;
typedef struct { PebbleSystemMessageEventType type;
  uint32_t bytes_transferred, total_transfer_size; } PebbleSystemMessageEvent;
typedef enum { PebblePutBytesEventTypeStart, PebblePutBytesEventTypeCleanup,
  PebblePutBytesEventTypeProgress, PebblePutBytesEventTypeInitTimeout } PebblePutBytesEventType;
typedef struct { PebblePutBytesEventType type; uint8_t progress_percent;
  PutBytesObjectType object_type; bool failed; uint32_t bytes_transferred;
} PebblePutBytesEvent;
""",
    "system/bootbits.h": """#pragma once
#include <assert.h>
#define BOOT_BIT_NEW_PRF_AVAILABLE 1
extern bool pending;
extern unsigned clears;
static inline bool boot_bit_test(int bit) { assert(bit == 1); return pending; }
static inline void boot_bit_clear(int bit) { assert(bit == 1); pending = false; ++clears; }
""",
}
for name in ("pbl/drivers/flash.h", "pbl/services/battery/battery_monitor.h",
             "apps/core/progress_ui.h", "kernel/ui/modals/modal_manager.h",
             "process_management/app_manager.h", "flash_region/flash_region.h",
             "system/firmware_storage.h"):
    HEADERS[name] = "#pragma once\n"


class UpdateCapabilitiesTest(unittest.TestCase):
    def compile_run(self, source, defines=(), sources=()):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for name, text in HEADERS.items():
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            path = directory / "test.c"
            path.write_text(source)
            binary = directory / "test"
            cmd = ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                   "-Dnullptr=NULL", "-include", "stdbool.h", "-include", "stddef.h",
                   "-I", str(directory), "-I", str(ROOT / "include")]
            cmd += [f"-D{define}" for define in defines]
            cmd += [str(path)] + [str(ROOT / name) for name in sources]
            result = subprocess.run(cmd + ["-o", str(binary)], check=False,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], check=False, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_platform_defaults(self):
        for variant in ("normal", "prf"):
            for board in ("unhygienix", "obelix", "asterix"):
                config = board_config(board, variant)
                with self.subTest(board=board, variant=variant):
                    for cap in CAPABILITIES:
                        self.assertEqual(config[cap], "n" if board == "unhygienix" else "y")
            for revision in ("dvt", "dvt2"):
                config = getafix_config(revision, variant)
                for cap in CAPABILITIES:
                    self.assertEqual(config[cap], "y")

    def test_hidden_capabilities_cannot_be_enabled_by_user_override_on_t5ai(self):
        with patch.dict(os.environ, {"srctree": str(ROOT), "BOARD": "unhygienix",
                                     "BOARD_REVISION": "_"}):
            config = kconfiglib.Kconfig(str(ROOT / "Kconfig"), warn=False)
            config.load_config(str(ROOT / "boards/unhygienix/defconfig"))
            for cap in CAPABILITIES:
                config.syms[cap].set_value(2)
                self.assertEqual(config.syms[cap].str_value, "n")

    def test_put_bytes_capability_matrix(self):
        gate = function(SOURCES[2], "static bool prv_is_object_allowed(")
        for flags in product((0, 1), repeat=3):
            for recovery in (False, True):
                defines = [f"CONFIG_{cap}" for cap, enabled in zip(CAPABILITIES, flags) if enabled]
                defines += [f"EXPECT_{i}={flag}" for i, flag in enumerate(flags)]
                if recovery:
                    defines += ["CONFIG_RECOVERY_FW", "EXPECT_PRF=1"]
                else:
                    defines += ["EXPECT_PRF=0"]
                source = """
#include <assert.h>
#include <pbl/services/put_bytes/put_bytes.h>
#define PBL_LOG_WRN(...)
""" + gate + """
int main(void) {
  assert(prv_is_object_allowed(ObjectFirmware) == EXPECT_0);
  assert(prv_is_object_allowed(ObjectRecovery) == (EXPECT_1 && !EXPECT_PRF));
  assert(prv_is_object_allowed(ObjectSysResources) == EXPECT_2);
  assert(prv_is_object_allowed(ObjectWatchApp) == !EXPECT_PRF);
  assert(prv_is_object_allowed(ObjectWatchWorker) == !EXPECT_PRF);
  assert(prv_is_object_allowed(ObjectAppResources) == !EXPECT_PRF);
  assert(prv_is_object_allowed(ObjectFile) == !EXPECT_PRF);
}
"""
                with self.subTest(flags=flags, recovery=recovery):
                    self.compile_run(source, defines)

    def test_disabled_firmware_endpoint_cancels_before_side_effects(self):
        self.compile_run("""
#include <assert.h>
#include <pbl/services/firmware_update.h>
unsigned warnings, replies, side_effects;
int main(void) {
  firmware_update_init();
  for (int type = PebbleSystemMessageFirmwareUpdateStartLegacy;
       type <= PebbleSystemMessageFirmwareUpdateStart; ++type) {
    PebbleSystemMessageEvent event = {.type=type, .total_transfer_size=100};
    firmware_update_event_handler(&event);
    assert(firmware_update_current_status() == FirmwareUpdateCancelled);
    assert(!firmware_update_is_in_progress());
    assert(firmware_update_get_percent_progress() == 0);
    PebblePutBytesEvent progress = {.type=PebblePutBytesEventTypeProgress,
                                   .object_type=ObjectFirmware, .bytes_transferred=99};
    firmware_update_pb_event_handler(&progress);
    assert(firmware_update_get_percent_progress() == 0);
  }
  assert(warnings == 2 && replies == 2 && side_effects == 0);
}
""", sources=(SOURCES[0],))

    def test_prf_stale_request_consumed_without_flash_copy(self):
        source = """
#include <assert.h>
bool pending;
unsigned warnings, clears;
void check_prf_update(void);
int main(void) {
  check_prf_update(); assert(!clears && !warnings);
  pending=true;
  check_prf_update(); assert(!pending && clears == 1);
  check_prf_update(); assert(clears == 1);
}
"""
        for defines in ((), ("CONFIG_RECOVERY_FW",),
                        ("CONFIG_RECOVERY_FW", "CONFIG_RECOVERY_UPDATE_SUPPORTED")):
            self.compile_run(source, defines, sources=(SOURCES[1],))

    def test_supported_paths_preprocess_identically_to_baseline(self):
        # Compare real shared C tokens with platform capabilities enabled. Includes
        # are empty here; native PutBytes tests and ARM builds check compilation.
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for name in SOURCES:
                before = subprocess.check_output(["git", "show", f"{BASELINE}:{name}"],
                                                 cwd=ROOT, text=True)
                after = (ROOT / name).read_text()
                includes = re.findall(r'^#include [<"]([^>"]+)', before + after, re.MULTILINE)
                for include in includes:
                    header = directory / include
                    header.parent.mkdir(parents=True, exist_ok=True)
                    header.touch()
                for recovery in (False, True):
                    outputs = []
                    for label, text in (("before", before), ("after", after)):
                        path = directory / f"{label}.c"
                        path.write_text(text)
                        cmd = ["cc", "-E", "-P", "-I", str(directory)]
                        cmd += [f"-DCONFIG_{cap}" for cap in CAPABILITIES]
                        if recovery:
                            cmd.append("-DCONFIG_RECOVERY_FW")
                        outputs.append(subprocess.check_output(cmd + [str(path)], text=True))
                    with self.subTest(source=name, recovery=recovery):
                        self.assertEqual(outputs[0], outputs[1])
