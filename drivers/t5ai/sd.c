/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
#include <pbl/soc/t5ai/sleep.h>
#endif

#include <errno.h>
#include <string.h>

#include <pbl/drivers/dma/t5ai.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/pinctrl.h>
#include <pbl/drivers/t5ai/sd.h>
#include <pbl/kernel/cache.h>
#include <pbl/kernel/kernel.h>
#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/core.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/soc/t5ai/memory.h>

#ifdef CONFIG_T5AI_SD_4BIT
#define DATA_WIDTH (1u << 2)
#else
#define DATA_WIDTH 0u
#endif

// BK7258 AP byte addresses/offsets from the pinned Tuya BSP.
#define SDIO_BASE_ADDRESS                    0x458d0000u
#define SYSTEM_CPU1_INTERRUPT_ENABLE_ADDRESS 0x44010088u
#define SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS    0x44010024u
#define SYSTEM_CLOCK_MODE_ADDRESS            0x44010020u
#define SYSTEM_CPU1_CLOCK_ADDRESS            0x44010014u

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

// AP kernel/worker/application SRAM; excludes CP memory and IPC alias windows.
#define SD_AP_SRAM_BEGIN            0x28010000u
#define SD_AP_SRAM_END              0x28066000u
#define SD_DMA_CACHE_ALIGNMENT      32u
#define SD_DMA_CACHE_ALIGNMENT_MASK (SD_DMA_CACHE_ALIGNMENT - 1u)
#define SD_PRIVATE_PSRAM_BEGIN      T5AI_DISPLAY_PSRAM_BASE
#define SD_PRIVATE_PSRAM_END        (T5AI_DISPLAY_PSRAM_BASE + T5AI_DISPLAY_PSRAM_SIZE)

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define SD(offset)   REG(SDIO_BASE_ADDRESS + (offset))
#define TX_CHANNEL   2u
#define RX_CHANNEL   3u
#define CMD_END      3u
#define CMD_TIMEOUT  (1u << 2)
#define CMD_CRC_FAIL (1u << 11)
#define TX_EMPTY     (1u << 9)
#define DATA_END     ((1u << 3) | (1u << 4))
#define DATA_ERROR   ((1u << 5) | (1u << 8) | (1u << 13))
#define DMA_FINISH   (1u << 18)
#define DMA_ERROR    (1u << 20)
#define R1_ERRORS    0xfdffe008u
#define FIFO_RESET   ((1u << 16) | (1u << 17) | (1u << 20))
#define COMMAND_MS   100u
#define TRANSFER_MS  2000u

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
#define PERIPHERAL_RESET_RELEASE           (1u << 0)
#define PERIPHERAL_CLOCK_GATE_BYPASS       (1u << 1)
#define SYSTEM_CPU1_SDIO_INTERRUPT         (1u << 10)
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
extern const struct pbl_pinctrl T5AI_SD_PINCTRL;
extern const struct pbl_gpio T5AI_SD_DETECT;
static const struct pbl_dma_chan s_tx_dma = PBL_DMA_CHAN(&T5AI_DMA, 2, 0x0b, 0);
static const struct pbl_dma_chan s_rx_dma = PBL_DMA_CHAN(&T5AI_DMA, 3, 0x0c, 0);
static unsigned s_last_command;
static PBL_MUTEX_DEFINE(s_lock);
static PBL_SEM_DEFINE(s_event, 0, 1);
static volatile uint32_t s_command_status, s_data_status, s_dma_status;
static bool s_ready, s_faulted, s_high_capacity;
#ifdef CONFIG_T5AI_POWER_PROFILE
static T5AISDPowerStats s_power_stats;
static uint32_t s_setup_mask_before, s_setup_mask_after;
static uint32_t s_dma_irq_sd, s_dma_irq_mask, s_sd_irq_status, s_sd_irq_mask;
#define SD_POWER_ADD(field, value) (s_power_stats.field += (value))
#else
#define SD_POWER_ADD(field, value) ((void)0)
#endif
#ifdef CONFIG_T5AI_SD_IDLE_GATING
static volatile bool s_clock_active;

