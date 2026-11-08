# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Run production failure paths with pthread lock/condition scheduling."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.test_t5ai_native_objects import body

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    if os.environ.get("A2_BASELINE"):
        return subprocess.check_output(
            ["git", "show", f"f3ebbee4c:{path}"], cwd=ROOT, text=True
        )
    return (ROOT / path).read_text()


def run(code):
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "probe.c"
        exe = Path(tmp) / "probe"
        c.write_text(code)
        subprocess.run(
            [
                "cc",
                "-std=gnu11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pthread",
                str(c),
                "-o",
                str(exe),
            ],
            check=True,
        )
        subprocess.run([str(exe)], check=True, timeout=8)


COMMON = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
typedef int status_t;
#define S_SUCCESS 0
#define E_ERROR -1
#define E_AGAIN -2
#define E_INVALID_OPERATION -3
#define E_OUT_OF_RESOURCES -4
#define FAILED(x) ((x)<0)
#define PBL_FOREVER UINT32_MAX
#define PBL_TICKS(x) (x)
#define pbl_ms_to_ticks(x) (x)
#define PBL_MUTEX_DEFINE(n) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
static pthread_mutex_t irq = PTHREAD_MUTEX_INITIALIZER;
static void pbl_irq_lock(void) { assert(!pthread_mutex_lock(&irq)); }
static void pbl_irq_unlock(void) { assert(!pthread_mutex_unlock(&irq)); }
static __attribute__((unused)) struct timespec deadline(void) {
  struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_sec+=2;return t;
}
"""


class MailboxFailuresTest(unittest.TestCase):
    def test_timeout_metadata_survives_without_logger_recursion(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        declarations = channel[
            channel.index("#define MAILBOX_TIMEOUT_TRACE_MAGIC") : channel.index(
                "static uint32_t prv_timeout_checksum"
            )
        ]
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#define CONFIG_T5AI_RETAINED_SLEEP_TEST 1
#define T5AI_RESET_REASON_MAILBOX_BASE 0x50u
#define RebootReasonCode_MailboxTimeout 1
#define T5RebootTrace_MailboxTimeout 1
typedef struct {unsigned code; union {unsigned value;} extra;} RebootReason;
static jmp_buf reset;
static unsigned reset_reason, logs, cleans;
static bool recovered, emergency_dumped;
#define __get_PRIMASK() 1u
#define __get_BASEPRI() 0x60u
static bool t5ai_fault_trace_words(const uint32_t *words,size_t count){
 assert(count==17 && words[1]==2 && words[4]==0x12345678);
 emergency_dumped=true;return false; // A failed emergency output must still reset.
}
static uint32_t T5AI_AON_RTC_COUNT_LOW=200, MBOX_CH1_CTRL=3;
static uint32_t MBOX_CH0_FIFO_STATUS=4, SYSTEM_CPU1_INT_32_63_ENABLE=5;
static struct {uint32_t ISER[2],ISPR[2];} nvic={{0,6},{0,7}};
#define NVIC (&nvic)
static struct {unsigned state,sequence; struct {uint32_t header,parameter1,
  parameter2,parameter3;} message; uint32_t active_start;} s_tx[16];
#define TxChannel __typeof__(s_tx[0])
static void t5ai_cache_clean(const void *p,size_t n){(void)p;(void)n;++cleans;}
static void log_values(const char *fmt,...){(void)fmt;assert(recovered);++logs;}
#define PBL_LOG_DBG(...) log_values(__VA_ARGS__)
static void t5ai_reboot_trace_event(unsigned e,unsigned v){assert(e==1&&v==2);}
static void reboot_reason_set(const RebootReason *r){assert(r->extra.value==2);}
_Noreturn static void t5ai_force_whole_chip_reset(unsigned r){reset_reason=r;longjmp(reset,1);}
"""
            + declarations
        )
        code += (
            "\n".join(
                line
                for line in (ROOT / "include/pbl/soc/t5ai/reset.h")
                .read_text()
                .splitlines()
                if line.startswith("#define T5AI_RESET_REASON_SLEEP_PM_")
            )
            + "\n"
        )
        code += (
            (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h")
            .read_text()
            .replace("#pragma once", "")
        )
        code += "\n#define TxState_Pending 1u\n#define TxState_Active 2u\n#define T5AI_MAILBOX_CHANNEL_COUNT 16u\n#define MAILBOX_HEADER_COMMAND_MASK 0xffu\n"
        for function in [
            "prv_timeout_checksum",
            "prv_save_timeout",
            "prv_timeout_reset_reason",
            "prv_timeout_owner",
            "t5ai_mailbox_channel_dump_previous_timeout",
            "prv_reset_after_timeout",
        ]:
            code += body(channel.replace("[[noreturn]]", "_Noreturn"), function)
        code += r"""
int main(void){
 s_tx[2].state=2;s_tx[2].sequence=9;s_tx[2].message.header=0x12345678;
 s_tx[2].message.parameter1=11;s_tx[2].message.parameter2=12;
 s_tx[2].message.parameter3=13;s_tx[2].active_start=100;
 if(!setjmp(reset))prv_reset_after_timeout(2);
 assert(reset_reason==T5AI_RESET_REASON_SLEEP_PM_COMMAND && logs==0 && cleans==2 && emergency_dumped);
 assert(s_timeout_trace.channel==2 && s_timeout_trace.age==100);
 assert(s_timeout_trace.header==0x12345678 && s_timeout_trace.parameter3==13);
 assert(s_timeout_trace.enabled==6 && s_timeout_trace.pending==7);
 assert(s_timeout_trace.interrupt_mask==1 && s_timeout_trace.priority_mask==0x60);
 const unsigned operations[]={T5AIAlarm_Cancel,T5AIAlarm_ArmSleepMaskBarrier,
   T5AIAlarm_State,T5AIAlarm_LowVoltageCount,T5AIAlarm_ReadySamples};
 const unsigned reasons[]={0x48,0x49,0x4a,0x4b,0x4c};
 s_tx[2].message.header=T5AI_PM_NATIVE_ALARM_COMMAND;
 for(unsigned i=0;i<5;++i){s_tx[2].message.parameter3=operations[i];
   assert(prv_timeout_reset_reason(2)==reasons[i]);}
 s_tx[2].state=TxState_Pending;
 assert(prv_timeout_reset_reason(2)==0x4e);
 assert(prv_timeout_reset_reason(3)==0x53);
 assert(prv_timeout_owner(14)==14); // Idle callers do not borrow an owner.
 s_tx[14].state=TxState_Pending;
 assert(prv_timeout_owner(14)==14); // No active physical request.
 s_tx[2].state=TxState_Active;
 assert(prv_timeout_owner(14)==2); // A queued log expired behind the PM owner.
 assert(prv_timeout_owner(2)==2); // An active caller owns its own failure.
 s_tx[2].state=0;s_tx[3].state=TxState_Active;
 assert(prv_timeout_owner(14)==3); // Preserve non-PM ownership too.
 recovered=true;t5ai_mailbox_channel_dump_previous_timeout();assert(logs==4);
 t5ai_mailbox_channel_dump_previous_timeout();assert(logs==4);
 prv_save_timeout(2);s_timeout_trace.header^=1;
 t5ai_mailbox_channel_dump_previous_timeout();assert(logs==4);
 return 0;
}
"""
        run(code)

    def test_bound_cp_snapshot_covers_non_pm_physical_owner(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = "\n".join(
            line
            for line in (ROOT / "include/pbl/soc/t5ai/reset.h").read_text().splitlines()
            if line.startswith("#define T5AI_RESET_REASON_")
        )
        protocol = (
            (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h")
            .read_text()
            .replace("#pragma once", "")
        )
        code = (
            r"""
#include <assert.h>
#include <stdint.h>
#define CONFIG_T5AI_RETAINED_SLEEP_CP_RUNTIME_DIAGNOSTIC 1
#define MAILBOX_PM_CHANNEL_INDEX 2u
#define MAILBOX_HEADER_COMMAND_MASK 0xffu
#define TxState_Pending 1u
static struct {unsigned state;struct {uint32_t header,parameter1,parameter3;} message;} s_tx[16];
#define TxChannel __typeof__(s_tx[0])
static uint32_t observed;
static uint8_t sampled;
static uint8_t prv_cp_runtime_reset_reason(uint32_t generation) {
 observed=generation;return sampled;
}
"""
            + constants
            + protocol
            + body(channel, "prv_timeout_reset_reason")
            + r"""
int main(void){
 s_tx[2].message.parameter1=123;s_tx[14].message.parameter1=456;
 for(unsigned marker=0;marker<16;++marker){
  sampled=0x40|marker;
  assert(prv_timeout_reset_reason(2)==sampled && observed==123);
  assert(prv_timeout_reset_reason(14)==sampled && observed==456);
 }
 return 0;
}
"""
        )
        run(code)

    def test_raw_cp_irq_snapshot_covers_every_physical_owner(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = "\n".join(
            line
            for line in (ROOT / "include/pbl/soc/t5ai/reset.h").read_text().splitlines()
            if line.startswith("#define T5AI_RESET_REASON_")
        )
        protocol = (
            (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h")
            .read_text()
            .replace("#pragma once", "")
        )
        code = (
            r"""
#include <assert.h>
#include <stdint.h>
#define CONFIG_T5AI_RETAINED_SLEEP_CP_IRQ_DIAGNOSTIC 1
#define MAILBOX_PM_CHANNEL_INDEX 2u
#define MAILBOX_HEADER_COMMAND_MASK 0xffu
#define TxState_Pending 1u
static struct {unsigned state;struct {uint32_t header,parameter1,parameter3;} message;} s_tx[16];
#define TxChannel __typeof__(s_tx[0])
static uint8_t sampled;
static uint8_t prv_cp_irq_reset_reason(void) {return sampled;}
"""
            + constants
            + protocol
            + body(channel, "prv_timeout_reset_reason")
            + r"""
int main(void){
 for(unsigned marker=0;marker<16;++marker){
  sampled=0x40|marker;
  for(unsigned owner=0;owner<16;++owner)assert(prv_timeout_reset_reason(owner)==sampled);
 }
 return 0;
}
"""
        )
        run(code)

    def test_cp_receive_snapshot_rejects_torn_and_invalid_records(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        protocol = (
            (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h")
            .read_text()
            .replace("#pragma once", "")
        )
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
"""
            + protocol
            + r"""
static volatile T5AIReceiveTrace receive_record;
#define CONFIG_T5AI_RETAINED_SLEEP_CP_RECEIVE_ADDRESS ((uintptr_t)&receive_record)
#define T5AI_RESET_REASON_CP_RECEIVE_BASE 0x40u
#define __DSB() ((void)0)
static unsigned reads;
static bool race;
static void t5ai_cache_invalidate_received(void *p,unsigned size) {
 assert(p==(void *)&receive_record && size==sizeof(receive_record));
 if(++reads==2 && race)receive_record.sequence+=T5AI_RECEIVE_TRACE_SEQUENCE_STEP;
}
"""
            + body(channel, "prv_cp_receive_snapshot")
            + body(channel, "prv_cp_receive_reset_reason")
            + r"""
int main(void) {
 const T5AIReceiveTrace valid={.magic=T5AI_RECEIVE_TRACE_MAGIC,.sequence=2,.inverse=~2u,
  .version=T5AI_RECEIVE_TRACE_VERSION,.generation=7,.stage=T5AIReceive_ACKEnter,
  .header=0x12345678,.payload=0x28012340};
 T5AIReceiveTrace got;
 for(unsigned stage=1;stage<=T5AIReceive_Rejected;++stage) {
  receive_record=valid;receive_record.stage=stage;reads=0;
  assert(prv_cp_receive_snapshot(&got));assert(reads==2 && got.stage==stage);
  assert(got.generation==7 && got.header==valid.header && got.payload==valid.payload);
  reads=0;assert(prv_cp_receive_reset_reason()==(0x40|stage));
 }
 for(unsigned bad=0;bad<7;++bad){
  receive_record=valid;reads=0;
  if(bad==0)receive_record.sequence|=T5AI_RECEIVE_TRACE_IN_PROGRESS;
  if(bad==1)receive_record.inverse^=1;
  if(bad==2)receive_record.magic^=1;
  if(bad==3)receive_record.version++;
  if(bad==4)receive_record.generation=0;
  if(bad==5)receive_record.stage=T5AIReceive_None;
  if(bad==6)receive_record.stage=T5AIReceive_Rejected+1;
  assert(!prv_cp_receive_snapshot(&got));reads=0;
  assert(prv_cp_receive_reset_reason()==0x40);
 }
 receive_record=valid;reads=0;race=true;assert(!prv_cp_receive_snapshot(&got));
 race=false;receive_record=valid;receive_record.sequence=0;receive_record.inverse=UINT32_MAX;reads=0;
 assert(prv_cp_receive_snapshot(&got));
 return 0;
}
"""
        )
        run(code)

    def test_receive_owner_properties_compare_channel_sequence_and_ack_independently(
        self,
    ):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = "\n".join(
            line
            for line in (ROOT / "include/pbl/soc/t5ai/reset.h").read_text().splitlines()
            if line.startswith("#define T5AI_RESET_REASON_")
        )
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
"""
            + constants
            + r"""
#define T5AI_MAILBOX_HEADER_CHANNEL_SHIFT 24u
#define T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT 16u
#define MBOX_ACK_BOX (1u << 12)
#define MBOX_FIFO_NOT_EMPTY (1u << 19)
typedef struct {uint32_t header;} T5AIReceiveTrace;
"""
            + body(channel, "prv_cp_receive_owner_properties")
            + r"""
int main(void) {
 const uint32_t owner=(0x1eu<<24)|(0xffu<<16)|0x55u;
 for(unsigned bits=0;bits<16;++bits) {
  T5AIReceiveTrace snapshot={.header=owner^0xffu};
  if(!(bits&1))snapshot.header^=1u<<24;
  if(!(bits&2))snapshot.header^=1u<<16;
  if(bits&4)snapshot.header|=MBOX_ACK_BOX;
  const uint32_t control=(bits&8)?MBOX_FIFO_NOT_EMPTY:0u;
  assert(prv_cp_receive_owner_properties(&snapshot,owner,control)==(0x40|bits));
 }
 return 0;
}
"""
        )
        run(code)

    def test_terminal_pm_fifo_snapshot_does_not_consume_or_change_routes(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = "\n".join(
            line
            for line in (ROOT / "include/pbl/soc/t5ai/reset.h").read_text().splitlines()
            if line.startswith("#define T5AI_RESET_REASON_")
        )
        protocol = (
            (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h")
            .read_text()
            .replace("#pragma once", "")
        )
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define CONFIG_T5AI_RETAINED_SLEEP_PM_FIFO_DIAGNOSTIC 1
#define MAILBOX_PM_CHANNEL_INDEX 2u
#define MBOX_FIFO_NOT_EMPTY (1u<<19)
#define MBOX_INTERRUPT_ENABLE (1u<<8)
#define MAILBOX_SYSTEM_INTERRUPT_ENABLE (1u<<31)
#define MAILBOX_HEADER_COMMAND_MASK 0xffu
#define TxState_Pending 1u
static uint32_t MBOX_CH0_CTRL,MBOX_CH1_CTRL,SYSTEM_CPU1_INT_32_63_ENABLE,primask,basepri;
#define __get_PRIMASK() primask
#define __get_BASEPRI() basepri
static struct {uint32_t ISER[2];} nvic;
#define NVIC (&nvic)
static struct {unsigned state;struct {uint32_t header,parameter3;} message;} s_tx[16];
#define TxChannel __typeof__(s_tx[0])
"""
            + constants
            + protocol
            + body(channel, "prv_timeout_reset_reason")
            + r"""
int main(void){
 for(unsigned bits=0;bits<16;++bits){
  MBOX_CH0_CTRL=(bits&1)?MBOX_FIFO_NOT_EMPTY:0;
  MBOX_CH1_CTRL=((bits&2)?MBOX_FIFO_NOT_EMPTY:0)|((bits&4)?MBOX_INTERRUPT_ENABLE:0);
  SYSTEM_CPU1_INT_32_63_ENABLE=nvic.ISER[1]=(bits&4)?MAILBOX_SYSTEM_INTERRUPT_ENABLE:0;
  primask=(bits&8)!=0;basepri=0;
  const uint32_t cp=MBOX_CH0_CTRL,ap=MBOX_CH1_CTRL,route=SYSTEM_CPU1_INT_32_63_ENABLE;
  assert(prv_timeout_reset_reason(2)==(0x40|bits));
  assert(cp==MBOX_CH0_CTRL && ap==MBOX_CH1_CTRL && route==SYSTEM_CPU1_INT_32_63_ENABLE);
  primask=0;basepri=(bits&8)?0x60:0;
  assert(prv_timeout_reset_reason(2)==(0x40|bits));
  assert(prv_timeout_reset_reason(3)==0x53);
 }
 return 0;
}
"""
        )
        run(code)

    def test_cp_trace_generation_validation_and_receive_only_snapshot(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = "\n".join(
            line
            for line in (ROOT / "include/pbl/soc/t5ai/reset.h").read_text().splitlines()
            if line.startswith("#define T5AI_RESET_REASON_PM_")
        )
        protocol = (
            (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h")
            .read_text()
            .replace("#pragma once", "")
        )
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {uint32_t magic,generation,stage,inverse;} CPStageTrace;
static CPStageTrace cp;
static unsigned invalidations,barriers;
static bool torn;
#define CONFIG_T5AI_RETAINED_SLEEP_CP_TRACE_ADDRESS ((uintptr_t)&cp)
static void t5ai_cache_invalidate_received(void *address,size_t size){
 assert(address==&cp && size==sizeof(cp));++invalidations;
}
static void barrier(void){if(++barriers%2==0 && torn)cp.stage^=1u;}
#define __DSB() barrier()
"""
            + protocol
            + constants
            + "\n"
            + body(channel, "prv_cp_trace_snapshot")
            + "\n"
            + body(channel, "prv_cp_trace_reset_reason")
            + r"""
int main(void){
 const unsigned stages[]={T5AISleepStage_CP_Resources,T5AISleepStage_CP_Restored,
                          T5AISleepStage_CP_AppVetoRestored,T5AISleepStage_CP_ResumeSent};
 for(unsigned i=0;i<4;++i){
  cp=(CPStageTrace){T5AI_SLEEP_TRACE_MAGIC,17,stages[i],~stages[i]};
  CPStageTrace before=cp;
  unsigned flags=1u|(i==1?4u:0u)|(i==2?8u:0u);
  assert(prv_cp_trace_reset_reason(17)==(0x42u|flags));
  assert(prv_cp_trace_reset_reason(18)==(0x40u|flags));
  assert(!memcmp(&cp,&before,sizeof(cp)));
  cp.inverse^=1;assert(prv_cp_trace_reset_reason(17)==0x40);
  cp=before;cp.magic^=1;assert(prv_cp_trace_reset_reason(17)==0x40);
 }
 cp=(CPStageTrace){T5AI_SLEEP_TRACE_MAGIC,17,T5AISleepStage_CP_Restored,~T5AISleepStage_CP_Restored};
 torn=true;assert(prv_cp_trace_reset_reason(17)==0x40);
 assert(invalidations==17 && barriers==34);
 return 0;
}
"""
        )
        run(code)

    def test_cp_idle_task_private_tcb_value_without_dereference(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define CONFIG_T5AI_RETAINED_SLEEP_CP_IDLE_TCB_ADDRESS 0x20001e7cu
"""
            + body(channel, "prv_cp_idle_task")
            + r"""
int main(void){
 assert(prv_cp_idle_task(0x20001e7cu));
 assert(!prv_cp_idle_task(0));
 assert(!prv_cp_idle_task(0x20001e80u));
 assert(!prv_cp_idle_task(0x28001000u));
 assert(!prv_cp_idle_task(0x60700000u));
 return 0;
}
"""
        )
        run(code)

    def test_cp_runtime_independent_flags_read_only_and_task_race(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = "\n".join(
            line
            for line in (ROOT / "include/pbl/soc/t5ai/reset.h").read_text().splitlines()
            if line.startswith("#define T5AI_RESET_REASON_PM_")
        )
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define CP_EXCEPTION_SYNC_OFFSET 32u
#define CP_EXCEPTION_MAGIC_HEAD 0xabcd9527u
#define CP_EXCEPTION_MAGIC_TAIL 0x9527abcdu
#define CONFIG_T5AI_RETAINED_SLEEP_CP_TICK_HZ 1000u
typedef struct {uint32_t magic_head,cp_entered,ap_entered,turn,dumper,magic_tail;} CPExceptionSync;
static uint32_t fake_task,fake_scheduler,fake_pending;
static CPExceptionSync fake_exception;
static bool race;
#define CONFIG_T5AI_RETAINED_SLEEP_CP_TASK_POINTER_ADDRESS ((uintptr_t)&fake_task)
#define CONFIG_T5AI_RETAINED_SLEEP_CP_SCHEDULER_ADDRESS ((uintptr_t)&fake_scheduler)
#define CONFIG_T5AI_RETAINED_SLEEP_CP_PENDING_TICKS_ADDRESS ((uintptr_t)&fake_pending)
#define CONFIG_T5AI_RETAINED_SLEEP_CP_EXCEPTION_BASE_ADDRESS ((uintptr_t)&fake_exception-CP_EXCEPTION_SYNC_OFFSET)
#define __DSB() ((void)0)
static unsigned invalidations;
static void t5ai_cache_invalidate_received(void *address,size_t size){
 const void *expected[]={&fake_task,&fake_scheduler,&fake_pending,&fake_exception,&fake_task};unsigned i=invalidations++%5;
 assert(address==expected[i] && size==(i==3?sizeof(fake_exception):sizeof(uint32_t)));
}
static bool prv_cp_idle_task(uintptr_t address){if(race)fake_task+=4;return address==0x28001000;}
"""
            + constants
            + "\n"
            + body(channel, "prv_cp_runtime_reset_reason")
            + r"""
int main(void){
 for(unsigned bits=0;bits<16;++bits){
  fake_task=(bits&2)?0x28001000:0;fake_scheduler=(bits&4)?2:0;fake_pending=(bits&8)?1001:1000;
  fake_exception=(CPExceptionSync){CP_EXCEPTION_MAGIC_HEAD,(bits&1)?1:0,0,0,0xffffffff,CP_EXCEPTION_MAGIC_TAIL};
  CPExceptionSync before=fake_exception;const uint32_t saved_task=fake_task;
  assert(prv_cp_runtime_reset_reason(17)==(0x40|bits));
  assert(!memcmp(&fake_exception,&before,sizeof(fake_exception)) && fake_task==saved_task);
 }
 race=true;assert(prv_cp_runtime_reset_reason(17)==0x4d);race=false;
 fake_task=0;fake_exception.magic_head^=1;assert(prv_cp_runtime_reset_reason(17)==0x4c);
 fake_exception.magic_head^=1;fake_exception.cp_entered=2;assert(prv_cp_runtime_reset_reason(17)==0x4c);
 fake_exception.cp_entered=1;fake_exception.dumper=2;assert(prv_cp_runtime_reset_reason(17)==0x4c);
 fake_exception.dumper=0;fake_exception.ap_entered=2;assert(prv_cp_runtime_reset_reason(17)==0x4c);
 return 0;
}
"""
        )
        run(code)
        protocol = (
            (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h")
            .read_text()
            .replace("#pragma once", "")
        )
        release_code = (
            "#define CONFIG_T5AI_RETAINED_SLEEP_CP_RUNTIME_RELEASE_DIAGNOSTIC 1\n"
            + protocol
            + r"""
typedef struct {uint32_t magic,generation,stage,inverse;} CPStageTrace;
static CPStageTrace cp_trace;
static CPStageTrace prv_cp_trace_snapshot(void){return cp_trace;}
"""
            + code[: code.index("int main(void){")]
            + r"""
int main(void){
 for(unsigned bits=0;bits<16;++bits){
  fake_task=(bits&2)?0x28001000:0;fake_scheduler=(bits&4)?2:0;fake_pending=UINT32_MAX;
  fake_exception=(CPExceptionSync){CP_EXCEPTION_MAGIC_HEAD,(bits&1)?1:0,0,0,0xffffffff,CP_EXCEPTION_MAGIC_TAIL};
  unsigned stage=(bits&8)?T5AISleepStage_CP_InterruptsReleased:T5AISleepStage_CP_Restored;
  cp_trace=(CPStageTrace){T5AI_SLEEP_TRACE_MAGIC,17,stage,~stage};
  assert(prv_cp_runtime_reset_reason(17)==(0x40|bits));
  assert(prv_cp_runtime_reset_reason(18)==(0x40|(bits&7)));
  cp_trace.inverse^=1;assert(prv_cp_runtime_reset_reason(17)==(0x40|(bits&7)));
  cp_trace.inverse^=1;cp_trace.magic^=1;assert(prv_cp_runtime_reset_reason(17)==(0x40|(bits&7)));
 }
 return 0;
}
"""
        )
        run(release_code)
        irq_code = (
            "#define CONFIG_T5AI_RETAINED_SLEEP_CP_CLEANUP_IRQ_DIAGNOSTIC 1\n"
            + r"""
#include <stdint.h>
#define CP_CLEANUP_IRQ_RESET_MASK 0x0fu
static uint32_t fake_cleanup_snapshot;
#define CONFIG_T5AI_RETAINED_SLEEP_CP_CLEANUP_IRQ_ADDRESS ((uintptr_t)&fake_cleanup_snapshot)
"""
            + code[: code.index("int main(void){")]
        )
        irq_code = irq_code.replace(
            "unsigned i=invalidations++%5;",
            "if(address==&fake_cleanup_snapshot){assert(size==4);return;}unsigned i=invalidations++%5;",
        )
        irq_code += r"""
int main(void){
 for(unsigned bits=0;bits<32;++bits){
  fake_cleanup_snapshot=bits;assert(prv_cp_runtime_reset_reason(17)==(0x40|(bits&15)));
  assert(fake_cleanup_snapshot==bits);
 }
 return 0;
}
"""
        run(irq_code)

    def test_cp_final_release_boundary_validation(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = "\n".join(
            line
            for line in (ROOT / "include/pbl/soc/t5ai/reset.h").read_text().splitlines()
            if line.startswith("#define T5AI_RESET_REASON_PM_")
        )
        protocol = (
            (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h")
            .read_text()
            .replace("#pragma once", "")
        )
        code = (
            r"""
#include <assert.h>
#include <stdint.h>
#define CONFIG_T5AI_RETAINED_SLEEP_CP_BOUNDARY_DIAGNOSTIC 1
typedef struct {uint32_t magic,generation,stage,inverse;} CPStageTrace;
static CPStageTrace cp;
static CPStageTrace prv_cp_trace_snapshot(void){return cp;}
"""
            + protocol
            + constants
            + "\n"
            + body(channel, "prv_cp_trace_reset_reason")
            + r"""
int main(void){
 const unsigned stages[]={T5AISleepStage_CP_Restored,T5AISleepStage_CP_AppVetoRestored,
  T5AISleepStage_CP_ReadyRevoked,T5AISleepStage_CP_ResumeSent,
  T5AISleepStage_CP_InterruptsReleased,T5AISleepStage_CP_HAL};
 for(unsigned i=0;i<6;++i){
  cp=(CPStageTrace){T5AI_SLEEP_TRACE_MAGIC,17,stages[i],~stages[i]};
  assert(prv_cp_trace_reset_reason(17)==0x41u+i);
  assert(prv_cp_trace_reset_reason(18)==0x40u);
  cp.inverse^=1;assert(prv_cp_trace_reset_reason(17)==0x40u);
  cp.inverse^=1;cp.magic^=1;assert(prv_cp_trace_reset_reason(17)==0x40u);
 }
 return 0;
}
"""
        )
        run(code)

    def test_cp_critical_history_stability_gaps_wrap_and_transaction(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = "\n".join(
            line
            for line in (ROOT / "include/pbl/soc/t5ai/reset.h").read_text().splitlines()
            if line.startswith("#define T5AI_RESET_REASON_PM_")
        )
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define CP_CRITICAL_STABLE_TICKS 1000u
#define CP_CRITICAL_MAX_SAMPLE_GAP_TICKS 100u
#define CP_CRITICAL_MIN_SAMPLES 3u
typedef struct {uint32_t value,transaction_start,stable_start,last_tick,samples;bool valid;} CPCriticalHistory;
static CPCriticalHistory s_cp_critical_history;
static uint32_t counter,tick,transaction;
"""
            + constants
            + "\n"
            + body(channel, "prv_cp_critical_observe")
            + r"""
static void prv_sample_cp_critical(void){prv_cp_critical_observe(counter,tick,transaction);}
"""
            + body(channel, "prv_cp_critical_reset_reason")
            + r"""
static unsigned sample(uint32_t value,uint32_t now){counter=value;tick=now;return prv_cp_critical_reset_reason();}
int main(void){
 transaction=1;
 assert(sample(1,0)==0x41);
 for(unsigned t=100;t<1000;t+=100)assert(sample(1,t)==0x41);
 assert(sample(1,1000)==0x49);
 assert(sample(2,1100)==0x42); // changed count restarts stability
 for(unsigned t=1200;t<=2100;t+=100)sample(2,t);
 assert(sample(2,2100)==0x4a);
 assert(sample(2,2301)==0x42); // gap invalidates sustained claim
 assert(sample(0,2400)==0x40);
 for(unsigned t=2500;t<=3500;t+=100)assert(sample(0,t)==0x40);
 assert(sample(100,3600)==0x47); // saturate rather than wrap
 transaction=2;
 assert(sample(100,3700)==0x47);
 for(unsigned t=3800;t<=4700;t+=100)sample(100,t);
 assert(sample(100,4700)==0x4f);
 transaction=3;
 const uint32_t start=UINT32_MAX-500u;
 for(unsigned t=0;t<1000;t+=100)assert(sample(1,start+t)==0x41);
 assert(sample(1,start+1000u)==0x49); // AON wrap
 return 0;
}
"""
        )
        run(code)

    def test_cp_live_state_read_only_flags_and_critical_counter(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = "\n".join(
            line
            for line in (ROOT / "include/pbl/soc/t5ai/reset.h").read_text().splitlines()
            if line.startswith("#define T5AI_RESET_REASON_PM_")
        )
        code = (
            r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {uint32_t cancelled,prepared,resources,critical;} CPLiveState;
static uint8_t cancelled,prepared,resources;static uint32_t critical;
#define CONFIG_T5AI_RETAINED_SLEEP_CP_CANCELLED_ADDRESS ((uintptr_t)&cancelled)
#define CONFIG_T5AI_RETAINED_SLEEP_CP_PREPARED_ADDRESS ((uintptr_t)&prepared)
#define CONFIG_T5AI_RETAINED_SLEEP_CP_RESOURCES_ADDRESS ((uintptr_t)&resources)
#define CONFIG_T5AI_RETAINED_SLEEP_CP_CRITICAL_ADDRESS ((uintptr_t)&critical)
#define __DSB() ((void)0)
static unsigned invalidations;
static void t5ai_cache_invalidate_received(void *address,size_t size){
 const void *expected[]={&cancelled,&prepared,&resources,&critical};
 const unsigned index=invalidations++%4;
 assert(address==expected[index] && size==(index==3?sizeof(uint32_t):sizeof(uint8_t)));
}
"""
            + constants
            + "\n"
            + body(channel, "prv_cp_live_state")
            + "\n"
            + body(channel, "prv_cp_state_reset_reason")
            + r"""
int main(void){
 for(unsigned bits=0;bits<16;++bits){
  cancelled=(bits&1)!=0;prepared=(bits&2)!=0;resources=(bits&4)!=0;
  for(unsigned counter=1;counter<100;counter+=31){
   critical=(bits&8)?counter:0;
   assert(prv_cp_state_reset_reason()==(0x40u|bits));
   assert(cancelled==((bits&1)!=0) && prepared==((bits&2)!=0));
   assert(resources==((bits&4)!=0) && critical==((bits&8)?counter:0));
  }
 }
 assert(invalidations==256);
 return 0;
}
"""
        )
        run(code)

    def test_terminal_cp_irq_snapshot_is_read_only(self):
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = "\n".join(
            line
            for line in (ROOT / "include/pbl/soc/t5ai/reset.h").read_text().splitlines()
            if line.startswith("#define T5AI_RESET_REASON_PM_")
        )
        code = (
            r"""
#include <assert.h>
#include <stdint.h>
#define MAILBOX_SYSTEM_INTERRUPT_ENABLE (1u<<31)
#define MBOX_INTERRUPT_ENABLE (1u<<8)
#define SYSTEM_CPU0_INTERRUPT_MASK (1u<<2)
#define SYSTEM_CPU0_HALTED (1u<<0)
static uint32_t SYSTEM_CPU0_MAILBOX_ROUTE,MBOX_CH0_CTRL,SYSTEM_CPU0_CONTROL,SYSTEM_CPU_RUN_STATUS;
"""
            + constants
            + "\n"
            + body(channel, "prv_cp_irq_reset_reason")
            + r"""
int main(void){
 for(unsigned bits=0;bits<16;++bits){
  SYSTEM_CPU0_MAILBOX_ROUTE=(bits&1)?MAILBOX_SYSTEM_INTERRUPT_ENABLE:0;
  MBOX_CH0_CTRL=(bits&2)?MBOX_INTERRUPT_ENABLE:0;
  SYSTEM_CPU0_CONTROL=(bits&4)?SYSTEM_CPU0_INTERRUPT_MASK:0;
  SYSTEM_CPU_RUN_STATUS=(bits&8)?SYSTEM_CPU0_HALTED:0;
  const uint32_t route=SYSTEM_CPU0_MAILBOX_ROUTE,control=SYSTEM_CPU0_CONTROL;
  const uint32_t mailbox=MBOX_CH0_CTRL,status=SYSTEM_CPU_RUN_STATUS;
  assert(prv_cp_irq_reset_reason()==(0x40|bits));
  assert(route==SYSTEM_CPU0_MAILBOX_ROUTE && control==SYSTEM_CPU0_CONTROL);
  assert(mailbox==MBOX_CH0_CTRL && status==SYSTEM_CPU_RUN_STATUS);
 }
 return 0;
}
"""
        )
        run(code)

    def test_hci_exhaustion_does_not_wait_for_logger(self):
        hci = source("third_party/nimble/transport/t5ai_hci_ipc.c")
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        adapter = (ROOT / "soc/t5ai/mailbox_logging.c").read_text()
        code = (
            COMMON
            + r"""
#define T5AI_HCI_PENDING_FRAME_CAPACITY 8
#define T5AI_MIPC_RX_LOGICAL_CHANNEL 65
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
typedef struct {uint32_t header,object,result;} T5AIMIPCFrame;
typedef struct {uintptr_t address;size_t length;} T5AICPMailboxFrame;
typedef struct {T5AIMIPCFrame frame;} T5AIHCIPendingFrame;
static T5AIHCIPendingFrame s_pending_frames[8];
static uint8_t s_pending_frame_head,s_pending_frame_count=8;
static bool s_accept_frames=true,s_started=true,s_faulted;
static struct {uint32_t count,header,object;bool reported;} s_queue_fault;
static pthread_mutex_t state_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pulse_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static bool logger_ready,mailbox_done,report_attempted,reported;
static unsigned acks,physical_acks,drains,supervision,wakes;
static int s_worker_wake,s_wake;
static bool s_reset_sent=true;
static unsigned MBOX_CH1_CTRL;
#define MBOX_INTERRUPT_ENABLE 1
#define MBOX_FIFO_NOT_EMPTY 2
#define MAILBOX_FAULT_POLL_MS 10
#define MAILBOX_BUSY_RETRY_MS 1
#define PBL_TICK_FOREVER UINT32_MAX
#define PBL_MSEC(x) (x)
typedef unsigned pbl_tick_t;
static int s_task;
static _Thread_local void *current;
static void *pbl_thread_current(void){return current;}
static bool mcu_state_is_isr(void){return false;}
static bool t5ai_mailbox_channel_is_owner(void);
typedef struct {int unused;} LogState;
static LogState state;
static LogState *t5ai_stock_get_log_state(void){
  assert(!pthread_mutex_lock(&state_lock));return &state;
}
static int t5ai_cp_mailbox_acknowledge_frame(const T5AICPMailboxFrame *f,bool ok){
  (void)f;assert(ok);++acks;return 0;
}
static int pbl_sem_give(int *s){if(s==&s_worker_wake)++wakes;else assert(s==&s_wake);return 0;}
static void pressure_log(void){
  assert(!pthread_mutex_lock(&gate));report_attempted=true;
  pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
  assert(!pthread_mutex_lock(&state_lock));
  assert(!pthread_mutex_lock(&pulse_lock));
  reported=true;pthread_mutex_unlock(&pulse_lock);pthread_mutex_unlock(&state_lock);
}
#define PBL_LOG_ERR(...) pressure_log_values(__VA_ARGS__)
static void pressure_log_values(const char *fmt,...){(void)fmt;pressure_log();}
"""
        )
        code += body(channel, "t5ai_mailbox_channel_is_owner")
        code += body(adapter, "kernel_applib_get_log_state")
        code += body(hci, "prv_set_started") + body(hci, "prv_mark_faulted")
        code += body(hci, "prv_queue_pending_frame")
        if "prv_report_queue_fault" in hci:
            code += body(hci, "prv_report_queue_fault")
        else:
            code += "static void prv_report_queue_fault(void){pressure_log();}\n"
        code += body(hci, "prv_route_frame")
        code += r"""
static void *logger(void *unused){
  (void)unused;
  pthread_mutex_lock(&state_lock);pthread_mutex_lock(&pulse_lock);
  pthread_mutex_lock(&gate);logger_ready=true;pthread_cond_broadcast(&changed);
  struct timespec t=deadline();
  while(!mailbox_done)assert(pthread_cond_timedwait(&changed,&gate,&t)==0);
  pthread_mutex_unlock(&gate);
  pthread_mutex_unlock(&pulse_lock);pthread_mutex_unlock(&state_lock);return NULL;
}
static void *reporter(void *unused){
  (void)unused;
  // Model the independent worker reaching the shared logger while it is owned.
  pressure_log();
  prv_report_queue_fault();return NULL;
}
static bool prv_any_active(void){return false;}
static unsigned prv_milliseconds_to_ticks(unsigned t){return t;}
static unsigned t5ai_mb_ipc_async_poll(void){return UINT32_MAX;}
static unsigned t5ai_cp_mailbox_heartbeat_poll(void){
  assert(kernel_applib_get_log_state()==NULL); // before either logging lock
  if(drains==32){
    assert(s_faulted && !s_started);
    pthread_mutex_lock(&gate);mailbox_done=true;pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);pthread_exit(NULL);
  }
  ++supervision;return 1000;
}
static int pbl_sem_take(int *s,unsigned t){(void)t;assert(s==&s_wake);return 0;}
static bool prv_drain_rx(void){
  T5AIMIPCFrame pending={.header=0x1234,.object=0x5678};
  T5AICPMailboxFrame frame={.address=(uintptr_t)&pending,.length=sizeof(pending)};
  prv_route_frame(65,&frame,NULL);++drains;return true;
}
static int prv_expired_channel(void){return -1;}
static void prv_reset_after_timeout(unsigned c){(void)c;assert(0);}
static bool prv_publish_pending_acks(void){assert(acks==drains);++physical_acks;return true;}
static void prv_send_reset(void){assert(0);}
static int prv_schedule_tx(void){return 0;}
#define E_BUSY -5
static void pbl_thread_sleep(unsigned t){assert(t==1);}
"""
        code += body(channel, "prv_mailbox_task")
        code += r"""
static void *owner(void *unused){current=&s_task;prv_mailbox_task(unused);return NULL;}
int main(void){
  pthread_t l,w,o;
  pthread_create(&l,NULL,logger,NULL);
  pthread_mutex_lock(&gate);
  while(!logger_ready)pthread_cond_wait(&changed,&gate);
  pthread_mutex_unlock(&gate);
  pthread_create(&w,NULL,reporter,NULL);
  pthread_mutex_lock(&gate);
  while(!report_attempted)pthread_cond_wait(&changed,&gate);
  pthread_mutex_unlock(&gate);
  pthread_create(&o,NULL,owner,NULL);
  pthread_join(o,NULL);pthread_join(l,NULL);pthread_join(w,NULL);
  assert(physical_acks==32 && acks==32 && drains==32 && supervision==32 && wakes==32 && reported);
  assert(s_queue_fault.count==32 && s_queue_fault.header==0x1234 &&
         s_queue_fault.object==0x5678 && s_queue_fault.reported);
  // Saturation preserves the first descriptor rather than wrapping or replacing it.
  s_queue_fault.count=UINT32_MAX;
  T5AIMIPCFrame p={.header=99,.object=88};
  T5AICPMailboxFrame f={.address=(uintptr_t)&p,.length=sizeof(p)};
  prv_route_frame(65,&f,NULL);
  assert(s_queue_fault.count==UINT32_MAX && s_queue_fault.header==0x1234);
  LogState *normal=kernel_applib_get_log_state();assert(normal==&state);
  pthread_mutex_unlock(&state_lock);
}
"""
        run(code)

    def test_cp_owner_apis_reject_before_locks_and_waits(self):
        cp = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        code = (
            COMMON
            + r"""
#define E_INVALID_ARGUMENT -5
#define PASSED(x) ((x)>=0)
#define PBL_MSEC(x) (x)
#define CP_RUNTIME_TIMEOUT_MS 5000
#define CP_LOG_ASSERT_OUT_COMMAND 4
#define CP_LOG_STATIC_TAG 0xffff
#define CP_LOG_TAG_SHIFT 16
#define CP_LOG_CHANNEL 0x1e
#define CP_PM_RX_CHANNEL 0x42
struct pbl_sem {int unused;};
typedef struct {uint32_t header,parameter1,parameter2,parameter3;} T5AIMailboxMessage;
typedef struct {uintptr_t address;} T5AICPMailboxFrame;
static struct {struct pbl_sem ready;} s_pm_queue;
static PBL_MUTEX_DEFINE(s_control_mutex);
static PBL_MUTEX_DEFINE(s_log_mutex);
static bool s_log_faulted;
static uint8_t s_log_buffer[160];
static T5AIMailboxMessage s_log_message;
static bool t5ai_mailbox_channel_is_owner(void){return true;}
static bool mcu_state_is_isr(void){return false;}
static bool pbl_kernel_is_running(void){assert(0);return true;}
static status_t prv_ensure_runtime(void){assert(0);return 0;}
static bool t5ai_mailbox_channel_faulted(void){assert(0);return false;}
static int pbl_mutex_lock(pthread_mutex_t *m,unsigned t){
 (void)m;(void)t;assert(0);return 0;
}
static void pbl_mutex_unlock(pthread_mutex_t *m){(void)m;assert(0);}
static int pbl_sem_take(struct pbl_sem *s,unsigned t){(void)s;(void)t;assert(0);return -1;}
static bool prv_dequeue(unsigned ch,T5AICPMailboxFrame *f){(void)ch;(void)f;assert(0);return false;}
static status_t prv_wait_for_log_buffer_release(bool running){(void)running;assert(0);return 0;}
static void t5ai_cache_clean(const void *p,size_t n){(void)p;(void)n;assert(0);}
static status_t t5ai_mailbox_channel_send(unsigned ch,const T5AIMailboxMessage *m,
                                        T5AIMailboxMessage *a,unsigned t){
 (void)ch;(void)m;(void)a;(void)t;assert(0);return 0;
}
#define t5ai_mailbox_channel_bootstrap_send t5ai_mailbox_channel_send
"""
        )
        code += body(cp, "t5ai_cp_mailbox_lock")
        code += body(cp, "t5ai_cp_mailbox_receive")
        code += body(cp, "t5ai_cp_mailbox_log")
        code += r"""
int main(void){
 pbl_irq_lock();pbl_irq_unlock();
 T5AICPMailboxFrame frame;
 assert(t5ai_cp_mailbox_lock()==E_INVALID_OPERATION);
 assert(t5ai_cp_mailbox_receive(CP_PM_RX_CHANNEL,&frame)==E_INVALID_OPERATION);
 assert(t5ai_cp_mailbox_log("pressure",8)==E_INVALID_OPERATION);
}
"""
        run(code)
