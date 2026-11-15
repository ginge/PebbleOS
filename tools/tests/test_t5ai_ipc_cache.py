# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Strict split RAM/cache model for T5AI IPC ranges and returned AP storage."""

import re
import unittest
from pathlib import Path

from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]

MODEL = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define LINE 32u
static _Alignas(LINE) unsigned char cache[320];
static unsigned char ram[sizeof(cache)];
static unsigned calls;
static void dcache_align(uintptr_t *p, size_t *n) {
 size_t offset=*p%LINE; *p-=offset; *n=(*n+offset+LINE-1)&~(LINE-1);
}
static void operation(const void *p,size_t n,bool clean) {
 uintptr_t offset=(uintptr_t)p-(uintptr_t)cache;
 assert((uintptr_t)p%LINE==0 && n%LINE==0 && n && offset+n<=sizeof(cache));
 ++calls;
 if(clean) memcpy(ram+offset,cache+offset,n);
 else memcpy(cache+offset,ram+offset,n);
}
static void pbl_dcache_flush_range(const void *p,size_t n) {uintptr_t a=(uintptr_t)p; dcache_align(&a,&n); operation((void *)a,n,true);}
static void pbl_dcache_invalidate_range(void *p,size_t n) {uintptr_t a=(uintptr_t)p; dcache_align(&a,&n); operation((void *)a,n,false);}
#include <pbl/soc/t5ai/cache.h>
static void reset(void) {memset(cache,0xa5,sizeof(cache));memset(ram,0x5a,sizeof(ram));calls=0;}
"""


class IPCCacheTest(unittest.TestCase):
    def test_all_offsets_lengths_and_dirty_neighbours(self):
        run_c(
            MODEL
            + r"""
int main(void) {
 // ACL, commands and flash writes may start at any byte. Cleaning must publish
 // every payload byte, with dirty neighbours preserved in both cache and RAM.
 for(unsigned off=0;off<32;++off) for(unsigned n=0;n<=161;++n) {
  reset();unsigned start=64+off;
  t5ai_cache_clean(cache+start,n);
  unsigned lo=start&~31u,hi=n?(start+n+31u)&~31u:lo;
  assert(calls==(n!=0));
  for(unsigned i=0;i<sizeof(cache);++i) {
   assert(cache[i]==0xa5);
   assert(ram[i]==((n && i>=lo && i<hi)?0xa5:0x5a));
  }
  // Receive lines belong entirely to CP, including boundary fragments.
  // Adjacent AP-owned lines are dirty; they must survive without writeback.
  reset();memset(cache+lo,0x11,hi-lo);memset(ram+lo,0x77,hi-lo);
  t5ai_cache_invalidate_received(cache+start,n);
  assert(calls==(n!=0));
  for(unsigned i=0;i<sizeof(cache);++i) {
   assert(cache[i]==((n && i>=lo && i<hi)?0x77:0xa5));
   assert(ram[i]==((n && i>=lo && i<hi)?0x77:0x5a));
  }
 }
 return 0;
}
""",
            {"pbl/kernel/cache.h": "#pragma once\n"},
        )

    def test_production_mailbox_layout_preserves_dirty_ack_state(self):
        source = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        tx = re.search(
            r"typedef struct \{\n  T5AIMailboxMessage message.*?} TxChannel;",
            source,
            re.S,
        )[0]
        slot = re.search(
            r"typedef struct \{\n  T5AIMailboxMessage message;.*?MailboxOwnedSlot;",
            source,
            re.S,
        )[0]
        run_c(
            MODEL
            + r"""
#define T5AI_SHARED_ALIGNMENT 32u
typedef struct {uint32_t header,parameter1,parameter2,parameter3;} T5AIMailboxMessage;
typedef void (*T5AIMailboxTransmitHandler)(void);
struct pbl_sem {unsigned value;};struct pbl_mutex {unsigned value;};
typedef unsigned TxState;
"""
            + tx
            + slot
            + r"""