static void prv_clock_acquire(void) {
  pbl_irq_lock();
  if (!s_clock_active) {
    SD(SD_GLOBAL_CONTROL) |= PERIPHERAL_CLOCK_GATE_BYPASS;
    __DSB();
    (void)SD(SD_GLOBAL_CONTROL);
    delay_us(10u);
    SD(SD_INTERRUPT_MASK) = 0;
    SD(SD_INTERRUPT_STATUS) = UINT32_MAX;
    NVIC_ClearPendingIRQ(SDIO_IRQn);
    s_clock_active = true;
    SD_POWER_ADD(acquires, 1u);
    NVIC_EnableIRQ(SDIO_IRQn);
  }
  pbl_irq_unlock();
}

static void prv_clock_release(void);
#endif
static uint32_t s_rca;
static uint64_t s_sectors;
static uint8_t s_bounce[T5AI_SD_MAX_BLOCKS * T5AI_SD_SECTOR_SIZE]
    __attribute__((section(".t5ai_board_cache"), aligned(SD_DMA_CACHE_ALIGNMENT)));

void t5ai_sd_irq(void) {
#ifdef CONFIG_T5AI_SD_IDLE_GATING
  if (!s_clock_active)
    return;
#endif
  const uint32_t status = SD(SD_INTERRUPT_STATUS);
  const uint32_t pending = status & SD(SD_INTERRUPT_MASK) &
                           (CMD_END | CMD_TIMEOUT | DATA_END | TX_EMPTY | (1u << 5) | (1u << 8));
  if (!pending) {
    return;
  }
#ifdef CONFIG_T5AI_POWER_PROFILE
  s_sd_irq_status = status;
  s_sd_irq_mask = SD(SD_INTERRUPT_MASK);
#endif
  if (pending & (CMD_END | CMD_TIMEOUT)) {
    s_command_status |= status & (CMD_END | CMD_TIMEOUT | CMD_CRC_FAIL);
  }
  s_data_status |= pending & (DATA_END | DATA_ERROR);
  if (pending & DATA_END) {
    s_data_status |= status & DATA_ERROR;
  }
  // SD completion may precede the DMA ISR that unmasks TX_EMPTY. Preserve
  // the latched final empty event before W1C, but ignore gaps during DMA.
  size_t residue = 1;
  const bool tx_finished = (s_dma_status & DMA_FINISH) != 0u ||
                           (pbl_dma_tx_status(&s_tx_dma, &residue) == 0 && residue == 0);
  if ((status & TX_EMPTY) && tx_finished) {
    s_data_status |= TX_EMPTY;
  }
  if (pending & TX_EMPTY)
    SD(SD_INTERRUPT_MASK) &= ~TX_EMPTY;
  // Acknowledge only this peripheral's captured W1C flags.
  SD(SD_INTERRUPT_STATUS) = status;
  pbl_sem_give(&s_event);
}

static void prv_dma_done(void *arg, int status) {
#ifdef CONFIG_T5AI_SD_IDLE_GATING
  if (!s_clock_active)
    return;
#endif
  const bool write = (uintptr_t)arg != 0;
  s_dma_status |= status == 0 ? DMA_FINISH : DMA_ERROR;
  if (write && status == 0) {
    // Preserve a final empty event even when SDIO ran before the DMA callback.
    if (SD(SD_INTERRUPT_STATUS) & TX_EMPTY)
      s_data_status |= TX_EMPTY;
    SD(SD_INTERRUPT_MASK) |= TX_EMPTY;
#ifdef CONFIG_T5AI_POWER_PROFILE
    s_dma_irq_sd = SD(SD_INTERRUPT_STATUS);
    s_dma_irq_mask = SD(SD_INTERRUPT_MASK);
#endif
  }
  pbl_sem_give(&s_event);
}

static void prv_reset_data(void) {
  SD(SD_DATA_CONTROL) = 0;
  SD(SD_FIFO_CONTROL) &= ~FIFO_RESET;
}

#ifdef CONFIG_T5AI_SD_IDLE_GATING
// The caller has verified card-ready and holds s_lock; shared clocks stay voted on.
static void prv_clock_release(void) {
  size_t tx_residue, rx_residue;
  pbl_irq_lock();
  if (s_clock_active && !(SD(SD_COMMAND_CONTROL) & 1u) &&
      pbl_dma_tx_status(&s_tx_dma, &tx_residue) == 0 && !tx_residue &&
      pbl_dma_tx_status(&s_rx_dma, &rx_residue) == 0 && !rx_residue) {
    SD(SD_INTERRUPT_MASK) = 0;
    NVIC_DisableIRQ(SDIO_IRQn);
    prv_reset_data();
    SD(SD_FIFO_CONTROL) &= ~SD_CLOCK_ALWAYS_ON;
    SD(SD_INTERRUPT_STATUS) = UINT32_MAX;
    NVIC_ClearPendingIRQ(SDIO_IRQn);
    __DSB();
    s_clock_active = false;
    SD(SD_GLOBAL_CONTROL) &= ~PERIPHERAL_CLOCK_GATE_BYPASS;
    __DSB();
    SD_POWER_ADD(releases, 1u);
  } else if (s_clock_active) {
    SD_POWER_ADD(fallbacks, 1u);
  }
  pbl_irq_unlock();
}
#endif

