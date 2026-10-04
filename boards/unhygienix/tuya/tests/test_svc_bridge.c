/* SPDX-License-Identifier: Apache-2.0 */
#include "svc_bridge.h"
#include "pbl/mcu/syscall_abi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  _Alignas(8) uint32_t input[128];
  _Alignas(8) uint32_t output[128];
  TuyaSvcRange source, destination;
  bool authorized;
  uint16_t instruction;
  unsigned reads, authorized_calls, fp_calls, setups, commits;
  uintptr_t original_sp, limit;
  uint32_t *committed;
} State;
static bool task_stack(void *ctx, TuyaSvcRange *range) {
  *range = ((State *)ctx)->source;
  return true;
}
static bool authorize(void *ctx, uint32_t pc) {
  State *s = ctx;
  ++s->authorized_calls;
  assert(pc == 0x1002);
  return s->authorized;
}
static uint16_t instruction(void *ctx, uint32_t pc) {
  State *s = ctx;
  ++s->reads;
  assert(s->authorized && pc == 0x1000);
  return s->instruction;
}
static bool syscall_stack(void *ctx, TuyaSvcRange *range) {
  *range = ((State *)ctx)->destination;
  return true;
}
static void fp(void *ctx) {
  State *s = ctx;
  assert(!s->setups && !s->commits);
  ++s->fp_calls;
  for (size_t i = 8; i < 26; ++i) {
    s->input[i] = 0xf000u + i;
  }
}
static void setup(void *ctx, uintptr_t sp, uint32_t *lr) {
  State *s = ctx;
  assert(!s->commits);
  ++s->setups;
  s->original_sp = sp;
  assert(*lr == 0x12345679);
  *lr = 0xdeadbeef;
}
static void commit(void *ctx, uint32_t *frame, uintptr_t limit) {
  State *s = ctx;
  assert(s->setups == 1);
  ++s->commits;
  s->committed = frame;
  s->limit = limit;
}
static TuyaSvcHooks init(State *s, bool floating, bool padding) {
  memset(s, 0, sizeof(*s));
  for (unsigned i = 0; i < 128; ++i) {
    s->input[i] = i;
    s->output[i] = 0xcccccccc;
  }
  s->input[5] = 0x12345679;
  s->input[6] = 0x1002;
  s->input[7] = (1u << 24) | (padding ? 1u << 9 : 0);
  s->source = (TuyaSvcRange){(uintptr_t)s->input, (uintptr_t)(s->input + 128)};
  s->destination = (TuyaSvcRange){(uintptr_t)s->output, (uintptr_t)(s->output + 128)};
  s->authorized = true;
  s->instruction = 0xdf00 | PBL_SYSCALL_SVC;
  if (floating) {
    for (unsigned i = 8; i < 26; ++i) {
      s->input[i] = 0;
    }
  }
  return (TuyaSvcHooks){s, task_stack, authorize, instruction, syscall_stack, fp, setup, commit};
}
static void test_frames(void) {
  for (unsigned mode = 0; mode < 8; ++mode) {
    State s;
    bool floating = mode & 1, padding = mode & 2, nonsecure = mode & 4;
    TuyaSvcHooks hooks = init(&s, floating, padding);
    uint32_t exc = nonsecure ? 0xffffffbc : 0xfffffffd;
    if (floating) {
      exc &= ~0x10u;
    }
    unsigned words = floating ? 26 : 8;
    unsigned total = words + padding;
    assert(tuya_svc_dispatch(s.input, exc, 3, 1, 1, &hooks) == TuyaSvc_Entered);
    assert(s.commits == 1 && s.setups == 1 && s.fp_calls == (unsigned)floating);
    assert(s.original_sp == (uintptr_t)(s.input + total));
    assert(s.committed == s.output + 128 - words - 16);
    assert(s.limit == s.destination.low && s.committed[5] == 0xdeadbeef);
    assert(s.input[5] == 0x12345679 && s.committed[7] == (1u << 24));
    for (unsigned i = 0; i < words; ++i) {
      if (i != 5 && i != 7) {
        assert(s.committed[i] == s.input[i]);
      }
    }
    assert(memcmp(s.committed + words, s.input + total, 16 * sizeof(uint32_t)) == 0);
    assert(s.output[128 - words - 17] == 0xcccccccc);
  }
}
static void test_short_args_and_no_relocation(void) {
  State s;
  TuyaSvcHooks hooks = init(&s, false, false);
  s.source.high = (uintptr_t)(s.input + 10);
  assert(tuya_svc_dispatch(s.input, 0xfffffffd, 3, 1, 1, &hooks) == TuyaSvc_Entered);
  assert(s.committed[8] == 8 && s.committed[9] == 9);
  for (unsigned i = 10; i < 24; ++i) {
    assert(s.committed[i] == 0);
  }
  hooks = init(&s, true, true);
  s.destination = (TuyaSvcRange){0};
  assert(tuya_svc_dispatch(s.input, 0xffffffed, 3, 1, 1, &hooks) == TuyaSvc_Entered);
  assert(s.committed == s.input && s.limit == 0 && s.input[5] == 0xdeadbeef);
  assert(s.input[7] == ((1u << 24) | (1u << 9)));
}
static void test_rejections(void) {
  for (unsigned mode = 0; mode < 14; ++mode) {
    State s;
    TuyaSvcHooks hooks = init(&s, false, false);
    uint32_t exc = 0xfffffffd, core = 1;
    switch (mode) {
      case 0:
        s.authorized = false;
        break;
      case 1:
        s.instruction = 0xdf02;
        break; /* Vendor scheduler SVC never accepted from an app. */
      case 2:
        core = 0;
        break;
      case 3:
        exc = 0xfffffff9;
        break; /* MSP */
      case 4:
        s.source.high = s.source.low + 28;
        break;
      case 5:
        exc = 0xffffffed;
        s.source.high = s.source.low + 100;
        break;
      case 6:
        s.input[7] |= 1u << 9;
        s.source.high = s.source.low + 32;
        break;
      case 7:
        s.destination.high = s.destination.low + 88;
        break;
      case 8:
        s.destination = s.source;
        break;
      case 9:
        s.destination.high -= 4;
        break;
      case 10:
        s.destination.high = s.destination.low;
        break;
      case 11:
        s.input[7] = 0;
        break;
      case 12:
        s.input[7] |= 3;
        break; /* Handler-mode frame */
      case 13:
        s.input[6] = 1;
        break;
    }
    assert(tuya_svc_dispatch(s.input, exc, 3, core, 1, &hooks) == TuyaSvc_Denied);
    assert(!s.fp_calls && !s.setups && !s.commits);
    assert(s.input[5] == 0x12345679);
    for (unsigned i = 0; i < 128; ++i) {
      assert(s.output[i] == 0xcccccccc);
    }
    if (mode == 0) {
      assert(!s.reads);
    }
  }
  assert(tuya_svc_dispatch(NULL, 0, 2, 0, 1, NULL) == TuyaSvc_Vendor);
  assert(tuya_svc_dispatch(NULL, 0xfffffffd, 3, 1, 1, NULL) == TuyaSvc_Denied);
}
int main(void) {
  test_frames();
  test_short_args_and_no_relocation();
  test_rejections();
  puts("SVC bridge: frame bounds, FP/padding, arguments, authorization and core policy passed");
}
