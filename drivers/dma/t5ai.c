/* SPDX-License-Identifier: Apache-2.0 */
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
#include <pbl/soc/t5ai/sleep.h>
#endif

#include <errno.h>

#include <pbl/drivers/dma/t5ai.h>
#include <pbl/kernel/irq.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/soc/t5ai/memory.h>

#include <cmsis_core.h>

#define DMA_BASE           0x45020000u
#define DMA_REG(offset)    (*(volatile uint32_t *)(DMA_BASE + (offset)))
#define DMA_CHANNEL_BASE   0x40u
#define DMA_CHANNEL_STRIDE 0x40u
#define DMA_CHANNEL_REG(channel, offset) \
  DMA_REG(DMA_CHANNEL_BASE + (channel) * DMA_CHANNEL_STRIDE + (offset))

#define DMA_GLOBAL_CONTROL           0x08u
#define DMA_SECURE_ATTRIBUTE         0x10u
#define DMA_PRIVILEGED_ATTRIBUTE     0x14u
#define DMA_INTERRUPT_ALLOCATION     0x28u
#define DMA_GLOBAL_RESET_RELEASE     (1u << 0)
#define DMA_GLOBAL_CLOCK_GATE_BYPASS (1u << 1)

#define DMA_CHANNEL_CONTROL           0x00u
#define DMA_DESTINATION_ADDRESS       0x04u
#define DMA_SOURCE_ADDRESS            0x08u
#define DMA_DESTINATION_LOOP_END      0x0cu
#define DMA_DESTINATION_LOOP_START    0x10u
#define DMA_SOURCE_LOOP_END           0x14u
#define DMA_SOURCE_LOOP_START         0x18u
#define DMA_REQUEST_MUX               0x1cu
#define DMA_SOURCE_READ_ADDRESS       0x28u
#define DMA_DESTINATION_WRITE_ADDRESS 0x2cu
#define DMA_CHANNEL_STATUS            0x30u

#define DMA_CONTROL_ENABLE                     (1u << 0)
#define DMA_CONTROL_FINISH_INTERRUPT_ENABLE    (1u << 1)
#define DMA_CONTROL_HALF_INTERRUPT_ENABLE      (1u << 2)
#define DMA_CONTROL_REPEAT_MODE                (1u << 3)
#define DMA_CONTROL_SOURCE_WIDTH_SHIFT         4u
#define DMA_CONTROL_DESTINATION_WIDTH_SHIFT    6u
#define DMA_CONTROL_SOURCE_INCREMENT           (1u << 8)
#define DMA_CONTROL_DESTINATION_INCREMENT      (1u << 9)
#define DMA_CONTROL_SOURCE_LOOP_ENABLE         (1u << 10)
#define DMA_CONTROL_DESTINATION_LOOP_ENABLE    (1u << 11)
#define DMA_CONTROL_LENGTH_SHIFT               16u
#define DMA_WIDTH_BYTE                         0u
#define DMA_WIDTH_HALFWORD                     1u
#define DMA_WIDTH_WORD                         2u
#define DMA_REQUEST_SOURCE_SHIFT               0u
#define DMA_REQUEST_DESTINATION_SHIFT          5u
#define DMA_REQUEST_SELECT_MASK                0x1fu
#define DMA_REQUEST_SOURCE_SECURE              (1u << 20)
#define DMA_REQUEST_DESTINATION_SECURE         (1u << 21)
#define DMA_REQUEST_BUS_ERROR_INTERRUPT_ENABLE (1u << 22)
#define DMA_REQUEST_SOURCE_BURST_SHIFT         24u
#define DMA_REQUEST_DESTINATION_BURST_SHIFT    26u
#define DMA_BURST_LENGTH_INC16                 3u
#define DMA_STATUS_FINISH                      (1u << 18)
#define DMA_STATUS_HALF                        (1u << 19)
#define DMA_STATUS_BUS_ERROR                   (1u << 20)
#define DMA_STATUS_INTERRUPTS (DMA_STATUS_FINISH | DMA_STATUS_HALF | DMA_STATUS_BUS_ERROR)

// Only AP channels 0..3 are configured or acknowledged; CP channels are untouched.
#define DMA_AP_CHANNEL_COUNT   4u
#define DMA_AP_CHANNEL_MASK    ((1u << DMA_AP_CHANNEL_COUNT) - 1u)
#define DMA_MAX_TRANSFER_BYTES (1u << DMA_CONTROL_LENGTH_SHIFT)
#define DMA_AP_SRAM_BASE       0x28010000u
#define DMA_AP_SRAM_END        0x28066000u
#define DMA_IRQ_PRIORITY       6u
#define DMA_POWER_SETTLE_US    10u