static void prv_abort(void) {
  (void)pbl_dma_terminate(&s_tx_dma);
  (void)pbl_dma_terminate(&s_rx_dma);
  SD(SD_INTERRUPT_MASK) = 0;
  prv_reset_data();
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

static int prv_dma_start(unsigned channel, void *buffer, size_t bytes, bool write) {
  (void)channel;
  const struct pbl_dma_chan *dma = write ? &s_tx_dma : &s_rx_dma;
  const struct pbl_dma_slave_config cfg = {
    .src_addr = SDIO_BASE_ADDRESS + SD_RX_FIFO,
    .dst_addr = SDIO_BASE_ADDRESS + SD_TX_FIFO,
    .src_addr_width = PBL_DMA_SLAVE_BUSWIDTH_4_BYTES,
    .dst_addr_width = PBL_DMA_SLAVE_BUSWIDTH_4_BYTES,
  };
  int ret = pbl_dma_slave_config(dma, &cfg);
  if (ret != 0)
    return ret;
  if (write)
    pbl_dcache_flush_range(buffer, bytes);
  else
    pbl_dcache_flush_invalidate_range(buffer, bytes);
  return pbl_dma_prep_slave_single(dma, buffer, bytes,
                                   write ? PBL_DMA_MEM_TO_DEV : PBL_DMA_DEV_TO_MEM, prv_dma_done,
                                   (void *)(uintptr_t)write);
}

static int prv_transfer(uint32_t sector, void *buffer, uint32_t count, bool write) {
  if (!buffer || !count || count > T5AI_SD_MAX_BLOCKS || sector >= s_sectors ||
      count > s_sectors - sector || (!s_high_capacity && sector > UINT32_MAX / 512u)) {
    return -EINVAL;
  }
  if (!s_ready || s_faulted) {
    return -ENODEV;
  }
#ifdef CONFIG_T5AI_SD_IDLE_GATING
  prv_clock_acquire();
#endif
  const size_t bytes = count * 512u;
  void *dma_buffer = prv_direct_buffer(buffer, bytes) ? buffer : s_bounce;
  if (write && dma_buffer != buffer) {
    memcpy(dma_buffer, buffer, bytes);
  }
#ifdef CONFIG_T5AI_POWER_PROFILE
  pbl_irq_lock();
  s_setup_mask_before = s_setup_mask_after = 0;
  s_dma_irq_sd = s_dma_irq_mask = s_sd_irq_status = s_sd_irq_mask = 0;
  pbl_irq_unlock();
#endif
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
  const uint32_t data_control =
      (512u << 4) | (1u << 17) | DATA_WIDTH | ((count > 1 || write) ? 8u : 0u);
  SD(SD_INTERRUPT_MASK) = CMD_END | CMD_TIMEOUT | DATA_END | (1u << 5) | (1u << 8);
  const unsigned channel = write ? TX_CHANNEL : RX_CHANNEL;
  const uint32_t argument = s_high_capacity ? sector : sector * 512u;
  if (!write) {
    SD(SD_FIFO_CONTROL) |= SD_RX_FIFO_RESET_RELEASE | SD_DATA_STATE_RESET_RELEASE;
    SD(SD_DATA_CONTROL) = data_control;
    status = prv_dma_start(channel, dma_buffer, bytes, false);
    if (status != 0)
      goto fail;
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
    status = prv_dma_start(channel, dma_buffer, bytes, true);
    if (status != 0)
      goto fail;
    // The DMA callback also updates this mask; exclude it across the RMW.
    pbl_irq_lock();
#ifdef CONFIG_T5AI_POWER_PROFILE
    s_setup_mask_before = SD(SD_INTERRUPT_MASK);
#endif
    SD(SD_INTERRUPT_MASK) |= SD_TX_FIFO_CLOCK_GATE; // Gate SD clock when transmit FIFO needs data.
    SD(SD_DATA_CONTROL) |= 1u << 16;
#ifdef CONFIG_T5AI_POWER_PROFILE
    s_setup_mask_after = SD(SD_INTERRUPT_MASK);
#endif
    pbl_irq_unlock();
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
  (void)pbl_dma_terminate(write ? &s_tx_dma : &s_rx_dma);
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
#ifdef CONFIG_T5AI_SD_IDLE_GATING
  prv_clock_release();
#endif
#ifdef CONFIG_T5AI_POWER_PROFILE
  pbl_irq_lock();
  if (write)
    SD_POWER_ADD(written_bytes, bytes);
  else
    SD_POWER_ADD(read_bytes, bytes);
  pbl_irq_unlock();
#endif
  if (!write) {
    pbl_dcache_invalidate_range(dma_buffer, bytes);
    if (dma_buffer != buffer) {
      memcpy(buffer, dma_buffer, bytes);
    }
  }
  return 0;
fail:
#ifdef CONFIG_T5AI_POWER_PROFILE
  pbl_irq_lock();
  const uint32_t failed_sd_status = SD(SD_INTERRUPT_STATUS);
  const uint32_t failed_dma_status = s_dma_status;
  const uint32_t failed_setup_before = s_setup_mask_before, failed_setup_after = s_setup_mask_after;
  const uint32_t failed_dma_sd = s_dma_irq_sd, failed_dma_mask = s_dma_irq_mask;
  const uint32_t failed_irq_sd = s_sd_irq_status, failed_irq_mask = s_sd_irq_mask;
  const uint32_t failed_cpu_mode = REG(SYSTEM_CLOCK_MODE_ADDRESS),
                 failed_cpu1 = REG(SYSTEM_CPU1_CLOCK_ADDRESS);
  const uint32_t failed_sd_clock = REG(SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS);
  const uint32_t failed_command_status = s_command_status;
  const uint32_t failed_data_status = s_data_status;
  const uint32_t failed_mask = SD(SD_INTERRUPT_MASK);
  const uint32_t failed_control = SD(SD_DATA_CONTROL);
  const uint32_t failed_fifo = SD(SD_FIFO_CONTROL);
  size_t failed_residue = 0;
  const int failed_dma = pbl_dma_tx_status(write ? &s_tx_dma : &s_rx_dma, &failed_residue);
  pbl_irq_unlock();
#endif
  SD_POWER_ADD(errors, 1u);
  PBL_LOG_ERR("SD I/O failed: write=%u sector=%lu count=%lu error=%d cmd=%u sd=%08lx dma=%08lx",
              write, (unsigned long)sector, (unsigned long)count, status, s_last_command,
#ifdef CONFIG_T5AI_POWER_PROFILE
              (unsigned long)failed_sd_status, (unsigned long)failed_dma_status);
#else
              (unsigned long)SD(SD_INTERRUPT_STATUS), (unsigned long)s_dma_status);
#endif
#ifdef CONFIG_T5AI_POWER_PROFILE
  PBL_LOG_ERR("SD state cmd=%08lx data=%08lx mask=%08lx ctl=%08lx fifo=%08lx left=%lu dma=%d",
              (unsigned long)failed_command_status, (unsigned long)failed_data_status,
              (unsigned long)failed_mask, (unsigned long)failed_control, (unsigned long)failed_fifo,
              (unsigned long)failed_residue, failed_dma);
  PBL_LOG_ERR("SD path setup=%08lx/%08lx dma=%08lx/%08lx irq=%08lx/%08lx",
              (unsigned long)failed_setup_before, (unsigned long)failed_setup_after,
              (unsigned long)failed_dma_sd, (unsigned long)failed_dma_mask,
              (unsigned long)failed_irq_sd, (unsigned long)failed_irq_mask);
  PBL_LOG_ERR("SD clocks mode=%08lx cpu1=%08lx sd=%08lx", (unsigned long)failed_cpu_mode,
              (unsigned long)failed_cpu1, (unsigned long)failed_sd_clock);
#endif
  prv_abort();
  s_faulted = true;
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_fault();
#endif
  return status;
}

int t5ai_sd_read(uint32_t sector, void *buffer, uint32_t count) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_block(T5AISleepOwner_SD);
#endif
  const int result = prv_transfer(sector, buffer, count, false);
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_unblock(T5AISleepOwner_SD);
#endif
  pbl_mutex_unlock(&s_lock);
  return result;
}

