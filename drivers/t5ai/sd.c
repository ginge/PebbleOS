/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <pbl/drivers/t5ai/sd.h>
#include <pbl/kernel/kernel.h>
#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/mcu/cache.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/soc/t5ai/memory.h>

#include <cmsis_core.h>

#ifdef CONFIG_T5AI_SD_4BIT
#define DATA_WIDTH (1u << 2)
#else
#define DATA_WIDTH 0u
#endif

// BK7258 AP byte addresses/offsets from the pinned Tuya BSP.
#define SDIO_BASE_ADDRESS                    0x458d0000u
#define GDMA_BASE_ADDRESS                    0x45020000u
#define GPIO_BASE_ADDRESS                    0x44000400u
#define SYSTEM_GPIO_FUNCTION_BASE_ADDRESS    0x440100c0u
#define SYSTEM_CPU1_INTERRUPT_ENABLE_ADDRESS 0x44010088u
#define SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS    0x44010024u
#define GPIO_REGISTER_STRIDE                 sizeof(uint32_t)
#define GPIO_FUNCTION_PINS_PER_REGISTER      8u
#define GPIO_FUNCTION_FIELD_BITS             4u
#define GPIO_FUNCTION_FIELD_MASK             0xfu
#define GPIO_FUNCTION_SDIO_GROUP0            1u

#define SD_GLOBAL_CONTROL     0x08u
#define SD_COMMAND_CONTROL    0x10u
#define SD_COMMAND_ARGUMENT   0x14u
#define SD_COMMAND_TIMER      0x18u
#define SD_DATA_CONTROL       0x1cu
#define SD_DATA_TIMER         0x20u
#define SD_RESPONSE_ARGUMENT0 0x24u
#define SD_RESPONSE_ARGUMENT1 0x28u
#define SD_RESPONSE_ARGUMENT2 0x2cu
#define SD_INTERRUPT_STATUS   0x34u
#define SD_INTERRUPT_MASK     0x38u
#define SD_TX_FIFO            0x3cu
#define SD_RX_FIFO            0x40u
#define SD_FIFO_CONTROL       0x44u

#define DMA_GLOBAL_CONTROL                 0x08u
#define DMA_SECURE_ATTRIBUTE               0x10u
#define DMA_PRIVILEGED_ATTRIBUTE           0x14u
#define DMA_INTERRUPT_ALLOCATION           0x28u
#define DMA_CHANNEL_BASE_OFFSET            0x40u
#define DMA_CHANNEL_STRIDE                 0x40u
#define DMA_CHANNEL_CONTROL                0x00u
#define DMA_CHANNEL_DESTINATION            0x04u
#define DMA_CHANNEL_SOURCE                 0x08u
#define DMA_CHANNEL_DESTINATION_LOOP_END   0x0cu
#define DMA_CHANNEL_DESTINATION_LOOP_START 0x10u
#define DMA_CHANNEL_SOURCE_LOOP_END        0x14u
#define DMA_CHANNEL_SOURCE_LOOP_START      0x18u
#define DMA_CHANNEL_REQUEST                0x1cu
#define DMA_CHANNEL_STATUS                 0x30u

// AP kernel/worker/application SRAM; excludes CP memory and IPC alias windows.
#define SD_AP_SRAM_BEGIN            0x28010000u
#define SD_AP_SRAM_END              0x28066000u
#define SD_DMA_CACHE_ALIGNMENT      32u
#define SD_DMA_CACHE_ALIGNMENT_MASK (SD_DMA_CACHE_ALIGNMENT - 1u)
#define SD_PRIVATE_PSRAM_BEGIN      T5AI_DISPLAY_PSRAM_BASE
#define SD_PRIVATE_PSRAM_END        (T5AI_DISPLAY_PSRAM_BASE + T5AI_DISPLAY_PSRAM_SIZE)

