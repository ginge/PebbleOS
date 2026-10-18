/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#ifdef CONFIG_T5AI_QMI8658_PROFILE
#include <pbl/kernel/time.h>
#include <pbl/soc/t5ai/rtc.h>
#ifdef CONFIG_T5AI_I2C_PROFILE
#include <pbl/drivers/i2c/t5ai.h>
#endif
static struct {
  uint32_t irq, watchdog, passes, acquired, delivered, drops, gaps, recoveries;
  uint32_t phase_max[6], phase_sum[6], fifo_count_max, catchups, irq_latency_max;
  uint32_t transfers, bytes, errors, io_ticks, fifo_max_ticks, latency_us, started, cpu_started,
      cpu, previous_cpu;
} s_profile;
static uint32_t prv_profile_cpu(void) {
  struct pbl_thread *t = pbl_thread_current();
  pbl_irq_lock();
  const uint32_t cpu = t->backend.run_time + pbl_uptime_ticks() - t->backend.switched_in_at;
  pbl_irq_unlock();
  return cpu;
}
static inline uint32_t qmi_profile_io_start(void) {
  return T5AI_AON_RTC_COUNT_LOW;
}
static inline void qmi_profile_io_end(uint32_t start, uint32_t bytes, bool ok) {
  s_profile.io_ticks += T5AI_AON_RTC_COUNT_LOW - start;
  ++s_profile.transfers;
  s_profile.bytes += bytes;
  s_profile.errors += !ok;
}
static inline void qmi_profile_fifo_end(uint32_t start) {
  const uint32_t ticks = T5AI_AON_RTC_COUNT_LOW - start;
  if (ticks > s_profile.fifo_max_ticks)
    s_profile.fifo_max_ticks = ticks;
}
static inline void qmi_profile_fifo_phase(unsigned phase, uint32_t start) {
  const uint32_t ticks = T5AI_AON_RTC_COUNT_LOW - start;
  s_profile.phase_sum[phase] += ticks;
  if (ticks > s_profile.phase_max[phase])
    s_profile.phase_max[phase] = ticks;
}
static inline void qmi_profile_irq_latency(uint32_t start) {
  const uint32_t ticks = T5AI_AON_RTC_COUNT_LOW - start;
  if (ticks > s_profile.irq_latency_max)
    s_profile.irq_latency_max = ticks;
}
static inline void qmi_profile_fifo_count(unsigned count) {
  if (count > s_profile.fifo_count_max)
    s_profile.fifo_count_max = count;
}
static inline void qmi_profile_catchup(void) {
  ++s_profile.catchups;
}
static inline void qmi_profile_irq(void) {
  ++s_profile.irq;
}
static inline void qmi_profile_watchdog(void) {
  ++s_profile.watchdog;
}
static inline void qmi_profile_gap(uint32_t n) {
  s_profile.gaps += n;
}
static inline void qmi_profile_drop(uint32_t n) {
  s_profile.drops += n;
}
static inline void qmi_profile_recovery(void) {
  ++s_profile.recoveries;
}
static inline void qmi_profile_acquire_start(void) {
  s_profile.cpu_started = prv_profile_cpu();
}
static inline void qmi_profile_delivery(uint32_t n, uint64_t now, uint64_t first) {
  s_profile.delivered += n;
  uint64_t age = now >= first ? now - first : 0;
  if (age > s_profile.latency_us)
    s_profile.latency_us = age;
}
static inline void qmi_profile_acquire_end(uint32_t n) {
  s_profile.cpu += prv_profile_cpu() - s_profile.cpu_started;
  ++s_profile.passes;
  s_profile.acquired += n;
  uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  if (now - s_profile.started < 5u * T5AI_AON_RTC_HZ)
    return;
  PBL_LOG_DBG("QMIACQ dt=%lu passes=%lu irq=%lu timer=%lu samples=%lu delivered=%lu cpu_ms=%lu",
              (unsigned long)(now - s_profile.started), (unsigned long)s_profile.passes,
              (unsigned long)s_profile.irq, (unsigned long)s_profile.watchdog,
              (unsigned long)s_profile.acquired, (unsigned long)s_profile.delivered,
              (unsigned long)s_profile.cpu);
  PBL_LOG_DBG(
      "QMIBUS transfers=%lu bytes=%lu wall_ticks=%lu errors=%lu drops=%lu gaps=%lu recovery=%lu",
      (unsigned long)s_profile.transfers, (unsigned long)s_profile.bytes,
      (unsigned long)s_profile.io_ticks, (unsigned long)s_profile.errors,
      (unsigned long)s_profile.drops, (unsigned long)s_profile.gaps,
      (unsigned long)s_profile.recoveries);
  const uint32_t cpu = prv_profile_cpu();
  PBL_LOG_DBG(
      "QMISTATE interval_us=%lu batch=%lu shake=%lu age_us=%lu worker_cpu_ms=%lu fifo_max_us=%lu irq_req_max_us=%lu",
      (unsigned long)s_dev->state->interval_us, (unsigned long)s_dev->state->num_samples,
      (unsigned long)s_dev->state->shake_enabled, (unsigned long)s_profile.latency_us,
      (unsigned long)(cpu - s_profile.previous_cpu),
      (unsigned long)((uint64_t)s_profile.fifo_max_ticks * 1000000u / T5AI_AON_RTC_HZ),
      (unsigned long)((uint64_t)s_profile.irq_latency_max * 1000000u / T5AI_AON_RTC_HZ));
  PBL_LOG_DBG("QMIFIFO max_ticks prep=%lu req=%lu ack=%lu count=%lu data=%lu resume=%lu frames=%lu",
              (unsigned long)s_profile.phase_max[0], (unsigned long)s_profile.phase_max[1],
              (unsigned long)s_profile.phase_max[2], (unsigned long)s_profile.phase_max[3],
              (unsigned long)s_profile.phase_max[4], (unsigned long)s_profile.phase_max[5],
              (unsigned long)s_profile.fifo_count_max);
  PBL_LOG_DBG(
      "QMIFIFOSUM ticks prep=%lu req=%lu ack=%lu count=%lu data=%lu resume=%lu catchups=%lu",
      (unsigned long)s_profile.phase_sum[0], (unsigned long)s_profile.phase_sum[1],
      (unsigned long)s_profile.phase_sum[2], (unsigned long)s_profile.phase_sum[3],
      (unsigned long)s_profile.phase_sum[4], (unsigned long)s_profile.phase_sum[5],
      (unsigned long)s_profile.catchups);
#ifdef CONFIG_T5AI_I2C_PROFILE
  t5ai_i2c_profile_dump();
#endif
  s_profile = (__typeof__(s_profile)){.started = now, .previous_cpu = cpu};
}
#else
#define qmi_profile_io_start()              0u
#define qmi_profile_io_end(a, b, c)         ((void)(a))
#define qmi_profile_fifo_phase(p, a)        ((void)(a))
#define qmi_profile_irq_latency(a)          ((void)(a))
#define qmi_profile_fifo_count(n)           ((void)0)
#define qmi_profile_fifo_end(a)             ((void)(a))
#define qmi_profile_catchup()               ((void)0)
#define qmi_profile_irq()                   ((void)0)
#define qmi_profile_watchdog()              ((void)0)
#define qmi_profile_gap(n)                  ((void)0)
#define qmi_profile_drop(n)                 ((void)0)
#define qmi_profile_recovery()              ((void)0)
#define qmi_profile_acquire_start()         ((void)0)
#define qmi_profile_acquire_end(n)          ((void)0)
#define qmi_profile_delivery(n, now, first) ((void)0)
#endif
