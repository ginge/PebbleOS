/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/time.h>
#include <pbl/mcu/cache.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/flash_ipc.h>
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
#include <pbl/soc/t5ai/io_trace.h>
#endif
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/soc/t5ai/mb_ipc.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/saradc_ipc.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DECLARE(driver_flash, CONFIG_DRIVER_FLASH_LOG_LEVEL);

// Pinned CP service ABI: flash address is above an 8-bit partition field;
// transfer length and signed API status occupy the low/high 16-bit halves.
#define FLASH_ADDRESS_SHIFT 8u
#define FLASH_STATUS_SHIFT 16u
#define FLASH_MAX_TRANSFER_BYTES UINT16_MAX
#define FLASH_ERASE_BLOCK_BYTES 4096u
#define SARADC_MAX_CHANNEL 15u
#define SARADC_MAX_SAMPLES 32u
#define SARADC_CONFIG_WIRE_SIZE 36u
#define SARADC_COMMAND_WIRE_SIZE 132u
#define SARADC_NOTIFY_WORDS 4u
#define SARADC_NOTIFY_ACK_WORD 1u
// Preserve the raw vendor clock configuration and continuous sampling setup.
#define SARADC_CLOCK_CONFIG 0x30a0c5u
#define SARADC_MODE_CONTINUOUS 3u
#define SARADC_SOURCE_XTAL 1u
#define SARADC_SATURATE_MODE 4u
#define SARADC_STEADY_COUNT 7u
// Vendor SARADC uses a 32-bit MSB-first accumulator with polynomial 0x31.
#define SARADC_CRC_HIGH_BIT 0x80000000u
#define SARADC_CRC_POLYNOMIAL 0x31u
#define SARADC_CRC_BITS_PER_BYTE 8u

#define FLASH_CLIENT_PORT 16u
#define FLASH_SERVER_PORT 1u
#define SARADC_CLIENT_PORT 17u
#define SARADC_SERVER_PORT 2u
#define IPC_TIMEOUT_MS 5000u
#define SARADC_IPC_TIMEOUT_MS 2000u
#define IPC_STALE_RESPONSE_LIMIT 4u
// Pinned CP server: FLASH_SVR_WAIT_TIME=50 at a 1 kHz FreeRTOS tick.
#define FLASH_READ_LEASE_AON_TICKS (T5AI_AON_RTC_HZ / 20u)
#define SARADC_NOTIFY_RX_CHANNEL 0x4cu
#define SARADC_NOTIFY_ACK 2u
#define MB_IPC_CLIENT_PRIORITY (CONFIG_KERNEL_NUM_PRIORITIES - 2u)

enum {
  FLASH_ERASE_SECTOR = 0,
  FLASH_READ = 3,
  FLASH_READ_DONE = 4,
  FLASH_WRITE = 5,
};

typedef struct {
  uint32_t part_and_address;
  uint32_t buffer;
  uint32_t length_and_status;
  uint32_t crc;
} FlashCommand;

typedef struct {
  uint32_t clock;
  uint32_t sample_rate;
  uint32_t filter;
  uint32_t steady;
  uint8_t mode;
  uint8_t source_clock;
  uint8_t channel;
  uint8_t saturate_mode;
  uint32_t is_open;
  uint16_t *output_buffer;
  int32_t output_length;
  uint16_t use_hardware_calibration;
  uint8_t voltage_divider;
} SaradcConfig;

typedef struct {
  uint32_t parameter;
  uint16_t samples[SARADC_MAX_SAMPLES];
  uint32_t size;
  uint32_t timeout;
  int16_t status;
  uint8_t sample_count;
  uint16_t calibration_data;
  uint32_t crc;
  SaradcConfig config;
  void *callback;
  void *context;
} SaradcCommand;

enum {
  SARADC_ACQUIRE = 0,
  SARADC_INIT = 1,
  SARADC_ENABLE_BYPASS_CALIBRATION = 2,
  SARADC_START = 3,
  SARADC_READ_RAW = 4,
  SARADC_READ_RAW_DONE = 5,
  SARADC_STOP = 6,
  SARADC_DEINIT = 7,
  SARADC_RELEASE = 8,
  SARADC_SET_CONFIG = 9,
  SARADC_INIT_GPIO = 29,
  SARADC_DEINIT_GPIO = 30,
};

