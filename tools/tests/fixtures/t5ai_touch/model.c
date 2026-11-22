/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pbl/drivers/touch/cst92xx_t5ai.h>
#include <pbl/input/input.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/util/misc.h>

PBL_CST92XX_T5AI_STATE_DEFINE(first);
PBL_CST92XX_T5AI_STATE_DEFINE(second);
static const struct pbl_i2c_bus buses[2];
static const struct pbl_gpio_port ports[2];
static struct pbl_device_state intc_states[2];
static const struct pbl_intc intcs[] = {
  {.dev.state = &intc_states[0]},
  {.dev.state = &intc_states[1]},
};
static const struct pbl_cst92xx_t5ai devices[] = {
  {
    .touch = PBL_TOUCH_INIT(first, "first", &buses[0].dev, &pbl_touch_cst92xx_t5ai_ops,
                            .size_x = 260, .size_y = 260, .inverted_x = true, .inverted_y = true),
    .state = &first_cst92xx_state,
    .i2c = PBL_I2C_DEV(&buses[0], 0x5a),
    .interrupt = PBL_IRQ_SPEC(&intcs[0], 42, PBL_IRQ_TYPE_EDGE_FALLING),
    .interrupt_input = PBL_GPIO(&ports[0], 42, 0),
    .reset = PBL_GPIO(&ports[0], 43, 0),
    .native_width = 466,
    .native_height = 466,
  },
  {
    .touch = PBL_TOUCH_INIT(second, "second", &buses[1].dev, &pbl_touch_cst92xx_t5ai_ops,
                            .size_x = 100, .size_y = 80, .inverted_y = true, .swapped_x_y = true),
    .state = &second_cst92xx_state,
    .i2c = PBL_I2C_DEV(&buses[1], 0x5b),
    .interrupt = PBL_IRQ_SPEC(&intcs[1], 7, PBL_IRQ_TYPE_EDGE_FALLING),
    .interrupt_input = PBL_GPIO(&ports[1], 7, 0),
    .reset = PBL_GPIO(&ports[1], 8, 0),
    .native_width = 400,
    .native_height = 300,
  },
};
static struct pbl_cst92xx_t5ai_state *const primary = &first_cst92xx_state;
#define TOUCH (&devices[0].touch)
struct Sample {
  bool down;
  int x, y;
};
static struct Hardware {
  uint8_t data[10];
  bool read_error, write_error, ack_error, irq_on, reset_on, irq_in_read;
  int int_level;
  unsigned read_count, write_count, ack_count, reset_count, sleep_count, sample_count,
      gesture_count;
  unsigned fail_probe_count, device_error;
  pbl_intc_handler_t handler;
  void *irq_arg;
  struct Sample records[4096], partial;
} hw[2];
static struct Timer {
  struct pbl_delayed_work *work;
  bool scheduled;
  unsigned milliseconds;
} timers[4];
static struct Timer *timer_model(struct pbl_delayed_work *work) {
  for (unsigned i = 0; i < 4; ++i)
    if (timers[i].work == work)
      return &timers[i];
  assert(false);
  return nullptr;
}
static unsigned owner(const struct pbl_work *work) {
  for (unsigned i = 0; i < 2; ++i)
    if (work == &devices[i].state->touch_work || work == &devices[i].state->retry_timer.work ||
        work == &devices[i].state->watchdog_timer.work)
      return i;
  assert(false);
  return 0;
}
#define wire                  hw[0].data
#define reads                 hw[0].read_count
#define writes                hw[0].write_count
#define acks                  hw[0].ack_count
#define resets                hw[0].reset_count
#define sleeps                hw[0].sleep_count
#define samples               hw[0].sample_count
#define gestures              hw[0].gesture_count
#define output                hw[0].records
#define fail_read             hw[0].read_error
#define fail_write            hw[0].write_error
#define fail_ack              hw[0].ack_error
#define irq_enabled           hw[0].irq_on
#define reset_pin             hw[0].reset_on
#define interrupt_during_read hw[0].irq_in_read
#define pad                   hw[0].int_level
#define timer_pending         (timer_model(&primary->retry_timer)->scheduled)
#define timer_ms              (timer_model(&primary->retry_timer)->milliseconds)
static unsigned active, depth, timer_creates, refs, qcount, enqueue_calls, input_phase;
static unsigned sequence, palm_sequence, last_sample_sequence, errors;
enum {
  USER,
  BG,
  TIMER,
  ISR
};
static unsigned context;
static struct pbl_work *queue[16];
struct pbl_workq pbl_sys_workq, pbl_sys_workq_high;
static void test_set_enabled(bool enabled);
static void fire_irq_index(unsigned index) {
  assert(hw[index].handler);
  unsigned saved = context;
  context = ISR;
  hw[index].handler(hw[index].irq_arg);
  context = saved;
}
static void fire_irq(void) {
  fire_irq_index(0);
}
void pbl_irq_lock(void) {
  ++depth;
}
void pbl_irq_unlock(void) {
  assert(depth);
  --depth;
}
int pbl_mutex_lock_lr(struct pbl_mutex *m, pbl_timeout_t t, uintptr_t lr) {
  (void)lr;
  assert(context != TIMER && context != ISR && !depth && !m->count && t.ticks == PBL_TICK_FOREVER);
  m->count = 1;
  return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *m) {
  assert(m->count == 1 && !depth);
  m->count = 0;
}
bool pbl_device_is_ready(const struct pbl_device *dev) {
  return dev->state->status == PBL_DEVICE_READY;
}
int pbl_device_init(const struct pbl_device *dev) {
  for (unsigned i = 0; i < 2; ++i)
    if (dev == &intcs[i].dev) {
      active = i;
      return -(int)hw[i].device_error;
    }
  assert(false);
  return -1;
}
void pbl_sleep_ms(uint32_t ms) {
  assert(context != TIMER && context != ISR && !depth && (ms == 5 || ms == 50));
  assert(devices[active].state->i2c_lock.count == 1);
  assert(ms == 5 ? !hw[active].reset_on : hw[active].reset_on);
  ++hw[active].sleep_count;
}
static unsigned reset_owner(const struct pbl_gpio *gpio) {
  for (unsigned i = 0; i < 2; ++i)
    if (gpio == &devices[i].reset)
      return i;
  assert(false);
  return 0;
}
int pbl_gpio_set(const struct pbl_gpio *gpio, bool value) {
  active = reset_owner(gpio);
  assert(context != TIMER && context != ISR && !depth);
  hw[active].reset_on = value;
  if (value)
    ++hw[active].reset_count;
  return 0;
}
int pbl_gpio_configure(const struct pbl_gpio *gpio, uint32_t flags) {
  for (unsigned i = 0; i < 2; ++i) {
    if (gpio == &devices[i].reset) {
      assert(flags == PBL_GPIO_OUTPUT_INACTIVE);
      return 0;
    }
    if (gpio == &devices[i].interrupt_input) {
      assert(flags == PBL_GPIO_INPUT);
      return 0;
    }
  }
  assert(false);
  return -1;
}
int pbl_gpio_get_raw(const struct pbl_gpio *gpio) {
  for (unsigned i = 0; i < 2; ++i)
    if (gpio == &devices[i].interrupt_input)
      return hw[i].int_level;
  assert(false);
  return -1;
}
static unsigned irq_owner(const struct pbl_irq_spec *spec) {
  for (unsigned i = 0; i < 2; ++i)
    if (spec == &devices[i].interrupt)
      return i;
  assert(false);
  return 0;
}
int pbl_intc_disable(const struct pbl_irq_spec *spec) {
  hw[irq_owner(spec)].irq_on = false;
  return 0;
}
int pbl_intc_enable(const struct pbl_irq_spec *spec) {
  hw[irq_owner(spec)].irq_on = true;
  return 0;
}
int pbl_intc_request(const struct pbl_irq_spec *spec, pbl_intc_handler_t cb, void *arg) {
  unsigned i = irq_owner(spec);
  assert(arg == &devices[i]);
  hw[i].handler = cb;
  hw[i].irq_arg = arg;
  return 0;
}
static unsigned bus_owner(const struct pbl_i2c_dev *dev) {
  for (unsigned i = 0; i < 2; ++i)
    if (dev == &devices[i].i2c)
      return i;
  assert(false);
  return 0;
}
static int model_write(const struct pbl_i2c_dev *dev, const void *buf, uint32_t n) {
  active = bus_owner(dev);
  const uint8_t *data = buf;
  assert(context != TIMER && context != ISR && !depth && data[0] == 0xd0 && data[1] == 0);
  if (n == 3) {
    assert(data[2] == 0xab);
    ++hw[active].ack_count;
    return hw[active].ack_error ? -1 : 0;
  }
  assert(n == 2);
  ++hw[active].write_count;
  return hw[active].write_error ? -1 : 0;
}
static int model_read(const struct pbl_i2c_dev *dev, void *buf, uint32_t n) {
  active = bus_owner(dev);
  assert(n == 10);
  ++hw[active].read_count;
  if (hw[active].irq_in_read) {
    fire_irq_index(active);
    fire_irq_index(active);
    hw[active].irq_in_read = false;
  }
  memcpy(buf, hw[active].data, 10);
  return hw[active].read_error || hw[active].read_count <= hw[active].fail_probe_count ? -1 : 0;
}
int pbl_i2c_transfer(const struct pbl_i2c_dev *dev, struct pbl_i2c_msg *msgs, uint8_t count) {
  assert(count == 1);
  assert(msgs[0].flags == PBL_I2C_MSG_READ || msgs[0].flags == PBL_I2C_MSG_WRITE);
  return msgs[0].flags == PBL_I2C_MSG_READ ? model_read(dev, msgs[0].buf, msgs[0].len)
                                           : model_write(dev, msgs[0].buf, msgs[0].len);
}
void pbl_work_init(struct pbl_work *work, pbl_work_fn_t fn) {
  *work = (struct pbl_work)PBL_WORK_INITIALIZER(fn);
}
void pbl_delayed_work_init(struct pbl_delayed_work *work, pbl_work_fn_t fn) {
  assert(timer_creates < 4);
  pbl_work_init(&work->work, fn);
  timers[timer_creates++].work = work;
}
bool pbl_delayed_work_reschedule(struct pbl_workq *q, struct pbl_delayed_work *work,
                                 pbl_timeout_t timeout) {
  assert(q == &pbl_sys_workq_high && !depth);
  struct Timer *t = timer_model(work);
  t->scheduled = true;
  t->milliseconds = pbl_ticks_to_ms(timeout.ticks);
  return true;
}
bool pbl_delayed_work_schedule(struct pbl_workq *q, struct pbl_delayed_work *work,
                               pbl_timeout_t timeout) {
  return pbl_delayed_work_reschedule(q, work, timeout);
}
void pbl_delayed_work_schedule_next(struct pbl_delayed_work *work, pbl_tick_t ticks) {
  assert(pbl_ticks_to_ms(ticks) == 1000);
  timer_model(work)->scheduled = true;
}
bool pbl_delayed_work_cancel(struct pbl_delayed_work *work) {
  timer_model(work)->scheduled = false;
  return true;
}
bool pbl_work_submit_boosted(struct pbl_workq *q, struct pbl_work *work) {
  assert(q == &pbl_sys_workq && depth);
  ++enqueue_calls;
  if (work->pending)
    return false;
  assert(qcount < 16);
  work->pending = true;
  queue[qcount++] = work;
  ++refs;
  return true;
}
static void check_input_lock(void) {
  for (unsigned i = 0; i < 2; ++i)
    assert(!devices[i].state->i2c_lock.count);
}
void pbl_input_report(enum pbl_input_type type, uint16_t code, int32_t value, bool sync) {
  check_input_lock();
  assert(context == BG && !depth);
  struct Hardware *h = &hw[active];
  if (type == PBL_INPUT_EV_KEY) {
    assert(code == PBL_INPUT_BTN_TOUCH && !sync && !input_phase);
    h->partial.down = value;
    input_phase = 1;
  } else if (type == PBL_INPUT_EV_ABS) {
    if (code == PBL_INPUT_ABS_X) {
      assert((input_phase == 0 || input_phase == 1) && !sync);
      h->partial.x = value;
      input_phase = 2;
    } else {
      assert(code == PBL_INPUT_ABS_Y && input_phase == 2);
      h->partial.y = value;
      if (sync) {
        assert(h->sample_count < 4096);
        h->records[h->sample_count++] = h->partial;
        input_phase = 0;
        last_sample_sequence = ++sequence;
      } else
        input_phase = 3;
    }
  } else {
    assert(type == PBL_INPUT_EV_GES && code == PBL_INPUT_GES_PALM && input_phase == 3 && sync);
    input_phase = 0;
    ++h->gesture_count;
    palm_sequence = ++sequence; /* GESTURE_HOOK */
  }
}
void pbl_log(uint8_t level, const char *file, int line, const char *fmt, ...) {
  ++errors;
}
[[noreturn]] void pbl_panic_bare_at(const char *file, int line) {
  fprintf(stderr, "panic %s:%d\n", file, line);
  abort();
}
#include "drivers/touch/cst92xx/cst92xx_t5ai.c"
#include "drivers/touch/touch.c"
static void test_set_enabled(bool enabled) {
  assert((enabled ? pbl_touch_cst92xx_t5ai_ops.enable
                  : pbl_touch_cst92xx_t5ai_ops.disable)(TOUCH) == 0);
}
static void run_one(void) {
  assert(qcount && context == USER);
  struct pbl_work *work = queue[0];
  memmove(queue, queue + 1, --qcount * sizeof(queue[0]));
  work->pending = false;
  active = owner(work);
  context = BG;
  work->fn(work);
  check_input_lock();
  assert(!depth);
  context = USER;
  assert(refs);
  --refs;
  assert(refs == qcount);
}
static void fire_timer_index(unsigned index) {
  struct pbl_delayed_work *work = &devices[index].state->retry_timer;
  assert(timer_model(work)->scheduled);
  timer_model(work)->scheduled = false;
  active = index;
  context = TIMER;
  work->work.fn(&work->work);
  context = USER;
}
static void fire_timer(void) {
  fire_timer_index(0);
}
static void fire_watchdog_index(unsigned index) {
  struct pbl_delayed_work *work = &devices[index].state->watchdog_timer;
  assert(timer_model(work)->scheduled);
  active = index;
  context = TIMER;
  work->work.fn(&work->work);
  context = USER;
}
static void fire_watchdog(void) {
  fire_watchdog_index(0);
}
static void report_index(unsigned index, bool down, unsigned x, unsigned y) {
  uint8_t *data = hw[index].data;
  memset(data, 0, 10);
  data[0] = 6;
  data[5] = down;
  data[6] = 0xab;
  data[1] = x >> 4;
  data[2] = y >> 4;
  data[3] = (x & 15) << 4 | (y & 15);
}
static void report(bool down, unsigned x, unsigned y) {
  report_index(0, down, x, y);
}
static void setup(void) {
  hw[0].int_level = hw[1].int_level = 1;
  report(false, 0, 0);
  assert(pbl_touch_dev_init(&devices[0].touch.dev) == 0);
  assert(pbl_touch_dev_init(&devices[0].touch.dev) == 0);
  assert(timer_creates == 2 && reads == 1 && !reset_pin && !irq_enabled);
  first_device_state.status = PBL_DEVICE_READY;
  test_set_enabled(true);
  assert(irq_enabled && reset_pin && qcount == 1 && refs == 1 &&
         timer_model(&primary->watchdog_timer)->scheduled);
}