#define SD_CLOCK_PIN           2u
#define SD_COMMAND_PIN         3u
#define SD_DATA0_PIN           4u
#define SD_DATA1_PIN           5u
#define SD_DATA2_PIN           10u
#define SD_DATA3_PIN           11u
#define SD_CARD_DETECT_PIN     8u
#define SD_CARD_DETECT_ADDRESS (GPIO_BASE_ADDRESS + SD_CARD_DETECT_PIN * GPIO_REGISTER_STRIDE)

#define REG(address)        (*(volatile uint32_t *)(uintptr_t)(address))
#define SD(offset)          REG(SDIO_BASE_ADDRESS + (offset))
#define DMA(offset)         REG(GDMA_BASE_ADDRESS + (offset))
#define CH(channel, offset) DMA(DMA_CHANNEL_BASE_OFFSET + (channel) * DMA_CHANNEL_STRIDE + (offset))
#define TX_CHANNEL          2u
#define RX_CHANNEL          3u
#define CMD_END             3u
#define CMD_TIMEOUT         (1u << 2)
#define CMD_CRC_FAIL        (1u << 11)
#define TX_EMPTY            (1u << 9)
#define DATA_END            ((1u << 3) | (1u << 4))
#define DATA_ERROR          ((1u << 5) | (1u << 8) | (1u << 13))
#define DMA_FINISH          (1u << 18)
#define DMA_ERROR           (1u << 20)
#define DMA_FLAGS           ((1u << 18) | (1u << 19) | (1u << 20))
#define R1_ERRORS           0xfdffe008u
#define FIFO_RESET          ((1u << 16) | (1u << 17) | (1u << 20))
#define COMMAND_MS          100u
#define TRANSFER_MS         2000u

#define SD_RX_FIFO_RESET_RELEASE    (1u << 16)
#define SD_TX_FIFO_RESET_RELEASE    (1u << 17)
#define SD_DATA_STATE_RESET_RELEASE (1u << 20)
#define SD_TX_FIFO_CLOCK_GATE       (1u << 13)
#define SD_CLOCK_ALWAYS_ON          (1u << 27)

// Register field encodings used by the controller setup.
#define SD_COMMAND_TIMER_LIMIT             0x00ffffffu
#define SD_DATA_TIMER_LIMIT                0x0fffffffu
#define SD_FIFO_THRESHOLD_MASK             0xffffu
#define SD_FIFO_RX_THRESHOLD_ONE           1u
#define SD_FIFO_TX_THRESHOLD_ONE           (1u << 8)
#define SD_FIFO_DEFAULT_THRESHOLDS         (SD_FIFO_RX_THRESHOLD_ONE | SD_FIFO_TX_THRESHOLD_ONE)
#define DMA_SOURCE_LOOP_ENABLE             (1u << 10)
#define DMA_DESTINATION_LOOP_ENABLE        (1u << 11)
#define DMA_SOURCE_INCREMENT               (1u << 8)
#define DMA_DESTINATION_INCREMENT          (1u << 9)
#define DMA_SOURCE_WIDTH_32                (2u << 4)
#define DMA_DESTINATION_WIDTH_32           (2u << 6)
#define DMA_TRANSFER_LENGTH_SHIFT          16u
#define DMA_ENABLE                         (1u << 0)
#define DMA_FINISH_INTERRUPT_ENABLE        (1u << 1)
#define PERIPHERAL_RESET_RELEASE           (1u << 0)
#define PERIPHERAL_CLOCK_GATE_BYPASS       (1u << 1)
#define DMA_REQUEST_SD_TX                  0x0bu
#define DMA_REQUEST_SD_RX                  0x0cu
#define DMA_DESTINATION_REQUEST_SHIFT      5u
#define DMA_REQUEST_SECURE_SOURCE          (1u << 20)
#define DMA_REQUEST_SECURE_DESTINATION     (1u << 21)
#define DMA_REQUEST_BUS_ERROR_INTERRUPT    (1u << 22)
#define SD_GPIO_CONFIG_MASK                0x3fffu
#define SD_GPIO_INPUT_ENABLE               (1u << 2)
#define SD_GPIO_OUTPUT_DISABLE             (1u << 3)
#define SD_GPIO_PULL_UP                    (3u << 4)
#define SD_GPIO_PERIPHERAL_FUNCTION        (1u << 6)
#define SD_GPIO_DRIVE_STRENGTH_MAX         (3u << 8)
#define SD_GPIO_INPUT_LEVEL                (1u << 0)
#define SYSTEM_CPU1_SDIO_INTERRUPT         (1u << 10)
#define SYSTEM_CPU1_GDMA_INTERRUPT         (1u << 11)
#define SYSTEM_SDIO_CLOCK_FIELD_SHIFT      14u
#define SYSTEM_SDIO_CLOCK_FIELD_MASK       (15u << SYSTEM_SDIO_CLOCK_FIELD_SHIFT)
#define SYSTEM_SDIO_CLOCK_XTAL_DIV256      (7u << SYSTEM_SDIO_CLOCK_FIELD_SHIFT)
#define SYSTEM_SDIO_CLOCK_PLL_DIV16        (14u << SYSTEM_SDIO_CLOCK_FIELD_SHIFT)
#define SD_INTERFACE_CONDITION_3V3_PATTERN 0x1aau
#define SD_INTERFACE_CONDITION_MASK        0xfffu
#define SD_OCR_VOLTAGE_2V7_TO_3V6          0x00ff8000u
#define SD_R6_ERROR_MASK                   0xe000u
#define SD_RELATIVE_CARD_ADDRESS_MASK      0xffff0000u

