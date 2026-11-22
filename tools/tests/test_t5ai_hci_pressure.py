# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Production receive paths with real locks, bounded pools and CP buffer reuse."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.test_t5ai_native_objects import body

ROOT = Path(__file__).resolve().parents[2]
PATH = "third_party/nimble/transport/hci_t5ai.c"


def harness():
    source = (ROOT / PATH).read_text()
    constants = source[source.index("#define T5AI_HCI_TASK_STACK_BYTES"):source.index("// Decode")]
    storage = "static PBL_MUTEX_DEFINE(s_tx_mutex);\n"
    code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <endian.h>
typedef uint32_t pbl_tick_t;
#define PBL_FOREVER 0
#define PBL_NO_WAIT 0
#define PBL_MSEC(x) (x)
#define pbl_ms_to_ticks(x) (x)
#define PBL_MUTEX_DEFINE(n) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
static unsigned contentions;
static void lock_mutex(pthread_mutex_t *m){
 int rc=pthread_mutex_trylock(m);
 if(rc==EBUSY){__atomic_add_fetch(&contentions,1,__ATOMIC_RELEASE);rc=pthread_mutex_lock(m);}
 assert(!rc);
}
#define pbl_mutex_lock(n,t) lock_mutex(n)
#define pbl_mutex_unlock(n) assert(!pthread_mutex_unlock(n))
#define PBL_LOG_ERR(...) ((void)0)
#define PBL_LOG_WRN(...) ((void)0)
static void fake_log(const char *format,...){(void)format;}
#define PBL_LOG_DBG(...) fake_log(__VA_ARGS__)
#define BLE_HS_ECONTROLLER 1
#define E_INVALID_OPERATION -1
#define BLE_ERR_MEM_CAPACITY 7
#define BLE_HCI_OP(g,c) (((g)<<10)|(c))
#define BLE_HCI_OGF_LE 8
#define BLE_HCI_OCF_LE_SET_ADV_ENABLE 10
#define BLE_HCI_OCF_LE_SET_ADV_PARAMS 6
#define BLE_HCI_OGF_INFO_PARAMS 4
#define BLE_HCI_OCF_IP_RD_BD_ADDR 9
#define BLE_HCI_OGF_CTLR_BASEBAND 3
#define BLE_HCI_OCF_CB_RESET 3
#define BLE_HCI_EVCODE_LE_META 0x3e
#define BLE_HCI_LE_SUBEV_CONN_COMPLETE 1
#define BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE 10
#define BLE_HCI_LE_SUBEV_ADV_RPT 2
#define BLE_HCI_LE_SUBEV_EXT_ADV_RPT 13
#define BLE_HCI_EVCODE_DISCONN_CMP 5
#define BLE_HCI_EVCODE_COMMAND_COMPLETE 14
#define BLE_HCI_EVCODE_COMMAND_STATUS 15
struct ble_hci_cmd {uint16_t opcode;uint8_t length;uint8_t data[];} __attribute__((packed));
struct os_mbuf {uint8_t data[1028];size_t length;};
#define OS_MBUF_PKTLEN(p) ((p)->length)
typedef bool (*T5AIHCIReceiveCallback)(const uint8_t *,size_t);
typedef struct {T5AIHCIReceiveCallback event,acl;bool (*discardable_event)(const uint8_t *,size_t);} T5AIHCICallbacks;
static unsigned s_rx_count;
static size_t s_rx_used;
static bool s_started,s_reset_scheduled,s_acl_tx_active;
static uint8_t s_active_connections,s_advertising_requested;
static void *s_acl_tx_queue;
static unsigned now,resets,evt_capacity,acl_capacity,evt_live,acl_live,delivered,credits,commands,fragments;
static bool append_fail,delivery_fail;
static unsigned stops,starts,sends;
static pbl_tick_t pbl_uptime_ticks(void){return now;}
static void pbl_thread_sleep(unsigned ms){now+=ms;}
static void ble_hs_sched_reset(int reason){assert(reason==1);++resets;}
static void *ble_transport_alloc_evt(int discard){
 assert(discard==0 || discard==1);if(!evt_capacity)return NULL;--evt_capacity;++evt_live;return malloc(257);
}
static void ble_transport_free(void *p){assert(evt_live);--evt_live;++evt_capacity;free(p);}
static struct os_mbuf *ble_transport_alloc_acl_from_ll(void){
 if(!acl_capacity)return NULL;--acl_capacity;++acl_live;return calloc(1,sizeof(struct os_mbuf));
}
static int os_mbuf_append(struct os_mbuf *p,const void *d,size_t n){
 if(append_fail){p->length=1;return -1;}memcpy(p->data,d,n);p->length=n;return 0;
}
static void os_mbuf_free_chain(struct os_mbuf *p){assert(acl_live);--acl_live;++acl_capacity;free(p);}
struct ble_hci_ev_num_comp_pkts {
 uint8_t count;
 struct {uint16_t handle,packets;} __attribute__((packed)) completed[];
} __attribute__((packed));
struct ble_hs_conn {unsigned bhc_outstanding_pkts;};
static struct ble_hs_conn conn={1000};
static pthread_mutex_t host_lock=PTHREAD_MUTEX_INITIALIZER;
static void ble_hs_lock(void){assert(!pthread_mutex_lock(&host_lock));}
static void ble_hs_unlock(void){assert(!pthread_mutex_unlock(&host_lock));}
static struct ble_hs_conn *ble_hs_conn_find(unsigned handle){assert(handle==1);return &conn;}
static void ble_hs_hci_add_avail_pkts(unsigned n){credits+=n;}
static void ble_hs_wakeup_tx(void){}
static int ble_hs_hci_evt_num_completed_pkts(uint8_t,const void *,unsigned);
static int ble_transport_to_hs_evt(void *p){
 uint8_t *d=p;++delivered;
 if(d[0]==0x13){assert(ble_hs_hci_evt_num_completed_pkts(d[0],d+2,d[1])==0);}
 if(d[0]==14)++commands;
 ble_transport_free(p);return delivery_fail?-1:0;
}
static int ble_transport_to_hs_acl(struct os_mbuf *p){
 assert((p->length==8 || p->length==1028) && p->data[4]==fragments);
 if(p->length==1028)assert(p->data[1027]==0x5a);++fragments;++delivered;
 os_mbuf_free_chain(p);return delivery_fail?-1:0;
}
static int t5ai_hci_ipc_stop(void){++stops;s_rx_count=0;s_rx_used=0;return 0;}
static int t5ai_hci_ipc_start(const T5AIHCICallbacks *c){assert(c->acl && c->event);s_rx_count=0;s_rx_used=0;++starts;return 0;}
static int t5ai_hci_ipc_send_command(const void *d,size_t n){assert(d && n);++sends;return 0;}
static bool t5ai_hci_ipc_faulted(void){return false;}
static void t5ai_hci_notify_work(void){}
static struct os_mbuf *tx_items[16];
static unsigned tx_count;
static int pbl_msgq_get(void *q,void *p,int t){
 (void)q;(void)t;if(!tx_count)return -1;*(struct os_mbuf **)p=tx_items[--tx_count];return 0;
}
static int pbl_msgq_put(void *q,const void *p,int t){
 (void)q;(void)t;if(tx_count==16)return -1;tx_items[tx_count++]=*(struct os_mbuf *const *)p;return 0;
}
'''
    host = (ROOT / "third_party/nimble/mynewt-nimble/nimble/host/src/ble_hs_hci_evt.c").read_text()
    code += body(host.replace("static int\nble_hs_hci_evt_num_completed_pkts", "static int ble_hs_hci_evt_num_completed_pkts"), "ble_hs_hci_evt_num_completed_pkts")
    code += constants + storage
    functions = ["prv_read_le16", "prv_started", "prv_set_started", "prv_schedule_reset", "prv_event_valid", "prv_acl_valid"]
    code += "".join(body(source, f) for f in functions)
    if os.environ.get("A3_BASELINE"):
        baseline = subprocess.check_output(["git", "show", f"f7f7cbd84:{PATH}"], cwd=ROOT, text=True)
        code += body(source, "prv_discardable_event")
        code += body(baseline, "prv_receive_event").replace("prv_receive_event(", "baseline_event(")
        code += body(baseline, "prv_receive_acl").replace("prv_receive_acl(", "baseline_acl(")
        code += "static bool prv_receive_event(const uint8_t *d,size_t n){baseline_event(d,n);return true;}\n"
        code += "static bool prv_receive_acl(const uint8_t *d,size_t n){baseline_acl(d,n);return true;}\n"
    else:
        code += "".join(body(source, f) for f in ["prv_discardable_event", "prv_deliver_event", "prv_deliver_acl", "prv_receive_event", "prv_receive_acl"])
    code += "".join(body(source, f) for f in ["ble_transport_ll_reinit", "ble_transport_ll_deinit", "ble_transport_to_ll_cmd_impl", "ble_transport_to_ll_acl_impl"])
    ipc = (ROOT / "third_party/nimble/transport/t5ai_hci_ipc.c").read_text()
    storage = ipc[ipc.index("static uint8_t s_rx_payload"):ipc.index("#ifdef CONFIG_T5AI_HCI_PROFILE", ipc.index("static uint8_t s_rx_payload"))]
    code += IPC_MODEL + storage
    code += body(ipc, "prv_retry_receive").replace("prv_retry_receive(", "ipc_retry_receive(")
    code += body(ipc, "prv_handle_command").replace("prv_retry_receive(", "ipc_retry_receive(")
    code += INJECTION
    return code


IPC_MODEL = r'''
#include <sys/mman.h>
#define T5AI_HCI_EVENT_TO_HOST 5
#define T5AI_HCI_ACL_TO_HOST 6
#define S_SUCCESS 0
#define E_ERROR -1
#define PASSED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
typedef int status_t;
typedef struct {unsigned dummy;} T5AIMIPCFrame;
typedef struct {unsigned dummy;} T5AIMIPCData;
typedef struct {uint8_t type,subtype;uint16_t result;uint32_t request,request_length,response,response_length;} T5AIHCIMessage;
static T5AIHCICallbacks s_callbacks={prv_receive_event,prv_receive_acl,prv_discardable_event};
#define T5AI_HCI_RX_CAPACITY 2
#define T5AI_HCI_RX_TIMEOUT_MS 250
#define T5AI_SHARED_ALIGNMENT 32
static pthread_mutex_t s_async_mutex=PTHREAD_MUTEX_INITIALIZER;
static bool ipc_faulted;
static bool prv_faulted(void){return ipc_faulted;}
static void prv_mark_faulted(void){ipc_faulted=true;}
static T5AIHCIMessage remote_message;
static T5AIMIPCData remote_descriptor;
static unsigned replies,acks;
static unsigned s_exchange_result;
static uint8_t *cp_payload;
static status_t prv_decode_remote(T5AIMIPCFrame *f,T5AIMIPCData **d,T5AIHCIMessage **m){
 (void)f;*d=&remote_descriptor;*m=&remote_message;return 0;
}
static bool t5ai_cp_mailbox_shared_range_valid(uintptr_t p,size_t n){return p==(uintptr_t)cp_payload && n<=1028;}
static void t5ai_cache_invalidate_received(void *p,size_t n){(void)p;(void)n;}
static status_t prv_respond_to_event(T5AIHCIMessage *m){
 ++replies;memset(cp_payload,0xcc,1028);memset(m,0xdd,sizeof(*m));return 0;
}
static status_t prv_mipc_ack(T5AIMIPCFrame *f,T5AIMIPCData *d){assert(f && d==&remote_descriptor);++acks;return 0;}
'''


INJECTION = r'''
static void inject(const uint8_t *data,size_t length,bool acl){
 pbl_mutex_lock(&s_async_mutex,0);
 memcpy(cp_payload,data,length);
 remote_message=(T5AIHCIMessage){.subtype=acl?6:5,.request=(uintptr_t)cp_payload,.request_length=length};
 T5AIMIPCFrame frame={0};bool matched=false;
 assert(prv_handle_command(&frame,255,&matched)==0 && !matched);
 pbl_mutex_unlock(&s_async_mutex);
 if(ipc_faulted)prv_schedule_reset();
}
static void inject_event(const uint8_t *d,size_t n){inject(d,n,false);}
static void inject_acl(const uint8_t *d,size_t n){inject(d,n,true);}
static void prv_retry_receive(void){
 pbl_mutex_lock(&s_async_mutex,0);ipc_retry_receive();pbl_mutex_unlock(&s_async_mutex);
 if(ipc_faulted)prv_schedule_reset();
}
'''

SCENARIOS = r'''
static const uint8_t complete[]={14,4,1,3,12,0};
static const uint8_t credit[]={0x13,5,1,1,0,2,0};
static const uint8_t disconnect[]={5,4,0,1,0,19};
static const uint8_t connection[]={0x3e,4,1,0,1,0};
static const uint8_t advert[]={0x3e,1,2};
static const uint8_t extended[]={0x3e,1,13};
static void init(void){
 ble_transport_ll_deinit();ble_transport_ll_reinit();ipc_faulted=false;replies=acks=0;
 if(!cp_payload)cp_payload=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
 assert(cp_payload!=MAP_FAILED && (uintptr_t)cp_payload<=UINT32_MAX);
 now=0;evt_capacity=acl_capacity=1;resets=delivered=credits=commands=fragments=0;
 append_fail=delivery_fail=false;assert(!evt_live && !acl_live);
}
static void recover(void){
 // Real host resync begins with HCI Reset after clearing old connections/credits.
 struct ble_hci_cmd *c=ble_transport_alloc_evt(0);
 c->opcode=BLE_HCI_OP(3,3);c->length=0;
 unsigned old=starts;assert(ble_transport_to_ll_cmd_impl(c)==0);
 assert(starts==old+1 && prv_started() && !s_rx_count && !s_reset_scheduled);
 credits=1;ipc_faulted=false;
}
static void event_pressure(void){
 init();evt_capacity=0;
 uint8_t cp_buffer[7];memcpy(cp_buffer,complete,6);
 inject_event(cp_buffer,6);memset(cp_buffer,0xcc,sizeof(cp_buffer));
 memcpy(cp_buffer,credit,7);inject_event(cp_buffer,7);memset(cp_buffer,0xdd,7);
 assert(!delivered && !resets);
 inject_event(advert,sizeof(advert));inject_event(extended,sizeof(extended));
 assert(!resets);evt_capacity=1;prv_retry_receive();
 assert(delivered==2 && commands==1 && credits==2 && conn.bhc_outstanding_pkts==998 && !s_rx_count && !evt_live);
 // Connection accounting happens once on delivery, not each failed allocation.
 evt_capacity=0;inject_event(connection,sizeof(connection));
 for(unsigned i=0;i<100;++i)prv_retry_receive();
 assert(!s_active_connections);evt_capacity=1;prv_retry_receive();assert(s_active_connections==1);
 evt_capacity=0;inject_event(disconnect,sizeof(disconnect));
 prv_retry_receive();assert(s_active_connections==1);evt_capacity=1;prv_retry_receive();
 assert(!s_active_connections && !evt_live && !resets);
}
static void acl_pressure(void){
 init();
 for(unsigned cycle=0;cycle<200;++cycle){
   uint8_t packet[]={1,0x20,4,0,0,1,2,3};packet[4]=(uint8_t)fragments;
   acl_capacity=0;inject_acl(packet,sizeof(packet));memset(packet,0xff,sizeof(packet));
   assert(s_rx_count==1 && !resets);
   acl_capacity=1;append_fail=true;
   for(unsigned i=0;i<10;++i){prv_retry_receive();assert(!acl_live && acl_capacity==1);}
   append_fail=false;prv_retry_receive();assert(!s_rx_count && !acl_live);
 }
 assert(fragments==200);
}
static void ipc_ownership(void){
 init();evt_capacity=0;
 T5AIMIPCFrame frame={0};bool matched=false;
 memcpy(cp_payload,complete,6);remote_message=(T5AIHCIMessage){.subtype=5,.request=(uintptr_t)cp_payload,.request_length=6};
 assert(prv_handle_command(&frame,255,&matched)==0 && !matched);
 memcpy(cp_payload,credit,7);remote_message=(T5AIHCIMessage){.subtype=5,.request=(uintptr_t)cp_payload,.request_length=7};
 assert(prv_handle_command(&frame,255,&matched)==0);
 assert(replies==2 && acks==2 && !delivered && !resets);
 evt_capacity=1;prv_retry_receive();
 assert(commands==1 && credits==2 && delivered==2 && !s_rx_count);

}
static void arena_bounds(void){
 init();acl_capacity=evt_capacity=0;
 uint8_t acl[1028]={1,0x20,0,4};acl[1027]=0x5a;
 uint8_t event[257]={0xff,255};
 inject_acl(acl,sizeof(acl));inject_event(event,sizeof(event));
 assert(!resets && s_rx_count==2 && s_rx_used==1285);
 acl_capacity=1;prv_retry_receive();
 assert(fragments==1 && s_rx_count==1 && s_rx_used==257 && s_rx_payload[0]==0xff);
 evt_capacity=1;prv_retry_receive();assert(!s_rx_count && !s_rx_used && delivered==2 && !resets);
 init();acl_capacity=0;inject_acl(acl,sizeof(acl));inject_acl(acl,sizeof(acl));
 assert(resets==1 && !s_rx_count && !s_rx_used && replies==2 && acks==2);
}
static void bounds(void){
 init();evt_capacity=0;
 for(unsigned i=0;i<3;++i)inject_event(credit,sizeof(credit));
 assert(resets==1 && !prv_started() && !s_rx_count);
 // An ATT response already in progress must not assert on a transport error
 // during the explicit reset. Its old-generation buffer is consumed once.
 struct os_mbuf *old_acl=ble_transport_alloc_acl_from_ll();old_acl->length=8;
 assert(ble_transport_to_ll_acl_impl(old_acl)==0 && !acl_live);
 for(unsigned i=0;i<100;++i){inject_event(complete,6);prv_retry_receive();}
 assert(resets==1);evt_capacity=1;recover();
 inject_event(credit,sizeof(credit));assert(credits==3);
 init();acl_capacity=0;uint8_t packet[]={1,0x20,4,0,0,1,2,3};
 now=UINT32_MAX-100;inject_acl(packet,8);now+=249;prv_retry_receive();assert(!resets);
 now+=1;prv_retry_receive();assert(resets==1 && !s_rx_count);acl_capacity=1;recover();
 inject_acl(packet,8);assert(fragments==1 && !acl_live);
 init();evt_capacity=0;inject_event(complete,6);ble_transport_ll_deinit();
 evt_capacity=1;prv_retry_receive();assert(!delivered);ble_transport_ll_reinit();
 inject_event(complete,6);assert(commands==1 && !s_rx_count);
 init();s_acl_tx_queue=(void *)1;
 struct os_mbuf *queued=ble_transport_alloc_acl_from_ll();queued->length=8;
 assert(ble_transport_to_ll_acl_impl(queued)==0 && tx_count==1);
 ble_transport_ll_deinit();assert(!tx_count && !acl_live);s_acl_tx_queue=NULL;
 init();s_acl_tx_active=true;unsigned old_stops=stops,old_starts=starts;
 ble_transport_ll_deinit();assert(now==2500 && stops==old_stops && !prv_started());
 ble_transport_ll_reinit();assert(starts==old_starts && !prv_started());
 s_acl_tx_active=false;ble_transport_ll_reinit();assert(prv_started());
 init();delivery_fail=true;inject_event(complete,6);assert(resets==1 && !evt_live);
}
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static unsigned phase;
static void *producer(void *unused){
 (void)unused;inject_event(complete,6);
 pthread_mutex_lock(&gate);phase=1;pthread_cond_broadcast(&cv);
 while(phase<2)pthread_cond_wait(&cv,&gate);pthread_mutex_unlock(&gate);
 // Late IPC delivery after stop must not reach the new host generation.
 inject_event(credit,sizeof(credit));return NULL;
}
static void concurrency(void){
 init();evt_capacity=0;pthread_t thread;
 pbl_mutex_lock(&s_async_mutex,0);assert(!pthread_create(&thread,NULL,producer,NULL));
 while(!__atomic_load_n(&contentions,__ATOMIC_ACQUIRE))sched_yield();
 pbl_mutex_unlock(&s_async_mutex);
 pthread_mutex_lock(&gate);while(!phase)pthread_cond_wait(&cv,&gate);pthread_mutex_unlock(&gate);
 // Capacity returns on an independent task; retry uses the actual production mutex.
 pbl_mutex_lock(&s_async_mutex,0);evt_capacity=1;pbl_mutex_unlock(&s_async_mutex);
 prv_retry_receive();assert(commands==1);ble_transport_ll_deinit();
 pthread_mutex_lock(&gate);phase=2;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&gate);
 assert(!pthread_join(thread,NULL));assert(!credits && !s_rx_count && !evt_live);
}
int main(int argc,char **argv){
 assert(argc==2);
 switch(argv[1][0]){case 'e':event_pressure();break;case 'a':acl_pressure();break;
 case 'b':bounds();break;case 'c':concurrency();break;case 'i':ipc_ownership();break;case 'm':arena_bounds();break;default:abort();}
 return 0;
}
'''


class HCIPressureTest(unittest.TestCase):
    def test_production_receive_pressure(self):
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / "pressure.c"
            exe = Path(tmp) / "pressure"
            c.write_text(harness() + SCENARIOS)
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-unused-variable", "-pthread", str(c), "-o", str(exe)], check=True)
            for scenario in (("event", "acl") if os.environ.get("A3_BASELINE") else ("event", "acl", "bounds", "concurrency", "ipc", "memory")):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(exe), scenario], check=True, timeout=8)

    def test_delayed_transport_ack_quarantine(self):
        source = (ROOT / "third_party/nimble/transport/t5ai_hci_ipc.c").read_text()
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
typedef unsigned pbl_tick_t;
typedef int status_t;
#define T5AI_HCI_REQUEST_CAPACITY 2
#define T5AI_HCI_EVENT_RESPONSE_CAPACITY 2
#define T5AI_HCI_STOP_RETIRE_TIMEOUT_MS 250
#define T5AI_HCI_OWNERSHIP_POLL_INTERVAL_MS 1
#define T5AI_HCI_DEINIT 1
#define T5AI_MIPC_RX_LOGICAL_CHANNEL 65
#define T5AI_MIPC_CPU_CP 0
#define T5AI_MIPC_TYPE_ACK 2
#define E_INVALID_OPERATION -1
#define E_AGAIN -2
#define FAILED(x) ((x)<0)
#define PBL_FOREVER 0
#define PBL_MSEC(x) (x)
#define pbl_ms_to_ticks(x) (x)
#define PBL_LOG_ERR(...) ((void)0)
typedef struct {uint32_t value;} T5AIMIPCData;
typedef struct {uint32_t header,object;} T5AIMIPCFrame;
typedef struct {T5AIMIPCData data;} T5AIHCIOutbound;
static T5AIHCIOutbound s_requests[2],s_event_responses[2];
static bool s_request_in_use[2],s_event_response_in_use[2];
static bool s_started=true,s_accept_frames=true,faulted;
static unsigned s_pending_frame_head,s_pending_frame_count,s_callbacks,s_rx_count;
static size_t s_rx_used;
static unsigned tick,wakes;
static int s_worker_wake;
static pthread_mutex_t irq=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_async_mutex=PTHREAD_MUTEX_INITIALIZER;
#define pbl_irq_lock() assert(!pthread_mutex_lock(&irq))
#define pbl_irq_unlock() assert(!pthread_mutex_unlock(&irq))
#define pbl_mutex_lock(m,t) assert(!pthread_mutex_lock(m))
#define pbl_mutex_unlock(m) assert(!pthread_mutex_unlock(m))
static unsigned t5ai_mipc_header(unsigned c,unsigned t){return c+t;}
static void t5ai_cache_invalidate_received(void *p,size_t n){(void)p;(void)n;}
static bool t5ai_mipc_frame_valid(const void *f,const void *d){(void)f;(void)d;return false;}
static bool prv_started(void){return s_started;}
static void prv_set_started(bool v){s_started=v;}
static void prv_mark_faulted(void){faulted=true;s_started=false;}
static unsigned pbl_uptime_ticks(void){return tick;}
static int prv_locked_exchange(unsigned t,void *p,unsigned n){assert(t==1 && !p && !n);return 0;}
static void pbl_sem_give(int *s){(void)s;++wakes;}
static void t5ai_cp_mailbox_unregister(unsigned c){assert(c==65);}
static void pbl_thread_sleep(unsigned t){tick+=t;sched_yield();}
'''
        code += "".join(body(source, f) for f in ["prv_is_transport_ack", "prv_retire_event_response", "prv_retire_request", "prv_allocate_request", "prv_all_outbound_retired", "t5ai_hci_ipc_stop"])
        code += r'''
static void *late_ack(void *unused){
 (void)unused;
 T5AIMIPCFrame ack={2,(uint32_t)(uintptr_t)&s_event_responses[0].data};
 assert(prv_retire_event_response(&ack));assert(!prv_retire_event_response(&ack));
 ack.object=(uint32_t)(uintptr_t)&s_requests[0].data;
 assert(prv_retire_request(&ack));assert(!prv_retire_request(&ack));return NULL;
}
int main(void){
 s_request_in_use[0]=s_event_response_in_use[0]=true;
 s_requests[0].data.value=0x12345678;s_event_responses[0].data.value=0x87654321;
 assert(t5ai_hci_ipc_stop()==E_AGAIN && faulted && wakes==250);
 // Neither timeout nor host storage cleanup may recycle CP-owned slots.
 assert(s_request_in_use[0] && s_event_response_in_use[0]);
 assert(s_requests[0].data.value==0x12345678 && s_event_responses[0].data.value==0x87654321);
 assert(prv_allocate_request()==&s_requests[1] && !prv_allocate_request());
 pthread_t t;assert(!pthread_create(&t,NULL,late_ack,NULL));assert(!pthread_join(t,NULL));
 assert(prv_allocate_request()==&s_requests[0]);
 s_request_in_use[0]=s_request_in_use[1]=false;s_started=true;faulted=false;
 assert(t5ai_hci_ipc_stop()==0 && !faulted && !s_accept_frames && prv_all_outbound_retired());
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / "ack.c"
            exe = Path(tmp) / "ack"
            c.write_text(code)
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread", str(c), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=8)
