# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Run the real UART class and T5AI driver against modeled registers."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.test_t5ai_i2c import ROOT, write_headers

HARNESS = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include "drivers/uart/t5ai.c"
#include "drivers/uart/uart.c"
static const struct pbl_pinctrl_state s_pins={0};
#if defined(CONFIG_T5AI_SD_PFS) && defined(CONFIG_T5AI_SD_4BIT)
#define TEST_PINS nullptr
#else
#define TEST_PINS (&s_pins)
#endif
PBL_UART_T5AI_DEFINE(port, 0x44820000u, 4, 5, TEST_PINS, 26000000u, 460800u);
static unsigned pin_calls;
int pbl_pinctrl_select_state(const struct pbl_pinctrl *pins,enum pbl_pinctrl_state_id id){assert(pins->states[id]==&s_pins);++pin_calls;return 0;}
static unsigned irq_depth, received;
static bool irq_enabled, stop_in_callback;
void pbl_irq_lock(void) { ++irq_depth; }
void pbl_irq_unlock(void) { assert(irq_depth); --irq_depth; }
void NVIC_DisableIRQ(int irq) { assert(irq==4); irq_enabled=false; }
void NVIC_ClearPendingIRQ(int irq) { assert(irq==4); }
void NVIC_SetPriority(int irq,unsigned priority) { assert(irq==4 && priority==5); }
void NVIC_EnableIRQ(int irq) { assert(irq==4); irq_enabled=true; }
bool pbl_device_is_ready(const struct pbl_device *dev) {
 return dev->state->status==PBL_DEVICE_READY;
}
static void receive(const struct pbl_uart *uart,uint8_t c,void *arg) {
 assert(uart==&port.uart && c==0x81 && arg==&received);
 ++received;
 if(stop_in_callback) assert(pbl_uart_rx_disable(uart)==0);
 else prv_regs(&port)->fifo_status |= UART_FIFO_RX_EMPTY;
}
int main(void) {
 assert(mmap((void *)0x44000000,0x20000,PROT_READ|PROT_WRITE,
  MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 assert(mmap((void *)port.base,4096,PROT_READ|PROT_WRITE,
  MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 const struct pbl_uart *uart=&port.uart;
 T5AIUartRegisters *regs=prv_regs(&port);
 struct pbl_uart_config cfg={.baudrate=460800};
 assert(pbl_uart_configure(uart,&cfg)==-ENODEV);
 pbl_uart_poll_out(uart,1);assert(!regs->fifo_port);
 // Early console is usable before device initialization and does not enable RX.
 assert(pbl_uart_early_init(uart,&cfg)==0 && uart->state->early && !irq_enabled);
 assert(regs->global_ctrl==(UART_GLOBAL_RESET_RELEASE|UART_GLOBAL_CLOCK_GATE_BYPASS));
 assert((regs->config&UART_BAUD_DIVISOR_MASK)==(55u<<UART_BAUD_DIVISOR_SHIFT));
 assert((regs->config&UART_CONFIG_RX_ENABLE)==0 && regs->int_enable==0);
#if defined(CONFIG_T5AI_SD_PFS) && defined(CONFIG_T5AI_SD_4BIT)
 assert(!pin_calls);
#else
 assert(pin_calls==1);
#endif
 regs->fifo_status=UART_FIFO_WRITE_READY|UART_FIFO_TX_EMPTY|UART_FIFO_RX_EMPTY;
 pbl_uart_poll_out(uart,0x7e);assert(regs->fifo_port==0x7e);pbl_uart_flush(uart);
 // Normal init inherits the early baud rate instead of replacing it.
 cfg.baudrate=115200;assert(pbl_uart_early_init(uart,&cfg)==0);
 assert(pbl_uart_init(&uart->dev)==0);uart->dev.state->status=PBL_DEVICE_READY;
 assert(pbl_uart_config_get(uart,&cfg)==0 && cfg.baudrate==115200);
 assert((regs->config&UART_BAUD_DIVISOR_MASK)==(225u<<UART_BAUD_DIVISOR_SHIFT));
 uint32_t before=regs->config;
 cfg.parity=PBL_UART_PARITY_ODD;assert(pbl_uart_configure(uart,&cfg)==-ENOTSUP);
 cfg.parity=PBL_UART_PARITY_NONE;cfg.flow_ctrl=PBL_UART_FLOW_CTRL_RTS_CTS;
 assert(pbl_uart_configure(uart,&cfg)==-ENOTSUP);
 cfg.flow_ctrl=PBL_UART_FLOW_CTRL_NONE;cfg.baudrate=0;
 assert(pbl_uart_configure(uart,&cfg)==-EINVAL);
 cfg.baudrate=1;assert(pbl_uart_configure(uart,&cfg)==-ENOTSUP);
 cfg.baudrate=UINT32_MAX;assert(pbl_uart_configure(uart,&cfg)==-ENOTSUP);
 assert(regs->config==before);
 assert(pbl_uart_rx_enable(uart,receive,&received)==0 && irq_enabled);
 assert(regs->config&UART_CONFIG_RX_ENABLE);
 assert(T5AI_SYSTEM_CPU0_INTERRUPT_ENABLE&T5AI_UART0_INTERRUPT_ENABLE);
 assert(T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE&T5AI_UART0_INTERRUPT_ENABLE);
 // RX callback can disable reception without the IRQ consuming more FIFO bytes.
 stop_in_callback=true;regs->fifo_port=0x81u<<UART_FIFO_RX_DATA_SHIFT;
 regs->fifo_status=0;regs->int_status=UART_INT_RX_READY;
 pbl_uart_t5ai_irq_handler(&port);
 assert(received==1 && !irq_enabled && !uart->state->rx_enabled && !regs->int_enable);
 pbl_uart_t5ai_irq_handler(&port);assert(received==1);
 stop_in_callback=false;assert(pbl_uart_rx_enable(uart,receive,&received)==0);
 pbl_uart_t5ai_irq_handler(&port);assert(received==2 && irq_enabled);
 cfg.baudrate=460800;assert(pbl_uart_configure(uart,&cfg)==0);
 assert(regs->config&UART_CONFIG_RX_ENABLE);
 assert(pbl_uart_rx_disable(uart)==0 && !irq_enabled && !irq_depth);
 // A fresh device without early initialization uses the board baud rate.
 *uart->state=(struct pbl_uart_state){0};
 assert(pbl_uart_init(&uart->dev)==0 && uart->state->cfg.baudrate==460800);
 return 0;
}
"""


class T5AIUartTest(unittest.TestCase):
    def test_device_model_early_console_and_receive_lifecycle(self):
        for sd in (False, True):
            with self.subTest(sd_4bit=sd), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)
                write_headers(path)
                # UART needs only the NVIC declarations from the hardware stub.
                (path / "pbl/soc/t5ai/core.h").write_text(
                    "void NVIC_DisableIRQ(int);\nvoid NVIC_ClearPendingIRQ(int);\n"
                    "void NVIC_SetPriority(int,unsigned);\nvoid NVIC_EnableIRQ(int);\n"
                )
                (path / "test.c").write_text(HARNESS)
                args = [
                    "clang-21",
                    "-std=gnu23",
                    "-D_GNU_SOURCE",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",
                    "-fno-pie",
                    "-no-pie",
                    "-I",
                    str(path),
                    "-I",
                    str(ROOT),
                    "-I",
                    str(ROOT / "include"),
                ]
                if sd:
                    args += ["-DCONFIG_T5AI_SD_PFS", "-DCONFIG_T5AI_SD_4BIT"]
                subprocess.run(
                    args + [str(path / "test.c"), "-o", str(path / "test")], check=True
                )
                subprocess.run([str(path / "test")], check=True, timeout=10)