PBL_LOG_MODULE_DEFINE(driver_sd_t5ai, LOG_LEVEL_DEBUG);
static unsigned s_last_command;
static PBL_MUTEX_DEFINE(s_lock);
static PBL_SEM_DEFINE(s_event, 0, 1);
static volatile uint32_t s_command_status, s_data_status, s_dma_status;
static bool s_ready, s_faulted, s_high_capacity;
static uint32_t s_rca;
static uint64_t s_sectors;
static uint8_t s_bounce[T5AI_SD_MAX_BLOCKS * T5AI_SD_SECTOR_SIZE]
    __attribute__((section(".t5ai_board_cache"), aligned(SD_DMA_CACHE_ALIGNMENT)));

void t5ai_sd_irq(void) {
  const uint32_t status = SD(SD_INTERRUPT_STATUS);
  const uint32_t pending = status & SD(SD_INTERRUPT_MASK) &
                           (CMD_END | CMD_TIMEOUT | DATA_END | TX_EMPTY | (1u << 5) | (1u << 8));
  if (!pending) {
    return;
  }
  if (pending & (CMD_END | CMD_TIMEOUT)) {
    s_command_status |= status & (CMD_END | CMD_TIMEOUT | CMD_CRC_FAIL);
  }
  s_data_status |= pending & (DATA_END | TX_EMPTY | DATA_ERROR);
  if (pending & DATA_END) {
    s_data_status |= status & DATA_ERROR;
  }
  if (pending & TX_EMPTY)
    SD(SD_INTERRUPT_MASK) &= ~TX_EMPTY;
  // Acknowledge only this peripheral's captured W1C flags.
  SD(SD_INTERRUPT_STATUS) = status;
  pbl_sem_give(&s_event);
}

void t5ai_sd_dma_irq(void) {
  for (unsigned channel = TX_CHANNEL; channel <= RX_CHANNEL; ++channel) {
    const uint32_t status = CH(channel, DMA_CHANNEL_STATUS) & DMA_FLAGS;
    if (status) {
      CH(channel, DMA_CHANNEL_STATUS) = status;
      s_dma_status |= status;
      if (channel == TX_CHANNEL && (status & DMA_FINISH))
        SD(SD_INTERRUPT_MASK) |= TX_EMPTY;
      pbl_sem_give(&s_event);
    }
  }
}

static void prv_reset_data(void) {
  SD(SD_DATA_CONTROL) = 0;
  SD(SD_FIFO_CONTROL) &= ~FIFO_RESET;
}