int t5ai_sd_write(uint32_t sector, const void *buffer, uint32_t count) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_block(T5AISleepOwner_SD);
#endif
  const int result = prv_transfer(sector, (void *)buffer, count, true);
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_unblock(T5AISleepOwner_SD);
#endif
  pbl_mutex_unlock(&s_lock);
  return result;
}

uint8_t *t5ai_sd_lock_workspace(void) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_block(T5AISleepOwner_SD);
#endif
  return s_bounce;
}

void t5ai_sd_unlock_workspace(void) {
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_unblock(T5AISleepOwner_SD);
#endif
  pbl_mutex_unlock(&s_lock);
}

uint64_t t5ai_sd_sector_count(void) {
  return s_sectors;
}

int t5ai_sd_init(void) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_block(T5AISleepOwner_SD);
#endif
  int status = 0;
  if (s_ready) {
    status = s_faulted ? -EIO : 0;
    goto done;
  }
  // Socket switch is active-low; presence is not proof of a readable card.
  status = pbl_device_init(&T5AI_SD_DETECT.port->dev);
  if (status == 0)
    status = pbl_gpio_configure(&T5AI_SD_DETECT, PBL_GPIO_INPUT);
  if (status != 0)
    goto done;
  delay_us(5000);
  const int detected = pbl_gpio_get(&T5AI_SD_DETECT);
  if (detected < 0) {
    status = detected;
    goto done;
  }
  if (!detected) {
    status = -ENODEV;
    goto done;
  }
  // Existing CP PM service owns domain voting; sector traffic stays on the AP.
  status = t5ai_cp_mailbox_enable_sd();
  if (status) {
    goto done;
  }
  status = pbl_device_init(&T5AI_DMA.dev);
  if (status == 0)
    status = pbl_pinctrl_select_state(&T5AI_SD_PINCTRL, PBL_PINCTRL_STATE_DEFAULT);
  if (status != 0)
    goto fail;
  pbl_irq_lock();
  REG(SYSTEM_CPU1_INTERRUPT_ENABLE_ADDRESS) |= SYSTEM_CPU1_SDIO_INTERRUPT;
  // 26 MHz source / 256 for identification; preserve unrelated clock fields.
  REG(SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS) =
      (REG(SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS) & ~SYSTEM_SDIO_CLOCK_FIELD_MASK) |
      SYSTEM_SDIO_CLOCK_XTAL_DIV256;
  pbl_irq_unlock();
  SD(SD_GLOBAL_CONTROL) = PERIPHERAL_RESET_RELEASE | PERIPHERAL_CLOCK_GATE_BYPASS;
