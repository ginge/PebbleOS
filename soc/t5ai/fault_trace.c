/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cmsis_core.h>
#include <pbl/mcu/cache.h>

#define FAULT_STACK_BYTES 512u
#define POLL_LIMIT        200000u
#define TIMEOUT_TICKS     3200u // 100 ms for the entire fault report at 32 kHz.
#define MAX_MPU_REGIONS   16u
#ifndef REG32
#define REG32(address) (*(volatile uint32_t *)(address))
#endif

// Private cache lines: CP clears byte zero after consuming the log text.
static uint8_t s_text[160] __attribute__((aligned(32)));
static uint32_t s_message[8] __attribute__((aligned(32)));
uint8_t t5ai_fault_stack[FAULT_STACK_BYTES] __attribute__((aligned(8)));
static bool s_entered;
static uint32_t s_regions[MAX_MPU_REGIONS][2];

extern void MemManage_Handler(void);

static bool prv_frame_valid(uintptr_t sp, uint32_t exc_return, uint32_t cfsr) {
  // Reject partial stacking, lazy-FP faults and stack overflow before any load.
  if (cfsr & ((1u << 4) | (1u << 5) | (1u << 12) | (1u << 13) | (1u << 20)))
    return false;
  if ((exc_return & 0xffffff81u) != 0xffffff81u || (sp & 3u))
    return false;
  const uintptr_t bytes = ((exc_return & 16u) ? 8u : 26u) * sizeof(uint32_t);
  // All native task and handler stacks are in internal AP RAM, never PSRAM.
  return sp >= 0x28010000u && sp <= 0x28066000u - bytes;
}

static size_t prv_hex(size_t at, uint32_t value) {
  static const char digits[] = "0123456789abcdef";
  for (unsigned i = 0; i < 8; ++i)
    s_text[at++] = digits[(value >> (28u - i * 4u)) & 15u];
  return at;
}

static size_t prv_field(size_t at, const char *label, uint32_t value) {
  while (*label)
    s_text[at++] = *label++;
  return prv_hex(at, value);
}

static bool prv_send(size_t length, uint32_t start) {
  if ((uint32_t)(REG32(0x4400020cu) - start) >= TIMEOUT_TICKS)
    return false;
  unsigned polls = POLL_LIMIT;
  while (REG32(0x41000060u) & 1u) {
    if (!--polls || (uint32_t)(REG32(0x4400020cu) - start) >= TIMEOUT_TICKS)
      return false;
  }
  // Same pinned assert-out ABI as cp_mailbox_log, using independent storage.
  // Fatal path only: do not drain RX, steal ACKs, reset or remux peripherals.
  s_message[0] = 0x1efe0004u;
  s_message[1] = (uint32_t)(uintptr_t)s_text;
  s_message[2] = 0xffff0000u | (uint32_t)length;
  s_message[3] = 0;
  dcache_flush(s_text, sizeof(s_text));
  dcache_flush(s_message, sizeof(s_message));
  REG32(0x41000088u) = (uint32_t)(uintptr_t)s_message;
  REG32(0x4100008cu) = 16u;
  __DSB();
  REG32(0x41000090u) = 0u;
  __DSB();
  polls = POLL_LIMIT;
  do {
    dcache_invalidate(s_text, sizeof(s_text));
    if (s_text[0] == 0)
      return true;
  } while (--polls && (uint32_t)(REG32(0x4400020cu) - start) < TIMEOUT_TICKS);
  return false;
}

static size_t prv_begin(const char *prefix) {
  s_text[0] = 1;
  size_t at = 1;
  while (*prefix)
    s_text[at++] = *prefix++;
  return at;
}

static bool prv_end(size_t at, uint32_t start) {
  s_text[at++] = '\r';
  s_text[at++] = '\n';
  return prv_send(at, start);
}

static bool prv_origin(uint32_t *frame, uint32_t exc_return, uint32_t cfsr, uint32_t **origin,
                       uint32_t *origin_exc) {
  if (!prv_frame_valid((uintptr_t)frame, exc_return, cfsr) || (exc_return & 12u) != 0 ||
      (frame[7] & 0x1ffu) != 4u || frame[6] != ((uintptr_t)MemManage_Handler & ~(uintptr_t)1u))
    return false;
  // At the first instruction, stacked LR is still the original EXC_RETURN.
  *origin_exc = frame[5];
  switch (*origin_exc) {
    case 0xfffffff1u:
    case 0xfffffff9u:
    case 0xfffffffdu:
    case 0xffffffe1u:
    case 0xffffffe9u:
    case 0xffffffedu:
      break;
    default:
      return false;
  }
  uintptr_t sp;
  if (*origin_exc & 4u) {
    sp = __get_PSP();
  } else {
    sp = (uintptr_t)frame + ((exc_return & 16u) ? 8u : 26u) * sizeof(uint32_t);
    if (frame[7] & (1u << 9))
      sp += sizeof(uint32_t);
  }
  if (!prv_frame_valid(sp, *origin_exc, cfsr))
    return false;
  *origin = (uint32_t *)sp;
  return true;
}

