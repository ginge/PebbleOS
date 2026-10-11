# SPDX-License-Identifier: Apache-2.0
"""Execute production read-lease cleanup with injected payload and transport faults."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
typedef int status_t;
typedef unsigned pbl_prio_t;
#define S_SUCCESS 0
#define E_ERROR -1
#define E_AGAIN -2
#define E_INVALID_ARGUMENT -3
#define FAILED(s) ((s)<0)
#define FLASH_MAX_TRANSFER_BYTES UINT16_MAX
#define FLASH_ADDRESS_SHIFT 8
#define FLASH_READ 3
#define FLASH_READ_DONE 4
typedef struct {uint32_t part_and_address, buffer, length_and_status, crc;} FlashCommand;
static FlashCommand s_flash_command;
static int s_flash_mutex, locks, boosts, transactions, read_status, done_status;
static bool available, range_valid, bad_crc, bad_length;
static unsigned char *payload;
static unsigned now, lease_start;
static bool expired, expire_copy;
static void *s_flash_socket;
#define T5AI_AON_RTC_COUNT_LOW now
#define FLASH_READ_LEASE_AON_TICKS 1600u
static uint32_t t5ai_mb_ipc_last_receive_aon(void *s) { return lease_start; }
static void pbl_mutex_lock(void *p, int t) { ++locks; }
static void pbl_mutex_unlock(void *p) { --locks; }
#define PBL_FOREVER 0
static pbl_prio_t prv_boost_client_priority(void) { ++boosts; return 1; }
static void prv_restore_client_priority(pbl_prio_t p) { --boosts; }
static void prv_flash_quarantine(void) { available=false; }
static void prv_flash_fault(status_t s, unsigned c) { }
static bool t5ai_cp_mailbox_shared_range_valid(uintptr_t p, size_t n) {return range_valid;}
static void dcache_invalidate(void *p, size_t n) { if(expire_copy) now+=1600; }
CRC
static status_t prv_flash_transaction(unsigned command) {
 assert(locks==1 && boosts==1); ++transactions;
 if(command==FLASH_READ) {
  s_flash_command.buffer=(uint32_t)(uintptr_t)payload;
  s_flash_command.length_and_status=bad_length?3:4;
  s_flash_command.crc=prv_crc32(payload,4) ^ bad_crc;
  if(read_status==E_AGAIN) available=false;
  if(expired) now+=1600;
  return read_status;
 }
 assert(command==FLASH_READ_DONE);
 assert(s_flash_command.buffer==0 && s_flash_command.length_and_status==0);
 return done_status;
}
READ
static void reset(void) {
 available=range_valid=true;bad_crc=bad_length=false;
 read_status=done_status=transactions=0;
 expired=expire_copy=false;lease_start=now=UINT32_MAX-10;
 assert(!locks && !boosts);
}
int main(void) {
 payload=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
 assert(payload!=MAP_FAILED && (uintptr_t)payload<=UINT32_MAX);
 memcpy(payload,"data",4); unsigned char out[4];
 reset();assert(t5ai_flash_ipc_read(0,out,4)==0);assert(transactions==2 && !memcmp(out,"data",4));
 reset();memset(out,0x55,4);bad_crc=true;
 assert(t5ai_flash_ipc_read(0,out,4)==E_ERROR);
 assert(transactions==2 && available && out[0]==0x55);
 reset();bad_crc=true;done_status=E_AGAIN;
 assert(t5ai_flash_ipc_read(0,out,4)==E_ERROR);assert(transactions==2 && !available);
 reset();done_status=E_ERROR;
 assert(t5ai_flash_ipc_read(0,out,4)==E_ERROR);assert(!available && transactions==2);
 reset();range_valid=false;
 assert(t5ai_flash_ipc_read(0,out,4)==E_ERROR);assert(!available && transactions==1);
 reset();bad_length=true;
 assert(t5ai_flash_ipc_read(0,out,4)==E_ERROR);assert(!available && transactions==1);
 reset();expired=true;
 assert(t5ai_flash_ipc_read(0,out,4)==E_AGAIN);assert(transactions==1 && !available);
 reset();expire_copy=true;bad_crc=true;
 assert(t5ai_flash_ipc_read(0,out,4)==E_ERROR);assert(transactions==1 && !available);
 reset();read_status=E_AGAIN;
 assert(t5ai_flash_ipc_read(0,out,4)==E_AGAIN);assert(transactions==1 && !available);
 reset();read_status=E_ERROR;
 assert(t5ai_flash_ipc_read(0,out,4)==E_ERROR);assert(transactions==1);
 reset();assert(t5ai_flash_ipc_read(0,0,4)==E_INVALID_ARGUMENT);assert(!transactions);
 assert(!locks && !boosts);
 munmap(payload,4096);
}
"""


class FlashReadLeaseTest(unittest.TestCase):
    def test_known_lease_crc_cleanup_and_ambiguous_quarantine(self):
        source = (ROOT / "fw/drivers/flash/t5ai_flash_ipc.c").read_text()
        crc = source[
            source.index("static const uint32_t s_flash_crc32_table") : source.index(
                "static uint32_t prv_saradc_crc32"
            )
        ]
        read = source[
            source.index("status_t t5ai_flash_ipc_read(") : source.index(
                "status_t t5ai_flash_ipc_write("
            )
        ]
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "test.c").write_text(
                HARNESS.replace("CRC", crc).replace("READ\n", read + "\n")
            )
            subprocess.run(
                ["cc", "-std=c11", str(p / "test.c"), "-o", str(p / "test")], check=True
            )
            subprocess.run([str(p / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