int main(void) {
 _Static_assert(offsetof(TxChannel,ack)>=32,"ACK state shares CP return line");
 _Static_assert(sizeof(TxChannel)%32==0,"array stride shares lines");
 _Static_assert(sizeof(MailboxOwnedSlot)==32,"mailbox slot not private");
 _Static_assert(sizeof(T5AIMailboxMessage)==16,"wire ABI changed");
 reset();TxChannel *tx=(TxChannel *)(cache+32);
 tx->ack.header=0x12345678;
 t5ai_cache_clean(&tx->message,sizeof(tx->message));
 // CP mutates original message into physical ACK in RAM.
 memset(ram+32,0x99,16);
 t5ai_cache_invalidate_received(&tx->message,sizeof(tx->message));
 assert(tx->ack.header==0x12345678);
 assert(tx->message.header==0x99999999);
 // Widening the old layout would have discarded the dirty ACK above.
 assert(ram[64]==0x5a);
 return 0;
}
""",
            {"pbl/kernel/cache.h": "#pragma once\n"},
        )

    def test_hci_remote_descriptors_and_indirect_tx_payloads(self):
        from tools.tests.test_t5ai_timing import function

        source = (ROOT / "third_party/nimble/transport/t5ai_hci_ipc.c").read_text()
        types = source[
            source.index("typedef struct {\n  uint8_t type;") : source.index(
                "typedef struct {\n  T5AIMIPCFrame frame;\n}"
            )
        ]
        # Map a low wire-address window; RAM is distinct from AP cached bytes.
        model = MODEL.replace(
            "static _Alignas(LINE) unsigned char cache[320];",
            "static unsigned char *cache;",
        )
        model = model.replace("sizeof(cache)", "4096")
        harness = (
            model
            + r"""
#include <sys/mman.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/mailbox_protocol.h>
#include <pbl/soc/t5ai/mipc.h>
typedef int status_t;
#define E_RANGE -1
#define E_ERROR -2
#define S_SUCCESS 0
#define T5AI_HCI_TYPE 1
#define T5AI_MIPC_CPU_AP 1
#define T5AI_MIPC_SEND_FLAG_SYNC 1
"""
            + types
            + r"""
static const char *s_channel_name=(void *)0x28000100;
static uint8_t prv_next_sequence(void) {return 1;}
static bool t5ai_cp_mailbox_shared_range_valid(uintptr_t p,size_t n) {
 return p>=(uintptr_t)cache && p+n<=(uintptr_t)cache+4096;
}
#include MIPC_SOURCE
static status_t prv_send_frame(T5AIMIPCFrame *frame) {
 t5ai_cache_clean(frame,sizeof(*frame));return 0;
}
"""
            + function(source, "prv_pointer")
            + "\n"
            + function(source, "prv_prepare_outbound")
            + "\n"
            + function(source, "prv_decode_remote")
            + r"""
