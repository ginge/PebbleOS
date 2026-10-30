/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/drivers/pinctrl.h>

// Packed pin state: pad[5:0], function[9:6], pad configuration[23:10].
#define T5AI_PIN_NUMBER_MASK    0x3fu
#define T5AI_PIN_FUNCTION_SHIFT 6u
#define T5AI_PIN_FUNCTION_MASK  0x0fu
#define T5AI_PIN_CONFIG_SHIFT   10u
#define T5AI_PIN_CONFIG_MASK    0x3fffu
#define T5AI_PIN_ENCODING_BITS  24u

#define T5AI_PAD_OUTPUT_DISABLE         (1u << 3)
#define T5AI_PAD_PULL_UP_SELECT         (1u << 4)
#define T5AI_PAD_PULL_ENABLE            (1u << 5)
#define T5AI_PAD_SECOND_FUNCTION_ENABLE (1u << 6)
#define T5AI_PAD_DRIVE_STRENGTH_SHIFT   8u

#define T5AI_PIN_PULL_UP  (T5AI_PAD_PULL_UP_SELECT | T5AI_PAD_PULL_ENABLE)
#define T5AI_PIN_DRIVE(n) ((n) << T5AI_PAD_DRIVE_STRENGTH_SHIFT)
#define T5AI_PIN(pin, function, options)                                    \
  ((pin) | ((function) << T5AI_PIN_FUNCTION_SHIFT) |                        \
   (((options) | T5AI_PAD_OUTPUT_DISABLE | T5AI_PAD_SECOND_FUNCTION_ENABLE) \
    << T5AI_PIN_CONFIG_SHIFT))
extern const struct pbl_pinctrl_controller T5AI_PINCTRL;
