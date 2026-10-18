# SPDX-License-Identifier: Apache-2.0
"""Exercise raw resource transfer rejection before any flash or allocation side effects."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STUB = r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef enum { ObjectFirmware=1, ObjectRecovery, ObjectSysResources } PutBytesObjectType;
typedef enum { PutBytesCrcType_Legacy, PutBytesCrcType_CRC32 } PutBytesCrcType;
typedef struct { void *impl_data; uint32_t current_offset; } PutBytesStorage;
typedef struct { int index; } PutBytesStorageInfo;
typedef struct { uint32_t num_bytes_written, crc_of_bytes; } PbInstallStatus;
typedef struct { uint32_t begin, end; } SystemResourceBank;
typedef struct { uint32_t words[4]; } FirmwareDescription;
#define CONFIG_PBLBOOT 1
#define FLASH_REGION_FIRMWARE_DEST_BEGIN 0x1000
#define FLASH_REGION_FIRMWARE_DEST_END 0x2000
#define PBL_BT_CONSUMER_PP_PUT_BYTES 1
#define PBL_BT_RESPONSE_TIME_MIDDLE 2
#define PBL_BT_RESPONSE_TIME_MIN 3
#define PBL_BT_MIN_LATENCY_MODE_TIMEOUT_PUT_BYTES_SECS 30
#define MIN(a,b) ((a)<(b)?(a):(b))
const SystemResourceBank *resource_storage_flash_get_unused_bank(void);
void *kernel_zalloc(size_t);
void kernel_free(void *);
void flash_read_bytes(void *, uint32_t, size_t);
void flash_write_bytes(const void *, uint32_t, size_t);
uint32_t flash_crc32_legacy(uint32_t,uint32_t);
uint32_t flash_crc32(uint32_t,uint32_t);
void *comm_session_get_system_session(void);
void comm_session_set_responsiveness(void *,int,int,int);
void flash_region_erase_optimal_range(uint32_t,uint32_t,uint32_t,uint32_t);
bool pb_storage_raw_get_status(PutBytesObjectType,PbInstallStatus *);
bool pb_storage_raw_init(PutBytesStorage *,PutBytesObjectType,uint32_t,PutBytesStorageInfo *,uint32_t);
uint32_t pb_storage_raw_get_max_size(PutBytesObjectType);
"""
HARNESS = r"""
#include "stub.h"
#include <assert.h>
static const SystemResourceBank bank={0x4000,0x5000};
static bool available;
static unsigned allocations, reads, writes, erases, responsiveness;
const SystemResourceBank *resource_storage_flash_get_unused_bank(void) {return available?&bank:NULL;}
void *kernel_zalloc(size_t n) {++allocations;return calloc(1,n);}
void kernel_free(void *p) {free(p);}
void flash_read_bytes(void *p,uint32_t a,size_t n) {(void)a;++reads;memset(p,0xff,n);}
void flash_write_bytes(const void *p,uint32_t a,size_t n) {(void)p;(void)a;(void)n;++writes;}
uint32_t flash_crc32_legacy(uint32_t a,uint32_t n) {(void)a;(void)n;return 0;}
uint32_t flash_crc32(uint32_t a,uint32_t n) {(void)a;(void)n;return 0;}
void *comm_session_get_system_session(void) {return NULL;}
void comm_session_set_responsiveness(void *s,int a,int b,int c) {(void)s;(void)a;(void)b;(void)c;++responsiveness;}
void flash_region_erase_optimal_range(uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
 assert(a==bank.begin && b==bank.begin && c==bank.end && d==bank.end);++erases;
}
int main(void) {
 PutBytesStorage s={.impl_data=(void *)1,.current_offset=37};
 PutBytesStorageInfo info={0}; PbInstallStatus status={11,22};
 assert(pb_storage_raw_get_max_size(ObjectSysResources)==0);
 assert(!pb_storage_raw_get_status(ObjectSysResources,&status));
 assert(!pb_storage_raw_init(&s,ObjectSysResources,4,&info,0));
 assert(!pb_storage_raw_init(&s,ObjectSysResources,4,&info,2));
 assert(s.impl_data==(void *)1 && s.current_offset==37);
 assert(status.num_bytes_written==11 && status.crc_of_bytes==22);
 assert(!allocations && !reads && !writes && !erases && !responsiveness);
 assert(pb_storage_raw_get_max_size(ObjectFirmware)==0x1000);
 assert(pb_storage_raw_get_max_size(ObjectRecovery)==0x1000);
 available=true;
 assert(pb_storage_raw_get_max_size(ObjectSysResources)==bank.end-bank.begin);
 assert(pb_storage_raw_init(&s,ObjectSysResources,4,&info,0));
 assert(erases==1 && responsiveness==2);
 assert(pb_storage_raw_init(&s,ObjectSysResources,4,&info,2));
 assert(s.current_offset==2 && erases==1);
 assert(!pb_storage_raw_get_status(ObjectSysResources,&status));
 assert(allocations==1 && reads==2);
 return 0;
}
"""


class ResourceUpdateBankTest(unittest.TestCase):
    def test_raw_transfer_rejects_missing_bank(self):
        headers = ("pbl/bluetooth/responsiveness.h", "pbl/drivers/flash.h",
                   "pbl/services/comm_session/session.h",
                   "pbl/services/put_bytes/put_bytes_storage_raw.h", "pbl/util/math.h",
                   "flash_region/flash_region.h", "kernel/pbl_malloc.h",
                   "resource/resource_storage_flash.h", "system/firmware_storage.h")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "stub.h").write_text(STUB)
            for name in headers:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "stub.h"\n')
            (root / "test.c").write_text(HARNESS)
            binary = root / "test"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-I", str(root),
                            str(root / "test.c"),
                            str(ROOT / "fw/services/put_bytes/put_bytes_storage_raw.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