#ifdef CONFIG_T5AI_SD_IDLE_GATING
  s_clock_active = true;
#endif
  prv_abort();
  SD(SD_FIFO_CONTROL) = SD_FIFO_DEFAULT_THRESHOLDS | FIFO_RESET | (1u << 25) | (1u << 27);
  NVIC_SetPriority(SDIO_IRQn, 6u);
  NVIC_EnableIRQ(SDIO_IRQn);
  // SD retains its handler across aborts, failed initialization and retries.
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
#ifdef CONFIG_T5AI_SD_IDLE_GATING
  status = prv_wait_ready();
  if (status)
    goto fail;
  s_faulted = false;
  prv_clock_release();
#endif
  s_ready = true;
  PBL_LOG_INFO("SD ready: sectors=%lu width=%u", (unsigned long)s_sectors, DATA_WIDTH ? 4u : 1u);
  goto done;
fail:
#ifdef CONFIG_T5AI_SD_IDLE_GATING
  if (s_clock_active)
#endif
    prv_abort();
  s_faulted = true;
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_fault();
#endif
done:
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_unblock(T5AISleepOwner_SD);
#endif
  pbl_mutex_unlock(&s_lock);
  return status;
}

#ifdef CONFIG_T5AI_POWER_PROFILE
void t5ai_sd_power_stats(T5AISDPowerStats *stats) {
  if (stats == NULL)
    return;
  pbl_irq_lock();
  *stats = s_power_stats;
#ifdef CONFIG_T5AI_SD_IDLE_GATING
  stats->clock_active = s_clock_active;
#else
  stats->clock_active = s_ready;
#endif
  pbl_irq_unlock();
}
#endif
