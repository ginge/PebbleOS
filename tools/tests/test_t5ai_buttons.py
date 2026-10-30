# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the board's gpio-keys wiring and upstream reset-key listener."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    "pbl/kernel/timer.h": """#pragma once
#include <stdbool.h>
struct pbl_timer {void (*cb)(struct pbl_timer *);unsigned ms;bool active;};
#define PBL_TIMER_INITIALIZER(fn) {.cb=(fn)}
#define PBL_MSEC(ms) (ms)
void pbl_timer_init(struct pbl_timer *,void (*)(struct pbl_timer *));
void pbl_timer_start(struct pbl_timer *,unsigned);
void pbl_timer_stop(struct pbl_timer *);
bool pbl_timer_is_active(struct pbl_timer *);
""",
    "pbl/kernel/irq.h": "void pbl_irq_lock(void);\n",
    "system/bootbits.h": "#define BOOT_BIT_FORCE_PRF 1\nvoid boot_bit_set(int);\n",
    "system/reboot_reason.h": """#define RebootReasonCode_PrfResetButtonsHeld 10
#define RebootReasonCode_ResetButtonsHeld 11
typedef struct {int code;} RebootReason;
void reboot_reason_set(RebootReason *);
""",
    "system/reset.h": "void system_hard_reset(void);\n",
    "pbl/util/misc.h": "#include <stddef.h>\n#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))\n",
}
HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <pbl/drivers/input/gpio_keys.h>
#include <pbl/input/input.h>
#include "drivers/input/gpio_keys.c"
#include "fw/system/reset_keys.c"
static unsigned events,irqs,irq_depth,resets,bootbit,resetcode;
static bool pressed[64];
static jmp_buf reset_target;
const struct pbl_gpio_port T5AI_GPIO={0};
const struct pbl_intc T5AI_GPIO_INTC={0};
BOARD_KEYS
int pbl_device_init(const struct pbl_device *dev){(void)dev;return 0;}
void pbl_irq_lock(void){++irq_depth;}
void pbl_timer_init(struct pbl_timer *t,void (*cb)(struct pbl_timer *)){*t=(struct pbl_timer){.cb=cb};}
void pbl_timer_start(struct pbl_timer *t,unsigned ms){t->active=true;t->ms=ms;}
void pbl_timer_stop(struct pbl_timer *t){t->active=false;}
bool pbl_timer_is_active(struct pbl_timer *t){return t->active;}
int pbl_gpio_configure(const struct pbl_gpio *g,uint32_t mode){assert(mode==PBL_GPIO_INPUT);assert(g->flags&PBL_GPIO_ACTIVE_LOW);return 0;}
int pbl_gpio_get(const struct pbl_gpio *g){return pressed[g->pin];}
int pbl_gpio_to_irq(const struct pbl_gpio *g,uint32_t type,struct pbl_irq_spec *irq){*irq=(struct pbl_irq_spec)PBL_IRQ_SPEC(&T5AI_GPIO_INTC,g->pin,type);return 0;}
int pbl_intc_request(const struct pbl_irq_spec *irq,void (*cb)(void *),void *arg){assert(irq->type==PBL_IRQ_TYPE_EDGE_BOTH && cb==prv_edge && arg);++irqs;return 0;}
int pbl_intc_enable(const struct pbl_irq_spec *irq){(void)irq;return 0;}
void pbl_input_report(enum pbl_input_type type,uint16_t code,int32_t value,bool sync){
 assert(type==PBL_INPUT_EV_KEY && sync);++events;
 struct pbl_input_event evt={.type=type,.code=code,.value=value,.sync=sync};
 prv_input_cb(&evt,nullptr);
}
void boot_bit_set(int bit){bootbit=bit;}
void reboot_reason_set(RebootReason *reason){resetcode=reason->code;}
void system_hard_reset(void){++resets;longjmp(reset_target,1);}
static void settle(unsigned i){struct pbl_timer *t=&s_buttons.state[i].timer;assert(t->active && t->ms==40);t->active=false;t->cb(t);}
int main(void){
 assert(s_buttons.num_keys==2 && s_buttons.keys[0].gpio.pin==18 && s_buttons.keys[1].gpio.pin==12);
 assert(s_buttons.keys[0].code==PBL_INPUT_KEY_BACK && s_buttons.keys[1].code==PBL_INPUT_KEY_SELECT);
 pressed[18]=true;
 assert(pbl_gpio_keys_init(&s_buttons.dev)==0 && irqs==2);
 settle(0);settle(1);assert(events==1 && !s_timer.active);
 // Bouncing edges restart the 40 ms timer without producing events.
 pressed[12]=true;for(unsigned i=0;i<20;++i)prv_edge(&s_buttons.state[1]);
 assert(events==1);settle(1);assert(events==2 && s_timer.active && s_timer.ms==5000);
 // Release cancels the reset hold; unchanged samples cannot duplicate events.
 pressed[18]=false;prv_edge(&s_buttons.state[0]);settle(0);assert(!s_timer.active && events==3);
 prv_edge(&s_buttons.state[0]);settle(0);assert(events==3);
 pressed[18]=true;prv_edge(&s_buttons.state[0]);settle(0);assert(s_timer.active);
 if(!setjmp(reset_target))s_timer.cb(&s_timer);
 assert(resets==1 && resetcode==RebootReasonCode_ResetButtonsHeld && !bootbit && irq_depth==1);
 return 0;
}
"""


class ButtonsTest(unittest.TestCase):
    def test_debounce_and_reset_hold(self):
        board = (ROOT / "fw/board/boards/board_unhygienix.c").read_text()
        start = board.index("PBL_GPIO_KEYS_DEFINE(")
        keys = board[start : board.index(";", start) + 1]
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            for name, text in HEADERS.items():
                header = path / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text(text)
            (path / "test.c").write_text(HARNESS.replace("BOARD_KEYS", keys))
            subprocess.run(
                [
                    "clang-21",
                    "-std=gnu23",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-I",
                    str(path),
                    "-I",
                    str(ROOT),
                    "-I",
                    str(ROOT / "include"),
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)
