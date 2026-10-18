# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise production MB-IPC and SARADC together against an asynchronous CP model."""
import re
import unittest
from pathlib import Path

from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]
MB = ROOT / "drivers/t5ai/mb_ipc.c"
ADC = ROOT / "drivers/t5ai/saradc_ipc.c"
HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "system/status_codes.h"
#include <pbl/soc/t5ai/mb_ipc.h>
#include <pbl/soc/t5ai/mailbox_channel.h>
#include <pbl/soc/t5ai/saradc_ipc.h>
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_ERR(...) assert(!"logging cannot run on mailbox owner")
#define PBL_LOG_WRN(...) ((void)0)
#define PBL_LOG_DBG(...) ((void)0)
#define PBL_TICKS(t) (t)
#define PBL_FOREVER UINT32_MAX
#define T5AI_SHARED_ALIGNMENT 32
#define T5AI_AON_RTC_COUNT_LOW ((uint32_t)now)
struct pbl_sem {unsigned count;};
struct pbl_mutex {bool locked;};
typedef struct {uintptr_t address;size_t length;} T5AICPMailboxFrame;
typedef void (*Rx)(uint8_t,const T5AICPMailboxFrame *,void *);
static uint64_t now;
static unsigned depth,physical_count,commands,completions,cp_tag;
static int fail_command=-1;
static unsigned busy_count;
static bool corrupt,wrong_size,hold,awaiting_done,bad_route,bad_wire_crc,bad_wire_command;
static bool drop_raw_response;
static unsigned step_ms;
static status_t result;
static uint16_t result_peak;
static Rx receive;
static void *receive_context;
static T5AIMailboxMessage pending;
static T5AIMailboxTransmitHandler physical_cb;
static void *physical_ctx;
static void *cp_buffer;
uint64_t pbl_uptime_ms(void){return now;}
uint64_t pbl_uptime_ticks(void){return now;}
unsigned pbl_ms_to_ticks(unsigned ms){return ms;}
void pbl_irq_lock(void){++depth;}
void pbl_irq_unlock(void){assert(depth);--depth;}
void pbl_sem_init(struct pbl_sem *s,unsigned initial,unsigned max){assert(max==1);s->count=initial;}
int pbl_sem_take(struct pbl_sem *s,unsigned timeout){
 assert(timeout==0);if(!s->count)return -1;--s->count;return 0;
}
int pbl_sem_give(struct pbl_sem *s){s->count=1;return 0;}
void pbl_mutex_init(struct pbl_mutex *m){m->locked=false;}
int pbl_mutex_lock(struct pbl_mutex *m,unsigned t){(void)t;assert(!m->locked);m->locked=true;return 0;}
void pbl_mutex_unlock(struct pbl_mutex *m){assert(m->locked);m->locked=false;}
void dcache_flush(void *p,size_t n){assert(p && n);}
void dcache_invalidate(void *p,size_t n){assert(p && n);}
bool t5ai_cp_mailbox_shared_range_valid(uintptr_t p,size_t n){return p && n && n<=256;}
status_t t5ai_cp_mailbox_register(uint8_t ch,Rx cb,void *ctx){
 if(ch==0x41){receive=cb;receive_context=ctx;}else{assert(ch==0x4c);}
 return 0;
}
status_t t5ai_cp_mailbox_acknowledge_frame(const T5AICPMailboxFrame *f,bool ok){(void)f;(void)ok;return 0;}
void t5ai_mailbox_channel_wake(void){}
status_t t5ai_mailbox_channel_send_async(uint8_t ch,const T5AIMailboxMessage *msg,
 T5AIMailboxTransmitHandler cb,void *ctx){
 assert(!depth && ch==0x11);
 if(busy_count){--busy_count;return E_BUSY;}
 assert(!physical_cb);pending=*msg;physical_cb=cb;physical_ctx=ctx;++physical_count;return 0;
}
status_t t5ai_mailbox_channel_send(uint8_t ch,const T5AIMailboxMessage *msg,
 T5AIMailboxMessage *ack,uint32_t ms){(void)ch;(void)msg;(void)ack;(void)ms;assert(false);return E_ERROR;}
