/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "dbgserial_input.h"

#include "pbl/util/attributes.h"
#include "pbl/util/size.h"

#if defined(CONFIG_SOC_T5AI)

#include <pbl/mcu/cache.h>
#include <pbl/soc/t5ai/cp_mailbox.h>

#include <string.h>

#define T5AI_CP_LOG_RX_CHANNEL 0x4eu
#define T5AI_CP_LOG_USER_INPUT_COMMAND 3u

typedef struct {
  uint32_t header;
  uint32_t buffer;
  uint32_t length;
  uint32_t result;
} T5AILogMailboxMessage;

static const uint8_t s_mailbox_rx_probe_request[] = {'P', 'B', 'L', 'R', 'X', '\r', '\n'};
static const uint8_t s_mailbox_timebase_probe_request[] = {'P', 'B', 'L',  'T',
                                                           'I', 'M', '\r', '\n'};
static const uint8_t s_mailbox_display_probe_request[] = {'P', 'B', 'L', 'D', 'S', 'P', '\r', '\n'};
static const uint8_t s_mailbox_display_bitbang_request[] = {'P', 'B', 'L', 'B', 'I', 'T', '\r', '\n'};
static const uint8_t s_mailbox_splash_request[] = {'P', 'B', 'L', 'S', 'P', 'L', '\r', '\n'};
static const uint8_t s_mailbox_button_request[][7] = {
  {'P', 'B', 'L', 'B', '0', '\r', '\n'},
  {'P', 'B', 'L', 'B', '1', '\r', '\n'},
  {'P', 'B', 'L', 'B', '2', '\r', '\n'},
  {'P', 'B', 'L', 'B', '3', '\r', '\n'},
};
static const uint8_t s_mailbox_touch_request[] = {'P', 'B', 'L', 'T', 'P', '\r', '\n'};
static const uint8_t s_mailbox_touch_nav_request[] = {'P', 'B', 'L', 'T', 'N', '\r', '\n'};
static const uint8_t s_mailbox_swipe_up_request[] = {'P', 'B', 'L', 'S', 'U', '\r', '\n'};
static volatile uint8_t s_mailbox_probe_received;

static void prv_log_mailbox_receive(uint8_t logical_channel, const T5AICPMailboxFrame *frame,
                                    void *context) {
  (void)logical_channel;
  (void)context;

  bool handled = false;
  uint32_t header = 0u;
  uint32_t parameter1 = 0u;
  uint32_t parameter2 = 0u;
  if (frame->length == sizeof(T5AILogMailboxMessage)) {
    T5AILogMailboxMessage message;
    dcache_invalidate((void *)frame->address, sizeof(message));
    memcpy(&message, (const void *)frame->address, sizeof(message));
    header = message.header;
    parameter1 = message.buffer;
    parameter2 = message.length;
    if ((uint8_t)message.header == T5AI_CP_LOG_USER_INPUT_COMMAND && message.buffer != 0u) {
      const void *request = (const void *)(uintptr_t)message.buffer;
      const uint16_t request_length = (uint16_t)message.length;
      DbgSerialMailboxProbe probe = DbgSerialMailboxProbe_None;
      if (request_length == sizeof(s_mailbox_button_request[0])) {
        dcache_invalidate((void *)request, request_length);
        if (memcmp(request, s_mailbox_touch_request, request_length) == 0) {
          probe = DbgSerialMailboxProbe_Touch;
        } else if (memcmp(request, s_mailbox_touch_nav_request, request_length) == 0) {
          probe = DbgSerialMailboxProbe_TouchNav;
        } else if (memcmp(request, s_mailbox_swipe_up_request, request_length) == 0) {
          probe = DbgSerialMailboxProbe_SwipeUp;
        } else {
          for (unsigned i = 0; i < ARRAY_LENGTH(s_mailbox_button_request); ++i) {
            if (memcmp(request, s_mailbox_button_request[i], request_length) == 0) {
              probe = (DbgSerialMailboxProbe)(DbgSerialMailboxProbe_ButtonBack + i);
              break;
            }
          }
        }
      } else if (request_length == sizeof(s_mailbox_rx_probe_request)) {
        dcache_invalidate((void *)request, sizeof(s_mailbox_rx_probe_request));
        if (memcmp(request, s_mailbox_rx_probe_request, sizeof(s_mailbox_rx_probe_request)) == 0) {
          probe = DbgSerialMailboxProbe_Rx;
        }
      } else if (request_length == sizeof(s_mailbox_timebase_probe_request)) {
        dcache_invalidate((void *)request, sizeof(s_mailbox_timebase_probe_request));
        if (memcmp(request, s_mailbox_timebase_probe_request,
                   sizeof(s_mailbox_timebase_probe_request)) == 0) {
          probe = DbgSerialMailboxProbe_Timebase;
        } else if (memcmp(request, s_mailbox_display_probe_request,
                          sizeof(s_mailbox_display_probe_request)) == 0) {
          probe = DbgSerialMailboxProbe_Display;
        } else if (memcmp(request, s_mailbox_display_bitbang_request,
                          sizeof(s_mailbox_display_bitbang_request)) == 0) {
          probe = DbgSerialMailboxProbe_DisplayBitbang;
        } else if (memcmp(request, s_mailbox_splash_request,
                          sizeof(s_mailbox_splash_request)) == 0) {
          probe = DbgSerialMailboxProbe_Splash;
        }
      }
      handled = probe != DbgSerialMailboxProbe_None;
      if (handled) {
        __atomic_store_n(&s_mailbox_probe_received, (uint8_t)probe, __ATOMIC_RELEASE);
      }
    }
  }
  (void)t5ai_cp_mailbox_acknowledge(header, parameter1, parameter2, handled);
}