static void prv_abort(void) {
  CH(TX_CHANNEL, DMA_CHANNEL_CONTROL) = 0;
  CH(RX_CHANNEL, DMA_CHANNEL_CONTROL) = 0;
  __DSB();
  (void)CH(TX_CHANNEL, DMA_CHANNEL_CONTROL);
  (void)CH(RX_CHANNEL, DMA_CHANNEL_CONTROL);
  SD(SD_INTERRUPT_MASK) = 0;
  prv_reset_data();
  CH(TX_CHANNEL, DMA_CHANNEL_STATUS) = DMA_FLAGS;
  CH(RX_CHANNEL, DMA_CHANNEL_STATUS) = DMA_FLAGS;
  SD(SD_INTERRUPT_STATUS) = UINT32_MAX;
}

static int prv_command(unsigned cmd, uint32_t arg, unsigned response, bool crc, uint32_t *out) {
  // Preserve data flags while issuing CMD12/CMD13 during a transfer.
  pbl_irq_lock();
  s_last_command = cmd;
  s_command_status = 0;
  SD(SD_INTERRUPT_STATUS) = CMD_END | CMD_TIMEOUT | CMD_CRC_FAIL | (1u << 10);
  SD(SD_COMMAND_ARGUMENT) = arg;
  SD(SD_COMMAND_TIMER) = SD_COMMAND_TIMER_LIMIT;
  SD(SD_INTERRUPT_MASK) |= CMD_END | CMD_TIMEOUT;
  SD(SD_COMMAND_CONTROL) =
      (cmd << 4) | (response ? 2u : 0u) | (response == 2 ? 4u : 0u) | (crc ? 8u : 0u) | 1u;
  pbl_irq_unlock();
  const uint64_t deadline = pbl_uptime_ms() + COMMAND_MS;
  while (!(s_command_status & (CMD_END | CMD_TIMEOUT))) {
    if (pbl_uptime_ms() >= deadline) {
      return -ETIMEDOUT;
    }
    (void)pbl_sem_take(&s_event, PBL_MSEC(1));
  }
  if (s_command_status & CMD_TIMEOUT) {
    return -ETIMEDOUT;
  }
  if (crc && (s_command_status & CMD_CRC_FAIL)) {
    return -EILSEQ;
  }
  if (out) {
    *out = SD(SD_RESPONSE_ARGUMENT0);
  }
  return 0;
}

static int prv_r1(unsigned cmd, uint32_t arg) {
  uint32_t response;
  int status = prv_command(cmd, arg, 1, true, &response);
  return status ? status : (response & R1_ERRORS) ? -EIO : 0;
}

static int prv_wait_ready(void) {
  const uint64_t deadline = pbl_uptime_ms() + TRANSFER_MS;
  do {
    uint32_t response;
    const int status = prv_command(13, s_rca, 1, true, &response);
    if (status || (response & R1_ERRORS)) {
      return status ? status : -EIO;
    }
    if ((response & (1u << 8)) && ((response >> 9) & 15u) == 4u) {
      return 0;
    }
    pbl_thread_sleep(PBL_MSEC(1));
  } while (pbl_uptime_ms() < deadline);
  return -ETIMEDOUT;
}

static bool prv_direct_buffer(const void *buffer, size_t bytes) {
  uintptr_t address = (uintptr_t)buffer;
  return !(address & SD_DMA_CACHE_ALIGNMENT_MASK) && !(bytes & SD_DMA_CACHE_ALIGNMENT_MASK) &&
         ((address >= SD_AP_SRAM_BEGIN && address < SD_AP_SRAM_END &&
           bytes <= SD_AP_SRAM_END - address) ||
          (address >= SD_PRIVATE_PSRAM_BEGIN && address < SD_PRIVATE_PSRAM_END &&
           bytes <= SD_PRIVATE_PSRAM_END - address));
}

