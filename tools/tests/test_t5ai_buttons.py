# SPDX-License-Identifier: Apache-2.0
"""Exercise T5 button debouncing with KernelBG and NewTimers absent."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STUB = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#define NUM_BUTTONS 4
typedef enum { BUTTON_ID_BACK, BUTTON_ID_UP, BUTTON_ID_SELECT, BUTTON_ID_DOWN } ButtonId;
typedef struct { uint8_t pin; int pull; } ButtonConfig;
static struct { ButtonConfig buttons[4]; } BOARD_CONFIG_BUTTON = {
 .buttons = {{1,0}, {2,0}, {3,0}, {UINT8_MAX,0}}
};
typedef struct { uint8_t pin; int pull; } ExtiConfig;
#define ExtiTrigger_RisingFalling 3
#define PBL_FOREVER -1
#define PBL_PRIO_MAX 4
#define PBL_MSEC(x) (x)
#define PBL_ASSERTN(x) assert(x)
#define PBL_SEM_DEFINE(n,i,l) struct pbl_sem n = {i}
#define PBL_THREAD_STACK_DEFINE(n,s) static char n[s]
struct pbl_sem { unsigned count; };
struct pbl_thread { int unused; };
struct pbl_thread_attr {
 const char *name; void (*entry)(void *); int prio; bool privileged;
 void *stack; size_t stack_size;
};
#define PEBBLE_BUTTON_DOWN_EVENT 1
#define PEBBLE_BUTTON_UP_EVENT 2
typedef struct { int type; struct { ButtonId button_id; } button; } PebbleEvent;
#define BOOT_BIT_FORCE_PRF 1
#define RebootReasonCode_PrfResetButtonsHeld 10
#define RebootReasonCode_ResetButtonsHeld 11
typedef struct { int code; } RebootReason;
void button_init(void);
bool button_is_pressed(ButtonId);
uint32_t button_get_state_bits(void);
void exti_configure_pin(ExtiConfig,int,void (*)(void));
void exti_enable(ExtiConfig);
void event_put(PebbleEvent *);
void boot_bit_set(int);
void reboot_reason_set(RebootReason *);
void system_hard_reset(void);
void pbl_sem_give(struct pbl_sem *);
int pbl_sem_take(struct pbl_sem *,int);
int pbl_thread_create(struct pbl_thread *, const struct pbl_thread_attr *);
void pbl_thread_sleep(unsigned);
static inline bool pbl_bitset32_get(uint32_t *p, unsigned i) { return (*p >> i) & 1; }
static inline void pbl_bitset32_update(uint32_t *p,unsigned i,bool b) {
 *p = (*p & ~(1u << i)) | ((uint32_t)b << i);
}
"""
HARNESS = r"""
#include "stub.h"
#include <setjmp.h>
#include <string.h>
static jmp_buf done;
static unsigned now, bits, events, configured, bootbit, resetcode;
static bool inject_edge;
static PebbleEvent output[16];
#include "fw/drivers/t5ai/debounced_button.c"
void button_init(void) {}
bool button_is_pressed(ButtonId id) { return (bits >> id) & 1; }
uint32_t button_get_state_bits(void) { return bits; }
void exti_configure_pin(ExtiConfig e,int t,void (*cb)(void)) {
 assert(e.pin != UINT8_MAX && t == 3 && cb == prv_button_interrupt_handler); ++configured;
}
void exti_enable(ExtiConfig e) { (void)e; }
void event_put(PebbleEvent *e) { assert(events < 16); output[events++] = *e; }
void boot_bit_set(int b) { bootbit = b; }
void reboot_reason_set(RebootReason *r) { resetcode = r->code; }
void system_hard_reset(void) { longjmp(done, 2); }
void pbl_sem_give(struct pbl_sem *s) { s->count = 1; }
int pbl_sem_take(struct pbl_sem *s,int t) {
 assert(t == PBL_FOREVER);
 if (!s->count && inject_edge && bits) {
  now = 60; bits = 0; prv_button_interrupt_handler();
 }
 if (!s->count) longjmp(done, 1);
 --s->count; return 0;
}
int pbl_thread_create(struct pbl_thread *t,const struct pbl_thread_attr *a) {
 (void)t; assert(a->prio == 4 && a->privileged && a->entry == prv_button_entry); return 0;
}
void pbl_thread_sleep(unsigned ms) {
 assert(ms == 2); now += ms;
 // Release while sampling; retain the coalesced wake even at the end of a run.
 if (inject_edge && now == 60) { bits = 0; prv_button_interrupt_handler(); }
}
static void clear(void) {
 now = bits = events = bootbit = resetcode = 0;
 s_debounced_state = s_hard_reset_samples = s_button_signal.count = 0;
 memset(s_sample_counts,0,sizeof(s_sample_counts)); inject_edge = false;
}
int main(void) {
 clear(); bits = 1u << BUTTON_ID_SELECT;
 debounced_button_init(); assert(configured == 3 && s_button_signal.count == 1);
 // A complete press/release works with no background or timer service execution.
 inject_edge = true;
 for (unsigned i=0;i<100;i++) prv_button_interrupt_handler();
 assert(s_button_signal.count == 1);
 if (!setjmp(done)) prv_button_entry(NULL);
 assert(now >= 100 && events == 2 && !s_button_signal.count);
 assert(output[0].type == 1 && output[1].type == 2);
 assert(output[0].button.button_id == BUTTON_ID_SELECT);
 // Bounce and absent buttons cannot generate accepted edges.
 clear();
 for (unsigned i=0;i<40;i++) { bits = (i & 1) ? 4 : 0; prv_sample_buttons(); }
 bits = 1u << BUTTON_ID_DOWN;
 for (unsigned i=0;i<30;i++) prv_sample_buttons();
 assert(!events);
 // Both reset variants retain their hold thresholds and reason codes.
 for (unsigned up=0;up<2;up++) {
  clear(); bits = RESET_BUTTONS | (up ? 1u << BUTTON_ID_UP : 0);
  assert(setjmp(done) == 0 || resetcode == (up ? 10 : 11));
  if (!resetcode) {
   for (unsigned i=0;i<NUM_DEBOUNCE_SAMPLES+RESET_THRESHOLD_SAMPLES-1;i++) {
    prv_sample_buttons(); assert(!resetcode);
   }
   prv_sample_buttons();
   assert(0);
  }
  assert(bootbit == up);
 }
 clear(); command_put_raw_button_event("2","1");
 command_put_raw_button_event("9","1"); command_put_raw_button_event("2","7");
 assert(events == 1 && output[0].button.button_id == BUTTON_ID_SELECT);
 return 0;
}
"""

class ButtonsTest(unittest.TestCase):
    def test_debounce_without_background_queue(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / "stub.h").write_text(STUB)
            source = ROOT / "fw/drivers/t5ai/debounced_button.c"
            import re
            for name in re.findall(r'#include [<"]([^>"]+)[>"]', source.read_text()):
                if name in ("stdint.h", "stdlib.h"):
                    continue
                header = directory / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "stub.h"\n')
            harness = directory / "test.c"
            harness.write_text(HARNESS)
            binary = directory / "test"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(directory), "-I", str(ROOT), str(harness),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