void dbgserial_input_init(void) {
  (void)t5ai_cp_mailbox_register(T5AI_CP_LOG_RX_CHANNEL, prv_log_mailbox_receive, NULL);
}

void dbgserial_register_character_callback(DbgSerialCharacterCallback callback) {
  (void)callback;
}

void dbgserial_set_rx_dma_enabled(bool enabled) {
  (void)enabled;
}

void dbgserial_set_input_enabled(bool enabled) {
  (void)enabled;
}

DbgSerialMailboxProbe dbgserial_input_consume_mailbox_probe(void) {
  return (DbgSerialMailboxProbe)__atomic_exchange_n(
      &s_mailbox_probe_received, (uint8_t)DbgSerialMailboxProbe_None, __ATOMIC_ACQ_REL);
}

#elif !defined(CONFIG_RELEASE) || defined(CONFIG_MFG)

static DbgSerialCharacterCallback s_character_callback;

//! We DMA into this buffer as a circular buffer
#define DMA_BUFFER_LENGTH (200)
static uint8_t s_dma_buffer[DMA_BUFFER_LENGTH] __attribute__((aligned(4)));
static bool s_dma_enabled = false;

static bool prv_uart_irq_handler(UARTDevice *dev, uint8_t data, const UARTRXErrorFlags *err_flags) {
  bool should_context_switch = false;
  if (s_character_callback) {
    s_character_callback(data, &should_context_switch);
  }
  return should_context_switch;
}

void dbgserial_input_init(void) {
  // set up the USART interrupt on RX
  uart_set_rx_interrupt_handler(DBG_UART, prv_uart_irq_handler);
  uart_set_rx_interrupt_enabled(DBG_UART, true);
}

void dbgserial_register_character_callback(DbgSerialCharacterCallback callback) {
  s_character_callback = callback;
}

void dbgserial_set_rx_dma_enabled(bool enabled) {
  if (enabled == s_dma_enabled) {
    return;
  }
  s_dma_enabled = enabled;
  if (enabled) {
    uart_start_rx_dma(DBG_UART, s_dma_buffer, DMA_BUFFER_LENGTH);
  } else {
    uart_stop_rx_dma(DBG_UART);
  }
}

void dbgserial_set_input_enabled(bool enabled) {
  uart_set_rx_interrupt_enabled(DBG_UART, enabled);
}

#else
void dbgserial_input_init(void) {}

void dbgserial_register_character_callback(DbgSerialCharacterCallback callback) {}

void dbgserial_set_rx_dma_enabled(bool enabled) {}

void dbgserial_set_input_enabled(bool enabled) {}
#endif