static void prv_dma_start(unsigned channel, void *buffer, size_t bytes, bool write) {
  const uint32_t memory = (uintptr_t)buffer;
  const uint32_t fifo = SDIO_BASE_ADDRESS + (write ? SD_TX_FIFO : SD_RX_FIFO);
  CH(channel, DMA_CHANNEL_CONTROL) = 0;
  CH(channel, DMA_CHANNEL_DESTINATION) = write ? fifo : memory;
  CH(channel, DMA_CHANNEL_SOURCE) = write ? memory : fifo;
  CH(channel, DMA_CHANNEL_DESTINATION_LOOP_END) = write ? fifo + sizeof(uint32_t) : memory + bytes;
  CH(channel, DMA_CHANNEL_DESTINATION_LOOP_START) = write ? fifo : memory;
  CH(channel, DMA_CHANNEL_SOURCE_LOOP_END) = write ? memory + bytes : fifo + sizeof(uint32_t);
  CH(channel, DMA_CHANNEL_SOURCE_LOOP_START) = write ? memory : fifo;
  CH(channel, DMA_CHANNEL_REQUEST) =
      (write ? DMA_REQUEST_SD_TX << DMA_DESTINATION_REQUEST_SHIFT : DMA_REQUEST_SD_RX) |
      DMA_REQUEST_SECURE_SOURCE | DMA_REQUEST_SECURE_DESTINATION | DMA_REQUEST_BUS_ERROR_INTERRUPT;
  CH(channel, DMA_CHANNEL_STATUS) = DMA_FLAGS;
  if (write) {
    dcache_flush(buffer, bytes);
  } else {
    dcache_flush_invalidate(buffer, bytes);
  }
  __DSB();
  CH(channel, DMA_CHANNEL_CONTROL) =
      ((bytes - 1u) << DMA_TRANSFER_LENGTH_SHIFT) | DMA_SOURCE_WIDTH_32 | DMA_DESTINATION_WIDTH_32 |
      (write ? DMA_SOURCE_INCREMENT : DMA_DESTINATION_INCREMENT) | DMA_SOURCE_LOOP_ENABLE |
      DMA_DESTINATION_LOOP_ENABLE | DMA_ENABLE | DMA_FINISH_INTERRUPT_ENABLE;
}

