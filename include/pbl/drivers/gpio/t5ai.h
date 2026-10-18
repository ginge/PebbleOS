/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/drivers/gpio.h>

extern const struct pbl_gpio_port T5AI_GPIO;
extern const struct pbl_intc T5AI_GPIO_INTC;
void pbl_gpio_t5ai_irq_handler(void);