#define SYSTEM_BASE                       0x44010000u
#define SYSTEM_REG(offset)                (*(volatile uint32_t *)(SYSTEM_BASE + (offset)))
#define SYSTEM_MEMORY_SHUTDOWN            0x38u
#define SYSTEM_MEMORY_DEEP_SLEEP          0x3cu
#define SYSTEM_POWER_SLEEP_WAKEUP         0x40u
#define SYSTEM_CPU1_INTERRUPT_ENABLE      0x88u
#define SYSTEM_BACKUP_POWER_DOWN          (1u << 4)
#define SYSTEM_DMA0_MEMORY_POWER_DOWN     (1u << 30)
#define SYSTEM_CPU1_GDMA_INTERRUPT_ENABLE (1u << 11)

struct channel {
  uint32_t request, options;
  uintptr_t memory;
  size_t len;
  enum pbl_dma_dir dir;
  bool cyclic, active;
};
static struct channel s_channels[DMA_AP_CHANNEL_COUNT];

static int prv_terminate(const struct pbl_dma_controller *ctrl, uint8_t c) {
  (void)ctrl;
  pbl_irq_lock();
  DMA_CHANNEL_REG(c, DMA_CHANNEL_CONTROL) = 0;
  __DSB();
  (void)DMA_CHANNEL_REG(c, DMA_CHANNEL_CONTROL);
  DMA_CHANNEL_REG(c, DMA_CHANNEL_STATUS) = DMA_STATUS_INTERRUPTS;
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  if (s_channels[c].active)
    t5ai_sleep_unblock(T5AISleepOwner_DMA);
#endif
  s_channels[c].active = false;
  pbl_irq_unlock();
  return 0;
}

static int prv_config(const struct pbl_dma_controller *ctrl, const struct pbl_dma_chan *chan,
                      const struct pbl_dma_slave_config *cfg) {
  (void)ctrl;
  (void)cfg;
  struct channel *state = &s_channels[chan->channel];
  if (state->active)
    return -EBUSY;
  if (chan->request > DMA_REQUEST_SELECT_MASK ||
      (chan->config & ~(T5AI_DMA_PERIPHERAL_INCREMENT | T5AI_DMA_BURST_INC16)))
    return -EINVAL;
  state->request = chan->request;
  state->options = chan->config;
  return 0;
}