static int prv_transfer(uint32_t sector, void *buffer, uint32_t count, bool write) {
  if (!buffer || !count || count > T5AI_SD_MAX_BLOCKS || sector >= s_sectors ||
      count > s_sectors - sector || (!s_high_capacity && sector > UINT32_MAX / 512u)) {
    return -EINVAL;
  }
  if (!s_ready || s_faulted) {
    return -ENODEV;
  }
  const size_t bytes = count * 512u;
  void *dma_buffer = prv_direct_buffer(buffer, bytes) ? buffer : s_bounce;
  if (write && dma_buffer != buffer) {
    memcpy(dma_buffer, buffer, bytes);
  }
  int status = prv_wait_ready();
  if (status) {
    goto fail;
  }
  prv_reset_data();
  pbl_irq_lock();
  s_data_status = s_dma_status = 0;
  SD(SD_INTERRUPT_STATUS) = UINT32_MAX;
  pbl_sem_reset(&s_event);
  pbl_irq_unlock();
  SD(SD_FIFO_CONTROL) =
      (SD(SD_FIFO_CONTROL) & ~SD_FIFO_THRESHOLD_MASK) | SD_FIFO_DEFAULT_THRESHOLDS;
  SD(SD_DATA_TIMER) = SD_DATA_TIMER_LIMIT;
  const uint32_t data_control = (512u << 4) | (1u << 17) | DATA_WIDTH | ((count > 1 || write) ? 8u : 0u);
  SD(SD_INTERRUPT_MASK) = CMD_END | CMD_TIMEOUT | DATA_END | (1u << 5) | (1u << 8);
  const unsigned channel = write ? TX_CHANNEL : RX_CHANNEL;
  const uint32_t argument = s_high_capacity ? sector : sector * 512u;
  if (!write) {
    SD(SD_FIFO_CONTROL) |= SD_RX_FIFO_RESET_RELEASE | SD_DATA_STATE_RESET_RELEASE;
    SD(SD_DATA_CONTROL) = data_control;
    prv_dma_start(channel, dma_buffer, bytes, false);
    SD(SD_DATA_CONTROL) |= 1u;
  }
  // The vendor uses CMD25 even for one sector; terminate every write with CMD12.
  status = prv_r1(write ? 25 : (count > 1 ? 18 : 17), argument);
  if (status) {
    goto fail;
  }
  if (write) {
    // The BK7258 gates the write clock until FIFO/data setup. Send CMD24/25
    // while the data state is reset, then release it as in the vendor driver.
    SD(SD_FIFO_CONTROL) |= SD_TX_FIFO_RESET_RELEASE | SD_DATA_STATE_RESET_RELEASE;
    SD(SD_DATA_CONTROL) = data_control;
    prv_dma_start(channel, dma_buffer, bytes, true);
    SD(SD_INTERRUPT_MASK) |= SD_TX_FIFO_CLOCK_GATE; // Gate SD clock when transmit FIFO needs data.
    SD(SD_DATA_CONTROL) |= 1u << 16;
  }
  const uint64_t deadline = pbl_uptime_ms() + TRANSFER_MS;
  while (!(s_dma_status & DMA_FINISH) || !(s_data_status & (write ? TX_EMPTY : (1u << 3)))) {
    if ((s_dma_status & DMA_ERROR) || (s_data_status & DATA_ERROR)) {
      status = -EIO;
      goto fail;
    }
    if (pbl_uptime_ms() >= deadline) {
      status = -ETIMEDOUT;
      goto fail;
    }
    (void)pbl_sem_take(&s_event, PBL_MSEC(1));
  }
  if ((s_dma_status & DMA_ERROR) || (s_data_status & DATA_ERROR)) {
    status = -EIO;
    goto fail;
  }
  CH(channel, DMA_CHANNEL_CONTROL) = 0;
  __DSB();
  if (count > 1 || write) {
    // The vendor requires FIFO/state reset before CMD12 to stop read-ahead.
    prv_reset_data();
    SD(SD_FIFO_CONTROL) |= SD_CLOCK_ALWAYS_ON;
    status = prv_r1(12, 0);
  }
  // Release FIFO clock gating after CMD12 before status polling, matching
  // the vendor stop sequence. An empty TX FIFO otherwise stalls CMD13.
  SD(SD_INTERRUPT_MASK) &= ~SD_TX_FIFO_CLOCK_GATE;
  SD(SD_FIFO_CONTROL) &= ~SD_CLOCK_ALWAYS_ON;
  if (count > 1 || write)
    prv_reset_data();
  if (!status) {
    status = prv_wait_ready();
  }
  if (status) {
    goto fail;
  }
  SD(SD_INTERRUPT_MASK) = 0;
  if (!write) {
    dcache_invalidate(dma_buffer, bytes);
    if (dma_buffer != buffer) {
      memcpy(buffer, dma_buffer, bytes);
    }
  }
  return 0;
fail:
  PBL_LOG_ERR("SD I/O failed: write=%u sector=%lu count=%lu error=%d cmd=%u sd=%08lx dma=%08lx",
              write, (unsigned long)sector, (unsigned long)count, status, s_last_command,
              (unsigned long)SD(SD_INTERRUPT_STATUS), (unsigned long)s_dma_status);
  prv_abort();
  s_faulted = true;
  return status;
}

int t5ai_sd_read(uint32_t sector, void *buffer, uint32_t count) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  const int result = prv_transfer(sector, buffer, count, false);
  pbl_mutex_unlock(&s_lock);
  return result;
}

int t5ai_sd_write(uint32_t sector, const void *buffer, uint32_t count) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  const int result = prv_transfer(sector, (void *)buffer, count, true);
  pbl_mutex_unlock(&s_lock);
  return result;
}

uint8_t *t5ai_sd_lock_workspace(void) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  return s_bounce;
}

void t5ai_sd_unlock_workspace(void) {
  pbl_mutex_unlock(&s_lock);
}

