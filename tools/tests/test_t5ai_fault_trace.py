# SPDX-License-Identifier: Apache-2.0
"""Exercise actual minimal fault formatting and bounded emergency publication."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
static struct { uint32_t CFSR, HFSR, MMFAR, BFAR, SHCSR, CCR; } scb;
#define SCB (&scb)
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define __get_CONTROL() 3u
#define __get_BASEPRI() 0x80u
#define __get_PSPLIM() 0x28010000u
static uint32_t psp = 0x28020200;
#define __get_PSP() psp
static struct { uint32_t TYPE, CTRL, RNR, RBAR, RLAR, MAIR0, MAIR1; } mpu;
static uint32_t regions[16][2];
static typeof(mpu) *mpu_access(void) {
  assert(mpu.RNR < 16);
  mpu.RBAR=regions[mpu.RNR][0]; mpu.RLAR=regions[mpu.RNR][1];
  return &mpu;
}
#define MPU (mpu_access())
void MemManage_Handler(void) {}
static char output[4096];
static unsigned publications;
static uint32_t clock_low, fifo, tx_address, tx_size, tx_id;
static unsigned invalidations, flushes;
static int release;
static uint32_t clock_step;
static volatile uint32_t *reg(uintptr_t address) {
  switch(address) {
    case 0x4400020c: clock_low += clock_step; return &clock_low;
    case 0x41000060: return &fifo;
    case 0x41000088: return &tx_address;
    case 0x4100008c: return &tx_size;
    case 0x41000090: return &tx_id;
    default: assert(0); return 0;
  }
}
#define REG32(address) (*reg(address))
#define T5AI_FAULT_TRACE_TEST
#include "soc/t5ai/fault_trace.c"
void dcache_flush(const void *p, size_t size) {
  assert((uintptr_t)p % 32 == 0 && size % 32 == 0); ++flushes;
  if (p == s_text) {
    unsigned n = s_message[2] & 0xffff;
    assert(n < sizeof(s_text));
    unsigned used=strlen(output);
    assert(used+n < sizeof(output));
    memcpy(output+used, s_text+1, n-1); output[used+n-1]=0;
    ++publications;
  }
}
void dcache_invalidate(void *p, size_t size) {
  assert(p == s_text && size == sizeof(s_text)); ++invalidations;
  if (release) s_text[0] = 0;
}
static void capture(uint32_t *frame, uint32_t exc) {
  s_entered = false; tx_size = 0; invalidations = flushes = publications = 0;
  output[0]=0;
  t5ai_fault_trace_capture(frame, exc, 0x28026000, 0);
  unsigned count=(mpu.TYPE>>8)&255;
  if (count>16) count=16;
  assert(tx_size == 16 && publications == 4+count);
  assert(flushes == 2*publications && invalidations == publications);
  assert(mpu.RNR == 7);
  assert(s_message[0] == 0x1efe0004 && s_message[2] >> 16 == 0xffff);
  unsigned n = s_message[2] & 0xffff;
  assert(n < sizeof(s_text));
  s_text[n] = 0;
  assert(strstr(output, "T5FAULT") && strstr(output,"hfsr=40000000"));
}
int main(void) {
  uint32_t *frame = mmap((void *)0x28020000, 4096, PROT_READ|PROT_WRITE,
       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
  assert(frame == (void *)0x28020000);
  release=1; scb.HFSR=0x40000000;
  mpu.TYPE=16<<8; mpu.CTRL=5; mpu.RNR=7; mpu.MAIR0=0x44aa;
  for (unsigned i=0;i<16;++i) { regions[i][0]=0x02000006+i*32; regions[i][1]=0x020ffff3+i*32; }
  frame[5]=0x02123457; frame[6]=0x021abcde; frame[7]=0x21000000;
  scb.CFSR=(1<<7)|(1<<15); scb.MMFAR=0x123; scb.BFAR=0x456;
  capture(frame,0xfffffffd);
  assert(strstr(output,"pc=021abcde lr=02123457 psr=21000000"));
  assert(strstr(output,"mmfar=00000123 bfar=00000456 V"));
  frame[23]=0x12345679; frame[24]=0x02113579; frame[25]=0x21000000;
  capture(frame,0xffffffed);
  assert(strstr(output,"pc=021abcde lr=02123457"));
  for (unsigned bit=0; bit<32; ++bit) {
    scb.CFSR=1u<<bit;
    if (bit==4 || bit==5 || bit==12 || bit==13 || bit==20) {
      capture((void *)0x28030000,0xfffffffd); // Unmapped frame must not be touched.
      assert(strstr(output,"pc=00000000 lr=00000000"));
      assert(strstr(output," X"));
    }
  }
  scb.CFSR=0;
  assert(!prv_frame_valid(0x2800fffcu,0xfffffffd,0));
  assert(!prv_frame_valid(0x28065fe4u,0xfffffffd,0));
  assert(!prv_frame_valid(0x28065f9cu,0xffffffed,0));
  assert(!prv_frame_valid(0x28020002u,0xfffffffd,0));
  assert(!prv_frame_valid(0x28020000u,0x1234,0));
  capture(frame,0xfffffffd);
  assert(strstr(output,"mmfar=00000000 bfar=00000000"));
  assert(strstr(output,"T5MPU type=00001000 ctrl=00000005 rnr=00000007 mair0=000044aa"));
  assert(strstr(output,"T5CPU control=00000003 basepri=00000080 primask=00000000 msplim=28026000"));
  assert(strstr(output,"T5REGION n=0000000f rbar=020001e6 rlar=021001d3"));
  uint32_t *origin=NULL, origin_exc=0;
  frame[5]=0xfffffff1; frame[6]=(uintptr_t)MemManage_Handler & ~(uintptr_t)1u; frame[7]=0x21000004;
  frame[8+5]=0x02123457; frame[8+6]=0x021abcdf; frame[8+7]=0x21000010;
  capture(frame,0xfffffff1);
  assert(strstr(output,"T5ORIGIN sp=28020020 exc=fffffff1 pc=021abcdf lr=02123457 psr=21000010 V"));
  frame[7] |= 1<<9;
  assert(prv_origin(frame,0xfffffff1,0,&origin,&origin_exc) && origin==frame+9);
  frame[7] &= ~(1<<9);
  assert(prv_origin(frame,0xffffffe1,0,&origin,&origin_exc) && origin==frame+26);
  frame[5]=0xfffffffd;
  assert(prv_origin(frame,0xfffffff1,0,&origin,&origin_exc) && origin==(void *)(uintptr_t)psp);
  psp=0x28066000; frame[5]=0xfffffffd;
  assert(!prv_origin(frame,0xfffffff1,0,&origin,&origin_exc));
  psp=0x28020200;
  frame[5]=0xffffffb1; // Unknown security-state encoding must not be followed.
  assert(!prv_origin(frame,0xfffffff1,0,&origin,&origin_exc));
  frame[5]=0x1234;
  assert(!prv_origin(frame,0xfffffff1,0,&origin,&origin_exc));
  frame[5]=0xfffffff1; frame[6]++;
  assert(!prv_origin(frame,0xfffffff1,0,&origin,&origin_exc));
  assert(!prv_origin((void *)0x28030000,0xfffffff1,1<<4,&origin,&origin_exc));
  mpu.TYPE=0xff<<8; capture(frame,0xfffffffd); // Report truncation via TYPE/count.
  mpu.TYPE=0; capture(frame,0xfffffffd); // No implemented regions is bounded.
  unsigned previous=flushes;
  t5ai_fault_trace_capture(frame,0xfffffffd,0,0);
  assert(flushes==previous); // Recursive entry does not republish.
  fifo=1; tx_size=0; prv_send(140, clock_low); assert(!tx_size); // Stopped AON still bounded.
  fifo=0; release=0; invalidations=0; s_text[0]=1;
  prv_send(140, clock_low); assert(invalidations==POLL_LIMIT); // CP never releases.
  clock_step=800; clock_low=0xfffffe00u; fifo=1; tx_size=0;
  prv_send(140, clock_low); assert(!tx_size); // Deadline is bounded across AON wrap.
  fifo=0; release=0; invalidations=0; s_text[0]=1;
  prv_send(140, clock_low); assert(invalidations > 0 && invalidations < POLL_LIMIT);
  clock_low=clock_step=0; fifo=0; release=0; s_entered=false;
  publications=0; output[0]=0;
  t5ai_fault_trace_capture(frame,0xfffffffd,0,0);
  assert(publications==1); // Never reuse an unacknowledged CP text buffer.
  clock_low=clock_step=0; fifo=0; release=1; s_entered=false;
  mpu.TYPE=16<<8; mpu.RNR=7; clock_step=200;
  publications=0; output[0]=0;
  t5ai_fault_trace_capture(frame,0xfffffffd,0,0);
  assert(publications>0 && publications<20); // One deadline across all lines.
  assert(mpu.RNR==7);
  clock_low=clock_step=0; fifo=0;
  munmap(frame,4096);
  return 0;
}
"""


class FaultTraceTests(unittest.TestCase):
    def test_actual_source(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / "cmsis_core.h").write_text("")
            cache = tmp / "pbl/mcu/cache.h"
            cache.parent.mkdir(parents=True)
            cache.write_text(
                "#include <stddef.h>\nvoid dcache_flush(const void *, size_t);\n"
                "void dcache_invalidate(void *, size_t);\n"
            )
            (tmp / "test.c").write_text(HARNESS)
            subprocess.run(
                ["cc", "-no-pie", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-pointer-to-int-cast", "-I", str(tmp), "-I", str(ROOT),
                 str(tmp / "test.c"), "-o", str(tmp / "test")],
                check=True,
            )
            subprocess.run([str(tmp / "test")], check=True, timeout=10)
