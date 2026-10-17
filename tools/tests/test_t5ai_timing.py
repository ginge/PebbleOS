# SPDX-License-Identifier: Apache-2.0
"""Exercise T5 clock transitions, elapsed tick accounting and critical-section scope."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def run_c(source, headers):
    with tempfile.TemporaryDirectory() as temp:
        d = Path(temp)
        for name, contents in headers.items():
            p = d / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(contents)
        (d / "test.c").write_text(source)
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(d), "-I", str(ROOT / "include"),
                        str(d / "test.c"), "-o", str(d / "test")], check=True)
        subprocess.run([str(d / "test")], check=True)


def function(source, name):
    start = source.index(name + "(")
    start = source.rfind("\n", 0, start) + 1
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class T5TimingTest(unittest.TestCase):
    def test_clock_and_elapsed_ticks(self):
        headers = {
            "cmsis_core.h": """#pragma once
#include <stdint.h>
static struct { uint32_t LOAD, VAL, CTRL; } systick;
#define SysTick (&systick)
#define SysTick_CTRL_ENABLE_Msk 1u
static uint32_t SystemCoreClock;
""",
            "pbl/soc/t5ai/cp_mailbox.h": """typedef int status_t;
#define S_SUCCESS 0
#define T5AICpuFrequency_240MHz 240
int t5ai_cp_mailbox_vote_cpu1_frequency(int);
""",
            "pbl/logging/logging.h": """#define PBL_LOG_MODULE_DEFINE(...)
#define LOG_LEVEL_DEBUG 0
#define PBL_LOG_DBG(...) (++logs)
#define PBL_LOG_ERR(...) (++logs)
""",
            "system/profiler.h": "#define PROFILER_INIT\n#define PROFILER_START\n",
            "pbl/kernel/idle.h": """#define PBL_TICK_HZ 1000u
void pbl_kernel_tick_isr(void);
void pbl_idle_slept(unsigned);
""",
        }
        source = r"""