static T5AIMbIpcSocket *s_flash_socket;
static T5AIMbIpcSocket *s_saradc_socket;
static PBL_MUTEX_DEFINE(s_flash_mutex);
static PBL_MUTEX_DEFINE(s_saradc_mutex);
static FlashCommand s_flash_command __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static SaradcCommand s_saradc_command __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
#ifdef CONFIG_T5AI_FLASH_PROFILE
static T5AIFlashDiagnostics s_flash_diagnostics = {.active_command = UINT32_MAX};

static void prv_sample_operation(T5AIFlashOperationDiagnostics *sample,
                                 uint32_t elapsed_ms, uint32_t offset, uint32_t length) {
  __atomic_fetch_add(&sample->count, 1u, __ATOMIC_RELAXED);
  if (elapsed_ms > __atomic_load_n(&sample->max_ms, __ATOMIC_RELAXED)) {
    __atomic_store_n(&sample->max_ms, elapsed_ms, __ATOMIC_RELAXED);
  }
  __atomic_store_n(&sample->last_ms, elapsed_ms, __ATOMIC_RELAXED);
  __atomic_store_n(&sample->last_offset, offset, __ATOMIC_RELAXED);
  __atomic_store_n(&sample->last_length, length, __ATOMIC_RELAXED);
  __atomic_store_n(&sample->last_tick, pbl_uptime_ticks(), __ATOMIC_RELAXED);
}

static void prv_get_operation(const T5AIFlashOperationDiagnostics *source,
                              T5AIFlashOperationDiagnostics *target) {
#define SAMPLE_FIELD(field) target->field = __atomic_load_n(&source->field, __ATOMIC_RELAXED)
  SAMPLE_FIELD(count);
  SAMPLE_FIELD(max_ms);
  SAMPLE_FIELD(last_ms);
  SAMPLE_FIELD(last_tick);
  SAMPLE_FIELD(last_offset);
  SAMPLE_FIELD(last_length);
#undef SAMPLE_FIELD
}

void t5ai_flash_ipc_get_diagnostics(T5AIFlashDiagnostics *diagnostics) {
  prv_get_operation(&s_flash_diagnostics.read, &diagnostics->read);
  prv_get_operation(&s_flash_diagnostics.write, &diagnostics->write);
  prv_get_operation(&s_flash_diagnostics.erase, &diagnostics->erase);
  diagnostics->active_command =
      __atomic_load_n(&s_flash_diagnostics.active_command, __ATOMIC_RELAXED);
  diagnostics->active_tick =
      __atomic_load_n(&s_flash_diagnostics.active_tick, __ATOMIC_RELAXED);
}

#endif

static bool s_flash_available;
static bool s_saradc_available = true;

static pbl_prio_t prv_boost_client_priority(void) {
  pbl_irq_lock();
  struct pbl_thread *thread = pbl_thread_current();
  const pbl_prio_t previous = pbl_thread_prio_get(thread);
  if (previous < MB_IPC_CLIENT_PRIORITY) {
    pbl_thread_prio_set(thread, MB_IPC_CLIENT_PRIORITY);
  }
  pbl_irq_unlock();
  return previous;
}

static void prv_restore_client_priority(pbl_prio_t previous) {
  pbl_irq_lock();
  struct pbl_thread *thread = pbl_thread_current();
  // Preserve a throttle or other priority change made during the transaction.
  if (previous < MB_IPC_CLIENT_PRIORITY &&
      pbl_thread_prio_get(thread) == MB_IPC_CLIENT_PRIORITY) {
    pbl_thread_prio_set(thread, previous);
  }
  pbl_irq_unlock();
}

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(SaradcConfig) == SARADC_CONFIG_WIRE_SIZE, "BK7258 SARADC config ABI changed");
_Static_assert(sizeof(SaradcCommand) == SARADC_COMMAND_WIRE_SIZE,
               "BK7258 SARADC command ABI changed");
#endif

