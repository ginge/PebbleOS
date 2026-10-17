/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>

#include <pbl/drivers/gpio/t5ai.h>

#include <clar.h>
#include <sys/mman.h>

#define REGS        ((volatile uint32_t *)0x44000400u)
#define OUT         (1u << 1)
#define OUT_DISABLE (1u << 3)
#define IRQ_ENABLE  (1u << 12)
uint32_t t5ai_test_primask;
static unsigned s_calls;
static void *s_arg;
static void test_handler(void *arg) {
  ++s_calls;
  s_arg = arg;
}

void test_gpio_t5ai__initialize(void) {
  cl_assert(mmap((void *)0x44000000u, 0x20000, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
  memset(T5AI_GPIO_INTC.lines, 0, T5AI_GPIO_INTC.num_irqs * sizeof(*T5AI_GPIO_INTC.lines));
  cl_assert_equal_i(pbl_device_init(&T5AI_GPIO.dev), 0);
  cl_assert_equal_i(pbl_device_init(&T5AI_GPIO_INTC.dev), 0);
  t5ai_test_primask = 0;
  s_calls = 0;
  s_arg = nullptr;
}
void test_gpio_t5ai__cleanup(void) {
  munmap((void *)0x44000000u, 0x20000);
}

void test_gpio_t5ai__logical_polarity_and_open_drain_preserve_physical_levels(void) {
  const struct pbl_gpio gpio = PBL_GPIO(&T5AI_GPIO, 28, PBL_GPIO_ACTIVE_LOW);
  cl_assert_equal_i(pbl_gpio_configure(&gpio, PBL_GPIO_OUTPUT_INACTIVE), 0);
  cl_assert(REGS[28] & OUT);
  cl_assert(!(REGS[28] & OUT_DISABLE));
  pbl_gpio_set(&gpio, true);
  cl_assert(!(REGS[28] & OUT));
  const struct pbl_gpio od = PBL_GPIO(&T5AI_GPIO, 20, PBL_GPIO_OPEN_DRAIN | PBL_GPIO_PULL_UP);
  cl_assert_equal_i(pbl_gpio_configure(&od, PBL_GPIO_OUTPUT_HIGH), 0);
  cl_assert(REGS[20] & OUT_DISABLE);
  cl_assert(!(REGS[20] & OUT));
  pbl_gpio_set_raw(&od, false);
  cl_assert(!(REGS[20] & (OUT | OUT_DISABLE)));
  t5ai_test_primask = 1;
  pbl_gpio_set_raw(&od, true);
  cl_assert_equal_i(t5ai_test_primask, 1);
  cl_assert(REGS[20] & OUT_DISABLE);
  pbl_gpio_configure(&gpio, PBL_GPIO_INPUT);
  REGS[28] &= ~1u;
  cl_assert_equal_i(pbl_gpio_get(&gpio), 1);
  REGS[28] |= 1u;
  cl_assert_equal_i(pbl_gpio_get(&gpio), 0);
}
void test_gpio_t5ai__both_edges_and_both_status_banks_dispatch_with_context(void) {
  int context;
  const struct pbl_irq_spec low = PBL_IRQ_SPEC(&T5AI_GPIO_INTC, 9, PBL_IRQ_TYPE_EDGE_BOTH);
  const struct pbl_irq_spec high = PBL_IRQ_SPEC(&T5AI_GPIO_INTC, 55, PBL_IRQ_TYPE_EDGE_FALLING);
  cl_assert_equal_i(pbl_intc_request(&low, test_handler, &context), 0);
  cl_assert_equal_i(pbl_intc_request(&high, test_handler, &context), 0);
  pbl_intc_enable(&low);
  pbl_intc_enable(&high);
  cl_assert_equal_i((REGS[9] >> 10) & 3u, 2);
  REGS[9] |= 1u;
  REGS[0x100 / 4] = 1u << 9;
  REGS[0x104 / 4] = 1u << 23;
  pbl_gpio_t5ai_irq_handler();
  cl_assert_equal_i(s_calls, 2);
  cl_assert(s_arg == &context);
  cl_assert_equal_i((REGS[9] >> 10) & 3u, 3);
  cl_assert(REGS[9] & (1u << 13));
  pbl_intc_disable(&low);
  pbl_intc_disable(&high);
  pbl_gpio_t5ai_irq_handler();
  cl_assert_equal_i(s_calls, 2);
  cl_assert(!(REGS[9] & IRQ_ENABLE));
}
void test_gpio_t5ai__invalid_pins_modes_and_unsupported_triggers_fail(void) {
  const struct pbl_gpio bad = PBL_GPIO(&T5AI_GPIO, 56, 0);
  cl_assert_equal_i(pbl_gpio_configure(&bad, PBL_GPIO_INPUT), -EINVAL);
  cl_assert_equal_i(pbl_gpio_set_raw(&bad, true), -EINVAL);
  const struct pbl_gpio gpio = PBL_GPIO(&T5AI_GPIO, 9, 0);
  cl_assert_equal_i(pbl_gpio_configure(&gpio, PBL_GPIO_INPUT | PBL_GPIO_OUTPUT), -EINVAL);
  const struct pbl_irq_spec level = PBL_IRQ_SPEC(&T5AI_GPIO_INTC, 9, PBL_IRQ_TYPE_LEVEL_HIGH);
  cl_assert_equal_i(pbl_intc_request(&level, test_handler, nullptr), -ENOTSUP);
}
