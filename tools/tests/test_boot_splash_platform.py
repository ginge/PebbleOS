# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Run the actual splash with deterministic host fibers and dependency stubs."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    "pbl/kernel/thread.h": """#pragma once
enum pbl_thread_state { PBL_THREAD_READY=1, PBL_THREAD_DEAD=2 };
struct pbl_thread { int state, id; };
struct pbl_thread_attr { const char *name; void (*entry)(void *); int prio;
 bool privileged; void *stack; unsigned stack_size; };
#define PBL_THREAD_STACK_DEFINE(name,n) unsigned char name[n]
#define PBL_PRIO_MAX 5
#define PBL_MSEC(n) (n)
int pbl_thread_create(struct pbl_thread *, const struct pbl_thread_attr *);
void pbl_thread_sleep(unsigned);
void pbl_thread_abort(struct pbl_thread *);
enum pbl_thread_state pbl_thread_state(const struct pbl_thread *);
""",
    "pbl/kernel/delay.h": "void pbl_sleep_ms(unsigned);\n",
    "kernel/pbl_malloc.h": "void *kernel_malloc(size_t);\nvoid *kernel_zalloc(size_t);\nvoid kernel_free(void *);\n",
    "pbl/drivers/display/display.h": "void display_init(void);\nvoid display_update_boot_frame(uint8_t *);\n",
    "board/display.h": """#pragma once
#define PBL_DISPLAY_WIDTH 260
#define PBL_DISPLAY_HEIGHT 260
#define DISPLAY_FRAMEBUFFER_BYTES (260*260)
""",
    "board/splash.h": '#include "SPLASH_PATH"\n',
    "pbl/services/compositor/compositor.h": """#pragma once
#include <applib/graphics/framebuffer.h>
FrameBuffer *compositor_get_framebuffer(void);
""",
    "applib/graphics/framebuffer.h": """#pragma once
