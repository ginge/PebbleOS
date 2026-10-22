# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Execute the production SD adapter and driver against host storage/MMIO models."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMMON = r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define nullptr NULL
#define CONFIG_T5AI_SD_4BIT 1
#define PBL_FOREVER 0
#define PBL_MSEC(x) (x)
#define PBL_MUTEX_DEFINE(n) int n
#define PBL_SEM_DEFINE(n,a,b) int n
#define PBL_ASSERTN(x) assert(x)
#define PBL_ASSERT(x,...) assert(x)
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_INFO(...) ((void)0)
#define PBL_LOG_ERR(...) ((void)0)
#define LOG_LEVEL_DEBUG 0
#define S_SUCCESS 0
#define __DSB() ((void)0)
#define SDIO_IRQn 10
#define GDMA_IRQn 11
#define NVIC_SetPriority(...) ((void)0)
#define NVIC_EnableIRQ(...) ((void)0)
typedef int status_t;
static inline void pbl_mutex_lock(void *m, int t) {(void)m;(void)t;}
static inline void pbl_mutex_unlock(void *m) {(void)m;}
static inline void pbl_irq_lock(void) {}
static inline void pbl_irq_unlock(void) {}
static inline void pbl_thread_yield(void) {}
static inline void pbl_sem_reset(void *s) {(void)s;}
static inline void pbl_sem_give(void *s) {(void)s;}
static inline void dcache_flush(const void *p, size_t n) {(void)p;(void)n;}
static inline void dcache_flush_invalidate(const void *p, size_t n) {(void)p;(void)n;}
static inline void dcache_invalidate(void *p, size_t n) {(void)p;(void)n;}
void delay_us(unsigned n);
uint64_t pbl_uptime_ms(void);
int pbl_sem_take(void *, int);
void pbl_thread_sleep(int);
int t5ai_cp_mailbox_enable_sd(void);
void pfs_set_size(uint32_t, bool);
void pfs_format(bool);
bool pfs_active(void);
void pfs_reboot_cleanup(void);
"""
ADAPTER = r"""
#include <stdio.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>
#include "SOURCE"
static uint8_t card[35u*1024u*1024u];
static unsigned reads, writes, formats, cleanups;
static uint8_t workspace[ERASE_BYTES];
static unsigned workspace_depth;
uint8_t *t5ai_sd_lock_workspace(void) { ++workspace_depth; return workspace; }
void t5ai_sd_unlock_workspace(void) { assert(workspace_depth); --workspace_depth; }
static uint32_t volume_size;
static int fault;
static uint64_t capacity = sizeof(card)/512;
int t5ai_sd_init(void) { return fault; }
uint64_t t5ai_sd_sector_count(void) { return capacity; }
int t5ai_sd_read(uint32_t lba, void *buf, uint32_t n) {
  if (fault) return fault;
  assert(workspace_depth);
  assert(n && n <= T5AI_SD_MAX_BLOCKS && (uint64_t)lba+n <= capacity);
  memcpy(buf, card+lba*512u, n*512u); ++reads; return 0;
}
int t5ai_sd_write(uint32_t lba, const void *buf, uint32_t n) {
  if (fault) return fault;
  assert(workspace_depth);
  assert(n && n <= T5AI_SD_MAX_BLOCKS && (uint64_t)lba+n <= capacity);
  memcpy(card+lba*512u, buf, n*512u); ++writes; return 0;
}
void pfs_set_size(uint32_t size, bool erased) { assert(!erased); volume_size=size; }
bool pfs_active(void) { return card[FIRST_SECTOR*512u] == 0x50; }
void pfs_reboot_cleanup(void) { ++cleanups; }
void pfs_format(bool headers) {
  assert(headers); ++formats; __wrap_filesystem_regions_erase_all();
  uint8_t mark=0x50; __wrap_ftl_write(&mark, 1, 0);
}
status_t __real_pfs_init(bool check) {
  assert(check); __wrap_ftl_populate_region_list();
  if (!pfs_active()) pfs_format(true);
  return 0;
}
static void expect_abort(unsigned scenario) {
  pid_t child=fork(); assert(child>=0);
  if (!child) {
    if (scenario==0) __wrap_ftl_read(s_work, 2, VOLUME_SIZE-1);
    if (scenario==1) { fault=-5; __wrap_ftl_write(s_work, 1, 17); }
    if (scenario==2) { capacity=FIRST_SECTOR+VOLUME_SIZE/512-1; __wrap_pfs_init(true); }
    if (scenario==3) { fault=-5; __wrap_pfs_init(true); }
    _exit(0);
  }
  int status; assert(waitpid(child,&status,0)==child);
  assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
}
int main(void) {
  setrlimit(RLIMIT_CORE, &(struct rlimit){0,0});
  memset(card,0x6e,sizeof(card));
  assert(__wrap_pfs_init(true)==0 && formats==1 && volume_size==32u*1024u*1024u);
  assert(__wrap_ftl_get_size()==volume_size);
  assert(card[HEADER_SECTOR*512u-1]==0x6e);
  assert(card[(FIRST_SECTOR+VOLUME_SIZE/512u)*512u]==0x6e);
  unsigned old_formats=formats;
  assert(__wrap_pfs_init(true)==0 && formats==old_formats && cleanups==1);
  uint8_t input[20001], output[20001];
  for (unsigned i=0;i<sizeof(input);++i) input[i]=(uint8_t)(i*17u+3u);
  __wrap_ftl_write(input,sizeof(input),511);
  __wrap_ftl_read(output,sizeof(output),511);
  assert(!memcmp(input,output,sizeof(input)));
  assert(card[FIRST_SECTOR*512u+510]==0xff);
  assert(card[FIRST_SECTOR*512u+511+sizeof(input)]==0xff);
  memset(input,0xff,sizeof(input));
  __wrap_ftl_write(input,sizeof(input),511);
  __wrap_ftl_read(output,sizeof(output),511);
  for (unsigned i=0;i<sizeof(output);++i) assert(output[i]==(uint8_t)(i*17u+3u));
  __wrap_ftl_erase_sector(65536,0);
  __wrap_ftl_read(output,sizeof(output),511);
  assert(!memcmp(input,output,sizeof(input)));
  uint8_t zero=0;
  __wrap_ftl_write(&zero,1,VOLUME_SIZE-1);
  __wrap_ftl_read(output,1,VOLUME_SIZE-1); assert(!output[0]);
  __wrap_ftl_erase_subsector(4096,VOLUME_SIZE-4096);
  __wrap_ftl_read(output,1,VOLUME_SIZE-1); assert(output[0]==0xff);
  unsigned old_reads=reads;
  __wrap_ftl_read(output,1,4096); __wrap_ftl_read(output,1,4097);
  assert(reads==old_reads+1);
  assert(workspace_depth==0);
  for(unsigned i=0;i<4;++i) expect_abort(i);
  printf("PASS: provision, reopen, boundaries, partial/multi-sector I/O, NOR semantics, cache, erase, faults (%u writes)\n",writes);
}
"""
DRIVER = r"""
#include <stdio.h>
#include <sys/mman.h>
#include "SOURCE"
// Independent MMIO model apertures verify the production base addresses.
#define MODEL_SYSTEM_BASE 0x44000000u
#define MODEL_SYSTEM_SIZE 0x20000u
#define MODEL_GDMA_BASE 0x45020000u
#define MODEL_SDIO_BASE 0x458d0000u
#define MODEL_PERIPHERAL_SIZE 0x1000u
#define MODEL_CARD_DETECT_ADDRESS 0x44000420u
static uint64_t ticks;
static uint8_t card[1024u*1024u];
static unsigned transfer_sector, transfer_count, ready_polls, fail_data, legacy, transfer_argument;
static bool card_missing;
static unsigned completion_order;
void delay_us(unsigned n) {
  // Model the GPIO input bit, which is read-only in the real peripheral.
  if(n==5000) REG(MODEL_CARD_DETECT_ADDRESS)=(REG(MODEL_CARD_DETECT_ADDRESS)&~1u)|(card_missing?1u:0u);
}
uint64_t pbl_uptime_ms(void) {return ticks++;}
void pbl_thread_sleep(int n) {ticks+=n;}
int t5ai_cp_mailbox_enable_sd(void) {return 0;}
int pbl_sem_take(void *unused, int timeout) {
  (void)unused; ticks+=timeout;
  if(SD(SD_COMMAND_CONTROL)&1u) {
    unsigned cmd=SD(SD_COMMAND_CONTROL)>>4;
    SD(SD_COMMAND_CONTROL)&=~1u;
    uint32_t response=0;
    if(cmd==8) response=0x1aa;
    if(cmd==41) response=legacy ? 0x80ff8000 : 0xc0ff8000;
    if(cmd==3) response=0x10000;
    if(cmd==9) {
      response=legacy ? 0 : 1u<<30;
      SD(SD_RESPONSE_ARGUMENT1)=legacy ? (9u<<16)|1023u : 0;
      SD(SD_RESPONSE_ARGUMENT2)=legacy ? (3u<<30)|(7u<<15) : 127u<<16;
    }
    if(cmd==13) {
      assert(!(SD(SD_INTERRUPT_MASK)&SD_TX_FIFO_CLOCK_GATE));
      response=0x900; ++ready_polls;
    }
    if(cmd==17||cmd==18||cmd==24||cmd==25) {
      if(cmd==24||cmd==25) {
        // A write command must precede release/configuration of the data engine.
        assert(cmd==25);
        assert(SD(SD_DATA_CONTROL)==0);
        assert(!(SD(SD_FIFO_CONTROL)&SD_DATA_STATE_RESET_RELEASE));
      }
      transfer_argument=SD(SD_COMMAND_ARGUMENT);
      transfer_sector=legacy ? transfer_argument/512u : transfer_argument;
    }
    SD(SD_RESPONSE_ARGUMENT0)=response;
    SD(SD_INTERRUPT_STATUS)=cmd==8 && legacy ? CMD_TIMEOUT : cmd ? 2u|(1u<<10) : 1u;
    t5ai_sd_irq(); SD(SD_INTERRUPT_STATUS)=0;
    return 0;
  }
  for(unsigned channel=TX_CHANNEL;channel<=RX_CHANNEL;++channel) {
    if(!(CH(channel,DMA_CHANNEL_CONTROL)&1u)) continue;
    const bool write=channel==TX_CHANNEL;
    const size_t bytes=(CH(channel,DMA_CHANNEL_CONTROL)>>16)+1u;
    void *memory=(void *)(uintptr_t)CH(channel,write?DMA_CHANNEL_SOURCE:DMA_CHANNEL_DESTINATION);
    assert(bytes && bytes<=8192 && transfer_sector*512u+bytes<=sizeof(card));
    CH(TX_CHANNEL,DMA_CHANNEL_STATUS)=0; CH(RX_CHANNEL,DMA_CHANNEL_STATUS)=0;
    if(fail_data==1) { CH(channel,DMA_CHANNEL_STATUS)=DMA_ERROR; t5ai_sd_dma_irq(); return 0; }
    if(fail_data==2) return 0;
    if(write) memcpy(card+transfer_sector*512u,memory,bytes);
    else memcpy(memory,card+transfer_sector*512u,bytes);
    if(write && completion_order==2) {
      // FIFO empty between blocks is not final completion while DMA is active.
      SD(SD_INTERRUPT_STATUS)=(1u<<4)|(1u<<9)|(2u<<20);
      t5ai_sd_irq(); SD(SD_INTERRUPT_STATUS)=0;
      assert(!(s_data_status & TX_EMPTY));
    }
    CH(channel,DMA_CHANNEL_CONTROL)&=~1u; CH(channel,DMA_CHANNEL_STATUS)=DMA_FINISH;
    if(write && completion_order==1) {
      // Hardware finished DMA, but the SD ISR runs before the DMA ISR.
      assert(!(SD(SD_INTERRUPT_MASK)&TX_EMPTY));
      SD(SD_INTERRUPT_STATUS)=(1u<<4)|(1u<<9)|(2u<<20);
      t5ai_sd_irq(); SD(SD_INTERRUPT_STATUS)=0;
      t5ai_sd_dma_irq(); CH(channel,DMA_CHANNEL_STATUS)=0;
    } else {
      t5ai_sd_dma_irq(); CH(channel,DMA_CHANNEL_STATUS)=0;
      SD(SD_INTERRUPT_STATUS)=write ? (1u<<4)|(1u<<9)|(2u<<20) : (1u<<3)|(1u<<12);
      t5ai_sd_irq(); SD(SD_INTERRUPT_STATUS)=0;
    }
    ++transfer_count;
  }
  return 0;
}
static void map(uintptr_t address, size_t size) {
  assert(mmap((void *)address,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==(void *)address);
}
int main(void) {
  map(MODEL_SYSTEM_BASE,MODEL_SYSTEM_SIZE); map(MODEL_GDMA_BASE,MODEL_PERIPHERAL_SIZE); map(MODEL_SDIO_BASE,MODEL_PERIPHERAL_SIZE);
  // Set unrelated DMA channel state; init/abort must not change it.
  CH(0,DMA_CHANNEL_CONTROL)=0x123456; CH(1,DMA_CHANNEL_CONTROL)=0xabcdef;
  card_missing=true;
  assert(t5ai_sd_init()==-ENODEV);
  card_missing=false;
  assert(t5ai_sd_init()==0 && t5ai_sd_sector_count()==131072);
  assert(CH(0,DMA_CHANNEL_CONTROL)==0x123456 && CH(1,DMA_CHANNEL_CONTROL)==0xabcdef);
  uint8_t input[8193], output[8193];
  for(unsigned i=0;i<sizeof(input);++i) input[i]=(uint8_t)(i*13u);
  assert(t5ai_sd_write(4,input+1,8)==0);
  assert(t5ai_sd_read(4,output+1,8)==0);
  assert(!memcmp(input+1,output+1,4096) && transfer_count==2 && ready_polls>=4);
  completion_order=1;
  assert(t5ai_sd_write(4,input+1,1)==0);
  completion_order=2;
  assert(t5ai_sd_write(4,input+1,8)==0);
  completion_order=0;
  assert(t5ai_sd_read(0,output,0)==-EINVAL);
  assert(t5ai_sd_read(0,output,9)==-EINVAL);
  assert(t5ai_sd_read(131072,output,1)==-EINVAL);
  s_ready=false; legacy=1;
  assert(t5ai_sd_init()==0 && t5ai_sd_sector_count()==2097152);
  // SDSC arguments must be byte addresses, not LBAs.
  assert(t5ai_sd_read(4,output,1)==0 && transfer_argument==4u*512u);
  fail_data=1;
  assert(t5ai_sd_read(0,output,1)==-EIO);
  assert(!CH(TX_CHANNEL,DMA_CHANNEL_CONTROL) && !CH(RX_CHANNEL,DMA_CHANNEL_CONTROL));
  assert(CH(0,DMA_CHANNEL_CONTROL)==0x123456 && CH(1,DMA_CHANNEL_CONTROL)==0xabcdef);
  assert(t5ai_sd_read(4,output,1)==-ENODEV);
  s_faulted=false; fail_data=2;
  assert(t5ai_sd_read(4,output,1)==-ETIMEDOUT);
  assert(!CH(TX_CHANNEL,DMA_CHANNEL_CONTROL) && !CH(RX_CHANNEL,DMA_CHANNEL_CONTROL));
  puts("PASS: SD init/CSD, DMA multi-block roundtrip, bounce buffers, bounds, DMA error/timeout, quarantine, other-channel preservation");
}
"""


class SDTests(unittest.TestCase):
    def compile_run(self, source, harness, four_bit=True):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "common.h").write_text(
                COMMON
                if four_bit
                else COMMON.replace("#define CONFIG_T5AI_SD_4BIT 1", "")
            )
            headers = [
                "pbl/kernel/kernel.h",
                "pbl/kernel/time.h",
                "pbl/mcu/cache.h",
                "pbl/soc/t5ai/cp_mailbox.h",
                "pbl/soc/t5ai/delay.h",
                "cmsis_core.h",
                "pbl/logging/logging.h",
                "pbl/services/filesystem/pfs.h",
                "system/passert.h",
            ]
            for name in headers:
                path = tmp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "common.h"\n')
            (tmp / "harness.c").write_text(
                harness.replace('"SOURCE"', '"' + str(ROOT / source) + '"')
            )
            subprocess.run(
                [
                    "cc",
                    "-std=gnu11",
                    "-no-pie",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(tmp),
                    "-I",
                    str(ROOT / "include"),
                    str(tmp / "harness.c"),
                    "-o",
                    str(tmp / "harness"),
                ],
                check=True,
            )
            subprocess.run([str(tmp / "harness")], check=True, timeout=30)

    def test_adapter(self):
        self.compile_run("soc/t5ai/sd_pfs.c", ADAPTER)

    def test_driver(self):
        self.compile_run("drivers/t5ai/sd.c", DRIVER)

    def test_one_bit_diagnostic_driver(self):
        self.compile_run("drivers/t5ai/sd.c", DRIVER, four_bit=False)


if __name__ == "__main__":
    unittest.main()