// Called on the private MSP stack, with interrupts disabled. Never logs via tasks.
__attribute__((used, noinline)) void t5ai_fault_trace_capture(uint32_t *frame, uint32_t exc_return,
                                                              uint32_t original_msplim,
                                                              uint32_t original_primask) {
  if (s_entered)
    return;
  s_entered = true;
  const uint32_t start = REG32(0x4400020cu);
  const uint32_t cfsr = SCB->CFSR;
  const uint32_t type = MPU->TYPE, ctrl = MPU->CTRL, selector = MPU->RNR;
  const uint32_t mair0 = MPU->MAIR0, mair1 = MPU->MAIR1;
  const uint32_t count = (type >> 8) & 0xffu;
  const uint32_t captured = count < MAX_MPU_REGIONS ? count : MAX_MPU_REGIONS;
  // Do not change region contents, MPU enable state or memory attributes.
  for (uint32_t i = 0; i < captured; ++i) {
    MPU->RNR = i;
    s_regions[i][0] = MPU->RBAR;
    s_regions[i][1] = MPU->RLAR;
  }
  MPU->RNR = selector;
  __DSB();
  __ISB();
  const bool valid = prv_frame_valid((uintptr_t)frame, exc_return, cfsr);
  uint32_t pc = 0, lr = 0, psr = 0;
  if (valid) {
    // FP state follows the core frame in Armv8-M; PC/LR offsets stay fixed.
    const uint32_t *core = frame;
    lr = core[5];
    pc = core[6];
    psr = core[7];
  }
  size_t at = prv_begin("\r\nT5FAULT");
  at = prv_field(at, " pc=", pc);
  at = prv_field(at, " lr=", lr);
  at = prv_field(at, " psr=", psr);
  at = prv_field(at, " exc=", exc_return);
  at = prv_field(at, " sp=", (uintptr_t)frame);
  at = prv_field(at, " cfsr=", cfsr);
  at = prv_field(at, " hfsr=", SCB->HFSR);
  at = prv_field(at, " mmfar=", (cfsr & (1u << 7)) ? SCB->MMFAR : 0);
  at = prv_field(at, " bfar=", (cfsr & (1u << 15)) ? SCB->BFAR : 0);
  s_text[at++] = ' ';
  s_text[at++] = valid ? 'V' : 'X';
  if (!prv_end(at, start))
    return;
  at = prv_begin("T5MPU");
  at = prv_field(at, " type=", type);
  at = prv_field(at, " ctrl=", ctrl);
  at = prv_field(at, " rnr=", selector);
  at = prv_field(at, " mair0=", mair0);
  at = prv_field(at, " mair1=", mair1);
  at = prv_field(at, " count=", captured);
  if (!prv_end(at, start))
    return;
  at = prv_begin("T5CPU");
  at = prv_field(at, " control=", __get_CONTROL());
  at = prv_field(at, " basepri=", __get_BASEPRI());
  at = prv_field(at, " primask=", original_primask);
  at = prv_field(at, " msplim=", original_msplim);
  at = prv_field(at, " psplim=", __get_PSPLIM());
  at = prv_field(at, " shcsr=", SCB->SHCSR);
  at = prv_field(at, " ccr=", SCB->CCR);
  if (!prv_end(at, start))
    return;
  uint32_t *origin = NULL, origin_exc = 0;
  const bool origin_valid = prv_origin(frame, exc_return, cfsr, &origin, &origin_exc);
  at = prv_begin("T5ORIGIN");
  at = prv_field(at, " sp=", (uintptr_t)origin);
  at = prv_field(at, " exc=", origin_exc);
  at = prv_field(at, " pc=", origin_valid ? origin[6] : 0);
  at = prv_field(at, " lr=", origin_valid ? origin[5] : 0);
  at = prv_field(at, " psr=", origin_valid ? origin[7] : 0);
  s_text[at++] = ' ';
  s_text[at++] = origin_valid ? 'V' : 'X';
  if (!prv_end(at, start))
    return;
  for (uint32_t i = 0; i < captured; ++i) {
    at = prv_begin("T5REGION");
    at = prv_field(at, " n=", i);
    at = prv_field(at, " rbar=", s_regions[i][0]);
    at = prv_field(at, " rlar=", s_regions[i][1]);
    if (!prv_end(at, start))
      return;
  }
}

// Save state without using the interrupted stack. Switch its limit before pushing.
#ifndef T5AI_FAULT_TRACE_TEST
__attribute__((naked)) void HardFault_Handler(void) {
  __asm volatile(
      "tst lr, #4\n"
      "ite eq\n"
      "mrseq r0, msp\n"
      "mrsne r0, psp\n"
      "mov r1, lr\n"
      "mrs r2, msp\n"
      "mrs r3, primask\n"
      "cpsid i\n"
      "mrs r12, msplim\n"
      "ldr lr, =t5ai_fault_stack\n"
      "msr msplim, lr\n"
      "add lr, lr, #512\n"
      "msr msp, lr\n"
      "push {r0, r1, r2, r3, r12, lr}\n"
      "mov r2, r12\n"
      "bl t5ai_fault_trace_capture\n"
      "pop {r0, r1, r2, r3, r12, lr}\n"
      "mov lr, r1\n"
      "msr msp, r2\n"
      "msr msplim, r12\n"
      "msr primask, r3\n"
      "b t5ai_stock_HardFault_Handler\n");
}
#endif