typedef struct { uint8_t buffer[260*260]; uint32_t metadata[4]; } FrameBuffer;
""",
    "system/passert.h": "#include <assert.h>\n#define PBL_ASSERTN(x) assert(x)\n",
}
HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include "STOCK_SOURCE"
#ifdef TEST_T5
#include "T5_SOURCE"
static FrameBuffer framebuffer;
FrameBuffer *compositor_get_framebuffer(void) { return &framebuffer; }
#endif
static ucontext_t main_context, task_context;
static unsigned char host_stack[65536];
static void (*entry)(void *);
static unsigned inits, frames, allocs, frees, live;
static bool fail_thread, fail_alloc, fail_context, stopping;
static bool created, thread_exited, in_worker;
static unsigned context_allocs, context_frees, exit_checks;
static void *context_allocation;
static size_t context_size;
void *kernel_zalloc(size_t n) {
 if (fail_context) return NULL;
 assert(!context_allocation); context_allocation=calloc(1,n); assert(context_allocation);
 context_size=n; ++context_allocs; return context_allocation;
}
void *kernel_malloc(size_t n) {
#ifdef TEST_T5
 assert(!"T5AI must not use the kernel heap for the splash");
#endif
 ++allocs; if (fail_alloc) return NULL;
 void *p=malloc(n); assert(p); ++live; return p;
}
void kernel_free(void *p) {
 if (p==context_allocation) {
   assert(!in_worker && (!created || thread_exited));
   if (created) assert(((struct pbl_thread *)p)->state==PBL_THREAD_DEAD);
   ++context_frees; context_allocation=NULL; free(p); return;
 }
 assert(p && live); --live; ++frees; free(p);
}
void display_init(void) { ++inits; }
static void resume(void) { assert(s_boot_splash_task); swapcontext(&main_context,&task_context); }
void pbl_thread_sleep(unsigned n) {
 assert(n==10); in_worker=false; swapcontext(&task_context,&main_context); in_worker=true;
}
void pbl_sleep_ms(unsigned n) { assert(n==10); resume(); }
void pbl_thread_abort(struct pbl_thread *t) {
 assert(!t && s_boot_splash_task==context_allocation && !s_boot_splash_fb);
 // Buffer release is not thread exit. Let stop observe this intermediate state.
 in_worker=false; swapcontext(&task_context,&main_context); in_worker=true;
 assert(!context_frees);
 s_boot_splash_task->state=PBL_THREAD_DEAD;
 thread_exited=true; in_worker=false; setcontext(&main_context); abort();
}
enum pbl_thread_state pbl_thread_state(const struct pbl_thread *t) {
 assert(!in_worker && t==context_allocation); ++exit_checks; return t->state;
}
static void trampoline(void) { in_worker=true; entry(NULL); abort(); }
int pbl_thread_create(struct pbl_thread *t,const struct pbl_thread_attr *a) {
 assert(t==s_boot_splash_task && t==context_allocation && !t->id && !t->state);
 assert(a->stack_size==2048 && a->privileged);
 assert((uintptr_t)a->stack % CONFIG_KERNEL_STACK_ALIGN==0);
 assert((uintptr_t)a->stack >= (uintptr_t)(t+1));
 assert((uintptr_t)a->stack+a->stack_size <= (uintptr_t)t+context_size);
 if (fail_thread) return -1;
 created=true; t->state=PBL_THREAD_READY;
 entry=a->entry; getcontext(&task_context);
 task_context.uc_stack.ss_sp=host_stack;
 task_context.uc_stack.ss_size=sizeof(host_stack);
 task_context.uc_link=&main_context;
 makecontext(&task_context,trampoline,0); return 0;
}
void display_update_boot_frame(uint8_t *fb) {
#ifdef TEST_T5
 assert(fb==framebuffer.buffer && s_borrowed);
#endif
 unsigned lx=(260-splash_width)/2, ly=(260-splash_height)/2;
 unsigned f=frames%20, offset=f<10 ? f*6 : 60-(f-10)*6;
 for (unsigned y=0;y<260;++y) for (unsigned x=0;x<260;++x) {
  uint8_t pixel=0xff;
  if (x>=lx && x<lx+splash_width && y>=ly && y<ly+splash_height &&
      (splash_bits[(y-ly)*(splash_width/8)+(x-lx)/8]&(1u<<((x-lx)%8))))
    pixel=SPLASH_COLOR_BLACK;
  if (!stopping && y>=ly+splash_height+18 && y<ly+splash_height+22) {
    if (x>=90 && x<170) pixel=SPLASH_COLOR_LGRAY;
    if (x>=90+offset && x<110+offset) pixel=SPLASH_COLOR_BLACK;
  }
  assert(fb[y*260+x]==pixel);
 }
 ++frames;
}
int main(int argc,char **argv) {
 assert(argc==2); int scenario=atoi(argv[1]);
#ifdef CONFIG_BOOT_SPLASH_ARGB2222
 assert(SPLASH_COLOR_BLACK==0xc0 && SPLASH_COLOR_LGRAY==0xea);
#else
 assert(SPLASH_COLOR_BLACK==0x00 && SPLASH_COLOR_LGRAY==0xb6);
#endif
#ifdef TEST_T5
 memset(framebuffer.metadata,0xa5,sizeof(framebuffer.metadata));
#else
 fail_thread=scenario==2; fail_alloc=scenario==3; fail_context=scenario==4;
#endif
 boot_splash_start();
#ifdef TEST_T5
 assert(!inits && !s_boot_splash_task);
 if (scenario==1) {
   boot_splash_stop(); t5ai_boot_splash_render();
   assert(!inits && !s_borrowed); return 0;
 }
 fail_thread=scenario==2; fail_context=scenario==4;
 t5ai_boot_splash_render();
 if (scenario==2 || scenario==4) {
   assert(!context_allocation && context_allocs==context_frees);
   assert(!s_borrowed && !s_boot_splash_fb && !s_boot_splash_running);
   boot_splash_stop(); assert(!frames); return 0;
 }
#else
 if (scenario==2 || scenario==3 || scenario==4) {
   assert(!context_allocation && context_allocs==context_frees);
   assert(!s_boot_splash_task && !s_boot_splash_fb && !s_boot_splash_running && !live);
   assert(allocs==(scenario!=4) && frees==(scenario==2)); boot_splash_stop(); return 0;
 }
#endif
 assert(inits==1 && s_boot_splash_task);
 boot_splash_start(); assert(inits==1 && context_allocs==1);
 for (unsigned i=0;i<201;++i) resume();
 assert(frames==21); stopping=true; boot_splash_stop();
 assert(frames==22 && !s_boot_splash_task && !s_boot_splash_fb && !live);
 assert(!context_allocation && context_allocs==1 && context_frees==1);
 assert(thread_exited && exit_checks>=3);
#ifdef TEST_T5
 assert(!s_borrowed && !allocs && !frees);
 for (unsigned i=0;i<sizeof(framebuffer.metadata);++i)
   assert(((uint8_t *)framebuffer.metadata)[i]==0xa5);
 assert(boot_splash_platform_alloc(67601)==NULL);
 boot_splash_start(); assert(inits==1 && !s_boot_splash_task);
#else
 assert(allocs==2 && frees==2);
#endif
}
"""