/* Vendor flash CRC: reflected CRC-32, seed zero, no final XOR. */
static const uint32_t s_flash_crc32_table[256] = {
  0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau,
  0x076dc419u, 0x706af48fu, 0xe963a535u, 0x9e6495a3u,
  0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u,
  0x09b64c2bu, 0x7eb17cbdu, 0xe7b82d07u, 0x90bf1d91u,
  0x1db71064u, 0x6ab020f2u, 0xf3b97148u, 0x84be41deu,
  0x1adad47du, 0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u,
  0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu,
  0x14015c4fu, 0x63066cd9u, 0xfa0f3d63u, 0x8d080df5u,
  0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u, 0xa2677172u,
  0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu,
  0x35b5a8fau, 0x42b2986cu, 0xdbbbc9d6u, 0xacbcf940u,
  0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u,
  0x26d930acu, 0x51de003au, 0xc8d75180u, 0xbfd06116u,
  0x21b4f4b5u, 0x56b3c423u, 0xcfba9599u, 0xb8bda50fu,
  0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u,
  0x2f6f7c87u, 0x58684c11u, 0xc1611dabu, 0xb6662d3du,
  0x76dc4190u, 0x01db7106u, 0x98d220bcu, 0xefd5102au,
  0x71b18589u, 0x06b6b51fu, 0x9fbfe4a5u, 0xe8b8d433u,
  0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u,
  0x7f6a0dbbu, 0x086d3d2du, 0x91646c97u, 0xe6635c01u,
  0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu,
  0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u,
  0x65b0d9c6u, 0x12b7e950u, 0x8bbeb8eau, 0xfcb9887cu,
  0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u,
  0x4db26158u, 0x3ab551ceu, 0xa3bc0074u, 0xd4bb30e2u,
  0x4adfa541u, 0x3dd895d7u, 0xa4d1c46du, 0xd3d6f4fbu,
  0x4369e96au, 0x346ed9fcu, 0xad678846u, 0xda60b8d0u,
  0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u,
  0x5005713cu, 0x270241aau, 0xbe0b1010u, 0xc90c2086u,
  0x5768b525u, 0x206f85b3u, 0xb966d409u, 0xce61e49fu,
  0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u,
  0x59b33d17u, 0x2eb40d81u, 0xb7bd5c3bu, 0xc0ba6cadu,
  0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au,
  0xead54739u, 0x9dd277afu, 0x04db2615u, 0x73dc1683u,
  0xe3630b12u, 0x94643b84u, 0x0d6d6a3eu, 0x7a6a5aa8u,
  0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u,
  0xf00f9344u, 0x8708a3d2u, 0x1e01f268u, 0x6906c2feu,
  0xf762575du, 0x806567cbu, 0x196c3671u, 0x6e6b06e7u,
  0xfed41b76u, 0x89d32be0u, 0x10da7a5au, 0x67dd4accu,
  0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u,
  0xd6d6a3e8u, 0xa1d1937eu, 0x38d8c2c4u, 0x4fdff252u,
  0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
  0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u,
  0xdf60efc3u, 0xa867df55u, 0x316e8eefu, 0x4669be79u,
  0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u,
  0xcc0c7795u, 0xbb0b4703u, 0x220216b9u, 0x5505262fu,
  0xc5ba3bbeu, 0xb2bd0b28u, 0x2bb45a92u, 0x5cb36a04u,
  0xc2d7ffa7u, 0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du,
  0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au,
  0x9c0906a9u, 0xeb0e363fu, 0x72076785u, 0x05005713u,
  0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu, 0x0cb61b38u,
  0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u,
  0x86d3d2d4u, 0xf1d4e242u, 0x68ddb3f8u, 0x1fda836eu,
  0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u,
  0x88085ae6u, 0xff0f6a70u, 0x66063bcau, 0x11010b5cu,
  0x8f659effu, 0xf862ae69u, 0x616bffd3u, 0x166ccf45u,
  0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u,
  0xa7672661u, 0xd06016f7u, 0x4969474du, 0x3e6e77dbu,
  0xaed16a4au, 0xd9d65adcu, 0x40df0b66u, 0x37d83bf0u,
  0xa9bcae53u, 0xdebb9ec5u, 0x47b2cf7fu, 0x30b5ffe9u,
  0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u,
  0xbad03605u, 0xcdd70693u, 0x54de5729u, 0x23d967bfu,
  0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u,
  0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du,
};

static uint32_t prv_crc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = 0u;
  while (length-- != 0u) {
    crc = (crc >> 8u) ^ s_flash_crc32_table[(crc ^ *bytes++) & 0xffu];
  }
  return crc;
}

