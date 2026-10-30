/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>

#include <pbl/drivers/pinctrl/t5ai.h>
#include <pbl/kernel/irq.h>

#define PAD_BASE                   ((volatile uint32_t *)0x44000400u)
#define MUX_BASE                   ((volatile uint32_t *)0x440100c0u)
#define T5AI_PAD_COUNT             56u
#define T5AI_MUX_PADS_PER_REGISTER 8u
#define T5AI_MUX_FIELD_BITS        4u

static int prv_configure(const struct pbl_pinctrl_controller *ctrl, const uint32_t *pins,
                         uint8_t count, uintptr_t reg) {
  (void)reg;
  for (uint8_t i = 0; i < count; ++i) {
    if ((pins[i] & T5AI_PIN_NUMBER_MASK) >= ctrl->num_pads ||
        (pins[i] >> T5AI_PIN_ENCODING_BITS) != 0u) {
      return -EINVAL;
    }
  }
  pbl_irq_lock();
  for (uint8_t i = 0; i < count; ++i) {
    const uint32_t pin = pins[i] & T5AI_PIN_NUMBER_MASK;
    const uint32_t shift = (pin % T5AI_MUX_PADS_PER_REGISTER) * T5AI_MUX_FIELD_BITS;
    PAD_BASE[pin] = (PAD_BASE[pin] & ~T5AI_PIN_CONFIG_MASK) |
                    ((pins[i] >> T5AI_PIN_CONFIG_SHIFT) & T5AI_PIN_CONFIG_MASK);
    MUX_BASE[pin / T5AI_MUX_PADS_PER_REGISTER] =
        (MUX_BASE[pin / T5AI_MUX_PADS_PER_REGISTER] & ~(T5AI_PIN_FUNCTION_MASK << shift)) |
        (((pins[i] >> T5AI_PIN_FUNCTION_SHIFT) & T5AI_PIN_FUNCTION_MASK) << shift);
  }
  pbl_irq_unlock();
  return 0;
}

static int prv_get(const struct pbl_pinctrl_controller *ctrl, uint16_t pad, uint32_t *val) {
  if (pad >= ctrl->num_pads || val == nullptr) {
    return -EINVAL;
  }
  *val = PAD_BASE[pad];
  return 0;
}

static const struct pbl_pinctrl_controller_ops s_ops = {.configure = prv_configure, .get = prv_get};
PBL_DEVICE_STATE_DEFINE(T5AI_PINCTRL);
const struct pbl_pinctrl_controller T5AI_PINCTRL =
    PBL_PINCTRL_CONTROLLER_INIT(T5AI_PINCTRL, "t5ai-pinctrl", &s_ops, T5AI_PAD_COUNT);
PBL_DEVICE_REGISTER(T5AI_PINCTRL, &T5AI_PINCTRL.dev);
