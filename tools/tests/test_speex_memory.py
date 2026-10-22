# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise codec allocation ownership with real Speex and the production heap."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HEADERS = {
    "pbl/kernel/mutex.h": """#pragma once
#include <assert.h>
struct pbl_mutex { unsigned locked; };
#define PBL_MUTEX_DEFINE(name) struct pbl_mutex name
#define PBL_FOREVER 0
static inline void pbl_mutex_lock(struct pbl_mutex *m, unsigned t) {
  (void)t; assert(!m->locked); m->locked = 1;
}
static inline void pbl_mutex_unlock(struct pbl_mutex *m) {
  assert(m->locked); m->locked = 0;
}
""",
    "pbl/kernel/assert.h": """#pragma once
#include <assert.h>
#define PBL_ASSERTN(x) assert(x)
""",
    "pbl/util/assert.h": """#pragma once
#include <assert.h>
#define UTIL_ASSERT(x) assert(x)
""",
    "pbl/util/math.h": """#pragma once
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
""",
    "pbl/logging/logging.h": """#pragma once
#define PBL_LOG_MODULE_DECLARE(...)
#define PBL_LOG_DBG(...)
#define PBL_LOG_ERR(...)
#define PBL_LOG_WRN(...)
#define PBL_LOG_VERBOSE(...)
""",
    "board/board.h": "#pragma once\n#define MIC NULL\n",
    "pbl/services/voice_endpoint.h": """#pragma once
#include <stdint.h>
typedef struct __attribute__((packed)) {
  char version[20]; uint32_t sample_rate; uint16_t bit_rate;
  uint8_t bitstream_version; uint16_t frame_size;
} AudioTransferInfoSpeex;
""",
}

T5_BACKEND = r"""
#include "soc/t5ai/voice_memory.c"
void util_dbgserial_str(const char *str) { (void)str; }
void util_log(const char *file, int line, const char *str) {
  (void)file; (void)line; (void)str;
}
static void check_empty(void) {
  unsigned used, available, largest;
  heap_calc_totals(&s_heap, &used, &available, &largest);
  assert(used == 0 && available && largest);
  assert(!s_mutex.locked);
}
"""

DEFAULT_BACKEND = r"""
#include <assert.h>
#include <stdlib.h>
static size_t live;
void *kernel_malloc_check(size_t n) {
  void *p = malloc(n); assert(p); ++live; return p;
}
void *kernel_zalloc_check(size_t n) {
  void *p = calloc(1, n); assert(p); ++live; return p;
}
void *kernel_realloc(void *p, size_t n) {
  if (n > 1000000) return NULL;
  void *result = realloc(p, n);
  if (!p && result) ++live;
  return result;
}
void kernel_free(void *p) { if (p) { assert(live); --live; } free(p); }
static void check_empty(void) { assert(!live); }
"""

