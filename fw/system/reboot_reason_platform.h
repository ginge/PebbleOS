/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <system/reboot_reason.h>

// Set hooks run on fault/reset paths and must not block or allocate.
#ifdef CONFIG_SOC_T5AI
void reboot_reason_platform_will_set(const RebootReason *reason);
void reboot_reason_platform_did_set(const RebootReason *reason);
void reboot_reason_platform_get(RebootReason *reason);
void reboot_reason_platform_will_clear(void);
void reboot_reason_platform_did_clear(void);
#else
static inline void reboot_reason_platform_will_set(const RebootReason *reason) {
  (void)reason;
}
static inline void reboot_reason_platform_did_set(const RebootReason *reason) {
  (void)reason;
}
static inline void reboot_reason_platform_get(RebootReason *reason) {
  (void)reason;
}
static inline void reboot_reason_platform_will_clear(void) {
}
static inline void reboot_reason_platform_did_clear(void) {
}
#endif