static uint32_t prv_saradc_crc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = 0u;
  while (length-- != 0u) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < SARADC_CRC_BITS_PER_BYTE; ++bit) {
      crc = (crc & SARADC_CRC_HIGH_BIT) != 0u ? (crc << 1u) ^ SARADC_CRC_POLYNOMIAL : crc << 1u;
    }
  }
  return crc;
}

static void prv_saradc_notify(uint8_t channel, const T5AICPMailboxFrame *frame, void *context) {
  (void)channel;
  (void)context;
  if (frame->length != SARADC_NOTIFY_WORDS * sizeof(uint32_t)) {
    (void)t5ai_cp_mailbox_acknowledge_frame(frame, false);
    return;
  }
  uint32_t *words = (uint32_t *)(uintptr_t)frame->address;
  words[SARADC_NOTIFY_ACK_WORD] = SARADC_NOTIFY_ACK;
  (void)t5ai_cp_mailbox_acknowledge_frame(frame, true);
}

status_t t5ai_flash_ipc_init(void) {
  status_t status = t5ai_mb_ipc_init();
  if (FAILED(status)) {
    return status;
  }
  status = t5ai_cp_mailbox_register(SARADC_NOTIFY_RX_CHANNEL, prv_saradc_notify, NULL);
  if (FAILED(status) && status != E_INVALID_OPERATION) {
    return status;
  }
  status = t5ai_mb_ipc_open(FLASH_CLIENT_PORT, FLASH_SERVER_PORT, &s_flash_socket);
  s_flash_available = PASSED(status);
  return status;
}

bool t5ai_flash_ipc_is_available(void) {
  return s_flash_available;
}

static void prv_flash_quarantine(void) {
  s_flash_available = false;
  t5ai_mb_ipc_poison(s_flash_socket);
}

static void prv_flash_fault(status_t status, uint8_t command) {
  const bool ambiguous = status == E_AGAIN;
  if (ambiguous) {
    s_flash_available = false;
    t5ai_mb_ipc_poison(s_flash_socket);
  }
  PBL_LOG_ERR("T5AI flash IPC fault command=%u status=%ld quarantined=%u", command,
              (long)status, ambiguous);
}

