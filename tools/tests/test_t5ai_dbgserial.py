# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the CP console UART, unsafe contexts, buffering and owner locks."""

import re
import unittest
from pathlib import Path

from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]


class T5AIDbgserialTest(unittest.TestCase):
    def test_output_contexts(self):
        source = (ROOT / "drivers/uart/t5ai_console.c").read_text()
        headers = {
            name: ""
            for name in re.findall(r"#include <([^>]+)>", source)
            if name.startswith("pbl/")
        }
        run_c(
            r"""
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define nullptr NULL
struct pbl_uart;
struct pbl_uart_config {unsigned baudrate;};
struct pbl_uart_ops {
 int (*init)(const struct pbl_uart *),(*early_init)(const struct pbl_uart *);
 int (*configure)(const struct pbl_uart *,const struct pbl_uart_config *);
 void (*poll_out)(const struct pbl_uart *,uint8_t),(*flush)(const struct pbl_uart *);
 int (*rx_enable)(const struct pbl_uart *);
 void (*rx_disable)(const struct pbl_uart *);
};
struct pbl_uart {const struct pbl_uart_ops *ops;};
#define PBL_UART_STATE_DEFINE(s)
#define PBL_UART_INIT(s,n,o,b,d) {.ops=o}
#define PBL_DEVICE_REGISTER(s,d)
struct pbl_work {void (*fn)(struct pbl_work *);};
#define PBL_WORK_DEFINE(n,f) struct pbl_work n={.fn=f}
static int pbl_sys_workq;
static void pbl_work_submit(int *q,struct pbl_work *w){assert(q==&pbl_sys_workq && w->fn);}
struct pbl_log_backend {int unused;};
struct pbl_log_msg {int unused;};
struct pbl_log_backend_api {
 void (*process)(const struct pbl_log_backend *,const struct pbl_log_msg *);
 void (*panic)(const struct pbl_log_backend *);
};
#define LOG_LEVEL_ALWAYS 0
#define PBL_LOG_BACKEND_DEFINE(n,a,l,e) static const void *const be_##n __attribute__((used))=&a
static bool running=true,isr,owner,irq_locked,sched_locked;
static void pbl_irq_lock(void){assert(!irq_locked);irq_locked=true;}
static void pbl_irq_unlock(void){assert(irq_locked);irq_locked=false;}
static unsigned locks,unlocks,sends;
static char output[512];
static size_t output_length;
static bool mcu_state_is_isr(void){return isr;}
static bool t5ai_mailbox_channel_is_owner(void){return owner;}
static bool pbl_kernel_is_running(void){return running;}
static bool pbl_irq_is_locked(void){return irq_locked;}
static bool pbl_sched_is_locked(void){return sched_locked;}
static int t5ai_cp_mailbox_log(const void *buf,size_t len){
 assert(running && !isr && !owner && !irq_locked && !sched_locked);
 assert(output_length+len<=sizeof(output));memcpy(output+output_length,buf,len);
 output_length+=len;++sends;return 0;
}
void __real_pbl_console_lock(void){++locks;}
void __real_pbl_console_unlock(void){++unlocks;}
"""
            + source
            + r"""
int main(void){
 const struct pbl_uart *uart=&T5AI_CONSOLE_UART;
 assert(prv_init(uart)==0);
 assert(prv_configure(uart,&(struct pbl_uart_config){460800})==0);
 assert(prv_configure(uart,&(struct pbl_uart_config){115200})<0);
 assert(prv_rx_enable(uart)<0);
 for(unsigned ctx=0;ctx<5;++ctx){
  output_length=0;prv_poll_out(uart,'A');unsigned prior=sends;
  irq_locked=ctx==0;sched_locked=ctx==1;isr=ctx==2;owner=ctx==3;running=ctx!=4;
  prv_poll_out(uart,'x');prv_flush(uart);assert(sends==prior);
  irq_locked=sched_locked=isr=owner=false;running=true;
  prv_flush(uart);prv_send(NULL);
  assert(output_length==(ctx==3?1u:2u) && output[0]=='A');
 }
 output_length=0;
 for(unsigned i=0;i<160;++i)prv_poll_out(uart,'B');
 assert(output_length==0 && s_length==160);prv_flush(uart);prv_send(NULL);assert(output_length==160);
 for(unsigned i=0;i<160;++i)assert(output[i]=='B');
 owner=true;__wrap_pbl_console_lock();__wrap_pbl_console_unlock();assert(!locks && !unlocks);
 owner=false;__wrap_pbl_console_lock();__wrap_pbl_console_unlock();assert(locks==1 && unlocks==1);
 return 0;
}
""",
            headers,
        )
