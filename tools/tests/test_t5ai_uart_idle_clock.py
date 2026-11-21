# SPDX-License-Identifier: Apache-2.0
"""Execute real UART TX/RX lifetimes, late IRQ and uncertain-drain fallback."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests import test_t5ai_uart as uart_model

MAIN = r"""
static unsigned waited, finish_at, empty_at;
static uint32_t aon_advance;
void delay_us(uint32_t us) {
 if(us==1){
  ++waited;T5AI_AON_RTC_COUNT_LOW+=aon_advance;
  if(waited==empty_at)prv_regs(&port)->fifo_status|=UART_FIFO_TX_EMPTY;
  if(waited==finish_at)prv_regs(&port)->int_status|=UART_INT_TX_FINISH;
 }
}
int main(void) {
 assert(mmap((void *)0x44000000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 assert(mmap((void *)port.base,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 const struct pbl_uart *uart=&port.uart;T5AIUartRegisters *regs=prv_regs(&port);
 struct pbl_uart_config cfg={.baudrate=460800};
 assert(pbl_uart_early_init(uart,&cfg)==0 && !port.power->active);
 assert(!(regs->global_ctrl&UART_GLOBAL_CLOCK_GATE_BYPASS));
 regs->fifo_status=UART_FIFO_WRITE_READY|UART_FIFO_TX_EMPTY|UART_FIFO_RX_EMPTY;
 pbl_uart_poll_out(uart,42);assert(port.power->active && port.power->tx_pending);
 regs->int_status=0;waited=0;finish_at=7;pbl_uart_flush(uart);
 assert(waited==7 && !port.power->active && !port.power->tx_pending && !port.power->drain_fault);
 // FIFO draining can exceed the last-bit timeout; flush must not return early.
 regs->fifo_status=UART_FIFO_WRITE_READY;
 pbl_uart_poll_out(uart,42);regs->int_status=0;
 waited=0;empty_at=100005;finish_at=100009;pbl_uart_flush(uart);
 assert(waited==100009 && !port.power->active && !port.power->drain_fault);
 empty_at=0;regs->fifo_status=UART_FIFO_WRITE_READY|UART_FIFO_TX_EMPTY|UART_FIFO_RX_EMPTY;
 // Nothing reads gated peripheral MMIO, including repeated flush and invalid configuration.
 assert(mprotect((void *)port.base,4096,PROT_NONE)==0);
 pbl_uart_t5ai_irq_handler(&port);pbl_uart_flush(uart);cfg.baudrate=0;
 assert(pbl_uart_configure(uart,&cfg)==-ENODEV);
 assert(mprotect((void *)port.base,4096,PROT_READ|PROT_WRITE)==0);
 assert(pbl_uart_init(&uart->dev)==0);uart->dev.state->status=PBL_DEVICE_READY;
 assert(pbl_uart_rx_enable(uart,receive,&received)==0 && irq_enabled && port.power->active);
 pbl_uart_flush(uart);assert(port.power->active); // RX wake is retained.
 pbl_uart_poll_out(uart,43);regs->int_status=0;waited=0;finish_at=3;pbl_uart_flush(uart);
 assert(port.power->active && !port.power->tx_pending && waited==3);
 regs->fifo_port=0x81u<<UART_FIFO_RX_DATA_SHIFT;regs->fifo_status=UART_FIFO_TX_EMPTY;
 regs->int_status=UART_INT_TX_FINISH|UART_INT_RX_READY;
 pbl_uart_t5ai_irq_handler(&port);assert(received==1 && regs->int_status==UART_INT_RX_READY);
 assert(pbl_uart_rx_disable(uart)==0 && !port.power->active && !irq_enabled);
 regs->fifo_status|=UART_FIFO_WRITE_READY;
 pbl_uart_poll_out(uart,44);regs->int_status=0;waited=0;finish_at=0;pbl_uart_flush(uart);
 assert(waited==100000 && port.power->active && port.power->drain_fault && port.power->tx_pending);
 regs->int_status=UART_INT_TX_FINISH;pbl_uart_flush(uart);
 assert(port.power->active && port.power->drain_fault && !port.power->tx_pending);
 // Explicit reset/reconfiguration is the only way to clear uncertain-drain quarantine.
 assert(prv_early_init(uart)==0 && !port.power->drain_fault && port.power->active);
 cfg.baudrate=460800;assert(pbl_uart_configure(uart,&cfg)==0 && !port.power->active);
 // Failed FIFO readiness/empty waits never gate the clock or release ownership.
 finish_at=empty_at=0;regs->fifo_status=0;regs->fifo_port=0x55;waited=0;
 pbl_uart_poll_out(uart,0xaa);
 assert(waited==UART_FIFO_TIMEOUT_US && regs->fifo_port==0x55);
 assert(port.power->active && port.power->drain_fault);
 waited=0;pbl_uart_flush(uart);assert(waited==UART_FIFO_TIMEOUT_US);
 assert(port.power->active && port.power->drain_fault);
#ifdef CONFIG_T5AI_RETAINED_SLEEP
 assert(sleep_owners==1 && sleep_faults);
#endif
 aon_advance=T5AI_AON_RTC_HZ;T5AI_AON_RTC_COUNT_LOW=UINT32_MAX-16u;waited=0;
 pbl_uart_flush(uart);assert(waited==1 && port.power->active && port.power->drain_fault);
 aon_advance=0;regs->fifo_status=UART_FIFO_TX_EMPTY;regs->int_status=UART_INT_TX_FINISH;
 pbl_uart_flush(uart);assert(port.power->active && port.power->drain_fault);
 assert(!irq_depth);return 0;
}
"""


class UARTIdleClockTest(unittest.TestCase):
    def test_drain_rx_and_late_irq(self):
        self.check_drain(False)
        self.check_drain(True)

    def check_drain(self, retained):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            uart_model.write_headers(path)
            (path / "pbl/soc/t5ai/core.h").write_text(
                "#define __DSB() ((void)0)\nvoid NVIC_DisableIRQ(int);\n"
                "void NVIC_ClearPendingIRQ(int);\nvoid NVIC_SetPriority(int,unsigned);\n"
                "void NVIC_EnableIRQ(int);\n"
            )
            source = (
                uart_model.HARNESS[
                    : uart_model.HARNESS.index("int main(void)")
                ].replace(
                    "static unsigned wait_steps;\nstatic uint32_t aon_advance;\nvoid delay_us(uint32_t us) { assert(us == 1u); ++wait_steps; T5AI_AON_RTC_COUNT_LOW += aon_advance; }",
                    "",
                )
                + MAIN
            )
            if retained:
                sleep_header = uart_model.ROOT / "include/pbl/soc/t5ai/sleep.h"
                spies = (
                    f'#include "{sleep_header}"\n'
                    + r"""
static unsigned sleep_owners,sleep_faults;
void t5ai_sleep_block(T5AISleepOwner owner){assert(owner==T5AISleepOwner_UART);++sleep_owners;}
void t5ai_sleep_unblock(T5AISleepOwner owner){assert(owner==T5AISleepOwner_UART && sleep_owners);--sleep_owners;}
void t5ai_sleep_fault(void){++sleep_faults;}
"""
                )
                source = source.replace(
                    '#include "drivers/uart/t5ai.c"',
                    spies + '\n#include "drivers/uart/t5ai.c"',
                )
                source = source.replace(
                    "assert(waited==7 &&", "assert(sleep_owners==0);assert(waited==7 &&"
                )
                source = source.replace(
                    "pbl_uart_flush(uart);assert(port.power->active);",
                    "assert(sleep_owners==1);pbl_uart_flush(uart);assert(port.power->active);",
                )
                source = source.replace(
                    "assert(!irq_depth);return 0;",
                    "assert(!sleep_owners && sleep_faults);assert(!irq_depth);return 0;",
                )
            (path / "test.c").write_text(source)
            subprocess.run(
                [
                    "clang-21",
                    "-std=gnu23",
                    "-D_GNU_SOURCE",
                    "-DCONFIG_T5AI_UART_IDLE_GATING",
                    *(["-DCONFIG_T5AI_RETAINED_SLEEP"] if retained else []),
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
                    str(uart_model.ROOT),
                    "-I",
                    str(uart_model.ROOT / "include"),
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True, timeout=10)