static status_t prv_flash_transaction(uint8_t command) {
  if (!s_flash_available) {
    PBL_LOG_ERR("T5AI flash IPC unavailable for command %u", command);
    return E_INVALID_OPERATION;
  }
  /* CPU0's stock flash server only waits 50 ticks for the AP to consume a
   * completion and return its logical MIPC response.  Flash is commonly
   * called from KernelBG while app and Bluetooth work is runnable, so keep the
   * client just below the physical mailbox worker until that handshake is
   * complete. Blocking waits still yield normally. */
#ifdef CONFIG_T5AI_FLASH_PROFILE
  const pbl_tick_t trace_start = pbl_uptime_ticks();
  const uint32_t trace_offset = s_flash_command.part_and_address >> FLASH_ADDRESS_SHIFT;
  const uint32_t trace_length = (uint16_t)s_flash_command.length_and_status;
  __atomic_store_n(&s_flash_diagnostics.active_tick, trace_start, __ATOMIC_RELAXED);
  __atomic_store_n(&s_flash_diagnostics.active_command, command, __ATOMIC_RELAXED);
#endif
  const pbl_prio_t previous_priority = prv_boost_client_priority();
  t5ai_mb_ipc_discard(s_flash_socket);
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  uint32_t send_start = pbl_uptime_ticks();
#endif
  status_t status = t5ai_mb_ipc_send(s_flash_socket, command, &s_flash_command,
                                     sizeof(s_flash_command), IPC_TIMEOUT_MS);
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  if (command == FLASH_WRITE) {
    t5ai_io_trace_sample(T5IO_WRITE_SEND, pbl_uptime_ticks() - send_start);
  }
#endif
  if (FAILED(status)) {
    prv_flash_fault(status, command);
    goto out;
  }
  for (unsigned stale = 0u; stale <= IPC_STALE_RESPONSE_LIMIT; ++stale) {
    uint8_t received_command = UINT8_MAX;
    FlashCommand response;
    status = t5ai_mb_ipc_receive(s_flash_socket, &received_command, &response,
                                 sizeof(response), IPC_TIMEOUT_MS);
    if (FAILED(status)) {
      PBL_LOG_ERR("T5AI flash completion failed expected=%u status=%ld", command,
                  (long)status);
      // An invalid completion cannot establish whether CP owns a read lease.
      prv_flash_quarantine();
      prv_flash_fault(status, command);
      goto out;
    }
    if (received_command != command) {
      PBL_LOG_WRN("T5AI flash ignored stale completion expected=%u received=%u count=%u",
                  command, received_command, stale + 1u);
      continue;
    }
    s_flash_command = response;
    const int16_t api_status = (int16_t)(response.length_and_status >> FLASH_STATUS_SHIFT);
    if (api_status != 0) {
      PBL_LOG_ERR("T5AI flash CP error command=%u api=%d length=%u buffer=%08lx", command,
                  api_status, (unsigned)(uint16_t)response.length_and_status,
                  (unsigned long)response.buffer);
      prv_flash_fault(E_ERROR, command);
      status = E_ERROR;
      goto out;
    }
    status = S_SUCCESS;
    goto out;
  }
  PBL_LOG_ERR("T5AI flash stale completion limit expected=%u", command);
  prv_flash_quarantine();
  prv_flash_fault(E_ERROR, command);
  status = E_ERROR;
out:
#ifdef CONFIG_T5AI_FLASH_PROFILE
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  if (command == FLASH_WRITE) {
    t5ai_io_trace_sample(T5IO_WRITE_TOTAL, pbl_uptime_ticks() - trace_start);
  }
#endif
  const uint32_t elapsed_ms = (pbl_uptime_ticks() - trace_start) * (1000u / PBL_TICK_HZ);
  T5AIFlashOperationDiagnostics *sample = command == FLASH_READ ? &s_flash_diagnostics.read :
      command == FLASH_WRITE ? &s_flash_diagnostics.write :
      command == FLASH_ERASE_SECTOR ? &s_flash_diagnostics.erase : NULL;
  if (sample != NULL) {
    prv_sample_operation(sample, elapsed_ms, trace_offset, trace_length);
  }
  __atomic_store_n(&s_flash_diagnostics.active_command, UINT32_MAX, __ATOMIC_RELAXED);
#endif
  prv_restore_client_priority(previous_priority);
  return status;
}

status_t t5ai_flash_ipc_read(uint32_t offset, void *buffer, size_t length) {
  if (buffer == NULL || length == 0u || length > FLASH_MAX_TRANSFER_BYTES) {
    return E_INVALID_ARGUMENT;
  }
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  uint32_t lock_start = pbl_uptime_ticks();
#endif
  pbl_mutex_lock(&s_flash_mutex, PBL_FOREVER);
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  t5ai_io_trace_sample(T5IO_READ_LOCK, pbl_uptime_ticks() - lock_start);
#endif
  /* CPU0's read buffer remains leased until READ_DONE. Keep copying and CRC
   * validation inside the same priority boost as both handshakes. */
  const pbl_prio_t previous_priority = prv_boost_client_priority();
  s_flash_command = (FlashCommand){
      .part_and_address = offset << FLASH_ADDRESS_SHIFT,
      .length_and_status = (uint32_t)length,
  };
  status_t status = prv_flash_transaction(FLASH_READ);
  if (FAILED(status)) {
    goto out;
  }
  const uint32_t lease_start = t5ai_mb_ipc_last_receive_aon(s_flash_socket);
  // Only a validated, timely successful response establishes a lease.
  if ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - lease_start) >= FLASH_READ_LEASE_AON_TICKS) {
    prv_flash_quarantine();
    status = E_AGAIN;
    prv_flash_fault(status, FLASH_READ);
    goto out;
  }
  if ((uint16_t)s_flash_command.length_and_status != length ||
      !t5ai_cp_mailbox_shared_range_valid((uintptr_t)s_flash_command.buffer, length)) {
    prv_flash_quarantine();
    status = E_ERROR;
    prv_flash_fault(status, FLASH_READ);
    goto out;
  }
  const void *source = (const void *)(uintptr_t)s_flash_command.buffer;
  dcache_invalidate((void *)source, length);
  const bool valid_payload = prv_crc32(source, length) == s_flash_command.crc;
  if (valid_payload) {
    memcpy(buffer, source, length);
  } else {
    status = E_ERROR;
    prv_flash_fault(status, FLASH_READ);
  }

  // A preemption or long validation may outlive CP's buffer ownership.
  if ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - lease_start) >= FLASH_READ_LEASE_AON_TICKS) {
    prv_flash_quarantine();
    status = FAILED(status) ? status : E_AGAIN;
    prv_flash_fault(status, FLASH_READ);
    goto out;
  }
  // CRC failure does not relinquish CP's buffer. Always finish a known lease.
  memset(&s_flash_command, 0, sizeof(s_flash_command));
  const status_t release_status = prv_flash_transaction(FLASH_READ_DONE);
  if (FAILED(release_status)) {
    prv_flash_quarantine();
  }
  status = FAILED(status) ? status : release_status;