uint64_t t5ai_sd_sector_count(void) {
  return s_sectors;
}

int t5ai_sd_init(void) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  int status = 0;
  if (s_ready) {
    status = s_faulted ? -EIO : 0;
    goto done;
  }
  // Socket switch is active-low; presence is not proof of a readable card.
  REG(SD_CARD_DETECT_ADDRESS) = (REG(SD_CARD_DETECT_ADDRESS) & ~SD_GPIO_CONFIG_MASK) |
                                SD_GPIO_INPUT_ENABLE | SD_GPIO_OUTPUT_DISABLE | SD_GPIO_PULL_UP;
  delay_us(5000);
  if (REG(SD_CARD_DETECT_ADDRESS) & SD_GPIO_INPUT_LEVEL) {
    status = -ENODEV;
    goto done;
  }
  // Existing CP PM service owns domain voting; sector traffic stays on the AP.
  status = t5ai_cp_mailbox_enable_sd();
  if (status) {
    goto done;
  }
  // Waveshare: CLK/CMD/D0/D1/D2/D3 = P2/P3/P4/P5/P10/P11; CD = P8.
  const unsigned pins[] = {
    SD_CLOCK_PIN, SD_COMMAND_PIN, SD_DATA0_PIN,
#ifdef CONFIG_T5AI_SD_4BIT
    SD_DATA1_PIN, SD_DATA2_PIN,   SD_DATA3_PIN,
#endif
  };
  pbl_irq_lock();
  for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
    const unsigned pin = pins[i],
                   shift = (pin % GPIO_FUNCTION_PINS_PER_REGISTER) * GPIO_FUNCTION_FIELD_BITS;
    const uintptr_t mux = SYSTEM_GPIO_FUNCTION_BASE_ADDRESS +
                          (pin / GPIO_FUNCTION_PINS_PER_REGISTER) * sizeof(uint32_t);
    REG(mux) =
        (REG(mux) & ~(GPIO_FUNCTION_FIELD_MASK << shift)) | (GPIO_FUNCTION_SDIO_GROUP0 << shift);
    REG(GPIO_BASE_ADDRESS + pin * GPIO_REGISTER_STRIDE) = SD_GPIO_OUTPUT_DISABLE | SD_GPIO_PULL_UP |
                                                          SD_GPIO_PERIPHERAL_FUNCTION |
                                                          SD_GPIO_DRIVE_STRENGTH_MAX;
  }
  // Preserve other DMA users, including CP logical channels 12..15.
  DMA(DMA_GLOBAL_CONTROL) |= PERIPHERAL_RESET_RELEASE | PERIPHERAL_CLOCK_GATE_BYPASS;
  DMA(DMA_SECURE_ATTRIBUTE) |= (1u << TX_CHANNEL) | (1u << RX_CHANNEL);
  DMA(DMA_PRIVILEGED_ATTRIBUTE) |= (1u << TX_CHANNEL) | (1u << RX_CHANNEL);
  DMA(DMA_INTERRUPT_ALLOCATION) &= ~((1u << TX_CHANNEL) | (1u << RX_CHANNEL));
  REG(SYSTEM_CPU1_INTERRUPT_ENABLE_ADDRESS) |=
      SYSTEM_CPU1_SDIO_INTERRUPT | SYSTEM_CPU1_GDMA_INTERRUPT;
  // 26 MHz source / 256 for identification; preserve unrelated clock fields.
  REG(SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS) =
      (REG(SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS) & ~SYSTEM_SDIO_CLOCK_FIELD_MASK) |
      SYSTEM_SDIO_CLOCK_XTAL_DIV256;
  pbl_irq_unlock();
  SD(SD_GLOBAL_CONTROL) = PERIPHERAL_RESET_RELEASE | PERIPHERAL_CLOCK_GATE_BYPASS;
  prv_abort();
  SD(SD_FIFO_CONTROL) = SD_FIFO_DEFAULT_THRESHOLDS | FIFO_RESET | (1u << 25) | (1u << 27);
  NVIC_SetPriority(SDIO_IRQn, 6u);
  NVIC_EnableIRQ(SDIO_IRQn);
  NVIC_SetPriority(GDMA_IRQn, 6u);
  NVIC_EnableIRQ(GDMA_IRQn);
  delay_us(2000);
  status = prv_command(0, 0, 0, false, NULL);
  if (status)
    goto fail;
  uint32_t response;
  status = prv_command(8, SD_INTERFACE_CONDITION_3V3_PATTERN, 1, true, &response);
  const bool v2 = !status;
  if ((status && status != -ETIMEDOUT) ||
      (v2 && (response & SD_INTERFACE_CONDITION_MASK) != SD_INTERFACE_CONDITION_3V3_PATTERN)) {
    status = -ENOTSUP;
    goto fail;
  }
  const uint64_t deadline = pbl_uptime_ms() + 2000u;
  do {
    status = prv_r1(55, 0);
    if (!status)
      status =
          prv_command(41, SD_OCR_VOLTAGE_2V7_TO_3V6 | (v2 ? 1u << 30 : 0), 1, false, &response);
    if (status)
      goto fail;
    if (response & (1u << 31))
      break;
    pbl_thread_sleep(PBL_MSEC(10));
  } while (pbl_uptime_ms() < deadline);
  if (!(response & (1u << 31))) {
    status = -ETIMEDOUT;
    goto fail;
  }
  s_high_capacity = (response & (1u << 30)) != 0;
  status = prv_command(2, 0, 2, false, NULL);
  if (status)
    goto fail;
  status = prv_command(3, 0, 1, true, &response);
  if (status || (response & SD_R6_ERROR_MASK)) {
    status = status ? status : -EIO;
    goto fail;
  }
  s_rca = response & SD_RELATIVE_CARD_ADDRESS_MASK;
  if (!s_rca) {
    status = -EIO;
    goto fail;
  }
  status = prv_command(9, s_rca, 2, false, NULL);
  if (status)
    goto fail;
  const uint32_t csd0 = SD(SD_RESPONSE_ARGUMENT0), csd1 = SD(SD_RESPONSE_ARGUMENT1),
                 csd2 = SD(SD_RESPONSE_ARGUMENT2);
  if ((csd0 >> 30) == 1u && s_high_capacity) {
    s_sectors = ((uint64_t)(((csd1 & 63u) << 16) | (csd2 >> 16)) + 1u) * 1024u;
  } else if ((csd0 >> 30) == 0u && !s_high_capacity) {
    const unsigned block_bits = (csd1 >> 16) & 15u;
    const unsigned multiplier = (csd2 >> 15) & 7u;
    const uint32_t size = ((csd1 & 1023u) << 2) | (csd2 >> 30);
    s_sectors = ((uint64_t)size + 1u) * (1u << (multiplier + 2u)) * (1u << block_bits) / 512u;
  } else {
    status = -ENOTSUP;
    goto fail;
  }
  status = prv_r1(7, s_rca);
  if (!status)
    status = prv_wait_ready();
  if (!status && !s_high_capacity)
    status = prv_r1(16, 512);
  if (!status)
    status = prv_r1(55, s_rca);
  if (!status)
    status = prv_r1(6, DATA_WIDTH ? 2u : 0u); // ACMD6: selected data width.
  if (status)
    goto fail;
  // Default-speed cards permit 20 MHz without a high-speed function switch.
  REG(SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS) =
      (REG(SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS) & ~SYSTEM_SDIO_CLOCK_FIELD_MASK) |
      SYSTEM_SDIO_CLOCK_PLL_DIV16;
  SD(SD_FIFO_CONTROL) &= ~(1u << 27);
  SD(SD_INTERRUPT_MASK) = 0;
  s_ready = true;
  PBL_LOG_INFO("SD ready: sectors=%lu width=%u", (unsigned long)s_sectors, DATA_WIDTH ? 4u : 1u);
  goto done;
fail:
  prv_abort();
  s_faulted = true;
done:
  pbl_mutex_unlock(&s_lock);
  return status;
}