int main(void) {
 cache=mmap((void *)0x28000000,4096,PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
 assert(cache==(void *)0x28000000);
 for(unsigned off=0;off<32;++off) for(unsigned n=0;n<=97;++n) {
  reset();
  // HCI command/ACL indirect packet clean, as performed by prv_exchange.
  unsigned char *packet=cache+1024+off;
  memset(packet,0x38,n);
  t5ai_cache_clean(packet,n);
  T5AIHCIOutbound *out=(void *)(cache+64);
  out->message=(T5AIHCIMessage){.type=1,.request=(uintptr_t)packet,.request_length=n};
  assert(prv_prepare_outbound(out,&out->message,sizeof(out->message),false)==0);
  assert(!memcmp(ram+64,cache+64,sizeof(out->config)));
  assert(!memcmp(ram+(uintptr_t)&out->handle-(uintptr_t)cache,&out->handle,sizeof(out->handle)));
  assert(!memcmp(ram+(uintptr_t)&out->data-(uintptr_t)cache,&out->data,sizeof(out->data)));
  assert(!memcmp(ram+(uintptr_t)&out->message-(uintptr_t)cache,&out->message,sizeof(out->message)));
  assert(!memcmp(ram+1024+off,packet,n));
  // CP descriptors and messages may be unaligned and stale in the AP cache.
  // Offset choices around 32-byte boundaries cover both final intersecting lines.
  T5AIMIPCData d={.data=(uintptr_t)(cache+640+off),.size=sizeof(T5AIHCIMessage)};
  T5AIHCIMessage m={.type=1,.subtype=6,.request=(uintptr_t)packet,.request_length=n};
  memcpy(ram+512+off,&d,sizeof(d));memcpy(ram+640+off,&m,sizeof(m));
  T5AIMIPCFrame frame={.object=(uintptr_t)(cache+512+off)};
  T5AIMIPCData *remote;T5AIHCIMessage *message;
  assert(prv_decode_remote(&frame,&remote,&message)==0);
  assert(!memcmp(remote,&d,sizeof(d)) && !memcmp(message,&m,sizeof(m)));
  assert(cache[480]==0xa5 && ram[480]==0x5a);
 }
 munmap(cache,4096);return 0;
}
"""
        )
        harness = harness.replace(
            "MIPC_SOURCE", '"' + str(ROOT / "drivers/t5ai/mipc.c") + '"'
        )
        run_c(harness, {"pbl/kernel/cache.h": "#pragma once\n"})

    def test_production_flash_write_publishes_unaligned_tail(self):
        from tools.tests.test_t5ai_timing import function

        source = (ROOT / "drivers/flash/t5ai_flash_ipc.c").read_text()
        model = MODEL.replace(
            "static _Alignas(LINE) unsigned char cache[320];",
            "static unsigned char *cache;",
        ).replace("sizeof(cache)", "4096")
        run_c(
            model
            + r"""
#include <sys/mman.h>
typedef int status_t;
#define E_INVALID_ARGUMENT -1
#define FLASH_MAX_TRANSFER_BYTES UINT16_MAX
#define FLASH_ADDRESS_SHIFT 8
#define FLASH_WRITE 5
#define PBL_FOREVER 0
static unsigned s_flash_mutex;
typedef struct {uint32_t part_and_address,buffer,length_and_status,crc;} FlashCommand;
static FlashCommand s_flash_command;
static void pbl_mutex_lock(unsigned *p,unsigned t) {(void)t;assert(!*p);*p=1;}
static void pbl_mutex_unlock(unsigned *p) {assert(*p);*p=0;}
static uint32_t prv_crc32(const void *p,size_t n) {(void)p;return n;}
static int prv_flash_transaction(unsigned command) {
 assert(command==FLASH_WRITE && s_flash_mutex==1);
 unsigned offset=s_flash_command.buffer-(uintptr_t)cache;
 assert(!memcmp(ram+offset,cache+offset,s_flash_command.length_and_status));
 assert(s_flash_command.crc==s_flash_command.length_and_status);
 return 0;
}
"""
            + function(source, "t5ai_flash_ipc_write")
            + r"""
int main(void) {
 cache=mmap((void *)0x28000000,4096,PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
 assert(cache==(void *)0x28000000);
 for(unsigned off=0;off<32;++off) for(unsigned n=1;n<=161;++n) {
  reset();memset(cache+64+off,0x42,n);
  assert(t5ai_flash_ipc_write(0,cache+64+off,n)==0);
  assert(!s_flash_mutex && calls==1);
  assert(cache[32]==0xa5 && ram[32]==0x5a);
  assert(cache[256]==0xa5 && ram[256]==0x5a);
 }
 munmap(cache,4096);return 0;
}
""",
            {"pbl/kernel/cache.h": "#pragma once\n"},
        )

    def test_production_flash_read_refreshes_cp_lease_before_copy(self):
        from tools.tests.test_t5ai_timing import function

        source = (ROOT / "drivers/flash/t5ai_flash_ipc.c").read_text()
        model = MODEL.replace(
            "static _Alignas(LINE) unsigned char cache[320];",
            "static unsigned char *cache;",
        ).replace("sizeof(cache)", "4096")
        run_c(
            model
            + r"""
#include <sys/mman.h>
typedef int status_t;
typedef unsigned pbl_prio_t;
#define E_INVALID_ARGUMENT -1
#define E_AGAIN -2
#define E_ERROR -3
#define S_SUCCESS 0
#define FAILED(s) ((s)<0)
#define FLASH_MAX_TRANSFER_BYTES UINT16_MAX
#define FLASH_ADDRESS_SHIFT 8
#define FLASH_READ 3
#define FLASH_READ_DONE 4
#define FLASH_READ_LEASE_AON_TICKS 1600
#define T5AI_AON_RTC_COUNT_LOW 0u
#define PBL_FOREVER 0
static unsigned s_flash_mutex,transactions;
static void *s_flash_socket;
static unsigned char *cp_payload;
static size_t cp_length;
typedef struct {uint32_t part_and_address,buffer,length_and_status,crc;} FlashCommand;
static FlashCommand s_flash_command;
static void pbl_mutex_lock(unsigned *p,unsigned t) {(void)t;assert(!*p);*p=1;}
static void pbl_mutex_unlock(unsigned *p) {assert(*p);*p=0;}
static pbl_prio_t prv_boost_client_priority(void) {return 0;}
static void prv_restore_client_priority(pbl_prio_t p) {(void)p;}
static unsigned t5ai_mb_ipc_last_receive_aon(void *p) {(void)p;return 0;}
static void prv_flash_quarantine(void) {assert(false);}
static void prv_flash_fault(status_t s,unsigned c) {(void)s;(void)c;assert(false);}
static bool t5ai_cp_mailbox_shared_range_valid(uintptr_t p,size_t n) {
 return p==(uintptr_t)cp_payload && n==cp_length;
}
static uint32_t prv_crc32(const void *p,size_t n) {
 const unsigned char *b=p;unsigned crc=0;
 while(n--)crc=crc*31+*b++;
 return crc;
}
static int prv_flash_transaction(unsigned command) {
 assert(s_flash_mutex);++transactions;
 if(command==FLASH_READ) {
  s_flash_command.buffer=(uintptr_t)cp_payload;
  s_flash_command.length_and_status=cp_length;
  s_flash_command.crc=prv_crc32(ram+(uintptr_t)cp_payload-(uintptr_t)cache,cp_length);
 } else assert(command==FLASH_READ_DONE && s_flash_command.buffer==0);
 return 0;
}
"""
            + function(source, "t5ai_flash_ipc_read")
            + r"""
int main(void) {
 cache=mmap((void *)0x28000000,4096,PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
 assert(cache==(void *)0x28000000);
 for(unsigned off=0;off<32;++off) for(unsigned n=1;n<=161;++n) {
  reset();transactions=0;cp_payload=cache+64+off;cp_length=n;
  memset(ram+64+off,0x42,n);
  unsigned char output[192];memset(output,0x66,sizeof(output));
  assert(t5ai_flash_ipc_read(0,output+3,n)==0);
  assert(!s_flash_mutex && calls==1 && transactions==2);
  for(unsigned i=0;i<n;++i)assert(output[3+i]==0x42);
  assert(output[2]==0x66 && output[n+3]==0x66);
  assert(cache[32]==0xa5 && ram[32]==0x5a);
  assert(cache[256]==0xa5 && ram[256]==0x5a);
 }
 munmap(cache,4096);return 0;
}
""",
            {"pbl/kernel/cache.h": "#pragma once\n"},
        )

    def test_ipc_call_sites_use_range_contract(self):
        # Keep descriptor and indirect-payload paths in the regression inventory.
        paths = {
            "third_party/nimble/transport/t5ai_hci_ipc.c": (7, 4),
            "drivers/flash/t5ai_flash_ipc.c": (1, 1),
            "drivers/t5ai/mb_ipc.c": (2, 2),
            "drivers/t5ai/mailbox_channel.c": (4, 14),
            "drivers/t5ai/cp_mailbox.c": (2, 1),
        }
        for path, (clean, receive) in paths.items():
            with self.subTest(path=path):
                text = (ROOT / path).read_text()
                self.assertNotRegex(text, r"\bdcache_(flush|invalidate)\(")
                self.assertEqual(text.count("t5ai_cache_clean("), clean)
                self.assertEqual(text.count("t5ai_cache_invalidate_received("), receive)


if __name__ == "__main__":
    unittest.main()
