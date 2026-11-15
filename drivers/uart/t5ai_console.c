/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>

#include <pbl/drivers/uart.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/mcu/interrupts.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/mailbox_channel.h>

static uint8_t s_buffer[159];
static size_t s_length;
static bool prv_can_write(void) {
  return !mcu_state_is_isr() && !t5ai_mailbox_channel_is_owner() && pbl_kernel_is_running() &&
         !pbl_irq_is_locked() && !pbl_sched_is_locked();
}
static void prv_flush(const struct pbl_uart *uart) {
  if (!prv_can_write())
    return;
  if (s_length != 0) {
    (void)t5ai_cp_mailbox_log(s_buffer, s_length);
    s_length = 0;
  }
}
static void prv_poll_out(const struct pbl_uart *uart, uint8_t c) {
  if (!prv_can_write())
    return;
  if (s_length == sizeof(s_buffer))
    prv_flush(uart);
  s_buffer[s_length++] = c;
}
static int prv_init(const struct pbl_uart *uart) {
  return 0;
}
static int prv_configure(const struct pbl_uart *uart, const struct pbl_uart_config *cfg) {
  return cfg->baudrate == 460800 ? 0 : -ENOTSUP;
}
static int prv_rx_enable(const struct pbl_uart *uart) {
  return -ENOTSUP;
}
static void prv_rx_disable(const struct pbl_uart *uart) {
}
static const struct pbl_uart_ops s_ops = {
  .init = prv_init,
  .early_init = prv_init,
  .configure = prv_configure,
  .poll_out = prv_poll_out,
  .flush = prv_flush,
  .rx_enable = prv_rx_enable,
  .rx_disable = prv_rx_disable,
};
PBL_UART_STATE_DEFINE(T5AI_CONSOLE_UART);
const struct pbl_uart T5AI_CONSOLE_UART =
    PBL_UART_INIT(T5AI_CONSOLE_UART, "t5ai-cp-log", &s_ops, 460800, nullptr);
PBL_DEVICE_REGISTER(T5AI_CONSOLE_UART, &T5AI_CONSOLE_UART.dev);
void __real_pbl_console_lock(void);
void __real_pbl_console_unlock(void);
void __wrap_pbl_console_lock(void) {
  if (!t5ai_mailbox_channel_is_owner())
    __real_pbl_console_lock();
}
void __wrap_pbl_console_unlock(void) {
  if (!t5ai_mailbox_channel_is_owner())
    __real_pbl_console_unlock();
}