static int prv_start(const struct pbl_dma_controller *ctrl, uint8_t c, void *buf, size_t len,
                     size_t period, enum pbl_dma_dir dir) {
  struct channel *s = &s_channels[c];
  const struct pbl_dma_slave_config *cfg = &ctrl->chans[c].cfg;
  const bool write = dir == PBL_DMA_MEM_TO_DEV;
  const unsigned width = write ? cfg->dst_addr_width : cfg->src_addr_width;
  const uintptr_t peripheral = write ? cfg->dst_addr : cfg->src_addr;
  const uintptr_t memory = (uintptr_t)buf;
  if (s->active)
    return -EBUSY;
  const bool reachable =
      (memory >= DMA_AP_SRAM_BASE && memory < DMA_AP_SRAM_END && len <= DMA_AP_SRAM_END - memory) ||
      (memory >= T5AI_DISPLAY_PSRAM_BASE &&
       memory < T5AI_DISPLAY_PSRAM_BASE + T5AI_DISPLAY_PSRAM_SIZE &&
       len <= T5AI_DISPLAY_PSRAM_BASE + T5AI_DISPLAY_PSRAM_SIZE - memory) ||
      (memory >= T5AI_DISPLAY_DMA_PSRAM_BASE &&
       memory < T5AI_DISPLAY_DMA_PSRAM_BASE + T5AI_DISPLAY_DMA_PSRAM_SIZE &&
       len <= T5AI_DISPLAY_DMA_PSRAM_BASE + T5AI_DISPLAY_DMA_PSRAM_SIZE - memory);
  if (!reachable || (dir != PBL_DMA_MEM_TO_DEV && dir != PBL_DMA_DEV_TO_MEM) ||
      (width != 1u && width != 2u && width != 4u) || !buf || !peripheral || !len ||
      len > DMA_MAX_TRANSFER_BYTES || len % width || memory % width || peripheral % width ||
      (period && (period != len / 2u || len % 2u || period % width)))
    return -EINVAL;
  const unsigned encoding = width == 4u   ? DMA_WIDTH_WORD
                            : width == 2u ? DMA_WIDTH_HALFWORD
                                          : DMA_WIDTH_BYTE;
  const bool increment = (s->options & T5AI_DMA_PERIPHERAL_INCREMENT) != 0;
  const uintptr_t source = write ? memory : peripheral;
  const uintptr_t destination = write ? peripheral : memory;
  const size_t source_len = write || increment ? len : width;
  const size_t destination_len = !write || increment ? len : width;
  pbl_irq_lock();
  DMA_CHANNEL_REG(c, DMA_CHANNEL_CONTROL) = 0;
  DMA_CHANNEL_REG(c, DMA_DESTINATION_ADDRESS) = destination;
  DMA_CHANNEL_REG(c, DMA_SOURCE_ADDRESS) = source;
  DMA_CHANNEL_REG(c, DMA_DESTINATION_LOOP_END) =
      destination + destination_len + (increment && !period ? width : 0u);
  DMA_CHANNEL_REG(c, DMA_DESTINATION_LOOP_START) = destination;
  DMA_CHANNEL_REG(c, DMA_SOURCE_LOOP_END) =
      source + source_len + (increment && !period ? width : 0u);
  DMA_CHANNEL_REG(c, DMA_SOURCE_LOOP_START) = source;
  DMA_CHANNEL_REG(c, DMA_REQUEST_MUX) =
      (s->request << (write ? DMA_REQUEST_DESTINATION_SHIFT : DMA_REQUEST_SOURCE_SHIFT)) |
      DMA_REQUEST_SOURCE_SECURE | DMA_REQUEST_DESTINATION_SECURE |
      DMA_REQUEST_BUS_ERROR_INTERRUPT_ENABLE |
      ((s->options & T5AI_DMA_BURST_INC16)
           ? (DMA_BURST_LENGTH_INC16 << DMA_REQUEST_SOURCE_BURST_SHIFT) |
                 (DMA_BURST_LENGTH_INC16 << DMA_REQUEST_DESTINATION_BURST_SHIFT)
           : 0u);
  DMA_CHANNEL_REG(c, DMA_CHANNEL_STATUS) = DMA_STATUS_INTERRUPTS;
  s->memory = memory;
  s->len = len;
  s->dir = dir;
  s->cyclic = period != 0;
  s->active = true;
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_block(T5AISleepOwner_DMA);
#endif
  __DSB();
  DMA_CHANNEL_REG(c, DMA_CHANNEL_CONTROL) =
      ((len - 1u) << DMA_CONTROL_LENGTH_SHIFT) | (encoding << DMA_CONTROL_SOURCE_WIDTH_SHIFT) |
      (encoding << DMA_CONTROL_DESTINATION_WIDTH_SHIFT) |
      ((write || increment) ? DMA_CONTROL_SOURCE_INCREMENT : 0u) |
      ((!write || increment) ? DMA_CONTROL_DESTINATION_INCREMENT : 0u) |
      DMA_CONTROL_SOURCE_LOOP_ENABLE | DMA_CONTROL_DESTINATION_LOOP_ENABLE |
      DMA_CONTROL_FINISH_INTERRUPT_ENABLE |
      (period ? DMA_CONTROL_HALF_INTERRUPT_ENABLE | DMA_CONTROL_REPEAT_MODE : 0u) |
      DMA_CONTROL_ENABLE;
  pbl_irq_unlock();
  return 0;
}
static int prv_single(const struct pbl_dma_controller *ctrl, uint8_t c, void *buf, size_t len,
                      enum pbl_dma_dir dir) {
  return prv_start(ctrl, c, buf, len, 0, dir);
}
static int prv_cyclic(const struct pbl_dma_controller *ctrl, uint8_t c, void *buf, size_t len,
                      size_t period, enum pbl_dma_dir dir) {
  return prv_start(ctrl, c, buf, len, period, dir);
}
static int prv_status(const struct pbl_dma_controller *ctrl, uint8_t c, size_t *residue) {
  (void)ctrl;
  const struct channel *s = &s_channels[c];
  if (DMA_CHANNEL_REG(c, DMA_CHANNEL_STATUS) & DMA_STATUS_BUS_ERROR)
    return -EIO;
  if (!s->active || (!s->cyclic && (DMA_CHANNEL_REG(c, DMA_CHANNEL_STATUS) & DMA_STATUS_FINISH))) {
    *residue = 0;
  } else {
    const uintptr_t position = DMA_CHANNEL_REG(
        c, s->dir == PBL_DMA_MEM_TO_DEV ? DMA_SOURCE_READ_ADDRESS : DMA_DESTINATION_WRITE_ADDRESS);
    *residue = position >= s->memory && position <= s->memory + s->len
                   ? s->len - (position - s->memory)
                   : s->len;
  }
  return 0;
}
void pbl_dma_t5ai_irq_handler(void) {
  pbl_irq_lock();
  for (uint8_t c = 0; c < DMA_AP_CHANNEL_COUNT; ++c) {
    const uint32_t status = DMA_CHANNEL_REG(c, DMA_CHANNEL_STATUS) & DMA_STATUS_INTERRUPTS;
    if (!status)
      continue;
    DMA_CHANNEL_REG(c, DMA_CHANNEL_STATUS) = status;
    struct channel *s = &s_channels[c];
    if (!s->active)
      continue;
    // Coalesced cyclic events mean the consumer has already lost a period.
    if ((status & DMA_STATUS_BUS_ERROR) ||
        (s->cyclic && (status & (DMA_STATUS_HALF | DMA_STATUS_FINISH)) ==
                          (DMA_STATUS_HALF | DMA_STATUS_FINISH))) {
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
      t5ai_sleep_fault();
#endif
      prv_terminate(&T5AI_DMA, c);
      pbl_dma_complete(&T5AI_DMA, c, -EIO);
    } else if (status & (s->cyclic ? DMA_STATUS_HALF | DMA_STATUS_FINISH : DMA_STATUS_FINISH)) {
      if (!s->cyclic)
        prv_terminate(&T5AI_DMA, c);
      pbl_dma_complete(&T5AI_DMA, c, 0);
    }
  }
  pbl_irq_unlock();
}
static int prv_init(const struct pbl_device *dev) {
  (void)dev;
  SYSTEM_REG(SYSTEM_POWER_SLEEP_WAKEUP) &= ~SYSTEM_BACKUP_POWER_DOWN;
  SYSTEM_REG(SYSTEM_MEMORY_SHUTDOWN) &= ~SYSTEM_DMA0_MEMORY_POWER_DOWN;
  SYSTEM_REG(SYSTEM_MEMORY_DEEP_SLEEP) &= ~SYSTEM_DMA0_MEMORY_POWER_DOWN;
  delay_us(DMA_POWER_SETTLE_US);
  DMA_REG(DMA_GLOBAL_CONTROL) |= DMA_GLOBAL_RESET_RELEASE | DMA_GLOBAL_CLOCK_GATE_BYPASS;
  DMA_REG(DMA_SECURE_ATTRIBUTE) |= DMA_AP_CHANNEL_MASK;
  DMA_REG(DMA_PRIVILEGED_ATTRIBUTE) |= DMA_AP_CHANNEL_MASK;
  DMA_REG(DMA_INTERRUPT_ALLOCATION) &= ~DMA_AP_CHANNEL_MASK;
  for (uint8_t c = 0; c < DMA_AP_CHANNEL_COUNT; ++c)
    prv_terminate(&T5AI_DMA, c);
  SYSTEM_REG(SYSTEM_CPU1_INTERRUPT_ENABLE) |= SYSTEM_CPU1_GDMA_INTERRUPT_ENABLE;
  NVIC_SetPriority(GDMA_IRQn, DMA_IRQ_PRIORITY);
  NVIC_EnableIRQ(GDMA_IRQn);
  return 0;
}
static const struct pbl_dma_controller_ops s_ops = {
  .slave_config = prv_config,
  .prep_slave_single = prv_single,
  .prep_dma_cyclic = prv_cyclic,
  .terminate = prv_terminate,
  .tx_status = prv_status,
};
PBL_DMA_CONTROLLER_STATE_DEFINE(T5AI_DMA, DMA_AP_CHANNEL_COUNT);
const struct pbl_dma_controller T5AI_DMA =
    PBL_DMA_CONTROLLER_INIT(T5AI_DMA, "t5ai-dma0", prv_init, &s_ops);
PBL_DEVICE_REGISTER(T5AI_DMA, &T5AI_DMA.dev);