#include <assert.h>
#include <sys/mman.h>
#include <stdint.h>
#define CONFIG_KERNEL_TICK_HZ 1000u
static unsigned logs, votes, ticks, tick_calls, catchup_calls;
#include POWER_SOURCE
#include TICK_SOURCE
void pbl_kernel_tick_isr(void) { ++ticks; ++tick_calls; }
void pbl_idle_slept(unsigned n) { ticks += n; ++catchup_calls; }
int t5ai_cp_mailbox_vote_cpu1_frequency(int f) { assert(f == 240); ++votes; return 0; }
#define MODE (*(volatile uint32_t *)0x44010020u)
#define CPU1 (*(volatile uint32_t *)0x44010014u)
int main(void) {
 assert(mmap((void *)0x44000000u, 0x11000, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
 MODE=0x33; CPU1=16; SystemCoreClock=120000000;
 SysTick->LOAD=119999; SysTick->VAL=17; SysTick->CTRL=1;
 T5AI_AON_RTC_COUNT_LOW=100; t5ai_tick_start();
 T5AI_AON_RTC_COUNT_LOW=132; SysTick_Handler();
 assert(ticks==1 && tick_calls==1 && !catchup_calls && SysTick->VAL==17);
 // Actual startup transition: stale 120 MHz reload at 60 MHz.
 MODE=0x37; T5AI_AON_RTC_COUNT_LOW=196; SysTick_Handler();
 assert(SystemCoreClock==60000000 && SysTick->LOAD==59999 && SysTick->VAL==0);
 assert(ticks==3 && tick_calls==2 && catchup_calls==1);
 // No tick double count after reload resets or an already-pending interrupt.
 SysTick->VAL=9; T5AI_AON_RTC_COUNT_LOW=197; SysTick_Handler();
 assert(ticks==3 && tick_calls==2 && SysTick->VAL==9);
 // Delayed SysTick recovers 54 ms in one bulk advance, not 54 ISR iterations.
 T5AI_AON_RTC_COUNT_LOW=1924; SysTick_Handler();
 assert(ticks==57 && tick_calls==3 && catchup_calls==2);
 // Fractions carry rather than accumulate truncation drift.
 T5AI_AON_RTC_COUNT_LOW=1955; SysTick_Handler(); assert(ticks==57);
 T5AI_AON_RTC_COUNT_LOW=1956; SysTick_Handler(); assert(ticks==58);
 // Clock rise, CPU1 half divider, 320 MHz and crystal sources.
 MODE=0x31; T5AI_AON_RTC_COUNT_LOW+=32; SysTick_Handler();
 assert(SystemCoreClock==240000000 && SysTick->LOAD==239999);
 CPU1=0; t5ai_power_sync_tick_clock();
 assert(SystemCoreClock==120000000 && SysTick->LOAD==119999);
 MODE=0x21; CPU1=16; t5ai_power_sync_tick_clock();
 assert(SystemCoreClock==160000000 && SysTick->LOAD==159999);
 MODE=0; t5ai_power_sync_tick_clock();
 assert(SystemCoreClock==26000000 && SysTick->LOAD==25999);
 // Unsupported calibrated DCO retains known settings without logging in ISR.
 MODE=0x10; SysTick->VAL=7; t5ai_power_sync_tick_clock();
 assert(SystemCoreClock==26000000 && SysTick->LOAD==25999 && SysTick->VAL==7);
 // Disabled SysTick is not inadvertently started or programmed.
 MODE=0x31; SysTick->CTRL=0; t5ai_power_sync_tick_clock();
 assert(SystemCoreClock==240000000 && SysTick->LOAD==25999 && !SysTick->CTRL);
 // Low 32-bit AON wrap retains exactly one elapsed millisecond.
 T5AI_AON_RTC_COUNT_LOW=UINT32_MAX-15; t5ai_tick_start();
 ticks=tick_calls=catchup_calls=0;
 T5AI_AON_RTC_COUNT_LOW=16; SysTick_Handler(); assert(ticks==1 && tick_calls==1);
 assert(!logs && !votes);
 return 0;
}
"""
        source = source.replace("POWER_SOURCE", '"' + str(ROOT / "soc/t5ai/power.c") + '"')
        source = source.replace("TICK_SOURCE", '"' + str(ROOT / "soc/t5ai/tick.c") + '"')
        run_c(source, headers)

    def test_mailbox_sync_storage_and_send(self):
        source = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        tx = source[source.index("typedef struct {\n  T5AIMailboxMessage message"):
                    source.index("} TxChannel;") + len("} TxChannel;")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define T5AI_SHARED_ALIGNMENT 32
#define T5AI_MAILBOX_CHANNEL_COUNT 16
#define MAILBOX_TASK_STACK_WORDS 512
#define MAILBOX_TASK_PRIORITY 4
#define E_OUT_OF_MEMORY -1
#define E_INVALID_ARGUMENT -2
#define E_BUSY -3
#define E_ERROR -4
#define S_SUCCESS 0
#define FAILED(s) ((s)<0)
#define PBL_FOREVER UINT32_MAX
#define PBL_TICK_FOREVER UINT32_MAX
#define PBL_TICKS(x) (x)
#define T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED 256u
struct pbl_sem { unsigned ready, limit; };
struct pbl_mutex { unsigned locked; };
struct pbl_thread { int unused; };
typedef int status_t;
typedef struct { uint32_t header, parameter1, parameter2, parameter3; } T5AIMailboxMessage;
typedef void (*T5AIMailboxTransmitHandler)(const T5AIMailboxMessage *, status_t, void *);
typedef enum { TxState_Idle, TxState_Pending, TxState_Active, TxState_Complete } TxState;
""" + tx + r"""
static TxChannel s_tx[16];
static bool s_initialized, s_started, s_faulted;
static struct pbl_sem wake, *s_wake;
static struct pbl_thread *s_task;
static unsigned depth, allocations, creations;
void pbl_irq_lock(void) { ++depth; }
void pbl_irq_unlock(void) { assert(depth); --depth; }
void pbl_sem_init(struct pbl_sem *s, unsigned initial, unsigned limit) { assert(!depth); s->ready=initial; s->limit=limit; }
void pbl_mutex_init(struct pbl_mutex *m) { assert(!depth); m->locked=0; }
struct pbl_sem *t5ai_sem_create(unsigned limit, unsigned initial) { assert(!depth); ++allocations; pbl_sem_init(&wake,initial,limit); return &wake; }
static void prv_mailbox_task(void *arg) { (void)arg; }
int t5ai_thread_create(void (*entry)(void *), const char *name, size_t words, void *arg,
                      unsigned prio, struct pbl_thread **out, void *stack) {
 assert(!depth && entry==prv_mailbox_task && name && words==512 && !arg && prio==4 && out && !stack);
 ++creations; return 0;
}
void pbl_mutex_lock(struct pbl_mutex *m, unsigned timeout) { assert(!depth && !m->locked && timeout==PBL_FOREVER); m->locked=1; }
void pbl_mutex_unlock(struct pbl_mutex *m) { assert(!depth && m->locked); m->locked=0; }
int pbl_sem_take(struct pbl_sem *s, unsigned timeout) {
 assert(!depth);
 if (s->ready) { --s->ready; return 0; }
 assert(timeout==0); return -1;
}
int pbl_sem_give(struct pbl_sem *s) {
 assert(s==s_wake && !depth);
 for (unsigned i=0;i<16;++i) {
   if (s_tx[i].state!=TxState_Pending) continue;
   s_tx[i].ack=(T5AIMailboxMessage){.parameter1=123+i};
   s_tx[i].completion.ready=1;
   s_tx[i].state=TxState_Complete;
 }
 return 0;
}
static bool prv_tx_channel(uint8_t channel, uint8_t *index) { if(channel>=16) return false; *index=channel; return true; }
static unsigned prv_milliseconds_to_ticks(unsigned n) { return n; }
static void prv_reset_after_timeout(unsigned i) { (void)i; assert(0); }
"""
        harness += function(source, "t5ai_mailbox_channel_init")
        harness += function(source, "t5ai_mailbox_channel_send")
        harness += r"""
int main(void) {
 assert(t5ai_mailbox_channel_init()==0 && allocations==1 && creations==1);
 assert(t5ai_mailbox_channel_init()==0 && allocations==1 && creations==1);
 s_started=true;
 for (unsigned i=0;i<16;++i) {
   assert(s_tx[i].completion.limit==1);
   T5AIMailboxMessage message={.header=1}, ack;
   s_tx[i].completion.ready=1; // A stale completion is drained before publishing.
   assert(t5ai_mailbox_channel_send(i,&message,&ack,100)==0);
   assert(ack.parameter1==123+i && s_tx[i].state==TxState_Idle && !s_tx[i].mutex.locked);
 }
 assert(allocations==1 && !depth);
 T5AIMailboxMessage message={0}; s_tx[0].state=TxState_Active;
 assert(t5ai_mailbox_channel_send(0,&message,NULL,100)==E_BUSY && !s_tx[0].mutex.locked);
 assert(t5ai_mailbox_channel_send(16,&message,NULL,100)==E_INVALID_ARGUMENT);
 return 0;
}
"""
        run_c(harness, {})

    def test_voice_heap_uses_task_mutex(self):
        headers = {
            "pbl/util/heap.h": """#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct { int ready; } Heap;
void heap_init(Heap *, void *, void *, bool);
void *heap_malloc(Heap *, size_t, unsigned);
void *heap_zalloc(Heap *, size_t, unsigned);
void *heap_realloc(Heap *, void *, size_t, unsigned);
void heap_free(Heap *, void *, unsigned);
bool heap_contains_address(Heap *, void *);
""",
            "pbl/kernel/mutex.h": """#pragma once
struct pbl_mutex { unsigned locked; };
#define PBL_MUTEX_DEFINE(name) struct pbl_mutex name
#define PBL_FOREVER 0
void pbl_mutex_lock(struct pbl_mutex *, unsigned);
void pbl_mutex_unlock(struct pbl_mutex *);
""",
            "system/passert.h": "#define PBL_ASSERTN(x) assert(x)\n",
        }
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include VOICE_SOURCE
static unsigned inits, locks;
void pbl_mutex_lock(struct pbl_mutex *m, unsigned t) { (void)t; assert(!m->locked); m->locked=1; ++locks; }
void pbl_mutex_unlock(struct pbl_mutex *m) { assert(m->locked); m->locked=0; }
void heap_init(Heap *h, void *start, void *end, bool fuzz) { assert(s_mutex.locked && !h->ready && start && end && !fuzz); h->ready=1; ++inits; }
void *heap_malloc(Heap *h, size_t n, unsigned pc) { (void)pc; assert(s_mutex.locked && h->ready); return malloc(n); }
void *heap_zalloc(Heap *h, size_t n, unsigned pc) { (void)pc; assert(s_mutex.locked && h->ready); return calloc(n,1); }
void *heap_realloc(Heap *h, void *p, size_t n, unsigned pc) { (void)pc; assert(s_mutex.locked && h->ready); return realloc(p,n); }
void heap_free(Heap *h, void *p, unsigned pc) { (void)pc; assert(s_mutex.locked && h->ready); free(p); }
bool heap_contains_address(Heap *h, void *p) { assert(s_mutex.locked && h->ready); return p!=NULL; }
int main(void) {
 void *p=t5ai_voice_malloc_check(32);
 p=t5ai_voice_realloc(p,64);
 t5ai_voice_free(p);
 p=t5ai_voice_zalloc_check(16); assert(*(unsigned char *)p==0); t5ai_voice_free(p);
 t5ai_voice_free(NULL);
 assert(inits==1 && locks==5 && !s_mutex.locked);
 return 0;
}
""".replace("VOICE_SOURCE", '"' + str(ROOT / "soc/t5ai/voice_memory.c") + '"')
        run_c(harness, headers)