CONTRACT = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <pbl/util/speex_memory.h>
int main(void) {
  pbl_speex_free(NULL);
  unsigned char *p = pbl_speex_zalloc_check(64);
  for (unsigned i = 0; i < 64; ++i) assert(p[i] == 0);
  memset(p, 0x5a, 64);
  unsigned char *grown = pbl_speex_realloc(p, 128);
  assert(grown);
  for (unsigned i = 0; i < 64; ++i) assert(grown[i] == 0x5a);
  assert(!pbl_speex_realloc(grown, 1000001));
  for (unsigned i = 0; i < 64; ++i) assert(grown[i] == 0x5a);
  p = pbl_speex_realloc(grown, 16); assert(p);
  for (unsigned i = 0; i < 16; ++i) assert(p[i] == 0x5a);
  pbl_speex_free(p);
  p = pbl_speex_realloc(NULL, 32); assert(p); pbl_speex_free(p);
  p = pbl_speex_malloc_check(32); assert(p); pbl_speex_free(p);
  check_empty();
#ifdef TEST_T5
  // Checked allocation failure must still be fatal; failed realloc above is recoverable.
  pid_t child = fork(); assert(child >= 0);
  if (!child) { pbl_speex_malloc_check(T5AI_VOICE_ARENA_BYTES); _exit(0); }
  int status; assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  child = fork(); assert(child >= 0);
  if (!child) { int foreign; pbl_speex_free(&foreign); _exit(0); }
  assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  check_empty();
#endif
}
"""

LIFECYCLE = r"""
#include <assert.h>
#include <string.h>
#include <pbl/services/voice/voice_speex.h>
#include <pbl/drivers/mic.h>
#include <speex/speex_bits.h>
uint32_t mic_get_channels(MicDevice *device) { (void)device; return 1; }
int main(void) {
  assert(!voice_speex_is_initialized());
  voice_speex_deinit();
  for (unsigned cycle = 0; cycle < 20; ++cycle) {
    assert(voice_speex_init() && voice_speex_init());
    assert(voice_speex_get_frame_size() == 320);
    int16_t *samples = voice_speex_get_frame_buffer(); assert(samples);
#ifdef TEST_T5
    assert(heap_contains_address(&s_heap, samples));
#endif
    assert(voice_speex_get_frame_buffer_size() == 640);
    AudioTransferInfoSpeex info;
    voice_speex_get_transfer_info(&info);
    assert(info.sample_rate == 16000 && info.frame_size == 320);
    unsigned char encoded[320];
    for (unsigned frame = 0; frame < 5; ++frame) {
      for (unsigned i = 0; i < 320; ++i) samples[i] = (i % 32) * 100 - 1600;
      int bytes = voice_speex_encode_frame(samples, encoded, sizeof(encoded));
      assert(bytes > 0 && bytes <= (int)sizeof(encoded));
    }
    voice_speex_deinit(); voice_speex_deinit();
    assert(!voice_speex_is_initialized() && !voice_speex_get_frame_buffer());
    check_empty();
  }
  SpeexBits bits;
  speex_bits_init(&bits);
  char input[20000]; memset(input, 0x5a, sizeof(input));
  speex_bits_read_from(&bits, input, 4096);
  assert(bits.buf_size == 4096 && bits.nbBits == 4096 * 8);
#ifdef TEST_T5
  assert(heap_contains_address(&s_heap, bits.chars));
  char *previous = bits.chars;
  // Real Speex must retain its allocation when the bounded arena cannot grow it.
  speex_bits_read_from(&bits, input, sizeof(input));
  assert(bits.chars == previous && bits.buf_size == 4096);
  assert(bits.nbBits == 4096 * 8);
#endif
  speex_bits_destroy(&bits);
  check_empty();
}
"""


class SpeexMemoryTest(unittest.TestCase):
    def run_harness(self, body, *, t5, codec=False):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for name, text in HEADERS.items():
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            source = directory / "test.c"
            source.write_text("#include <stdbool.h>\n" +
                              (T5_BACKEND if t5 else DEFAULT_BACKEND) + body)
            sources = [source]
            if t5:
                sources.append(ROOT / "lib/util/heap.c")
            else:
                sources.append(ROOT / "third_party/speex/memory.c")
            flags = []
            if codec:
                cmake = (ROOT / "third_party/speex/CMakeLists.txt").read_text()
                names = re.search(r"set\(speex_sources(.*?)\)", cmake, re.DOTALL)[1].split()
                sources += [ROOT / "third_party/speex" / name for name in names]
                sources.append(ROOT / "fw/services/voice/voice_speex.c")
                flags = ["-DHAVE_CONFIG_H", "-DOS_SUPPORT_CUSTOM", "-DFIXED_POINT",
                         "-DDISABLE_FLOAT_API", "-DDISABLE_VBR", "-DDISABLE_WARNINGS",
                         "-DDISABLE_NOTIFICATIONS", "-DNB_ENC_STACK=(2000*sizeof(spx_sig_t))",
                         "-DNB_DEC_STACK=(1500*sizeof(spx_sig_t))"]
            command = [compiler, "-std=gnu11", "-O2", "-include", "stdbool.h",
                       "-Dstatic_assert=_Static_assert", "-Dnullptr=NULL",
                       "-I", str(directory), "-I", str(ROOT / "include"),
                       "-I", str(ROOT), "-I", str(ROOT / "fw"),
                       "-I", str(ROOT / "third_party/speex"),
                       "-I", str(ROOT / "third_party/speex/speex/include"),
                       "-I", str(ROOT / "third_party/speex/speex/libspeex")]
            if t5:
                command.append("-DTEST_T5")
            command += flags + [str(path) for path in sources]
            binary = directory / "test"
            result = subprocess.run(command + ["-lm", "-o", str(binary)], check=False, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # Child assertion failures are intentional in the T5 contract case.
            result = subprocess.run([str(binary)], check=False, capture_output=True, text=True, cwd=directory)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_t5_contract(self):
        self.run_harness(CONTRACT, t5=True)

    def test_default_contract(self):
        self.run_harness(CONTRACT, t5=False)

    def test_t5_voice_codec_lifecycle(self):
        self.run_harness(LIFECYCLE, t5=True, codec=True)

    def test_default_voice_codec_lifecycle(self):
        self.run_harness(LIFECYCLE, t5=False, codec=True)
