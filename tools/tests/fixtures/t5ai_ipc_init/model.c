/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/mailbox_channel.h>
#include <pbl/soc/t5ai/saradc_ipc.h>

static uint64_t now;
static unsigned depth, route_calls[256], connects, sync_initializations, sleep_faults;
static status_t route_error[256];
static bool reject_connect, drop_response;
static T5AICPMailboxReceiveCallback routes[256];
static void *route_contexts[256];
static unsigned completed;
static status_t completed_status;
void pbl_irq_lock(void) {
  ++depth;
}
void pbl_irq_unlock(void) {
  assert(depth);
  --depth;
}
uint64_t pbl_uptime_ms(void) {
  return now;
}
uint64_t pbl_uptime_ticks(void) {
  return now * PBL_TICK_HZ / 1000;
}
void pbl_mutex_init(struct pbl_mutex *mutex) {
  *mutex = (struct pbl_mutex)PBL_MUTEX_INITIALIZER;
  ++sync_initializations;
}
int pbl_mutex_lock_lr(struct pbl_mutex *mutex, pbl_timeout_t timeout, uintptr_t lr) {
  assert(!depth && !mutex->count);
  ++mutex->count;
  return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *mutex) {
  assert(mutex->count == 1);
  --mutex->count;
}
void pbl_sem_init(struct pbl_sem *sem, unsigned initial, unsigned limit) {
  *sem = (struct pbl_sem)PBL_SEM_INITIALIZER(initial, limit);
}
int pbl_sem_take(struct pbl_sem *sem, pbl_timeout_t timeout) {
  if (sem->backend.count) {
    --sem->backend.count;
    return 0;
  }
  now += pbl_ticks_to_ms(timeout.ticks);
  return -EAGAIN;
}
void pbl_sem_give(struct pbl_sem *sem) {
  if (sem->backend.count < sem->limit)
    ++sem->backend.count;
}
status_t t5ai_cp_mailbox_register(uint8_t channel, T5AICPMailboxReceiveCallback callback,
                                  void *ctx) {
  assert(!depth && callback);
  ++route_calls[channel];
  if (FAILED(route_error[channel]))
    return route_error[channel];
  if (routes[channel])
    return E_INVALID_OPERATION;
  routes[channel] = callback;
  route_contexts[channel] = ctx;
  return S_SUCCESS;
}
status_t t5ai_cp_mailbox_acknowledge_frame(const T5AICPMailboxFrame *frame, bool handled) {
  return S_SUCCESS;
}
bool t5ai_cp_mailbox_shared_range_valid(uintptr_t address, size_t size) {
  return address && size;
}
void t5ai_mailbox_channel_wake(void) {
}
void t5ai_sleep_fault(void) {
  ++sleep_faults;
}
void pbl_log(uint8_t level, const char *file, int line, const char *fmt, ...) {
}
[[noreturn]] void pbl_panic_bare_at(const char *file, int line) {
  abort();
}

// Only disambiguate private identifiers; all three production units remain complete.
#define _pbl_log_module_level _mb_log_module_level
#define _pbl_log_module_name  _mb_log_module_name
#define _pbl_log_module_map   _mb_log_module_map
#define s_initialized         s_mb_initialized
#include "drivers/t5ai/mb_ipc.c"
#undef s_initialized
#undef _pbl_log_module_level
#undef _pbl_log_module_name
#undef _pbl_log_module_map
#include "drivers/t5ai/saradc_ipc.c"
#include FLASH_SOURCE

static void respond_connect(const T5AIMailboxMessage *request) {
  T5AIMailboxMessage response = {
    .header = request->header | MB_IPC_RESPONSE_FLAG,
    .parameter1 = prv_address(MB_IPC_CPU_AP, prv_source_port(request), MB_IPC_CPU_CP,
                              prv_destination_port(request), prv_tag(request)),
  };
  if (reject_connect)
    response.parameter1 |= 1u << MB_IPC_API_STATUS_SHIFT;
  T5AICPMailboxFrame frame = {.address = (uintptr_t)&response, .length = sizeof(response)};
  assert(routes[MB_IPC_RX_CHANNEL]);
  routes[MB_IPC_RX_CHANNEL](MB_IPC_RX_CHANNEL, &frame, route_contexts[MB_IPC_RX_CHANNEL]);
  assert(prv_route_status(&response) == MB_IPC_ROUTE_OK);
}
status_t t5ai_mailbox_channel_send(uint8_t channel, const T5AIMailboxMessage *message,
                                   T5AIMailboxMessage *ack, uint32_t timeout) {
  assert(!depth && channel == MB_IPC_TX_CHANNEL && message->header == MB_IPC_COMMAND_CONNECT);
  assert(prv_source_port(message) == FLASH_CLIENT_PORT &&
         prv_destination_port(message) == FLASH_SERVER_PORT);
  ++connects;
  *ack = *message;
  prv_set_route_status(ack, MB_IPC_ROUTE_OK);
  if (!drop_response)
    respond_connect(message);
  return S_SUCCESS;
}
status_t t5ai_mailbox_channel_send_async(uint8_t channel, const T5AIMailboxMessage *message,
                                         T5AIMailboxTransmitHandler callback, void *ctx) {
  // These initialization checks intentionally leave ADC connect pending until its deadline.
  assert(false);
  return E_ERROR;
}
static void adc_completed(status_t status, uint16_t peak, void *ctx) {
  assert(!depth && !peak);
  ++completed;
  completed_status = status;
}