#define s_initialized s_mb_initialized
#include MB_SOURCE
#undef s_initialized
#include ADC_SOURCE

static void received(T5AIMailboxMessage message){
 T5AICPMailboxFrame frame={.address=(uintptr_t)&message,.length=sizeof(message)};
 receive(0x41,&frame,receive_context);
 assert(prv_route_status(&message)==(s_available?MB_IPC_ROUTE_OK:MB_IPC_ROUTE_UNREACHABLE));
}
static uint32_t vendor_checksum(const uint8_t *bytes,size_t length){
 uint32_t crc=0;
 for(size_t n=0;n<length;++n){
   crc^=bytes[n];
   for(unsigned b=0;b<8;++b)crc=(crc<<1)^((crc&0x80u)?0x31u:0u);
 }
 return crc;
}
static void deliver_physical(void){
 assert(physical_cb);
 T5AIMailboxMessage msg=pending,ack=msg;
 T5AIMailboxTransmitHandler cb=physical_cb;void *ctx=physical_ctx;physical_cb=NULL;
 if((uint8_t)msg.header==MB_IPC_COMMAND_CONNECT || (uint8_t)msg.header==MB_IPC_COMMAND_SEND){
   received((T5AIMailboxMessage){.header=(uint8_t)msg.header|0x80,
      .parameter1=prv_address(MB_IPC_CPU_AP,17,MB_IPC_CPU_CP,2,prv_tag(&msg))});
   if((uint8_t)msg.header==MB_IPC_COMMAND_SEND){
     ++commands;unsigned cmd=(uint8_t)(msg.parameter2>>24);
     assert(!awaiting_done || cmd==SARADC_READ_RAW_DONE);
     assert(msg.parameter3==(uint32_t)(uintptr_t)&s_command);
     SaradcCommand response=s_command;
     response.status=(int)cmd==fail_command?-7:0;
     if(cmd==SARADC_SET_CONFIG){assert(response.config.output_buffer==s_adc_buffer &&
        response.config.channel==15 && response.config.clock==0x30a0c5);}
     if(cmd==SARADC_READ_RAW && response.status==0){
       assert(response.size==16 && response.timeout==2000);
       for(unsigned i=0;i<16;++i)response.samples[i]=2100+i;
       response.crc=vendor_checksum((const uint8_t *)response.samples,32);
       if(corrupt)response.crc^=1;
       if(wrong_size)response.size=33;
       awaiting_done=true;
     }
     if(cmd==SARADC_READ_RAW_DONE)awaiting_done=false;
     memcpy(cp_buffer,&response,sizeof(response));
     uint8_t wire_crc=prv_crc8(cp_buffer,sizeof(response));
     if(!(drop_raw_response && cmd==SARADC_READ_RAW))received((T5AIMailboxMessage){.header=MB_IPC_COMMAND_SEND,
       .parameter1=prv_address(MB_IPC_CPU_AP,17,MB_IPC_CPU_CP,2,++cp_tag),
       .parameter2=sizeof(response)|((uint32_t)(wire_crc^(bad_wire_crc?1:0))<<16)|
         ((uint32_t)(cmd^(bad_wire_command?1:0))<<24),
       .parameter3=(uint32_t)(uintptr_t)cp_buffer});
   }
 }
 if(bad_route)prv_set_route_status(&ack,1);
 cb(&ack,S_SUCCESS,ctx);
}
static void completed(status_t status,uint16_t peak,void *ctx){
 assert(!depth && ctx==(void *)7);++completions;result=status;result_peak=peak;
}
static void setup(void){
 cp_buffer=mmap((void *)0x20000000,4096,PROT_READ|PROT_WRITE,
   MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
 assert(cp_buffer==(void *)0x20000000);
}
static void pump(void){
 for(unsigned i=0;i<300 && !completions;++i){
   (void)t5ai_mb_ipc_async_poll();
   now+=step_ms;
   if(physical_cb && !hold)deliver_physical();
   if(hold)now+=100;
 }
 assert(completions==1 && !depth);
}
static void start(void){assert(t5ai_saradc_request(15,16,completed,(void *)7)==0);}
"""

class T5AISaradcTest(unittest.TestCase):
    def run_client(self, checks):
        headers = {n: "" for p in (MB, ADC)
                   for n in re.findall(r'#include [<"]([^>"\n]+)[>"]', p.read_text())
                   if "/" in n and n not in (
                       "pbl/soc/t5ai/mb_ipc.h", "pbl/soc/t5ai/mailbox_channel.h",
                       "pbl/soc/t5ai/saradc_ipc.h")}
        headers["system/status_codes.h"] = (ROOT / "fw/system/status_codes.h").read_text()
        src = HARNESS.replace("MB_SOURCE", '"'+str(MB)+'"').replace("ADC_SOURCE", '"'+str(ADC)+'"')
        run_c(src+"\nint main(void){setup();\n"+checks+"\nmunmap(cp_buffer,4096);return 0;}\n", headers)

    def test_complete_sequence_and_shared_channel_backpressure(self):
        self.run_client(r"""
 busy_count=4;start();assert(t5ai_saradc_request(15,16,completed,0)==E_BUSY);
 pump();assert(result==0 && result_peak==2115 && commands==12 && physical_count==25);
 assert(!awaiting_done && !s_started && !s_acquired && !s_adc_initialized && !s_gpio);
 completions=0;start();pump();assert(result==0 && commands==24);
""")

    def test_rejected_raw_data_still_completes_read_done_before_cleanup(self):
        for bad in ("corrupt", "wrong_size"):
            with self.subTest(bad=bad):
                self.run_client(bad+r"""=true;start();pump();
 assert(result==E_ERROR && !result_peak && commands==12 && !awaiting_done);
 assert(s_available);
""")

    def test_remote_errors_cleanup_only_owned_resources(self):
        for command in (29,0,1,9,2,3,4,5,6,7,8,30):
            with self.subTest(command=command):
                self.run_client(f"fail_command={command};start();pump();"+r"""
 assert(result==E_ERROR && !result_peak && !awaiting_done);
 if((fail_command>=6 && fail_command<=8) || fail_command==30)assert(!s_available);
""")

    def test_deadline_quarantines_descriptors_and_late_physical_completion(self):
        self.run_client(r"""
 hold=true;start();pump();assert(result==E_AGAIN && !s_available && physical_cb);
 SaradcCommand original=s_command;
 hold=false;deliver_physical();assert(!memcmp(&original,&s_command,sizeof(original)));
 assert(t5ai_saradc_request(15,16,completed,0)==E_INVALID_OPERATION && completions==1);
""")

    def test_transport_corruption_and_route_rejection_are_terminal(self):
        for bad in ("bad_route", "bad_wire_crc", "bad_wire_command"):
            with self.subTest(bad=bad):
                self.run_client(bad+r"""=true;start();pump();
 assert(result<0 && !s_available && !result_peak);
 assert(t5ai_saradc_request(15,16,completed,0)==E_INVALID_OPERATION);
""")

    def test_missing_raw_reply_retains_live_adc_storage_until_reset(self):
        self.run_client(r"""
 drop_raw_response=true;step_ms=25;start();pump();
 assert(result==E_AGAIN && !s_available && awaiting_done && !result_peak);
 SaradcCommand original=s_command;
 (void)t5ai_mb_ipc_async_poll();
 assert(!memcmp(&original,&s_command,sizeof(original)));
 assert(t5ai_saradc_request(15,16,completed,0)==E_INVALID_OPERATION);
""")

    def test_whole_session_deadline_does_not_restart_for_each_command(self):
        self.run_client(r"""
 step_ms=100;start();pump();
 assert(result==E_AGAIN && !s_available && now>=5000 && commands<12);
""")

    def test_vendor_checksum_uses_bit_7_with_unmasked_32_bit_accumulator(self):
        self.run_client(r"""
 uint8_t data[32];for(unsigned i=0;i<32;++i)data[i]=i*17+3;
 assert(vendor_checksum((const uint8_t *)"123456789",9)==0x47ffc2a2u);
 assert(prv_saradc_crc32("123456789",9)==0x47ffc2a2u);
 assert(prv_saradc_crc32(data,32)==0x8cdb1bcbu);
 for(unsigned n=0;n<=32;++n)assert(prv_saradc_crc32(data,n)==vendor_checksum(data,n));
 start();pump();assert(result==0);
""")
