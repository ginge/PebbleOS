/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>

#include <pbl/drivers/uart.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/work.h>
#include <pbl/logging/backend.h>
#include <pbl/logging/logging.h>
#include <pbl/mcu/interrupts.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/mailbox_channel.h>

// UART callbacks run with IRQs locked. Send CP IPC from work context instead.
#define CONSOLE_WORK_BYTE_BUDGET (4u * 159u)

static uint8_t s_buffer[1024];
static size_t s_head, s_length;
// Debugger-visible counters; emitting diagnostics here would recurse into logging.
static volatile uint32_t s_overflow_drops, s_send_failures;
static bool s_draining;

static bool prv_drain(size_t budget) {
  uint8_t chunk[159];
  pbl_irq_lock();
  if (s_draining || t5ai_mailbox_channel_is_owner()) {
    const bool pending = s_length != 0u;
    pbl_irq_unlock();
    return pending;
  }
  s_draining = true;
  pbl_irq_unlock();
  while (budget != 0u) {
    pbl_irq_lock();
    size_t count = s_length < sizeof(chunk) ? s_length : sizeof(chunk);
    if (count > budget) {
      count = budget;
    }
    for (size_t i = 0; i < count; ++i) {
      chunk[i] = s_buffer[s_head];
      s_head = (s_head + 1u) % sizeof(s_buffer);
    }
    s_length -= count;
    pbl_irq_unlock();
    if (count == 0u) {
      break;
    }
    // A failed send may already have been published: never replay its bytes.
    if (t5ai_cp_mailbox_log(chunk, count) != 0) {
      pbl_irq_lock();
      ++s_send_failures;
      pbl_irq_unlock();
    }
    budget -= count;
  }
  pbl_irq_lock();
  s_draining = false;
  const bool pending = s_length != 0u;
  pbl_irq_unlock();
  return pending;
}

static void prv_send(struct pbl_work *work) {
  if (prv_drain(CONSOLE_WORK_BYTE_BUDGET)) {
    pbl_work_submit(&pbl_sys_workq, work);
  }
}
static PBL_WORK_DEFINE(s_send, prv_send);
static void prv_flush(const struct pbl_uart *uart) {
  if (pbl_kernel_is_running() && !t5ai_mailbox_channel_is_owner()) {
    pbl_work_submit(&pbl_sys_workq, &s_send);
  }
}
static void prv_poll_out(const struct pbl_uart *uart, uint8_t c) {
  if (t5ai_mailbox_channel_is_owner()) {
    return;
  }
  if (s_length < sizeof(s_buffer)) {
    s_buffer[(s_head + s_length) % sizeof(s_buffer)] = c;
    ++s_length;
  } else {
    ++s_overflow_drops;
  }
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

// Panic sends at most one ring capacity, even if producers keep writing.
static void prv_panic(const struct pbl_log_backend *backend) {
  if (!mcu_state_is_isr() && !t5ai_mailbox_channel_is_owner() && !pbl_irq_is_locked() &&
      !pbl_sched_is_locked() && pbl_kernel_is_running()) {
    (void)prv_drain(sizeof(s_buffer));
  }
}
static void prv_log_noop(const struct pbl_log_backend *backend, const struct pbl_log_msg *msg) {
}
static const struct pbl_log_backend_api s_panic_api = {
  .process = prv_log_noop,
  .panic = prv_panic,
};
PBL_LOG_BACKEND_DEFINE(zz_t5ai_console_drain, s_panic_api, LOG_LEVEL_ALWAYS, true);