out:
  prv_restore_client_priority(previous_priority);
  pbl_mutex_unlock(&s_flash_mutex);
  return status;
}

status_t t5ai_flash_ipc_write(uint32_t offset, const void *buffer, size_t length) {
  if (buffer == NULL || length == 0u || length > FLASH_MAX_TRANSFER_BYTES) {
    return E_INVALID_ARGUMENT;
  }
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  uint32_t lock_start = pbl_uptime_ticks();
#endif
  pbl_mutex_lock(&s_flash_mutex, PBL_FOREVER);
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  t5ai_io_trace_sample(T5IO_WRITE_LOCK, pbl_uptime_ticks() - lock_start);
#endif
  s_flash_command = (FlashCommand){
      .part_and_address = offset << FLASH_ADDRESS_SHIFT,
      .buffer = (uint32_t)(uintptr_t)buffer,
      .length_and_status = (uint32_t)length,
      .crc = prv_crc32(buffer, length),
  };
  dcache_flush((void *)buffer, length);
  const status_t status = prv_flash_transaction(FLASH_WRITE);
  pbl_mutex_unlock(&s_flash_mutex);
  return status;
}

status_t t5ai_flash_ipc_erase_4k(uint32_t offset) {
  if ((offset & (FLASH_ERASE_BLOCK_BYTES - 1u)) != 0u) {
    return E_INVALID_ARGUMENT;
  }
  pbl_mutex_lock(&s_flash_mutex, PBL_FOREVER);
  s_flash_command = (FlashCommand){.part_and_address = offset << FLASH_ADDRESS_SHIFT};
  const status_t status = prv_flash_transaction(FLASH_ERASE_SECTOR);
  pbl_mutex_unlock(&s_flash_mutex);
  return status;
}

static status_t prv_saradc_connect(void) {
  if (!s_saradc_available) {
    return E_INVALID_OPERATION;
  }
  if (s_saradc_socket != NULL) {
    return S_SUCCESS;
  }
  return t5ai_mb_ipc_open(SARADC_CLIENT_PORT, SARADC_SERVER_PORT, &s_saradc_socket);
}

static void prv_saradc_disconnect(void) {
  t5ai_mb_ipc_poison(s_saradc_socket);
  s_saradc_available = false;
}

static status_t prv_saradc_transaction(uint8_t command) {
  const pbl_prio_t previous_priority = prv_boost_client_priority();
  t5ai_mb_ipc_discard(s_saradc_socket);
  status_t status = t5ai_mb_ipc_send(s_saradc_socket, command, &s_saradc_command,
                                     sizeof(s_saradc_command), SARADC_IPC_TIMEOUT_MS);
  if (FAILED(status)) {
    if (status == E_AGAIN) {
      prv_saradc_disconnect();
    }
    goto out;
  }
  for (unsigned stale = 0u; stale <= IPC_STALE_RESPONSE_LIMIT; ++stale) {
    uint8_t received_command = UINT8_MAX;
    SaradcCommand response;
    status = t5ai_mb_ipc_receive(s_saradc_socket, &received_command, &response,
                                 sizeof(response), SARADC_IPC_TIMEOUT_MS);
    if (FAILED(status)) {
      if (status == E_AGAIN) {
        prv_saradc_disconnect();
      }
      goto out;
    }
    if (received_command != command) {
      PBL_LOG_WRN("T5AI SARADC ignored stale completion expected=%u received=%u count=%u",
                  command, received_command, stale + 1u);
      continue;
    }
    s_saradc_command = response;
    status = response.status == 0 ? S_SUCCESS : E_ERROR;
    goto out;
  }
  status = E_ERROR;
out:
  prv_restore_client_priority(previous_priority);
  return status;
}

