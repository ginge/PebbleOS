/* SPDX-License-Identifier: Apache-2.0 */
#include "kernel/events.h"
#include "system/status_codes.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pbl/drivers/battery/t5ai.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/time.h>
#include <pbl/kernel/work.h>
#include <pbl/logging/logging.h>
#include <pbl/services/battery/battery_state.h>
#include <pbl/soc/t5ai/saradc_ipc.h>
static const struct pbl_gpio_port port;
static const struct pbl_intc intc;
const T5AIBatteryConfig T5AI_BATTERY_CONFIG = {
  .charge = PBL_GPIO(&port, 30, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP),
  .interrupt = PBL_IRQ_SPEC(&intc, 30, PBL_IRQ_TYPE_EDGE_BOTH),
  .channel = 15,
  .samples = 16,
  .cal_low = 2716,
  .cal_span = 2854,
  .divider_numerator = 2510,
  .divider_denominator = 510,
};
struct pbl_workq pbl_sys_workq_high;
static uint64_t now;
static unsigned depth, context, requests, updates, events, configures, registrations, queued;
static unsigned sampled_logs, failed_logs, high_callbacks, reads;
static int gpio_error, gpio_read_error, irq_error, irq_enable_error;
static bool charge, drop, edge_on_enable;
static status_t request_error;
static void (*irq_cb)(void *), (*work)(void *), (*on_update)(void);
static T5AISaradcCompletion completion;
static struct {
  struct pbl_delayed_work *item;
  uint64_t due;
  bool armed;
} timers[4];
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
int pbl_gpio_configure(const struct pbl_gpio *gpio, uint32_t flags) {
  assert(!depth && gpio == &T5AI_BATTERY_CONFIG.charge && flags == PBL_GPIO_INPUT);
  ++configures;
  return gpio_error;
}
int pbl_gpio_get(const struct pbl_gpio *gpio) {
  assert(gpio == &T5AI_BATTERY_CONFIG.charge);
  ++reads;
  return gpio_read_error ? gpio_read_error : charge;
}
int pbl_intc_request(const struct pbl_irq_spec *irq, pbl_intc_handler_t callback, void *data) {
  assert(irq == &T5AI_BATTERY_CONFIG.interrupt && !data);
  irq_cb = callback;
  return irq_error;
}
int pbl_intc_enable(const struct pbl_irq_spec *irq) {
  assert(irq == &T5AI_BATTERY_CONFIG.interrupt);
  if (edge_on_enable && !irq_enable_error) {
    charge = true;
    context = 4;
    irq_cb(NULL);
    context = 1;
  }
  return irq_enable_error;
}
static unsigned timer_index(struct pbl_delayed_work *item) {
  for (unsigned i = 0; i < 4; ++i)
    if (timers[i].item == item)
      return i;
  assert(false);
  return 0;
}
void pbl_delayed_work_expired(struct pbl_timer *timer) {
  abort();
}
void pbl_delayed_work_init(struct pbl_delayed_work *item, pbl_work_fn_t fn) {
  *item = (struct pbl_delayed_work)PBL_DELAYED_WORK_INITIALIZER(fn);
  for (unsigned i = 0; i < 4; ++i)
    if (!timers[i].item || timers[i].item == item) {
      timers[i].item = item;
      timers[i].armed = false;
      return;
    }
  assert(false);
}
bool pbl_delayed_work_schedule(struct pbl_workq *queue, struct pbl_delayed_work *item,
                               pbl_timeout_t delay) {
  assert(queue == &pbl_sys_workq_high);
  unsigned i = timer_index(item);
  if (timers[i].armed)
    return false;
  timers[i].armed = true;
  timers[i].due = now + pbl_ticks_to_ms(delay.ticks);
  item->work.q = queue;
  item->timer.timeout.expires = timers[i].due;
  ++registrations;
  return true;
}
bool pbl_delayed_work_cancel(struct pbl_delayed_work *item) {
  unsigned i = timer_index(item);
  bool was = timers[i].armed;
  timers[i].armed = false;
  return was;
}
bool pbl_delayed_work_reschedule(struct pbl_workq *queue, struct pbl_delayed_work *item,
                                 pbl_timeout_t delay) {
  bool was = pbl_delayed_work_cancel(item);
  pbl_delayed_work_schedule(queue, item, delay);
  return was;
}
void pbl_delayed_work_schedule_next(struct pbl_delayed_work *item, pbl_tick_t period) {
  unsigned i = timer_index(item);
  uint64_t next = item->timer.timeout.expires + pbl_ticks_to_ms(period);
  pbl_delayed_work_reschedule(item->work.q, item, PBL_MSEC(next > now ? next - now : 1));
  (void)i;
}
bool pbl_sys_work_try_call(pbl_work_call_fn_t fn, void *data) {
  assert(context == 2 && depth && !data);
  if (drop)
    return false;
  assert(!work);
  work = fn;
  ++queued;
  return true;
}
void battery_state_force_update(void) {
  assert(context == 1 && !depth);
  ++updates;
  if (on_update)
    on_update();
}
void event_put(PebbleEvent *event) {
  assert(context == 1 && !depth && event->type == PEBBLE_BATTERY_CONNECTION_EVENT);
  assert(event->battery_connection.is_connected == charge);
  ++events;
}
void t5ai_saradc_get_diagnostics(T5AISaradcDiagnostics *out) {
  *out = (T5AISaradcDiagnostics){.failed_command = 255};
}
status_t t5ai_saradc_request(uint8_t channel, uint8_t count, T5AISaradcCompletion callback,
                             void *data) {
  assert(context == 1 && !depth && channel == 15 && count == 16 && !data && !completion);
  ++requests;
  if (FAILED(request_error))
    return request_error;
  completion = callback;
  return S_SUCCESS;
}
void pbl_log(uint8_t level, const char *file, int line, const char *fmt, ...) {
  assert(context == 1);
  if (strstr(fmt, "Battery ADC sample:"))
    ++sampled_logs;
  if (strstr(fmt, "Battery ADC failed:"))
    ++failed_logs;
}