class BootSplashPlatformTest(unittest.TestCase):
    def test_real_service_lifecycle_and_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for name, text in HEADERS.items():
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    text.replace(
                        "SPLASH_PATH", str(ROOT / "fw/board/splash/splash_getafix.xbm")
                    )
                )
            source = directory / "test.c"
            source.write_text(
                HARNESS.replace(
                    "STOCK_SOURCE", str(ROOT / "fw/services/boot_splash/service.c")
                ).replace(
                    "T5_SOURCE", str(ROOT / "fw/services/boot_splash/service_t5ai.c")
                )
            )
            for t5, argb in ((False, False), (False, True), (True, True)):
                binary = directory / f"test-{t5}-{argb}"
                cmd = [
                    "cc",
                    "-std=gnu11",
                    "-O2",
                    "-Dnullptr=NULL",
                    "-DCONFIG_KERNEL_STACK_ALIGN=32",
                    "-I",
                    str(directory),
                    "-I",
                    str(ROOT / "include"),
                ]
                if t5:
                    cmd += ["-DTEST_T5", "-DCONFIG_BOOT_SPLASH_CUSTOM_PLATFORM"]
                else:
                    cmd += ["-DCONFIG_PBLBOOT"]
                if argb:
                    cmd += ["-DCONFIG_BOOT_SPLASH_ARGB2222"]
                subprocess.run(
                    cmd + [str(source), "-o", str(binary)], check=True, timeout=30
                )
                for scenario in (0, 1, 2, 4) if t5 else (0, 2, 3, 4):
                    with self.subTest(t5=t5, argb=argb, scenario=scenario):
                        subprocess.run(
                            [str(binary), str(scenario)], check=True, timeout=10
                        )

    def test_kconfig_defaults(self):
        from tools.tests.test_getafix_boards import load_config as getafix_config
        from tools.tests.test_unhygienix_boards import load_config

        for board in ("unhygienix", "obelix", "asterix", "qemu_emery"):
            config = load_config(board, "normal")
            self.assertEqual(
                config["BOOT_SPLASH_CUSTOM_PLATFORM"],
                "y" if board == "unhygienix" else "n",
            )
            self.assertEqual(
                config["BOOT_SPLASH_ARGB2222"],
                "y" if board in ("unhygienix", "qemu_emery") else "n",
            )
        for revision in ("dvt", "dvt2"):
            self.assertEqual(
                getafix_config(revision, "normal")["BOOT_SPLASH_ARGB2222"], "n"
            )

    def test_display_boot_transfer_serialization(self):
        source = (ROOT / "drivers/display/t5ai/display_co5300.c").read_text()
        function = source[source.index("void display_update_boot_frame(") :]
        harness = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PBL_FOREVER 0
#define PANEL_X_OFFSET 0
#define PANEL_WIDTH 260
#define PANEL_HEIGHT 260
#define PBL_DISPLAY_WIDTH 260
#define PBL_DISPLAY_HEIGHT 260
static bool s_initialized, locked;
static int s_panel_mutex;
static unsigned locks, unlocks, rows, ends, mode;
static uint8_t pixels[260*260];
void pbl_mutex_lock(int *m,int wait) {
 assert(m==&s_panel_mutex && wait==PBL_FOREVER && !locked); locked=true; ++locks;
}
void pbl_mutex_unlock(int *m) { assert(m==&s_panel_mutex && locked); locked=false; ++unlocks; }
static bool prv_set_window(unsigned x0,unsigned y0,unsigned x1,unsigned y1) {
 assert(locked && x0==0 && y0==0 && x1==259 && y1==259); return mode!=2;
}
static bool prv_pixel_stream_begin(void) { assert(locked); return mode!=3; }
static bool prv_stream_logical_row(unsigned y,const uint8_t *p,size_t *word,bool dma,bool invert) {
 (void)word; assert(locked && !dma && !invert && p==pixels+y*260); ++rows; return mode!=4;
}
static bool prv_finish_row_dma(size_t *word) { (void)word; assert(locked); return true; }
static void prv_pixel_stream_end(void) { assert(locked); ++ends; }
"""
            + function
            + r"""
int main(void) {
 display_update_boot_frame(NULL); assert(!locks);
 for (mode=0;mode<5;++mode) {
   s_initialized=mode!=1; locks=unlocks=rows=ends=0;
   display_update_boot_frame(pixels);
   assert(locks==1 && unlocks==1 && !locked);
   assert(rows==(mode==0 ? 260 : mode==4 ? 1 : 0));
   assert(ends==(mode==0 || mode==4));
 }
}
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "transfer.c"
            binary = Path(directory) / "transfer"
            path.write_text(harness)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_bootloader_owned_splash_remains_noop(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for name, text in HEADERS.items():
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            source = directory / "noop.c"
            source.write_text(
                "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n"
                f'#include "{ROOT}/fw/services/boot_splash/service.c"\n'
                "int main(void) { boot_splash_start(); boot_splash_stop(); }\n"
            )
            binary = directory / "noop"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-I",
                    str(directory),
                    "-I",
                    str(ROOT / "include"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=10)