static status_t prv_saradc_cleanup(uint8_t channel, status_t result) {
  if (!s_saradc_available) {
    return result;
  }
  status_t cleanup_status = S_SUCCESS;
  memset(&s_saradc_command, 0, sizeof(s_saradc_command));
  status_t status = prv_saradc_transaction(SARADC_STOP);
  if (FAILED(status)) {
    return status;
  }
  s_saradc_command.config.channel = channel;
  status = prv_saradc_transaction(SARADC_DEINIT);
  if (FAILED(status)) {
    return status;
  }
  status = prv_saradc_transaction(SARADC_RELEASE);
  if (FAILED(status)) {
    return status;
  }
  s_saradc_command.config.channel = channel;
  status = prv_saradc_transaction(SARADC_DEINIT_GPIO);
  cleanup_status = FAILED(status) ? status : cleanup_status;
  return FAILED(result) ? result : cleanup_status;
}

static status_t prv_saradc_read_peak_locked(uint8_t channel, uint8_t samples, uint16_t *peak) {
  status_t status = prv_saradc_connect();
  if (FAILED(status)) {
    return status;
  }
  memset(&s_saradc_command, 0, sizeof(s_saradc_command));
  s_saradc_command.config.channel = channel;
  status = prv_saradc_transaction(SARADC_INIT_GPIO);
  if (FAILED(status)) {
    return status;
  }
  status = prv_saradc_transaction(SARADC_ACQUIRE);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }
  s_saradc_command.config.channel = channel;
  status = prv_saradc_transaction(SARADC_INIT);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }

  memset(&s_saradc_command.config, 0, sizeof(s_saradc_command.config));
  s_saradc_command.config.clock = SARADC_CLOCK_CONFIG;
  s_saradc_command.config.mode = SARADC_MODE_CONTINUOUS;
  s_saradc_command.config.source_clock = SARADC_SOURCE_XTAL;
  s_saradc_command.config.channel = channel;
  s_saradc_command.config.saturate_mode = SARADC_SATURATE_MODE;
  s_saradc_command.config.steady = SARADC_STEADY_COUNT;
  s_saradc_command.config.output_buffer = s_saradc_command.samples;
  s_saradc_command.config.output_length = samples;
  status = prv_saradc_transaction(SARADC_SET_CONFIG);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }
  status = prv_saradc_transaction(SARADC_ENABLE_BYPASS_CALIBRATION);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }
  status = prv_saradc_transaction(SARADC_START);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }

  memset(s_saradc_command.samples, 0, sizeof(s_saradc_command.samples));
  s_saradc_command.size = samples;
  s_saradc_command.timeout = SARADC_IPC_TIMEOUT_MS;
  status = prv_saradc_transaction(SARADC_READ_RAW);
  if (PASSED(status) && s_saradc_command.size == samples &&
      prv_saradc_crc32(s_saradc_command.samples, samples * sizeof(uint16_t)) ==
          s_saradc_command.crc) {
    uint16_t maximum = 0u;
    for (uint8_t i = 0; i < samples; ++i) {
      if (s_saradc_command.samples[i] > maximum) {
        maximum = s_saradc_command.samples[i];
      }
    }
    *peak = maximum;
    status = prv_saradc_transaction(SARADC_READ_RAW_DONE);
  } else {
    status = E_ERROR;
  }
  return prv_saradc_cleanup(channel, status);
}

status_t t5ai_saradc_read_peak(uint8_t channel, uint8_t samples, uint16_t *peak) {
  if (peak == NULL || channel > SARADC_MAX_CHANNEL || samples == 0u ||
      samples > SARADC_MAX_SAMPLES) {
    return E_INVALID_ARGUMENT;
  }
  pbl_mutex_lock(&s_saradc_mutex, PBL_FOREVER);
  const status_t status = prv_saradc_read_peak_locked(channel, samples, peak);
  pbl_mutex_unlock(&s_saradc_mutex);
  return status;
}
