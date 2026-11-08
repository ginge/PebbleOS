# SPDX-License-Identifier: Apache-2.0
"""Execute native AP and board-owned CP alarm code with modeled endpoints."""

import json
import re
import unittest
from pathlib import Path

from tools.tests.native_t5ai_harness import run
from tools.tests.test_t5ai_native_objects import body

ROOT = Path(__file__).resolve().parents[2]
PROTOCOL = f'#include "{ROOT / "include/pbl/soc/t5ai/sleep_protocol.h"}"\n'
CP_PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
typedef int bk_err_t;
typedef unsigned aon_rtc_id_t;
#define BK_OK 0
#define BK_FAIL -1
#define AON_RTC_ID_1 0
#define PM_WAKEUP_SOURCE_INT_RTC 1
#define LOW_PWR_CORE_UNHYGIENIX_ALARM_FIRED 16
#define LOW_PWR_CORE_UNHYGIENIX_SLEEP_RESUMED 17
#define PM_SLEEP_MODULE_NAME_APP 12
#define CONFIG_NMI_WDT_EN 1
#define CONFIG_AON_WDT 1
#define CONFIG_INT_AON_WDT 0
#define __DSB() ((void)0)
#define __DMB() ((void)0)
#define __ISB() ((void)0)
#define SCB_SCR_SLEEPDEEP_Msk 4u
static struct {uint32_t DCCMVAC, ICSR, SCR;} fake_scb;
static bool fake_deep_rejected, fake_force_deep, fake_expect_shallow;
static uint32_t fake_sleep_control_read(void){return fake_force_deep?(fake_scb.SCR|SCB_SCR_SLEEPDEEP_Msk):(fake_deep_rejected?(fake_scb.SCR&~SCB_SCR_SLEEPDEEP_Msk):fake_scb.SCR);}
static uint32_t fake_boot_reset;
static uint32_t aon_pmu_ll_get_r7a(void){return fake_boot_reset;}
static uint32_t sys_ll_get_cpu0_lv_sleep_cfg_value(void){return 5;}
static uint32_t sys_ll_get_cpu_device_mem_ctrl3_value(void){return 0;}
static uint32_t fake_rtc_control=72;
#define SCB (&fake_scb)
static bool memory_retained=true, peripherals_retained=true;
static uint32_t fake_aon;
static uint32_t fake_gpio_status_low, fake_gpio_status_high;
static uint32_t fake_mailbox_control=256, fake_mailbox_fifo, fake_mailbox_config=65544;
static uint32_t fake_mailbox_global_control=5,fake_mailbox_interrupt_status;
static uint32_t fake_uart[10];
static uint8_t fake_trace[4096];static unsigned fake_trace_count;
static bool fake_trace_stalls, fake_trace_sets_tick, fake_trace_expires, fake_trace_sets_gpio;
static void fake_trace_out(uint8_t byte){
 assert(fake_trace_count<sizeof(fake_trace));fake_trace[fake_trace_count++]=byte;fake_uart[7]=byte;
 if(fake_trace_stalls){fake_uart[6]&=~(1u<<17);}
 if(fake_trace_sets_tick){fake_scb.ICSR|=1u<<26;}
 if(fake_trace_sets_gpio){fake_gpio_status_high|=1u<<23;}
 if(fake_trace_expires){fake_aon+=200;fake_trace_expires=false;}
}
static bool mailbox_ready=true;
static bool unhygienix_native_mailbox_sleep_ready(void){return mailbox_ready;}
static uint32_t unhygienix_native_mailbox_snapshot(T5AISleepSnapshot key){return key;}
static uint32_t sys_ll_get_cpu0_int_halt_clk_op_value(void);
static uint32_t fake_cp_route;
static uint32_t sys_ll_get_cpu0_int_32_63_en_value(void){return fake_cp_route;}
static uint32_t fake_irq_enable_low, fake_peripheral_clocks;
static uint32_t sys_ll_get_cpu0_int_0_31_en_value(void){return fake_irq_enable_low;}
static uint32_t sys_ll_get_cpu_device_clk_enable_value(void){return fake_peripheral_clocks;}
static uint32_t sys_ll_get_cpu1_int_halt_clk_op_value(void){return 16;}
static uint32_t sys_ll_get_cpu1_int_32_63_en_value(void){return 1u<<31;}
static uint32_t fake_ap_pending_low, fake_ap_pending_high;
static uint32_t sys_ll_get_cpu1_int_0_31_status_value(void){return fake_ap_pending_low;}
static uint32_t sys_ll_get_cpu1_int_32_63_status_value(void){return fake_ap_pending_high;}
static uint32_t sys_ll_get_cpu_clk_div_mode2_value(void){return 0;}
static uint32_t sys_ll_get_cpu_current_run_status_value(void){return 0;}
static uint32_t aon_pmu_ll_get_r3(void);
static uint32_t fake_uart_clock=1;
static uint32_t fake_primask=1, fake_faultmask;
static uint32_t __get_FAULTMASK(void){return fake_faultmask;}
static uint32_t __get_PRIMASK(void){return fake_primask;}
static uint32_t fake_basepri;
static uint32_t __get_BASEPRI(void){return fake_basepri;}
static uint32_t sys_ll_get_cpu_device_clk_enable_uart1_cken(void){return fake_uart_clock;}
static unsigned gpio_enters,gpio_exits,gpio_wakes;
static int gpio_enter_low_power(void *p){assert(!p);++gpio_enters;return 0;}
static int gpio_exit_low_power(void *p){assert(!p);++gpio_exits;return 0;}
static int gpio_enable_interrupt_mult_for_wake(void){++gpio_wakes;return 0;}
static bool unhygienix_native_memory_retained(void){return memory_retained;}
static bool unhygienix_native_peripherals_retained(void){return peripherals_retained;}
#define CONFIG_PSRAM_POWER_DOMAIN_LV_DISABLE 0
#define CONFIG_AHBP_POWER_DOMAIN_LV_DISABLE 0
typedef struct {unsigned event;uint32_t param1,param2,param3;} low_pwr_core_msg_t;
typedef struct {uint8_t name[8];uint64_t period_tick;unsigned period_cnt;
 void (*callback)(aon_rtc_id_t,uint8_t *,void *);void *param_p;} alarm_info_t;
static uint32_t coordination=0x80000000u,lpo;
static uint32_t fake_admission_tick, fake_admission_release=UINT32_MAX;
static uint32_t fake_admission_clock(void){
 if(fake_admission_tick==fake_admission_release)coordination|=2u;
 return fake_admission_tick++;
}
static uint32_t aon_pmu_ll_get_r41(void){return lpo;}
static void aon_pmu_ll_set_r41(uint32_t v){lpo=v;}
static uint64_t missing_votes=(1ull<<33)|(1ull<<8);
static uint64_t unhygienix_native_sleep_missing_votes(void){return missing_votes;}
static bool sibling_reset=true, sibling_power_down=true, sibling_ack=true, sibling_power_ack=true, app_allowed;
static unsigned ready_writes;
static unsigned sys_ll_get_cpu2_int_halt_clk_op_cpu2_sw_rst(void){return !sibling_reset;}
static unsigned sys_ll_get_cpu2_int_halt_clk_op_cpu2_pwr_dw(void){return sibling_power_down;}
static unsigned sys_ll_get_cpu_current_run_status_cpu2_sw_reset(void){return !sibling_ack;}
static unsigned sys_ll_get_cpu_current_run_status_cpu2_pwr_dw_state(void){return sibling_power_ack;}
static unsigned aon_pmu_ll_get_r3_cp1_enter_wfi_state(void){return (coordination&2)!=0;}
static void aon_pmu_ll_set_r3_cp0_sleep_vote_state(unsigned value){
 ++ready_writes;coordination=(coordination&~8u)|(value?8u:0);
}
static int bk_pm_module_vote_sleep_ctrl(unsigned module,unsigned allow,unsigned time){
 assert(module==12 && !time);app_allowed=allow;return 0;
}
static alarm_info_t installed;
static low_pwr_core_msg_t pending;
static unsigned locked, registers, unregisters, responses, wakes;
static bool reject,queue_full,send_fail,unregister_fail,ack_during_response;
void unhygienix_native_sleep_reply_ack(uint32_t,uint32_t,uint32_t,uint32_t);
static unsigned queued_calls;
static uint32_t reply_status,reply_generation;
static uint32_t rtos_disable_int(void){return locked++;}
static void rtos_enable_int(unsigned previous){assert(locked==previous+1);locked=previous;}
static int bk_pm_wakeup_source_set(unsigned source,void *context){assert(locked && source==1 && !context);return 0;}
static uint64_t bk_aon_rtc_get_current_tick(unsigned id){assert(id==0);return 456;}
static int bk_alarm_register(unsigned id,alarm_info_t *a){
 assert(locked && id==0 && !strcmp((char*)a->name,"pbl_ap"));
 assert(a->period_cnt==1 && a->period_tick>=64);++registers;
 if(reject){return -27;}installed=*a;return 0;
}
static int bk_alarm_unregister(unsigned id,uint8_t *name){
 assert(!locked && id==0 && !strcmp((char*)name,"pbl_ap"));++unregisters;
 const unsigned before=queued_calls;
 if(installed.callback)installed.callback(0,installed.name,installed.param_p);
 assert(queued_calls==before); // Cancellation disarms before releasing native state lock.
 return unregister_fail?-1:0;
}
static unsigned direct_resume_count, last_resume_generation;
static int bk_low_pwr_core_send_msg(low_pwr_core_msg_t *m){++queued_calls;pending=*m;return queue_full?-1:0;}
static int unhygienix_native_sleep_notify_ap(uint32_t generation,uint32_t tick){
 assert(!(coordination&8) && !app_allowed && generation && tick==456);
 last_resume_generation=generation;++direct_resume_count;++wakes;return send_fail?-1:0;
}
static int bk_pm_cp0_response_cp1(unsigned cmd,uint32_t status,uint32_t generation,uint32_t abi){
 assert(!locked && abi==T5AI_PM_NATIVE_ALARM_ABI);
 if(cmd==T5AI_PM_NATIVE_ALARM_COMMAND){++responses;reply_status=status;reply_generation=generation;}
 else {assert((cmd==T5AI_PM_NATIVE_ALARM_WAKE || cmd==T5AI_PM_NATIVE_SLEEP_RESUMED) && registers);++wakes;}
 if(ack_during_response && !send_fail)unhygienix_native_sleep_reply_ack(cmd,generation,abi,0);
 return send_fail?-1:0;
}
"""
CP_PREFIX += r"""
static uint32_t fake_mode,fake_hf=2,fake_sleep_config=0x80000,fake_pending_low,fake_pending_high;
static struct {uint32_t ISPR[2];} fake_nvic;
typedef int IRQn_Type;
#define INT_SRC_MAILBOX 63
static uint32_t fake_cp_mailbox_control, fake_mailbox_nvic_enabled, fake_irq_kicks;
static uint32_t NVIC_GetEnableIRQ(IRQn_Type irq){assert(irq==63);return fake_mailbox_nvic_enabled;}
static uint32_t NVIC_GetPendingIRQ(IRQn_Type irq){assert(irq==63);return fake_nvic.ISPR[1]>>31;}
static void NVIC_SetPendingIRQ(IRQn_Type irq){assert(irq==63);fake_nvic.ISPR[1]|=1u<<31;++fake_irq_kicks;}
#define NVIC (&fake_nvic)
static uint32_t fake_cpu_mask, fake_flash_operation_control;
static uint32_t aon_pmu_ll_get_r3(void){return coordination;}
static uint32_t sys_ll_get_cpu0_int_halt_clk_op_cpu0_int_mask(void){return fake_cpu_mask;}
static void (*fake_mask_observer)(uint32_t);
static void sys_ll_set_cpu0_int_halt_clk_op_cpu0_int_mask(uint32_t mask){
 if(fake_mask_observer){fake_mask_observer(mask);}
 fake_cpu_mask=mask;
}
static unsigned arch_wfis, mask_barriers;
static uint32_t fake_power_parameter=0x1234,fake_wake_status,fake_next_wake,fake_clear_status;
static bool fake_update_wake;
static uint32_t aon_pmu_ll_get_r40(void){return fake_power_parameter;}
static bool fake_reject_power_restore;
static void aon_pmu_ll_set_r40(uint32_t value){if(!fake_reject_power_restore)fake_power_parameter=value;}
static uint32_t aon_pmu_ll_get_r71(void){return fake_wake_status;}
static uint32_t aon_pmu_ll_get_r43(void){return fake_clear_status;}
static void arch_deep_sleep(void){++arch_wfis;fake_aon+=7;if(fake_update_wake)fake_wake_status=fake_next_wake;}
static uint32_t sys_ll_get_cpu0_int_halt_clk_op_value(void){return 0x10u | (fake_cpu_mask<<2);}
void unhygienix_cp_mask_barrier(volatile uint32_t *control,uint32_t value,uint32_t before){
 assert((uintptr_t)control==0x44010010u && value==0x14u && before==0x10u);
 assert(!(fake_scb.SCR & SCB_SCR_SLEEPDEEP_Msk));
 ++mask_barriers;sys_ll_set_cpu0_int_halt_clk_op_cpu0_int_mask(1);
 sys_ll_set_cpu0_int_halt_clk_op_cpu0_int_mask(0);
}
void unhygienix_cp_masked_wfi(volatile uint32_t *control,uint32_t value){
 assert((uintptr_t)control==0x44010010u && value==0x14u);
 assert(!!(fake_scb.SCR & SCB_SCR_SLEEPDEEP_Msk) != fake_expect_shallow);
 sys_ll_set_cpu0_int_halt_clk_op_cpu0_int_mask(1);
 arch_deep_sleep();
}
static uint32_t fake_reset_word, fake_reset_latched, fake_reset_keys;
static void fake_persistent_key(uint32_t key){
 assert(key==((fake_reset_keys++&1u)?0xbdb4aa55u:0x424b55aau));
 if(!(fake_reset_keys&1u))fake_reset_latched=fake_reset_word;
}
void unhygienix_cp_masked_wfi_trace(volatile uint32_t *control,uint32_t value,uint32_t reason){
 unhygienix_cp_masked_wfi(control,value);
 fake_reset_word=(fake_reset_word&~0xff0u)|(reason&0xff0u);
 fake_persistent_key(0x424b55aau);fake_persistent_key(0xbdb4aa55u);
}
static uint32_t fake_analog_enabled, fake_analog_period=5, fake_analog_clock=1, fake_analog_latch;
static unsigned fake_analog_writes;
static uint32_t sys_ll_get_ana_reg10_spi_timerwken(void){return fake_analog_enabled;}
static uint32_t sys_ll_get_ana_reg10_timer_sel(void){return fake_analog_period;}
static uint32_t sys_ll_get_ana_reg12_clk_sel(void){return fake_analog_clock;}
static uint32_t sys_ll_get_ana_reg9_spi_latch1v(void){return fake_analog_latch;}
static void sys_ll_set_ana_reg9_spi_latch1v(uint32_t v){fake_analog_latch=v;++fake_analog_writes;}
static void sys_ll_set_ana_reg10_spi_timerwken(uint32_t v){assert(fake_analog_latch);fake_analog_enabled=v;++fake_analog_writes;}
static void sys_ll_set_ana_reg10_timer_sel(uint32_t v){assert(fake_analog_latch);fake_analog_period=v;++fake_analog_writes;}
static bool fake_flash_ready=true;
static bool unhygienix_native_flash_ready(void){return fake_flash_ready;}
static bool unhygienix_native_flash_resume(void){return true;}
static uint32_t sys_ll_get_cpu_clk_div_mode1_value(void){return fake_mode;}
static uint32_t sys_ll_get_ana_reg5_value(void){return fake_hf;}
static uint32_t sys_ll_get_cpu_power_sleep_wakeup_value(void){return fake_sleep_config;}
static void sys_ll_set_cpu_power_sleep_wakeup_value(uint32_t value){fake_sleep_config=value;}
static uint32_t sys_ll_get_cpu0_int_0_31_status_value(void){return fake_pending_low;}
static uint32_t sys_ll_get_cpu0_int_32_63_status_value(void){return fake_pending_high;}
"""
CP_MAIN = r"""
static void test_resume_complete(void){
 unhygienix_native_sleep_resumed();
 const uint32_t before=fake_primask;fake_primask=0;
 unhygienix_native_sleep_interrupts_restored();fake_primask=before;
}

static void observe_mask_admission(uint32_t mask){
 assert(mask==NATIVE_CP_IRQ_MASKED && !fake_cpu_mask);
 assert(s_native_trace.stage==T5AISleepStage_CP_WFI);
 assert(unhygienix_native_lowvol_count==1);
}
static void request(unsigned op,unsigned gen,unsigned ticks){
 low_pwr_core_msg_t m={.param1=gen,.param2=ticks,.param3=op};unhygienix_alarm_request(&m);
}
// Legacy cases exercise a complete owner transaction; dedicated cases below
// retain the service lease and inject each transport boundary explicitly.
static void cancel_and_finish(unsigned gen,unsigned ticks){
 request(T5AIAlarm_Cancel,gen,ticks);
 if(!reply_status && s_native_cleanup_pending && CASE!=81 && CASE!=82){
  const unsigned cancelled_responses=responses;
  request(T5AIAlarm_FinishCleanup,gen,0);assert(!reply_status);
  responses=cancelled_responses;
 }
}
static void fire(void){assert(installed.callback);installed.callback(0,installed.name,installed.param_p);}
int main(void){
 ack_during_response=CASE!=81 && CASE!=82;
 request(T5AIAlarm_Query,0,0);assert(!reply_status && !reply_generation);
 request(T5AIAlarm_Arm,1,0);assert(reply_status && !registers);
#if CASE >= 90 && CASE <= 93
 request(T5AIAlarm_ArmSleep,1,100);coordination|=2u;
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_sleep_mask_exit();unhygienix_native_sleep_resumed();
 fake_primask=fake_basepri=0;unhygienix_native_sleep_interrupts_restored();
 coordination&=~2u;send_fail=CASE==92;ack_during_response=CASE==93;
 request(T5AIAlarm_Cancel,1,0);ack_during_response=false;
 assert(s_native_cancelled && !unhygienix_native_sleep_prepared);
 assert(s_native_cleanup_pending && !s_cleanup_reply_pending);
 if(CASE==93){
  ack_during_response=true;request(T5AIAlarm_FinishCleanup,1,0);ack_during_response=false;
  assert(!s_native_cleanup_pending && !s_cleanup_reply_pending);
 }
 else {
  assert(s_native_cleanup_pending && !s_cleanup_reply_pending);
  assert(!unhygienix_native_sleep_tickless_allowed());
  unhygienix_native_sleep_reply_ack(T5AI_PM_NATIVE_ALARM_COMMAND,1,T5AI_PM_NATIVE_ALARM_ABI,0);
  assert(s_native_cleanup_pending && !s_cleanup_reply_pending);
  request(T5AIAlarm_FinishCleanup,1,0);
  assert(s_native_cleanup_pending && s_cleanup_reply_pending);
  unhygienix_native_sleep_reply_ack(T5AI_PM_NATIVE_SLEEP_RESUMED,1,T5AI_PM_NATIVE_ALARM_ABI,0);
  unhygienix_native_sleep_reply_ack(T5AI_PM_NATIVE_ALARM_COMMAND,2,T5AI_PM_NATIVE_ALARM_ABI,0);
  unhygienix_native_sleep_reply_ack(T5AI_PM_NATIVE_ALARM_COMMAND,1,T5AI_PM_NATIVE_ALARM_ABI+1,0);
  assert(s_native_cleanup_pending && s_cleanup_reply_pending);
  if(CASE==91){
   unhygienix_native_sleep_reply_ack(T5AI_PM_NATIVE_ALARM_COMMAND,1,T5AI_PM_NATIVE_ALARM_ABI,1);
   assert(s_native_faulted && s_native_cleanup_pending && s_cleanup_reply_pending);
  } else if(CASE==92){assert(s_native_faulted && s_native_cleanup_pending);}
  else {
   request(T5AIAlarm_Query,0,0);assert(s_native_cleanup_pending);
   request(T5AIAlarm_ArmSleep,2,100);assert(reply_status && s_native_generation==1);
   unhygienix_native_sleep_reply_ack(T5AI_PM_NATIVE_ALARM_COMMAND,1,T5AI_PM_NATIVE_ALARM_ABI,0);
   assert(!s_native_cleanup_pending && !s_cleanup_reply_pending);
   assert(unhygienix_native_sleep_tickless_allowed());
   assert(s_cleanup_cancel_progress==T5AICancelProgress_CleanupReleased);
  }
 }
#elif CASE == 88 || CASE == 89
 request(T5AIAlarm_ArmSleep,1,100);assert(!reply_status);
 assert(s_cleanup_cancel_progress==T5AICancelProgress_None);
 send_fail=CASE==89;
 cancel_and_finish(1,0);
 assert(s_native_cancelled && !s_native_cleanup_pending && unregisters==1);
 assert(s_cleanup_cancel_progress==(CASE==89 ? T5AICancelProgress_ReplyFailed : T5AICancelProgress_ReplySubmitted));
 const uint32_t cancel_progress=s_cleanup_cancel_progress;
 request(T5AIAlarm_Query,0,0);assert(s_cleanup_cancel_progress==cancel_progress);
#elif CASE == 87
 unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_ACKSend);assert(!s_cleanup_mailbox_progress);
 s_native_generation=7;s_native_cleanup_pending=true;
 unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_ACKSend);assert(s_cleanup_mailbox_progress==T5AIMailboxProgress_ACKSend);
 unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_ACKFailed+1);assert(s_cleanup_mailbox_progress==T5AIMailboxProgress_ACKSend);
 request(T5AIAlarm_CleanupMailboxProgress,0,0);assert(!reply_status && reply_generation==T5AIMailboxProgress_ACKSend);
 s_native_cleanup_pending=false;
 unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_ISRReturn);
 assert(s_cleanup_mailbox_progress==T5AIMailboxProgress_ISRReturn);
 s_native_generation=0;
 unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_ACKSend);
 assert(s_cleanup_mailbox_progress==T5AIMailboxProgress_ISRReturn);
#elif CASE == 86
 unhygienix_native_sleep_mailbox_irq_observed(0);assert(!s_cleanup_mailbox_isrs);
 unhygienix_native_sleep_prepared=s_native_cleanup_pending=true;
 unhygienix_native_sleep_mailbox_irq_observed(1);
 assert(s_cleanup_irq_delivery==NATIVE_DELIVERY_ISR_SEEN && s_cleanup_mailbox_isrs==1);
 fake_faultmask=1;fake_nvic.ISPR[1]=1u<<31;
 unhygienix_native_sleep_mailbox_irq_observed(0);
 assert(s_cleanup_irq_delivery==15 && s_cleanup_mailbox_isrs==2 && s_cleanup_mailbox_empty_status==1);
 s_cleanup_mailbox_isrs=s_cleanup_mailbox_empty_status=UINT32_MAX;
 unhygienix_native_sleep_mailbox_irq_observed(0);assert(s_cleanup_mailbox_isrs==UINT32_MAX && s_cleanup_mailbox_empty_status==UINT32_MAX);
 request(T5AIAlarm_CleanupIRQDelivery,0,0);assert(!reply_status && reply_generation==15);
#elif CASE == 95
 assert(unhygienix_native_normal_sleep_allowed());assert(!s_native_normal_sleep_skips);
 s_native_generation=7;s_native_system_mask_used=true;
 assert(!unhygienix_native_normal_sleep_allowed());assert(s_native_normal_sleep_skips==1);
 assert(!unhygienix_native_sleep_tickless_allowed());assert(s_native_normal_sleep_skips==2);
 unhygienix_native_sleep_prepared=true;
 assert(unhygienix_native_sleep_tickless_allowed());assert(s_native_normal_sleep_skips==2);
 unhygienix_native_sleep_prepared=false;
 request(T5AIAlarm_NormalSleepSkips,0,0);assert(!reply_status && reply_generation==2);
 assert(s_native_system_mask_used && s_native_normal_sleep_skips==2);
 s_native_normal_sleep_skips=UINT32_MAX;
 assert(!unhygienix_native_normal_sleep_allowed());assert(s_native_normal_sleep_skips==UINT32_MAX);
#elif CASE == 94
 // The finish ACK closes cleanup before later ordinary mailbox traffic arrives.
 s_native_generation=7;s_native_cleanup_pending=false;
 fake_primask=fake_basepri=fake_faultmask=fake_cpu_mask=0;
 fake_cp_route=NATIVE_MAILBOX_CP_ROUTE;fake_mailbox_nvic_enabled=1;
 fake_cp_mailbox_control=NATIVE_MAILBOX_INTERRUPT_ENABLE|NATIVE_MAILBOX_FIFO_NOT_EMPTY;
 assert(!unhygienix_native_sleep_tickless_allowed());assert(fake_irq_kicks==1);
 assert(!unhygienix_native_sleep_tickless_allowed());assert(fake_irq_kicks==1);
 fake_nvic.ISPR[1]=0;fake_cp_route=0;
 assert(!unhygienix_native_sleep_tickless_allowed());assert(fake_irq_kicks==1);
 fake_cp_mailbox_control=NATIVE_MAILBOX_INTERRUPT_ENABLE;
 assert(unhygienix_native_sleep_tickless_allowed());assert(fake_irq_kicks==1);
 fake_cp_mailbox_control|=NATIVE_MAILBOX_FIFO_NOT_EMPTY;s_native_generation=0;
 assert(unhygienix_native_sleep_tickless_allowed());assert(fake_irq_kicks==1);
#elif CASE == 85
 s_native_cleanup_pending=true;unhygienix_native_sleep_prepared=true;
 fake_primask=fake_basepri=0;fake_cp_route=NATIVE_MAILBOX_CP_ROUTE;
 fake_cp_mailbox_control=NATIVE_MAILBOX_INTERRUPT_ENABLE|NATIVE_MAILBOX_FIFO_NOT_EMPTY;
 fake_mailbox_nvic_enabled=1;
 assert(!unhygienix_native_sleep_tickless_allowed());assert(fake_irq_kicks==1);
 assert(s_cleanup_irq_state==NATIVE_CLEANUP_DELIVERY_READY);
 assert(!unhygienix_native_sleep_tickless_allowed());assert(fake_irq_kicks==1);
 fake_nvic.ISPR[1]=0;
 for(unsigned reason=0;reason<8;++reason){
  fake_primask=reason==0;fake_basepri=reason==1;fake_cpu_mask=reason==2;fake_faultmask=reason==7;
  fake_cp_route=reason==3?0:NATIVE_MAILBOX_CP_ROUTE;fake_mailbox_nvic_enabled=reason!=4;
  fake_cp_mailbox_control=(reason==5?0:NATIVE_MAILBOX_INTERRUPT_ENABLE)|
                            (reason==6?0:NATIVE_MAILBOX_FIFO_NOT_EMPTY);
  assert(!unhygienix_native_sleep_tickless_allowed());assert(fake_irq_kicks==1);
 }
 request(T5AIAlarm_CleanupIRQKicks,0,0);assert(!reply_status && reply_generation==1);
 request(T5AIAlarm_CleanupIRQState,0,0);assert(!reply_status && reply_generation==s_cleanup_irq_state);
 s_native_cleanup_pending=false;assert(unhygienix_native_sleep_tickless_allowed());
#elif CASE == 84
 s_native_trace=(NativeSleepTrace){T5AI_SLEEP_TRACE_MAGIC,1,T5AISleepStage_CP_InterruptsReleased,~T5AISleepStage_CP_InterruptsReleased};
 request(T5AIAlarm_ArmSleep,1,100);assert(!reply_status && !s_native_trace.magic);
 request(T5AIAlarm_Stage,0,0);assert(!reply_status && reply_generation==T5AISleepStage_None);
 assert(unhygienix_native_sleep_tickless_allowed());
 cancel_and_finish(1,0);assert(!reply_status);
#elif CASE == 83
 request(T5AIAlarm_ArmSleep,1,100);coordination|=2u;
 assert(unhygienix_native_sleep_subcores_ready());
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_sleep_mask_exit();unhygienix_native_sleep_resumed();
 assert((coordination&8u) && s_native_resume_pending && !wakes);
 coordination&=~2u;cancel_and_finish(1,0);assert(!reply_status);
 fake_primask=fake_basepri=0;unhygienix_native_sleep_interrupts_restored();
 assert(!s_native_resume_pending && !wakes && !(coordination&8u));
#elif CASE == 81 || CASE == 82
 request(T5AIAlarm_ArmSleep,1,100);coordination|=2u;
 assert(unhygienix_native_sleep_subcores_ready());
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_sleep_mask_exit();unhygienix_native_sleep_resumed();
 assert((coordination&8u) && !app_allowed && s_native_resume_pending && !wakes);
 fake_primask=CASE==81;fake_basepri=CASE==82;
 unhygienix_native_sleep_interrupts_restored();
 assert((coordination&8u) && s_native_resume_pending && !wakes);
 fake_primask=fake_basepri=0;
 unhygienix_native_sleep_interrupts_restored();
 assert(!(coordination&8u) && !s_native_resume_pending && wakes==1);
 assert(s_native_cleanup_pending && !unhygienix_native_sleep_tickless_allowed());
 assert(s_cleanup_idle_refusals==1);
 request(T5AIAlarm_CleanupIdleRefusals,0,0);assert(!reply_status && reply_generation==1);
 assert(s_native_trace.stage==T5AISleepStage_CP_InterruptsReleased);
 unhygienix_native_sleep_interrupts_restored();assert(wakes==1);
 coordination&=~2u;cancel_and_finish(1,0);assert(!reply_status);
 assert(s_native_cleanup_pending && !unhygienix_native_sleep_prepared);
 assert(!unhygienix_native_sleep_tickless_allowed());
 request(T5AIAlarm_FinishCleanup,1,0);
 unhygienix_native_sleep_reply_ack(T5AI_PM_NATIVE_ALARM_COMMAND,1,T5AI_PM_NATIVE_ALARM_ABI,0);
 assert(!s_native_cleanup_pending && unhygienix_native_sleep_tickless_allowed());
#elif CASE == 80
 fake_reset_word=0x4f000023u;request(T5AIAlarm_ArmSleepPersistentTrace,1,100);
 coordination|=2;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Restored);
 const uint32_t marker=fake_reset_latched;unregister_fail=true;
 cancel_and_finish(1,0);
 assert(reply_status && s_native_faulted && s_persistent_saved);
 assert(fake_reset_latched==marker && unregisters==1);assert(!app_allowed);assert(!s_native_armed);
 request(T5AIAlarm_Arm,2,100);assert(reply_status && registers==1);
#elif CASE == 78 || CASE == 79
 request(T5AIAlarm_ArmSleep,1,100);coordination|=2;
 assert(unhygienix_native_sleep_subcores_ready());
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 const unsigned before_queued=queued_calls;send_fail=CASE==79;
 test_resume_complete();
 assert(direct_resume_count==1 && queued_calls==before_queued);
 assert(!app_allowed && !(coordination&8) && s_native_faulted==(CASE==79));
#elif CASE == 77
 request(T5AIAlarm_ArmSleepMaskBarrier,1,NATIVE_ANALOG_WAKE_MIN_TICKS+64);coordination|=2;
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_analog_wake_prepare();fake_pending_low=1;
 unhygienix_native_sleep_wait();assert(!mask_barriers && !s_mask_barrier_count && !arch_wfis);
 assert(!s_native_system_mask_used && unhygienix_native_normal_sleep_allowed());
 assert(s_clock_count==1 && s_final_reject==T5AI_SLEEP_FINAL_REJECT_IRQ);
 assert(!(fake_reset_latched & (T5AI_SLEEP_CP_TRACE_MASK_RETURNED << NATIVE_CP_REASON_SHIFT)));
 request(T5AIAlarm_MaskBarrierCount,0,0);assert(!reply_status && reply_generation==0);
 fake_pending_low=0;unhygienix_native_sleep_wait();
 assert(mask_barriers==1 && s_mask_barrier_count==1 && s_clock_count==2 && !arch_wfis);
 assert(s_native_system_mask_used && !unhygienix_native_normal_sleep_allowed());
 request(T5AIAlarm_MaskBarrierCount,0,0);assert(!reply_status && reply_generation==1);
 unhygienix_native_sleep_mask_exit();test_resume_complete();
 cancel_and_finish(1,0);assert(!reply_status && !s_analog_owned);
 assert(s_native_system_mask_used && !unhygienix_native_normal_sleep_allowed());
 assert(!unhygienix_native_sleep_tickless_allowed());
#elif CASE == 75 || CASE == 76
 fake_scb.SCR=SCB_SCR_SLEEPDEEP_Msk;
 request(T5AIAlarm_ArmSleepMaskBarrier,1,NATIVE_ANALOG_WAKE_MIN_TICKS+64);
 assert(!reply_status && s_native_mask_barrier && s_native_domain_bypass && s_native_shallow_wfi);
 assert(!s_native_no_hw_mask && !s_native_clocks_only);
 coordination|=2;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_analog_wake_prepare();
 if(CASE==76)fake_reset_word=(fake_reset_word&~NATIVE_CP_REASON_MASK)|(0x11u<<NATIVE_CP_REASON_SHIFT);
 unhygienix_native_sleep_wait();
 if(CASE==76){
  assert(!mask_barriers && !arch_wfis && !fake_cpu_mask && s_native_faulted);
  assert(s_final_reject&T5AI_SLEEP_FINAL_REJECT_TRACE);
  assert(((fake_reset_word>>NATIVE_CP_REASON_SHIFT)&0xffu)==0x11u);
  return 0;
 }
 assert(mask_barriers==1 && s_mask_barrier_count==1 && !arch_wfis && !unhygienix_native_lowvol_count);
 assert(fake_reset_latched & (T5AI_SLEEP_CP_TRACE_MASK_RETURNED << NATIVE_CP_REASON_SHIFT));
 assert(!fake_cpu_mask && fake_scb.SCR==SCB_SCR_SLEEPDEEP_Msk && !s_native_faulted);
 unhygienix_native_sleep_mask_exit();test_resume_complete();
 assert(!s_persistent_recording);
 assert(((fake_reset_latched >> NATIVE_CP_REASON_SHIFT) & 0xffu) ==
        (T5AI_SLEEP_CP_TRACE_REASON_BASE | T5AI_SLEEP_CP_TRACE_MASK_RETURNED |
         T5AISleepStage_CP_AppVetoRestored));
 const uint32_t completed_marker = fake_reset_latched;
 unhygienix_native_sleep_stage(T5AISleepStage_CP_ReadyRevoked);
 assert(fake_reset_latched==completed_marker);
 cancel_and_finish(1,0);assert(!reply_status && !s_analog_owned);
#elif CASE == 73 || CASE == 74
 fake_expect_shallow=true;fake_scb.SCR=SCB_SCR_SLEEPDEEP_Msk;
 request(T5AIAlarm_ArmSleepAnalogShallowWFI,1,NATIVE_ANALOG_WAKE_MIN_TICKS+64);
 assert(!reply_status && s_native_analog_wake && s_native_domain_bypass && s_native_shallow_wfi);
 assert(!s_native_keep_hf && !s_native_clocks_only && !s_native_no_hw_mask);
 coordination|=2;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_analog_wake_prepare();assert(s_analog_owned);
 fake_force_deep=CASE==74;
 unhygienix_native_sleep_wait();
 assert(fake_scb.SCR==SCB_SCR_SLEEPDEEP_Msk);
 if(CASE==73)assert(arch_wfis==1 && fake_cpu_mask && !s_native_faulted);
 else assert(!arch_wfis && !fake_cpu_mask && s_final_reject==T5AI_SLEEP_FINAL_REJECT_ARCH);
 fake_force_deep=false;
 unhygienix_native_sleep_mask_exit();test_resume_complete();
 cancel_and_finish(1,0);assert(!reply_status && !s_analog_owned && !fake_analog_enabled);
#elif CASE == 72
 request(T5AIAlarm_ArmSleepAnalogRetainDomains,1,NATIVE_ANALOG_WAKE_MIN_TICKS+64);
 assert(!reply_status && s_native_analog_wake && s_native_domain_bypass);
 assert(!s_native_keep_hf && !s_native_clocks_only && !s_native_no_hw_mask);
 assert(s_native_persistent_trace && s_native_vendor_lpo_halt);
 coordination|=2;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_analog_wake_prepare();assert(s_analog_owned);
 unhygienix_native_sleep_wait();assert(arch_wfis==1 && !s_native_faulted);
 unhygienix_native_sleep_mask_exit();test_resume_complete();
 cancel_and_finish(1,0);assert(!reply_status && !s_analog_owned && !fake_analog_enabled);
#elif CASE == 64
 request(T5AIAlarm_AnalogWakeState,0,0);assert(!reply_status && reply_generation==0x150u);
 request(T5AIAlarm_ArmSleepAnalogWake,1,NATIVE_ANALOG_WAKE_MIN_TICKS);assert(!reply_status);
 assert(s_native_analog_wake && s_native_persistent_trace && s_native_vendor_lpo_halt);
 coordination|=2;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_analog_wake_prepare();assert(s_analog_owned && fake_analog_enabled);
 assert(fake_analog_period==0 && fake_analog_clock==1 && !fake_analog_latch);
 unhygienix_native_sleep_wait();assert(arch_wfis==1 && !s_native_faulted);
 unhygienix_native_sleep_mask_exit();test_resume_complete();
 assert(!s_analog_owned && !fake_analog_enabled && fake_analog_period==5 && fake_analog_clock==1);
 cancel_and_finish(1,0);assert(!reply_status && !s_persistent_saved);
#elif CASE == 65
 fake_analog_enabled=1;request(T5AIAlarm_ArmSleepAnalogWake,1,NATIVE_ANALOG_WAKE_MIN_TICKS);
 assert(reply_status && !registers && !fake_analog_writes);
 fake_analog_enabled=0;fake_analog_latch=1;
 request(T5AIAlarm_ArmSleepAnalogWake,1,NATIVE_ANALOG_WAKE_MIN_TICKS);
 assert(reply_status && !registers && !fake_analog_writes);
 fake_analog_latch=0;request(T5AIAlarm_ArmSleepAnalogWake,1,NATIVE_ANALOG_WAKE_MIN_TICKS-1);
 assert(reply_status && !registers && !fake_analog_writes);
#elif CASE == 66
 request(T5AIAlarm_ArmSleepAnalogWake,1,NATIVE_ANALOG_WAKE_MIN_TICKS);coordination|=2;
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);unhygienix_native_analog_wake_prepare();
 fake_analog_period=2;const unsigned writes=fake_analog_writes;
 test_resume_complete();assert(s_native_faulted && s_analog_owned);
 assert(fake_analog_period==2 && fake_analog_writes==writes && !app_allowed && (coordination&8));
 cancel_and_finish(1,0);assert(reply_status && fake_analog_period==2);
#elif CASE == 67
 request(T5AIAlarm_ArmSleepAnalogWake,1,NATIVE_ANALOG_WAKE_MIN_TICKS);coordination|=2;
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);unhygienix_native_analog_wake_prepare();
 fake_aon=NATIVE_ANALOG_WAKE_ENTRY_BUDGET_TICKS;unhygienix_native_sleep_wait();
 assert(!arch_wfis && s_final_reject==T5AI_SLEEP_FINAL_REJECT_ANALOG_WAKE);
 test_resume_complete();cancel_and_finish(1,0);
 assert(!reply_status && !s_analog_owned && fake_analog_period==5);
#elif CASE == 68 || CASE == 70 || CASE == 71
 if(CASE==70)fake_aon=UINT32_MAX;
 request(T5AIAlarm_ArmSleepAnalogWake,1,NATIVE_ANALOG_WAKE_MIN_TICKS);coordination|=2;
 fake_aon+=(CASE==71 ? NATIVE_ANALOG_WAKE_MIN_TICKS+1 : NATIVE_ANALOG_WAKE_MIN_TICKS-NATIVE_ANALOG_WAKE_REQUIRED_LEAD_TICKS+1);
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_analog_wake_prepare();
 assert(!s_native_permit && !s_native_faulted && !s_analog_owned && !fake_analog_writes);
 assert(s_analog_prepare_reject==T5AI_SLEEP_ANALOG_PREPARE_REJECT_DEADLINE);
 unhygienix_native_sleep_wait();assert(!arch_wfis);
 assert(s_final_reject&T5AI_SLEEP_FINAL_REJECT_ANALOG_WAKE);
 test_resume_complete();cancel_and_finish(1,0);
 assert(!reply_status && !s_native_faulted && !s_analog_owned && !fake_analog_writes);
 assert(fake_analog_period==5 && fake_analog_clock==1 && !fake_analog_enabled);
 request(T5AIAlarm_AnalogPrepareReject,0,0);assert(!reply_status && reply_generation==T5AI_SLEEP_ANALOG_PREPARE_REJECT_DEADLINE);
#elif CASE == 69
 request(T5AIAlarm_ArmSleepAnalogWake,1,NATIVE_ANALOG_WAKE_MIN_TICKS);coordination|=2;
 fake_aon=NATIVE_ANALOG_WAKE_MIN_TICKS-NATIVE_ANALOG_WAKE_REQUIRED_LEAD_TICKS;
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 unhygienix_native_analog_wake_prepare();assert(s_analog_owned && !s_native_faulted);
 unhygienix_native_sleep_wait();assert(arch_wfis==1 && !s_native_faulted);
 unhygienix_native_sleep_mask_exit();test_resume_complete();
 cancel_and_finish(1,0);assert(!reply_status && !s_analog_owned && !fake_analog_enabled);
#elif CASE == 60
 fake_reset_word=0x4f000023u;
 request(T5AIAlarm_ArmSleepPersistentTrace,1,100);assert(!reply_status);
 assert(s_native_persistent_trace && s_native_vendor_lpo_halt && !s_native_no_hw_mask);
 coordination|=2;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 assert(s_persistent_saved && fake_reset_latched==fake_reset_word);
 unhygienix_native_sleep_wait();assert(arch_wfis==1 && !s_native_faulted);
 assert(((fake_reset_latched>>4)&0xffu)==(T5AI_SLEEP_CP_TRACE_REASON_BASE|T5AISleepStage_CP_FlashControllerRestored));
 assert((fake_reset_latched&~0xff0u)==0x4f000003u);
 unhygienix_native_sleep_mask_exit();
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Restored);
 assert(s_persistent_recording);
 unhygienix_native_sleep_stage(T5AISleepStage_CP_AppVetoRestored);
 assert(!s_persistent_recording && s_persistent_saved);
 fake_reset_word=(fake_reset_word&~0x7f000000u)|0x12000000u;
 cancel_and_finish(1,0);assert(!reply_status && !s_persistent_saved);
 assert(fake_reset_latched==0x12000023u);
#elif CASE == 61
 fake_reset_word=0x4f000023u;request(T5AIAlarm_ArmSleepPersistentTrace,1,100);
 coordination|=2;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 const uint32_t marker=fake_reset_latched;cancel_and_finish(1,0);
 assert(reply_status && s_native_faulted && s_persistent_saved && fake_reset_latched==marker);
#elif CASE == 62
 fake_reset_word=0x4f000023u;request(T5AIAlarm_ArmSleepPersistentTrace,1,100);
 coordination|=2;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 fake_reset_word=(fake_reset_word&~0xff0u)|(0x11u<<4);const unsigned keys=fake_reset_keys;
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Restored);
 assert(s_native_faulted && fake_reset_keys==keys);
 cancel_and_finish(1,0);assert(reply_status && fake_reset_keys==keys);
 assert(((fake_reset_word>>4)&0xffu)==0x11u);
#elif CASE == 63
 fake_reset_word=0x4f000023u;request(T5AIAlarm_ArmSleepPersistentTrace,1,100);
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 assert(s_native_faulted && !s_persistent_saved && !fake_reset_keys);
#elif CASE == 0
 reject=true;request(T5AIAlarm_Arm,1,100);assert(reply_status==(uint32_t)-27 && registers==1);
 assert(!s_native_armed && !s_native_faulted);
 reject=false;request(T5AIAlarm_Arm,2,100);assert(!reply_status && registers==2);
 request(T5AIAlarm_Arm,3,100);assert(reply_status && registers==2); // no replacement
 cancel_and_finish(1,0);assert(reply_status && !unregisters);
 cancel_and_finish(2,0);assert(!reply_status && unregisters==1);
 fire();assert(!s_native_fired && !wakes);
#elif CASE == 1
 request(T5AIAlarm_Arm,1,100);fire();assert(!wakes && pending.param1==1); // ISR only queues
 unhygienix_alarm_fired(&pending);assert(wakes==1);
 unhygienix_alarm_fired(&pending);assert(wakes==1); // duplicate
 request(T5AIAlarm_Arm,2,100);assert(!reply_status);
 unhygienix_alarm_fired(&pending);assert(wakes==1); // stale event
 cancel_and_finish(2,0);assert(!reply_status);
#elif CASE == 2
 request(T5AIAlarm_Arm,1,100);fire();
 cancel_and_finish(1,0);unhygienix_alarm_fired(&pending);assert(!wakes);
 request(T5AIAlarm_Arm,1,100);assert(reply_status); // generation never reused
 request(T5AIAlarm_Arm,2,100);queue_full=true;fire();assert(s_native_faulted);
 request(T5AIAlarm_Query,0,0);assert(reply_status);
#elif CASE == 3
 request(T5AIAlarm_Arm,UINT32_MAX,100);assert(!reply_status);
 cancel_and_finish(UINT32_MAX,0);assert(!reply_status);
 request(T5AIAlarm_Arm,1,100);assert(reply_status && registers==1);
#endif
#if CASE == 4
 request(T5AIAlarm_ArmSleep,1,100);assert(!reply_status && !app_allowed);
 assert(unhygienix_native_sleep_prepared && (coordination&8));
 const uint32_t ap_read=coordination;fire();
 assert(!unhygienix_native_sleep_subcores_ready() && (coordination&8));
 coordination=ap_read|2; // AP publication races with early alarm, preserving ready.
 assert(!unhygienix_native_sleep_subcores_ready() && !(coordination&8) && !app_allowed);
 const unsigned writes=ready_writes;coordination&=~2u;
 assert(!unhygienix_native_sleep_subcores_ready() && ready_writes==writes);
 cancel_and_finish(1,0);assert(!reply_status && !unhygienix_native_sleep_prepared);
#elif CASE == 5
 request(T5AIAlarm_ArmSleep,1,100);assert(!reply_status && !app_allowed);
 assert(!unhygienix_native_sleep_subcores_ready() && !app_allowed);
 coordination|=2;assert(unhygienix_native_sleep_subcores_ready() && app_allowed);
 ++unhygienix_native_lowvol_count;test_resume_complete();
 assert(!app_allowed && !(coordination&8) && last_resume_generation==1);
 assert(direct_resume_count==1 && wakes==1);
 const unsigned writes=ready_writes;coordination&=~2u;
 assert(!unhygienix_native_sleep_subcores_ready() && ready_writes==writes);
 cancel_and_finish(1,0);assert(!reply_status && !unhygienix_native_sleep_prepared);
 assert(coordination==0x80000000u);
#elif CASE == 6
 sibling_reset=false;request(T5AIAlarm_ArmSleep,1,100);assert(reply_status && !registers);
 sibling_reset=true;sibling_power_down=false;request(T5AIAlarm_ArmSleep,1,100);assert(reply_status && !registers);
 sibling_power_down=true;sibling_ack=false;request(T5AIAlarm_ArmSleep,1,100);assert(reply_status && !registers);
 sibling_ack=true;sibling_power_ack=false;request(T5AIAlarm_ArmSleep,1,100);assert(reply_status && !registers);
 assert(!unhygienix_native_sleep_prepared && !app_allowed && !ready_writes);
#elif CASE == 8
 memory_retained=false;request(T5AIAlarm_ArmSleep,1,100);assert(reply_status && !registers);
 memory_retained=true;request(T5AIAlarm_ArmSleep,1,100);assert(!reply_status);
 memory_retained=false;coordination|=2;
 assert(!unhygienix_native_sleep_subcores_ready() && !app_allowed && !(coordination&8));
#elif CASE == 11 || CASE == 12
 request(T5AIAlarm_ArmSleep,1,100);assert(!reply_status);
 coordination|=2;assert(unhygienix_native_sleep_subcores_ready() && app_allowed);
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);assert(s_resources_active);
 fire();assert(!unhygienix_native_sleep_subcores_ready() && !app_allowed);
 assert(coordination&8); // Flash/resource restore has not finished; AP must stay in SRAM.
#if CASE == 12
 cancel_and_finish(1,0);assert(reply_status && s_native_faulted && !unregisters);
 assert((coordination&8) && unhygienix_native_sleep_prepared);
#endif
 test_resume_complete();assert(!s_resources_active && !(coordination&8));
 assert(!app_allowed && s_native_trace.stage==T5AISleepStage_CP_InterruptsReleased);
#elif CASE == 10
 request(T5AIAlarm_ArmSleep,1,100);assert(!reply_status);
 lpo=0x1230u|T5AI_SLEEP_LPO_HALT_MASK;
 assert(unhygienix_native_sleep_halt_config(lpo)==0x1230u && s_lpo_saved);
 lpo=0x4560u;assert(unhygienix_native_sleep_halt_config(lpo)==0x4560u);
 cancel_and_finish(1,0);assert(!reply_status && !s_lpo_saved);
 assert(lpo==(0x4560u|T5AI_SLEEP_LPO_HALT_MASK));
 assert(unhygienix_native_sleep_halt_config(lpo)==lpo);
#elif CASE == 9
 s_trace_captured=false;s_native_trace=(NativeSleepTrace){.magic=T5AI_SLEEP_TRACE_MAGIC,.generation=47,
   .stage=T5AISleepStage_CP_WFI,.inverse=~(uint32_t)T5AISleepStage_CP_WFI};
 request(T5AIAlarm_PreviousStage,0,0);assert(!reply_status && reply_generation==T5AISleepStage_CP_WFI);
 request(T5AIAlarm_PreviousGeneration,0,0);assert(!reply_status && reply_generation==47);
 request(T5AIAlarm_ArmSleep,1,100);assert(!reply_status);
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Woke);
 request(T5AIAlarm_Stage,0,0);assert(reply_generation==T5AISleepStage_CP_Woke);
 request(T5AIAlarm_PreviousStage,0,0);assert(reply_generation==T5AISleepStage_CP_WFI);
#elif CASE == 13
 request(T5AIAlarm_ArmSleep,1,100);coordination|=2;fake_aon=10;
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);fake_aon=25;
 assert(unhygienix_native_sleep_hal_ready(0,0,0,0));
 request(T5AIAlarm_PrepareTicks,0,0);assert(reply_generation==15);
 assert(!unhygienix_native_sleep_hal_ready(1ull<<40,2,4,1));
 request(T5AIAlarm_HalReject,0,0);assert(reply_generation==15);
 request(T5AIAlarm_HalPendingHigh,0,0);assert(reply_generation==256);
 request(T5AIAlarm_HalStatusLow,0,0);assert(reply_generation==2);
 request(T5AIAlarm_HalStatusHigh,0,0);assert(reply_generation==4);
 unhygienix_native_sleep_stage(T5AISleepStage_CP_WFI);fake_aon=65;
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Woke);
 request(T5AIAlarm_LowVoltageTicks,0,0);assert(reply_generation==40);
#elif CASE == 14
 assert(!unhygienix_native_gpio_enter(NULL) && !unhygienix_native_gpio_exit(NULL) && !unhygienix_native_gpio_wake());
 assert(gpio_enters==1 && gpio_exits==1 && gpio_wakes==1);
 request(T5AIAlarm_ArmSleep,1,100);
 assert(!unhygienix_native_gpio_enter(NULL) && !unhygienix_native_gpio_exit(NULL) && !unhygienix_native_gpio_wake());
 assert(gpio_enters==1 && gpio_exits==1 && gpio_wakes==1);
 cancel_and_finish(1,0);assert(!reply_status);
 assert(!unhygienix_native_gpio_enter(NULL) && gpio_enters==2);
#elif CASE == 26
 assert(!unhygienix_native_sleep_bsp_restore());
 request(T5AIAlarm_ArmSleepBspRestore,1,100);coordination|=2;
 assert(!reply_status && unhygienix_native_sleep_bsp_restore() && unhygienix_native_sleep_core_only() && unhygienix_native_sleep_keep_hf() && !unhygienix_native_sleep_flash_restore());
 unhygienix_native_sleep_wait();assert(!arch_wfis && s_clock_count==1);
 test_resume_complete();coordination&=~2u;cancel_and_finish(1,0);
 assert(!unhygienix_native_sleep_bsp_restore());
 request(T5AIAlarm_ArmSleep,2,100);assert(!unhygienix_native_sleep_bsp_restore());
#elif CASE == 25
 const uint32_t original=0x8000803fu;
 assert(unhygienix_native_sleep_power_domains(original)==original);
 peripherals_retained=false;request(T5AIAlarm_ArmSleep,1,100);assert(reply_status && !registers);
 peripherals_retained=true;request(T5AIAlarm_ArmSleep,1,100);assert(!reply_status);
 assert(unhygienix_native_sleep_power_domains(original)==(original & ~T5AI_SLEEP_AP_RETAINED_DOMAINS));
 peripherals_retained=false;coordination|=2;assert(!unhygienix_native_sleep_subcores_ready() && !app_allowed);
 peripherals_retained=true;coordination&=~2u;cancel_and_finish(1,0);
 assert(unhygienix_native_sleep_power_domains(original)==original);
#elif CASE == 24
 assert(!unhygienix_native_sleep_flash_restore());
 request(T5AIAlarm_ArmSleepFlashRestore,1,100);coordination|=2;
 assert(!reply_status && unhygienix_native_sleep_flash_restore() && unhygienix_native_sleep_core_only() && unhygienix_native_sleep_keep_hf());
 unhygienix_native_sleep_wait();assert(!arch_wfis && s_clock_count==1);
 test_resume_complete();coordination&=~2u;cancel_and_finish(1,0);
 assert(!unhygienix_native_sleep_flash_restore());
 request(T5AIAlarm_ArmSleep,2,100);assert(!unhygienix_native_sleep_flash_restore());
#elif CASE == 22
 fake_cpu_mask=1;unhygienix_native_sleep_mask_enter();fake_cpu_mask=0;
 unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask);
 request(T5AIAlarm_ArmSleepClocksOnly,1,100);
 unhygienix_native_sleep_mask_enter();fake_cpu_mask=1;
 unhygienix_native_sleep_mask_enter();unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask);
 request(T5AIAlarm_MaskBefore,0,0);assert(!reply_generation);
 request(T5AIAlarm_MaskAfter,0,0);assert(!reply_generation);
 fake_cpu_mask=1;unhygienix_native_sleep_mask_enter();fake_cpu_mask=0;
 unhygienix_native_sleep_mask_exit();assert(fake_cpu_mask==1);
 cancel_and_finish(1,0);assert(!reply_status);
#elif CASE == 23
 request(T5AIAlarm_ArmSleep,1,100);coordination|=2;
 fake_aon=40;unhygienix_native_sleep_wait();assert(!arch_wfis && s_final_reject==T5AI_SLEEP_FINAL_REJECT_LEAD);
 fake_aon=20;fake_pending_high=1;unhygienix_native_sleep_wait();assert(!arch_wfis && s_final_reject==T5AI_SLEEP_FINAL_REJECT_IRQ);
 fake_pending_high=0;fake_scb.ICSR=1u<<26;unhygienix_native_sleep_wait();assert(!arch_wfis && s_final_reject==T5AI_SLEEP_FINAL_REJECT_TICK);
 fake_scb.ICSR=0;unhygienix_native_sleep_wait();assert(arch_wfis==1 && !s_final_reject && s_final_skips==3);
 unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask && !s_mask_saved);
 test_resume_complete();coordination&=~2u;cancel_and_finish(1,0);
 fake_aon=UINT32_MAX-40;request(T5AIAlarm_ArmSleep,2,100);coordination|=2;
 fake_aon=10;unhygienix_native_sleep_wait();assert(arch_wfis==1 && s_final_reject==T5AI_SLEEP_FINAL_REJECT_LEAD);
 fake_aon=UINT32_MAX-20;unhygienix_native_sleep_wait();assert(arch_wfis==2 && !s_final_reject);
 unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask && !s_mask_saved);
#elif CASE == 21
 assert(!unhygienix_native_sleep_core_only());
 request(T5AIAlarm_ArmSleepCoreOnly,1,100);coordination|=2;
 assert(!reply_status && unhygienix_native_sleep_core_only() && unhygienix_native_sleep_keep_hf());
 unhygienix_native_sleep_wait();assert(!arch_wfis && s_clock_count==1);
 test_resume_complete();coordination&=~2u;cancel_and_finish(1,0);
 assert(!unhygienix_native_sleep_core_only() && !unhygienix_native_sleep_keep_hf());
 request(T5AIAlarm_ArmSleep,2,100);assert(!unhygienix_native_sleep_core_only());
#elif CASE == 20
 assert(!unhygienix_native_sleep_keep_hf());
 request(T5AIAlarm_ArmSleepClocksLive,1,100);coordination|=2;
 assert(!reply_status && unhygienix_native_sleep_keep_hf());
 unhygienix_native_sleep_wait();assert(!arch_wfis && s_clock_count==1);
 test_resume_complete();coordination&=~2u;cancel_and_finish(1,0);
 assert(!unhygienix_native_sleep_keep_hf());
 request(T5AIAlarm_ArmSleep,2,100);assert(!unhygienix_native_sleep_keep_hf());
#elif CASE == 18 || CASE == 19
 request(T5AIAlarm_ArmSleepClocksOnly,1,100);coordination|=2;
 assert(!reply_status && unhygienix_native_sleep_subcores_ready() && app_allowed);
 fake_aon=CASE==18?25:150;fake_pending_high=1u<<22;fake_nvic.ISPR[1]=1u<<22;
 unhygienix_native_sleep_wait();assert(!arch_wfis && !unhygienix_native_lowvol_count);
 request(T5AIAlarm_ClockCount,0,0);assert(reply_generation==1);
 request(T5AIAlarm_FinalLeadTicks,0,0);assert(reply_generation==(CASE==18?75u:(uint32_t)-50));
 request(T5AIAlarm_ClockModeLow,0,0);assert(!reply_generation);
 request(T5AIAlarm_HighFrequencyLow,0,0);assert(reply_generation==2);
 request(T5AIAlarm_FinalPendingHigh,0,0);assert(reply_generation==(1u<<22));
 request(T5AIAlarm_FinalNvicHigh,0,0);assert(reply_generation==(1u<<22));
 test_resume_complete();coordination&=~2u;cancel_and_finish(1,0);
 fake_pending_high=0;fake_nvic.ISPR[1]=0;
 request(T5AIAlarm_ArmSleep,2,100);coordination|=2;unhygienix_native_sleep_wait();
 assert(arch_wfis==1 && unhygienix_native_lowvol_count==1 && s_clock_count==2);
 unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask && !s_mask_saved);
 test_resume_complete();coordination&=~2u;cancel_and_finish(2,0);
 unhygienix_native_sleep_wait();assert(arch_wfis==2 && s_clock_count==2);
#elif CASE == 16
 request(T5AIAlarm_ArmSleepResourcesOnly,1,100);assert(!reply_status && !app_allowed);
 coordination|=2;assert(unhygienix_native_sleep_subcores_ready() && app_allowed);
 fake_aon=10;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);fake_aon=25;
 assert(!unhygienix_native_sleep_hal_ready(0,0,0,0));
 request(T5AIAlarm_HalReject,0,0);assert(reply_generation==T5AI_SLEEP_HAL_REJECT_PROBE);
 request(T5AIAlarm_ResourceCount,0,0);assert(reply_generation==1 && !unhygienix_native_lowvol_count);
 test_resume_complete();assert(!(coordination&8) && !app_allowed);
 assert(direct_resume_count==1 && wakes==1);
 coordination&=~2u;cancel_and_finish(1,0);assert(!reply_status);
 request(T5AIAlarm_ArmSleep,2,100);coordination|=2;
 assert(unhygienix_native_sleep_hal_ready(0,0,0,0));
#elif CASE == 27
 request(T5AIAlarm_ArmSleepClocksLive,1,100);assert(!reply_status);
 fake_uart[2]=1;fake_uart[4]=1;fake_uart[6]=(1u<<17)|(1u<<20);fake_uart[9]=1u<<5;
 assert(!unhygienix_native_sleep_uart_trace_claim(1)); // AP still owns mailbox logger.
 coordination|=2;assert(!unhygienix_native_sleep_uart_trace_claim(0));
 fake_uart_clock=0;assert(!unhygienix_native_sleep_uart_trace_claim(1));fake_uart_clock=1;
 fake_primask=0;assert(!unhygienix_native_sleep_uart_trace_claim(1));fake_primask=1;
 fake_uart[6]&=~(1u<<17);assert(!unhygienix_native_sleep_uart_trace_claim(1));
 fake_uart[6]|=1u<<17;assert(unhygienix_native_sleep_uart_trace_claim(1));
 prv_native_trace_uart(T5AISleepStage_CP_HAL);assert(fake_uart[7]=='\n');
 fake_uart[7]=0;fake_uart[6]&=~(1u<<20);
 prv_native_trace_uart(T5AISleepStage_CP_HAL);assert(!fake_uart[7]); // Never wait for room.
 fake_uart[6]|=1u<<20;fake_uart[2]=0;
 prv_native_trace_uart(T5AISleepStage_CP_HAL);assert(!fake_uart[7]); // Never ungate.
 fake_uart[2]=1;s_native_keep_hf=false;
 prv_native_trace_uart(T5AISleepStage_CP_HAL);assert(!fake_uart[7]);
 s_native_keep_hf=true;fake_trace_count=0;
 prv_native_trace_word(T5AISleepSnapshot_APControl,0x12345678u);
 assert(fake_trace_count==8 && fake_trace[0]==T5AI_SLEEP_SNAPSHOT_MARKER && fake_trace[1]==0);
 assert(fake_trace[2]==0x78 && fake_trace[3]==0x56 && fake_trace[4]==0x34 && fake_trace[5]==0x12);
 assert(fake_trace[6]==(T5AI_SLEEP_SNAPSHOT_MARKER^0x78^0x56^0x34^0x12) && fake_trace[7]=='\n');
 fake_primask=0;prv_native_trace_word(T5AISleepSnapshot_APControl,0);assert(fake_trace_count==8);
 fake_primask=1;prv_native_trace_bytes(fake_trace,9);assert(fake_trace_count==8);
 assert(fake_uart[7]=='\n');
 s_resources_active=true;coordination&=~2u;fake_trace_count=0;
 prv_native_trace_word(T5AISleepSnapshot_APControl,0);assert(!fake_trace_count);
 s_resources_active=false;
 prv_native_trace_word(T5AISleepSnapshot_APControl,0);assert(fake_trace_count==8);
 unhygienix_native_sleep_stage(T5AISleepStage_CP_ResumeQueued);
 fake_uart[7]=0;prv_native_trace_uart(T5AISleepStage_CP_Restored);assert(!fake_uart[7]);
 assert(fake_uart[4]==1 && fake_uart[9]==(1u<<5));
#elif CASE == 59
 lpo=0;
 request(T5AIAlarm_ArmSleepVendorLPOHalt,1,200);assert(!reply_status);coordination|=2;
 assert(s_native_vendor_lpo_halt && !s_native_keep_hf && !s_native_clocks_only && !s_native_no_hw_mask);
 assert((unhygienix_native_sleep_condition(0)&NATIVE_SLEEP_CONDITION_MASK)==
        (NATIVE_SLEEP_NEEDS_FLASH_IDLE|NATIVE_SLEEP_NEEDS_AP_WFI|NATIVE_SLEEP_NEEDS_CP_WFI|NATIVE_SLEEP_GLOBAL_ENABLE));
 const uint32_t other_lpo_bits=1u<<11;
 lpo=unhygienix_native_sleep_halt_config(T5AI_SLEEP_LPO_HALT_MASK|other_lpo_bits);
 assert(lpo==(T5AI_SLEEP_LPO_HALT_MASK|other_lpo_bits) && s_lpo_saved && !s_lpo_halt);
 unhygienix_native_sleep_wait();assert(arch_wfis==1 && fake_cpu_mask && s_lowvol_ticks==7);
 unhygienix_native_sleep_mask_exit();test_resume_complete();coordination&=~2u;
 cancel_and_finish(1,0);assert(!reply_status && lpo==other_lpo_bits && !s_lpo_saved);
 request(T5AIAlarm_ArmSleep,2,200);assert(!reply_status && !s_native_vendor_lpo_halt);
 assert(!(unhygienix_native_sleep_halt_config(T5AI_SLEEP_LPO_HALT_MASK)&T5AI_SLEEP_LPO_HALT_MASK));
#elif CASE == 56 || CASE == 57
 request(T5AIAlarm_ArmSleepMaskedCPWFIKeepHF,1,200);assert(!reply_status);coordination|=2;
 fake_scb.SCR=0x10;fake_deep_rejected=CASE==56;
 unhygienix_native_sleep_wait();assert(fake_scb.SCR==0x10);
 if(CASE==56){assert(!arch_wfis && !fake_cpu_mask && s_final_reject==T5AI_SLEEP_FINAL_REJECT_ARCH);}
 else{assert(arch_wfis==1 && fake_cpu_mask);unhygienix_native_sleep_mask_exit();}
#elif CASE == 58
 fake_boot_reset=0x1234;fake_wake_status=1u<<21;fake_power_parameter=0x5678;lpo=0x99;
 unhygienix_native_sleep_boot_capture();fake_wake_status=0;fake_boot_reset=0;lpo=0;
 unhygienix_native_sleep_boot_capture();
 request(T5AIAlarm_BootPMUSnapshotValid,0,0);assert(!reply_status && reply_generation==1);
 request(T5AIAlarm_BootPMUWake,0,0);assert(reply_generation==(1u<<21));
 request(T5AIAlarm_BootPMUReset,0,0);assert(reply_generation==0x1234);
 request(T5AIAlarm_BootPMUPower,0,0);assert(reply_generation==0x5678);
 request(T5AIAlarm_BootPMUWakeConfig,0,0);assert(reply_generation==0x99);
#elif CASE == 54 || CASE == 55
 request(T5AIAlarm_ArmSleepMaskedCPWFIKeepHF,1,200);assert(!reply_status);coordination|=2;
 if(CASE==54){fake_gpio_status_low=1u<<9;fake_gpio_status_high=1u<<23;}
 else{fake_uart[2]=1;fake_uart[4]=1;fake_uart[6]=(1u<<17)|(1u<<20);
      assert(unhygienix_native_sleep_uart_trace_claim(1));fake_trace_sets_gpio=true;}
 unhygienix_native_sleep_wait();
 assert(!arch_wfis && !fake_cpu_mask && !s_mask_saved && !unhygienix_native_lowvol_count);
 assert(s_final_reject==T5AI_SLEEP_FINAL_REJECT_AP_IRQ && fake_gpio_status_high==(1u<<23));
 assert(!fake_ap_pending_low && !fake_ap_pending_high); // Route masking cannot hide raw work.
#elif CASE == 50 || CASE == 51 || CASE == 52 || CASE == 53
 request(T5AIAlarm_ArmSleepMaskedCPWFIKeepHF,1,200);assert(!reply_status);coordination|=2;
 fake_uart[2]=1;fake_uart[4]=1;fake_uart[6]=(1u<<17)|(1u<<20);
 assert(unhygienix_native_sleep_uart_trace_claim(1));
 fake_trace_stalls=CASE==51;fake_trace_sets_tick=CASE==52;fake_trace_expires=CASE==53;
 unhygienix_native_sleep_wait();
 assert(fake_trace_count>=44 && fake_trace[1]==T5AISleepSnapshot_SleepConfig);
 if(CASE==50){assert(arch_wfis==1 && fake_cpu_mask);unhygienix_native_sleep_mask_exit();}
 else{assert(!arch_wfis && !fake_cpu_mask && !s_mask_saved && !unhygienix_native_lowvol_count);
      assert(s_final_reject==(CASE==51?T5AI_SLEEP_FINAL_REJECT_UART:CASE==52?T5AI_SLEEP_FINAL_REJECT_TICK:T5AI_SLEEP_FINAL_REJECT_LEAD));}
#elif CASE == 49
 request(T5AIAlarm_ArmSleepMaskedCPWFIKeepHF,1,200);assert(!reply_status);coordination|=2;
 fake_mask_observer=observe_mask_admission;
 unhygienix_native_sleep_wait();assert(arch_wfis==1 && fake_cpu_mask);
 fake_mask_observer=NULL;unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask);
#elif CASE == 48
 request(T5AIAlarm_ArmSleepMaskedCPWFIKeepHF,1,200);assert(!reply_status);coordination|=2;
 assert(s_native_keep_hf && !s_native_clocks_only && !s_native_no_hw_mask);
 assert((unhygienix_native_sleep_condition(0)&NATIVE_SLEEP_CONDITION_MASK)==
        (NATIVE_SLEEP_GLOBAL_ENABLE|NATIVE_SLEEP_NEEDS_CP_WFI));
 fake_pending_high=1;unhygienix_native_sleep_wait();assert(!arch_wfis && !fake_cpu_mask);
 fake_pending_high=0;unhygienix_native_sleep_wait();assert(arch_wfis==1 && fake_cpu_mask);
 unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask);
#elif CASE == 47
 request(T5AIAlarm_ArmSleepVendorSubcoresOnKeepHF,1,200);assert(!reply_status);coordination|=2;
 assert(s_native_keep_hf && !s_native_clocks_only && s_native_no_hw_mask);
 assert((unhygienix_native_sleep_condition(0)&NATIVE_SLEEP_CONDITION_MASK)==NATIVE_SLEEP_GLOBAL_ENABLE);
 unhygienix_native_sleep_wait();assert(arch_wfis==1 && !fake_cpu_mask);
 unhygienix_native_sleep_mask_exit();test_resume_complete();coordination&=~2u;
 cancel_and_finish(1,0);request(T5AIAlarm_ArmSleepVendorMaskedKeepHF,2,200);coordination|=2;
 assert(s_native_keep_hf && !s_native_clocks_only && !s_native_no_hw_mask);
 assert((unhygienix_native_sleep_condition(0)&NATIVE_SLEEP_CONDITION_MASK)==NATIVE_SLEEP_GLOBAL_ENABLE);
 unhygienix_native_sleep_wait();assert(arch_wfis==2 && fake_cpu_mask==NATIVE_CP_IRQ_MASKED);
 unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask);
#elif CASE == 46
 request(T5AIAlarm_ArmSleepVendorMasked,1,200);assert(!reply_status);coordination|=2;
 fake_flash_ready=false;unhygienix_native_sleep_wait();
 assert(!arch_wfis && !fake_cpu_mask && s_final_reject==T5AI_SLEEP_FINAL_REJECT_FLASH);
 assert(s_final_skips==1 && !unhygienix_native_lowvol_count);
#elif CASE == 45
 request(T5AIAlarm_ArmSleepVendorSubcoresOn,1,200);assert(!reply_status);coordination|=2;
 fake_clear_status=NATIVE_PMU_WAKE_CLEAR_ASSERTED;unhygienix_native_sleep_wait();
 assert(s_pmu_clear_held_count==1 && s_wake_clear_before==fake_clear_status && s_wake_clear_after==fake_clear_status);
 assert(!s_mask_after_wfi && arch_wfis==1);
 request(T5AIAlarm_PMUClearHeldCount,0,0);assert(reply_generation==1);
 unhygienix_native_sleep_mask_exit();test_resume_complete();coordination&=~2u;
 cancel_and_finish(1,0);request(T5AIAlarm_ArmSleepVendorMasked,2,200);coordination|=2;
 fake_clear_status=0;unhygienix_native_sleep_wait();
 assert(arch_wfis==2 && s_mask_after_wfi==NATIVE_CP_IRQ_MASKED && s_pmu_clear_held_count==1);
 request(T5AIAlarm_HardwareMaskAfterWFI,0,0);assert(reply_generation==NATIVE_CP_IRQ_MASKED);
 unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask);
#elif CASE == 44
 request(T5AIAlarm_ArmSleepVendorSubcoresOn,1,200);assert(!reply_status);coordination|=2;
 fake_update_wake=true;fake_next_wake=1u<<21;unhygienix_native_sleep_wait();
 assert(s_pmu_wake_count==1 && s_pmu_wake_ticks==7 && s_pmu_wake_sources==2 && !s_pmu_ambiguous_count);
 fake_wake_status=0;fake_next_wake=0;unhygienix_native_sleep_wait();
 assert(s_pmu_wake_count==1 && s_pmu_wake_ticks==7 && arch_wfis==2);
 fake_wake_status=1u<<21;fake_next_wake=1u<<21;unhygienix_native_sleep_wait();
 assert(s_pmu_wake_count==1 && s_pmu_ambiguous_count==1 && arch_wfis==3);
 request(T5AIAlarm_PMUWakeCount,0,0);assert(reply_generation==1);
 request(T5AIAlarm_PMUWakeTicks,0,0);assert(reply_generation==7);
 unhygienix_native_sleep_mask_exit();test_resume_complete();coordination&=~2u;
 cancel_and_finish(1,0);request(T5AIAlarm_ArmSleepVendorMasked,2,200);coordination|=2;
 assert(s_native_vendor_masked && !s_native_no_hw_mask);
 fake_pending_high=1;unhygienix_native_sleep_wait();assert(arch_wfis==3 && !fake_cpu_mask);
 fake_pending_high=0;fake_wake_status=0;unhygienix_native_sleep_wait();
 assert(arch_wfis==4 && s_mask_saved && fake_cpu_mask==NATIVE_CP_IRQ_MASKED);
 unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask && !s_mask_saved);
#elif CASE == 43
 request(T5AIAlarm_ArmSleepVendorSubcoresOn,1,200);assert(!reply_status);
 assert(!unhygienix_native_sleep_subcores_ready() && !app_allowed);
 coordination|=2;assert(unhygienix_native_sleep_subcores_ready() && app_allowed);
 assert(s_native_no_hw_mask && !s_native_keep_hf && !s_native_clocks_only);
 uint32_t condition=unhygienix_native_sleep_condition(UINT32_MAX);
 assert((condition & NATIVE_SLEEP_CONDITION_MASK)==NATIVE_SLEEP_GLOBAL_ENABLE);
 fake_pending_high=1;unhygienix_native_sleep_wait();assert(!arch_wfis);
 fake_pending_high=0;unhygienix_native_sleep_wait();
 assert(arch_wfis==1 && !s_mask_saved && !fake_cpu_mask && fake_primask==1);
 unhygienix_native_sleep_mask_exit();test_resume_complete();coordination&=~2u;
 cancel_and_finish(1,0);request(T5AIAlarm_ArmSleep,2,200);
 assert(unhygienix_native_sleep_condition(0) & NATIVE_SLEEP_NEEDS_AP_WFI);
#elif CASE == 42
 request(T5AIAlarm_ArmSleepVendorCPUIdle,1,200);assert(!reply_status);coordination|=2;
 assert(s_native_no_hw_mask && !s_native_keep_hf && !s_native_clocks_only);
 uint32_t condition=unhygienix_native_sleep_condition(UINT32_MAX);
 assert(!(condition & (NATIVE_SLEEP_NEEDS_FLASH_IDLE|NATIVE_SLEEP_NEEDS_CP_WFI|NATIVE_SLEEP_BUS_IDLE_BYPASS|NATIVE_SLEEP_NEEDS_CPU2_WFI)));
 assert((condition & (NATIVE_SLEEP_NEEDS_AP_WFI|NATIVE_SLEEP_GLOBAL_ENABLE)) ==
        (NATIVE_SLEEP_NEEDS_AP_WFI|NATIVE_SLEEP_GLOBAL_ENABLE));
 fake_flash_operation_control=NATIVE_FLASH_BUSY;unhygienix_native_sleep_wait();
 assert(!arch_wfis && s_final_reject==T5AI_SLEEP_FINAL_REJECT_FLASH);
 fake_flash_operation_control=0;unhygienix_native_sleep_wait();
 assert(arch_wfis==1 && !s_mask_saved && !fake_cpu_mask && fake_primask==1);
 unhygienix_native_sleep_mask_exit();test_resume_complete();coordination&=~2u;
 cancel_and_finish(1,0);request(T5AIAlarm_ArmSleep,2,200);
 assert(unhygienix_native_sleep_condition(0) & NATIVE_SLEEP_NEEDS_CP_WFI);
#elif CASE == 41
 request(T5AIAlarm_ArmSleepVendorFlashIdle,1,200);assert(!reply_status);coordination|=2;
 assert(s_native_no_hw_mask && !s_native_keep_hf && !s_native_clocks_only);
 uint32_t condition=unhygienix_native_sleep_condition(UINT32_MAX);
 assert(!(condition & (NATIVE_SLEEP_NEEDS_FLASH_IDLE|NATIVE_SLEEP_BUS_IDLE_BYPASS|NATIVE_SLEEP_NEEDS_CPU2_WFI)));
 assert((condition & (NATIVE_SLEEP_NEEDS_AP_WFI|NATIVE_SLEEP_NEEDS_CP_WFI|NATIVE_SLEEP_GLOBAL_ENABLE)) ==
        (NATIVE_SLEEP_NEEDS_AP_WFI|NATIVE_SLEEP_NEEDS_CP_WFI|NATIVE_SLEEP_GLOBAL_ENABLE));
 fake_flash_operation_control=NATIVE_FLASH_BUSY;unhygienix_native_sleep_wait();
 assert(!arch_wfis && s_final_reject==T5AI_SLEEP_FINAL_REJECT_FLASH && !fake_cpu_mask);
 fake_flash_operation_control=0;unhygienix_native_sleep_wait();assert(arch_wfis==1);
 unhygienix_native_sleep_mask_exit();test_resume_complete();coordination&=~2u;
 cancel_and_finish(1,0);request(T5AIAlarm_ArmSleep,2,200);
 assert(unhygienix_native_sleep_condition(0) & NATIVE_SLEEP_NEEDS_FLASH_IDLE);
#elif CASE == 40
 request(T5AIAlarm_ArmSleepNoHardwareMaskClocksOff,1,200);assert(!reply_status);coordination|=2;
 assert(s_native_no_hw_mask && !s_native_keep_hf && !s_native_clocks_only);
 fake_wake_status=0x456;unhygienix_native_sleep_wait();
 assert(arch_wfis==1 && !s_mask_saved && !fake_cpu_mask && fake_primask==1);
 request(T5AIAlarm_PowerParameter,0,0);assert(!reply_status && reply_generation==0x1234);
 request(T5AIAlarm_WakeStatusBefore,0,0);assert(!reply_status && reply_generation==0x456);
 request(T5AIAlarm_WakeStatusAfter,0,0);assert(!reply_status && reply_generation==0x456);
 unhygienix_native_sleep_mask_exit();test_resume_complete();coordination&=~2u;
 cancel_and_finish(1,0);assert(!reply_status);
#elif CASE == 39
 request(T5AIAlarm_ArmSleepNoHardwareMask,1,200);assert(!reply_status);coordination|=2;
 assert(s_native_no_hw_mask && s_native_keep_hf && !s_native_clocks_only);
 fake_pending_high=1;unhygienix_native_sleep_wait();
 assert(!arch_wfis && s_final_reject==T5AI_SLEEP_FINAL_REJECT_IRQ);
 fake_pending_high=0;fake_cpu_mask=1;unhygienix_native_sleep_wait();
 assert(!arch_wfis && s_final_reject==T5AI_SLEEP_FINAL_REJECT_MASK && fake_cpu_mask==1);
 fake_cpu_mask=0;unhygienix_native_sleep_wait();
 assert(arch_wfis==1 && !s_mask_saved && !fake_cpu_mask && fake_primask==1);
 assert(unhygienix_native_lowvol_count==1 && s_lowvol_ticks==7);
 unhygienix_native_sleep_mask_exit();test_resume_complete();coordination&=~2u;
 cancel_and_finish(1,0);assert(!reply_status);
 request(T5AIAlarm_ArmSleepKeepHF,2,200);assert(!reply_status);coordination|=2;
 assert(!s_native_no_hw_mask);unhygienix_native_sleep_wait();
 assert(arch_wfis==2 && s_mask_saved && fake_cpu_mask==NATIVE_CP_IRQ_MASKED);
 unhygienix_native_sleep_mask_exit();assert(!fake_cpu_mask);
#elif CASE == 38
 request(T5AIAlarm_ArmSleepKeepHF,1,200);assert(!reply_status);coordination|=2;
 fake_uart[2]=1;fake_uart[4]=1;fake_uart[6]=(1u<<17)|(1u<<20);
 assert(unhygienix_native_sleep_uart_trace_claim(1));fake_uart[6]=1u<<20;
 unhygienix_native_sleep_wait();
 assert(!arch_wfis && !s_mask_saved && !fake_cpu_mask);
 assert(s_final_reject==T5AI_SLEEP_FINAL_REJECT_UART && s_final_skips==1);
 fake_uart[6]|=1u<<17;unhygienix_native_sleep_wait();
 assert(arch_wfis==1 && s_mask_saved);unhygienix_native_sleep_mask_exit();
#elif CASE == 37
 request(T5AIAlarm_ArmSleepKeepHF,1,200);assert(!reply_status);coordination|=2;
 assert(unhygienix_native_sleep_keep_hf() && !s_native_clocks_only);
 fake_uart[2]=1;fake_uart[4]=1;fake_uart[6]=(1u<<17)|(1u<<20);
 assert(unhygienix_native_sleep_uart_trace_claim(1));
 unhygienix_native_sleep_wait();
 assert(arch_wfis==1 && unhygienix_native_lowvol_count==1 && s_lowvol_ticks==7);
 assert(fake_trace_count==8 && fake_trace[1]==T5AISleepStage_CP_Woke);
 assert(fake_trace[5]==T5AISleepStage_CP_FlashControllerRestored);
 unhygienix_native_sleep_mask_exit();test_resume_complete();
 coordination&=~2u;cancel_and_finish(1,0);assert(!reply_status);
#elif CASE == 36
 request(T5AIAlarm_ArmSleep,1,1000);assert(!reply_status);
 fake_admission_release=8;assert(unhygienix_native_sleep_subcores_ready());
 assert(fake_admission_tick>=8 && fake_admission_tick<12 && app_allowed);
 test_resume_complete();coordination&=~2u;cancel_and_finish(1,0);
 fake_aon=UINT32_MAX-5;fake_admission_tick=fake_aon;fake_admission_release=2;
 request(T5AIAlarm_ArmSleep,2,1000);assert(!reply_status);
 assert(unhygienix_native_sleep_subcores_ready() && fake_admission_tick<5);
 test_resume_complete();coordination&=~2u;cancel_and_finish(2,0);
 fake_aon=0;fake_admission_tick=0;fake_admission_release=UINT32_MAX;
 request(T5AIAlarm_ArmSleep,3,1000);fake_pending_low=1;
 assert(!unhygienix_native_sleep_subcores_ready() && fake_admission_tick<3 && !app_allowed);
 fake_pending_low=0;fake_admission_tick=0;
 assert(!unhygienix_native_sleep_subcores_ready());
 assert(fake_admission_tick>=NATIVE_AP_PUBLICATION_WAIT_TICKS && fake_admission_tick<100);
 cancel_and_finish(3,0);fake_admission_tick=0;
 request(T5AIAlarm_ArmSleep,4,70);
 assert(!unhygienix_native_sleep_subcores_ready() && fake_admission_tick<10);
 coordination|=2;fake_aon=0;fake_gpio_status_low=1u<<9;fake_gpio_status_high=1u<<4;
 unhygienix_native_sleep_wait();
 request(T5AIAlarm_FinalGPIOStatusLow,4,0);assert(reply_generation==(1u<<9));
 request(T5AIAlarm_FinalGPIOStatusHigh,4,0);assert(reply_generation==(1u<<4));
#elif CASE == 36
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepVendorLPOHalt);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 35
 assert(unhygienix_native_sleep_cp_gpio_owned());
 request(T5AIAlarm_ArmSleep,1,200);assert(!reply_status);coordination|=2;
 assert(!unhygienix_native_sleep_cp_gpio_owned());
 fake_ap_pending_high=1u<<23;unhygienix_native_sleep_wait();
 assert(!arch_wfis && !s_mask_saved && !fake_cpu_mask);
 assert(s_final_reject==T5AI_SLEEP_FINAL_REJECT_AP_IRQ);
 request(T5AIAlarm_FinalAPPendingHigh,1,0);assert(reply_generation==fake_ap_pending_high);
 fake_ap_pending_high=0;fake_ap_pending_low=1;unhygienix_native_sleep_wait();
 assert(!arch_wfis && s_final_reject==T5AI_SLEEP_FINAL_REJECT_AP_IRQ);
 request(T5AIAlarm_FinalAPPendingLow,1,0);assert(reply_generation==1);
 fake_ap_pending_low=0;unhygienix_native_sleep_wait();assert(arch_wfis==1);
 unhygienix_native_sleep_mask_exit();test_resume_complete();
 coordination&=~2u;cancel_and_finish(1,0);
 assert(!reply_status && unhygienix_native_sleep_cp_gpio_owned());
#elif CASE == 35
 for(unsigned op=T5AIAlarm_BootPMUWake;op<=T5AIAlarm_BootPMUSnapshotValid;++op){
   assert(!t5ai_native_alarm_cp_query(op,&gen));assert(last_op==op);}
#elif CASE == 34
 request(T5AIAlarm_ArmSleep,1,100);assert(!reply_status);coordination|=2;
 fake_aon=40;unhygienix_native_sleep_wait();
 assert(!arch_wfis && !s_mask_saved && !fake_cpu_mask);
 fake_aon=0;fake_pending_high=1;unhygienix_native_sleep_wait();
 assert(!arch_wfis && !s_mask_saved && !fake_cpu_mask);
 fake_pending_high=0;fake_cpu_mask=1;unhygienix_native_sleep_wait();
 assert(!arch_wfis && !s_mask_saved && fake_cpu_mask==1);
 assert(s_final_reject==T5AI_SLEEP_FINAL_REJECT_MASK);
 fake_cpu_mask=0;unhygienix_native_sleep_wait();
 assert(arch_wfis==1 && s_mask_saved && fake_cpu_mask==1);
 unhygienix_native_sleep_mask_exit();assert(!s_mask_saved && !fake_cpu_mask);
 test_resume_complete();coordination&=~2u;cancel_and_finish(1,0);
 request(T5AIAlarm_ArmSleepRetainModules,2,100);coordination|=2;
 unhygienix_native_sleep_wait();assert(arch_wfis==1 && !s_mask_saved && !fake_cpu_mask);
#elif CASE == 33
 assert(unhygienix_native_sleep_mask_required());
 request(T5AIAlarm_ArmSleepClocksOnly,1,100);assert(!reply_status);
 assert(!unhygienix_native_sleep_mask_required());
 test_resume_complete();cancel_and_finish(1,0);
 assert(unhygienix_native_sleep_mask_required());
 request(T5AIAlarm_ArmSleepRetainModules,2,100);assert(!reply_status);
 assert(!unhygienix_native_sleep_mask_required());
 test_resume_complete();cancel_and_finish(2,0);
 request(T5AIAlarm_ArmSleep,3,100);assert(!reply_status);
 assert(!unhygienix_native_sleep_mask_required());
#elif CASE == 32
 assert(!unhygienix_native_sleep_retain_modules());
 request(T5AIAlarm_ArmSleepRetainModules,1,100);assert(!reply_status);
 assert(unhygienix_native_sleep_retain_modules() && unhygienix_native_sleep_domain_bypass());
 assert(unhygienix_native_sleep_keep_hf());
 coordination|=2;unhygienix_native_sleep_wait();assert(!arch_wfis && s_clock_count==1);
 test_resume_complete();coordination&=~2u;cancel_and_finish(1,0);
 assert(!unhygienix_native_sleep_retain_modules());
 request(T5AIAlarm_ArmSleepDomainBypass,2,100);
 assert(!unhygienix_native_sleep_retain_modules() && unhygienix_native_sleep_domain_bypass());
 test_resume_complete();cancel_and_finish(2,0);
 request(T5AIAlarm_ArmSleep,3,100);assert(!unhygienix_native_sleep_retain_modules());
#elif CASE == 31
 assert(!unhygienix_native_sleep_domain_bypass());
 request(T5AIAlarm_ArmSleepDomainBypass,1,100);assert(!reply_status);
 assert(unhygienix_native_sleep_domain_bypass() && unhygienix_native_sleep_keep_hf());
 coordination|=2;unhygienix_native_sleep_wait();assert(!arch_wfis && s_clock_count==1);
 test_resume_complete();coordination&=~2u;cancel_and_finish(1,0);
 assert(!reply_status && !unhygienix_native_sleep_domain_bypass());
 request(T5AIAlarm_ArmSleep,2,100);assert(!reply_status && !unhygienix_native_sleep_domain_bypass());
#elif CASE == 97
 assert(!unhygienix_native_sleep_mask_only());
 request(T5AIAlarm_ArmSleepMaskOnly,1,200);assert(!reply_status);
 assert(unhygienix_native_sleep_mask_only() && s_native_mask_barrier);
 assert(s_native_keep_hf && !s_native_clocks_only && !s_native_analog_wake);
 assert(!s_native_persistent_trace && !s_native_vendor_lpo_halt);
 coordination|=2u;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 fake_pending_low=1;unhygienix_native_sleep_wait();
 assert(!mask_barriers && !arch_wfis && !s_native_system_mask_used);
 fake_pending_low=0;unhygienix_native_sleep_wait();
 assert(mask_barriers==1 && s_mask_barrier_count==1 && !arch_wfis);
 assert(!unhygienix_native_lowvol_count && !fake_cpu_mask && !s_native_faulted);
 unhygienix_native_sleep_mask_exit();test_resume_complete();coordination&=~2u;
 cancel_and_finish(1,0);assert(!reply_status && !unhygienix_native_sleep_mask_only());
 request(T5AIAlarm_ArmSleep,2,200);assert(!reply_status && !s_native_mask_only);
#elif CASE == 96
 request(T5AIAlarm_ArmSleepClocksOnly,1,200);assert(!reply_status);coordination|=2u;
 const uint32_t power_before=0x3a214356u, irq_low_before=0x1234u;
 const uint32_t irq_high_before=0x81000000u, clocks_before=0x11223344u;
 fake_power_parameter=power_before;fake_irq_enable_low=irq_low_before;
 fake_cp_route=irq_high_before;fake_peripheral_clocks=clocks_before;
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 fake_power_parameter=0x69111324u;fake_irq_enable_low=0x5678u;
 fake_cp_route=0x82000000u;fake_peripheral_clocks=0x55667788u;
 unhygienix_native_sleep_mask_exit();unhygienix_native_sleep_resumed();
 const uint32_t expected[]={power_before,power_before,irq_low_before,0x5678u,
   irq_high_before,0x82000000u,clocks_before,0x55667788u};
 for(unsigned i=0;i<sizeof(expected)/sizeof(expected[0]);++i){
  request((T5AIAlarmOperation)(T5AIAlarm_PMUPolicyBefore+i),0,0);
  assert(!reply_status && reply_generation==expected[i]);
 }
 assert(fake_power_parameter==power_before && fake_irq_enable_low==0x5678u);
 assert(fake_cp_route==0x82000000u && fake_peripheral_clocks==0x55667788u);
#elif CASE == 30
 const uint32_t original=0xa03182c0u;
 const uint32_t original_power_parameter=0x3a214356u;
 fake_power_parameter=original_power_parameter;
 assert(unhygienix_native_sleep_condition(original)==original);
 fake_sleep_config=original;
 request(T5AIAlarm_ArmSleepClocksOnly,1,100);assert(!reply_status);
 coordination|=2;unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 const uint32_t required=NATIVE_SLEEP_NEEDS_FLASH_IDLE|NATIVE_SLEEP_NEEDS_AP_WFI|
     NATIVE_SLEEP_NEEDS_CP_WFI|NATIVE_SLEEP_GLOBAL_ENABLE;
 const uint32_t configured=unhygienix_native_sleep_condition(original);
 assert((configured&NATIVE_SLEEP_CONDITION_MASK)==required);
 assert((configured&~NATIVE_SLEEP_CONDITION_MASK)==(original&~NATIVE_SLEEP_CONDITION_MASK));
 const uint32_t unrelated_change=1u<<30;
 fake_sleep_config=configured^unrelated_change;
 fake_power_parameter=0x69111324u;
 unhygienix_native_sleep_mask_exit();
 assert(fake_sleep_config==(original^unrelated_change) && !s_sleep_condition_saved);
 assert(fake_power_parameter==original_power_parameter);
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 fake_power_parameter=0x69111324u;
 fake_sleep_config=unhygienix_native_sleep_condition(fake_sleep_config);
 test_resume_complete();
 assert(fake_sleep_config==(original^unrelated_change) && !s_sleep_condition_saved);
 assert(fake_power_parameter==original_power_parameter);
 // A failed restore readback must poison the generation rather than claim success.
 unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
 fake_power_parameter ^= 1u;fake_reject_power_restore=true;
 unhygienix_native_sleep_mask_exit();assert(s_native_faulted);
#elif CASE == 29
 request(T5AIAlarm_ArmSleep,1,100);coordination|=2u;mailbox_ready=false;
 fire();assert(!unhygienix_native_sleep_subcores_ready());
 assert(!(coordination&8u) && !app_allowed && !s_native_permit);
#elif CASE == 28
 request(T5AIAlarm_ArmSleep,1,100);coordination|=2u;mailbox_ready=false;
 assert(!unhygienix_native_sleep_subcores_ready() && !app_allowed && (coordination&8u));
 assert(!s_native_faulted && s_native_permit);
 mailbox_ready=true;assert(unhygienix_native_sleep_subcores_ready() && app_allowed);
#elif CASE == 17
 s_native_trace=(NativeSleepTrace){.magic=1,.stage=123,.inverse=~123u};
 request(T5AIAlarm_Stage,0,0);assert(reply_generation==T5AISleepStage_None);
 s_native_trace.magic=T5AI_SLEEP_TRACE_MAGIC;s_native_trace.inverse=0;
 request(T5AIAlarm_Stage,0,0);assert(reply_generation==T5AISleepStage_None);
#elif CASE == 15
 request(T5AIAlarm_ArmSleepWakeOnly,1,100);assert(!reply_status && !app_allowed);
 coordination|=2;assert(!unhygienix_native_sleep_subcores_ready());
 assert((coordination&8) && !app_allowed && !unhygienix_native_lowvol_count);
 fire();assert(!unhygienix_native_sleep_subcores_ready() && !(coordination&8));
 unhygienix_alarm_fired(&pending);assert(wakes==1 && !app_allowed);
 coordination&=~2u;cancel_and_finish(1,0);assert(!reply_status);
 request(T5AIAlarm_ArmSleep,2,100);coordination|=2;
 assert(unhygienix_native_sleep_subcores_ready() && app_allowed);
#elif CASE == 7
 request(T5AIAlarm_ArmSleep,1,100);coordination|=2;
 assert(unhygienix_native_sleep_subcores_ready() && app_allowed);
 fire();assert(app_allowed); // ISR only revokes the permit; no PM API in ISR.
 assert(!unhygienix_native_sleep_subcores_ready() && !(coordination&8) && !app_allowed);
 assert(unhygienix_native_lowvol_count==0); // final recheck must abort clock shutdown
#endif
 assert(!locked);return 0;
}
"""
AP_PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define S_SUCCESS 0
#define E_ERROR -1
#define E_INVALID_OPERATION -2
#define E_INVALID_ARGUMENT -3
#define PBL_FOREVER 0
typedef int status_t;
#define PBL_MUTEX_DEFINE(name) unsigned name
static unsigned locks;
static void pbl_irq_lock(void){assert(!locks);++locks;}
static void pbl_irq_unlock(void){assert(locks==1);--locks;}
static void pbl_mutex_lock(unsigned *m,unsigned timeout){assert(!*m && !timeout);*m=1;}
static void pbl_mutex_unlock(unsigned *m){assert(*m);*m=0;}
void t5ai_native_alarm_notify(uint32_t,uint32_t,uint32_t);
static bool timeout,early_fire,bad_abi,bad_gen;
static unsigned requests, last_op;
static int t5ai_cp_mailbox_alarm_request(unsigned op,uint32_t gen,uint32_t ticks,uint32_t *applied,uint32_t *abi){
 ++requests;last_op=op;assert(!locks);
 *abi=bad_abi?0:T5AI_PM_NATIVE_ALARM_ABI;*applied=bad_gen?gen+1:gen;
 if(op==T5AIAlarm_Query)*applied=17; // old CP state after warm AP reboot
 if(op==T5AIAlarm_Arm){assert(ticks==100);if(early_fire)t5ai_native_alarm_notify(gen,456,*abi);}
 return timeout?-1:0;
}
"""
AP_MAIN = r"""
int main(void){
 assert(!t5ai_native_alarm_healthy());
 assert(t5ai_native_alarm_init()==0 && requests==3); // query, cancel and finish the old owner
 assert(t5ai_native_alarm_init()==0 && requests==3);
 uint32_t gen=0,tick=0;
 assert(t5ai_native_alarm_arm(0,&gen)==E_INVALID_ARGUMENT);
#if CASE == 37 || CASE == 38
 assert(t5ai_native_alarm_finish_cleanup(0)==E_INVALID_ARGUMENT);
 assert(t5ai_native_alarm_finish_cleanup(16)==E_INVALID_ARGUMENT);
 assert(!t5ai_native_alarm_finish_cleanup(17));
 assert(!t5ai_native_alarm_arm(100,&gen));
 assert(t5ai_native_alarm_finish_cleanup(gen)==E_INVALID_ARGUMENT);
 assert(!t5ai_native_alarm_cancel(gen));
 if(CASE==37)bad_gen=true;else bad_abi=true;
 assert(t5ai_native_alarm_finish_cleanup(gen)==E_ERROR);
 assert(s_faulted && !s_active && !locks && !s_alarm_mutex);
#elif CASE == 0
 early_fire=true;assert(!t5ai_native_alarm_arm(100,&gen) && gen==18);
 assert(t5ai_native_alarm_fired(gen,&tick) && tick==456);
 assert(t5ai_native_alarm_arm(100,&gen)==E_INVALID_OPERATION);
 assert(t5ai_native_alarm_cancel(gen-1)==E_INVALID_ARGUMENT);
 assert(!t5ai_native_alarm_cancel(gen));
 early_fire=false;assert(!t5ai_native_alarm_arm(100,&gen) && gen==19);
 t5ai_native_alarm_notify(18,123,T5AI_PM_NATIVE_ALARM_ABI);
 assert(!t5ai_native_alarm_fired(gen,&tick));
 assert(!t5ai_native_alarm_cancel(gen));
 t5ai_native_alarm_notify(gen,789,T5AI_PM_NATIVE_ALARM_ABI);
 assert(!t5ai_native_alarm_fired(gen,&tick));
#elif CASE == 34
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepMaskedCPWFIKeepHF);
 assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 32 || CASE == 33
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==(CASE==32 ? T5AIAlarm_ArmSleepVendorSubcoresOnKeepHF : T5AIAlarm_ArmSleepVendorMaskedKeepHF));
 assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 31
 for(unsigned op=T5AIAlarm_WakeClearBefore;op<=T5AIAlarm_HardwareMaskAfterWFI;++op){
  uint32_t value;assert(!t5ai_native_alarm_cp_query(op,&value));
 }
#elif CASE == 30
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepVendorMasked);assert(!t5ai_native_alarm_cancel(gen));
 for(unsigned op=T5AIAlarm_PMUWakeCount;op<=T5AIAlarm_PMUAmbiguousCount;++op){
  uint32_t value;assert(!t5ai_native_alarm_cp_query(op,&value));
 }
#elif CASE == 29
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepVendorSubcoresOn);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 28
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepVendorCPUIdle);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 27
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepVendorFlashIdle);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 26
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepNoHardwareMaskClocksOff);assert(!t5ai_native_alarm_cancel(gen));
 for(unsigned op=T5AIAlarm_PowerParameter;op<=T5AIAlarm_WakeStatusAfter;++op){
  uint32_t value;assert(!t5ai_native_alarm_cp_query(op,&value));
 }
#elif CASE == 25
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepNoHardwareMask);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 24
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepKeepHF);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 23
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen));
 assert(last_op==T5AIAlarm_ArmSleepRetainModules);
 assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 21
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen));
 assert(!t5ai_native_alarm_event_received(gen));
 t5ai_native_alarm_resume_notify(gen-1,123,T5AI_PM_NATIVE_ALARM_ABI);
 assert(!t5ai_native_alarm_event_received(gen));
 t5ai_native_alarm_resume_notify(gen,123,T5AI_PM_NATIVE_ALARM_ABI);
 assert(t5ai_native_alarm_event_received(gen) && !t5ai_native_alarm_event_received(gen-1));
 assert(!t5ai_native_alarm_cancel(gen) && !t5ai_native_alarm_event_received(gen));
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen));
 t5ai_native_alarm_resume_notify(gen,456,0);
 assert(!t5ai_native_alarm_event_received(gen));
#elif CASE == 22
 assert(!t5ai_native_alarm_arm(100,&gen));
 assert(!t5ai_native_alarm_event_received(gen));
 t5ai_native_alarm_notify(gen,456,T5AI_PM_NATIVE_ALARM_ABI);
 assert(t5ai_native_alarm_event_received(gen));
 assert(!t5ai_native_alarm_cancel(gen) && !t5ai_native_alarm_event_received(gen));
#elif CASE == 1
 timeout=true;assert(t5ai_native_alarm_arm(100,&gen)<0);
 assert(!t5ai_native_alarm_healthy());assert(t5ai_native_alarm_arm(100,&gen)==E_INVALID_OPERATION);
#elif CASE == 2
 bad_abi=true;assert(t5ai_native_alarm_arm(100,&gen)==E_ERROR);
 assert(!t5ai_native_alarm_healthy());
#elif CASE == 3
 bad_gen=true;assert(t5ai_native_alarm_arm(100,&gen)==E_ERROR);
 assert(!t5ai_native_alarm_healthy());
#elif CASE == 4 || CASE == 5
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==(CASE==4 ? T5AIAlarm_ArmSleep : T5AIAlarm_ArmSleepWakeOnly));
 assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 6
 bad_gen=true;assert(t5ai_native_alarm_prepare_sleep(100,&gen)==E_ERROR);
 assert(!t5ai_native_alarm_healthy());
#elif CASE == 7
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepResourcesOnly);
 assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 8
 bad_gen=true;assert(t5ai_native_alarm_prepare_sleep(100,&gen)==E_ERROR);
 assert(!t5ai_native_alarm_healthy());
#elif CASE == 9
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepClocksOnly);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 11
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepClocksLive);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 13
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepCoreOnly);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 15
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepFlashRestore);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 17
 assert(!t5ai_native_alarm_prepare_sleep(100,&gen) && gen==18);
 assert(last_op==T5AIAlarm_ArmSleepBspRestore);assert(!t5ai_native_alarm_cancel(gen));
#elif CASE == 19 || CASE == 20
 bad_gen=CASE==20;
 if(bad_gen){assert(t5ai_native_alarm_prepare_sleep(100,&gen)==E_ERROR);assert(!t5ai_native_alarm_healthy());}
 else {assert(!t5ai_native_alarm_prepare_sleep(100,&gen));assert(last_op==T5AIAlarm_ArmSleepDomainBypass);assert(!t5ai_native_alarm_cancel(gen));}
#elif CASE == 18
 bad_gen=true;assert(t5ai_native_alarm_prepare_sleep(100,&gen)==E_ERROR);
 assert(!t5ai_native_alarm_healthy());
#elif CASE == 16
 bad_gen=true;assert(t5ai_native_alarm_prepare_sleep(100,&gen)==E_ERROR);
 assert(!t5ai_native_alarm_healthy());
#elif CASE == 14
 bad_gen=true;assert(t5ai_native_alarm_prepare_sleep(100,&gen)==E_ERROR);
 assert(!t5ai_native_alarm_healthy());
#elif CASE == 12
 bad_gen=true;assert(t5ai_native_alarm_prepare_sleep(100,&gen)==E_ERROR);
 assert(!t5ai_native_alarm_healthy());
#elif CASE == 10
 bad_gen=true;assert(t5ai_native_alarm_prepare_sleep(100,&gen)==E_ERROR);
 assert(!t5ai_native_alarm_healthy());
#endif
 assert(!locks);return 0;
}
"""


def cp_hardware_endpoints(source):
    source = source.replace(
        "return NATIVE_AON_COUNT_LOW;", "return fake_admission_clock();"
    )
    source = re.sub(
        r"#define NATIVE_FLASH_OPERATION_CONTROL[ \t]+\\\n[^\n]*",
        "#define NATIVE_FLASH_OPERATION_CONTROL fake_flash_operation_control",
        source,
    )
    source = source.replace("return SCB->SCR;", "return fake_sleep_control_read();")
    endpoints = {
        "NATIVE_AON_COUNT_LOW": "fake_aon",
        "NATIVE_RESET_REASON_WORD": "fake_reset_word",
        "NATIVE_RTC_CONTROL": "fake_rtc_control",
        "NATIVE_FLASH_OPERATION_CONTROL": "fake_flash_operation_control",
        "NATIVE_GPIO_STATUS_LOW": "fake_gpio_status_low",
        "NATIVE_GPIO_STATUS_HIGH": "fake_gpio_status_high",
        "NATIVE_MAILBOX_CP_CONTROL": "fake_cp_mailbox_control",
        "NATIVE_MAILBOX_AP_CONTROL": "fake_mailbox_control",
        "NATIVE_MAILBOX_AP_FIFO_STATUS": "fake_mailbox_fifo",
        "NATIVE_MAILBOX_AP_FIFO_CONFIG": "fake_mailbox_config",
        "NATIVE_MAILBOX_GLOBAL_CONTROL": "fake_mailbox_global_control",
        "NATIVE_MAILBOX_INTERRUPT_STATUS": "fake_mailbox_interrupt_status",
    }
    for name, endpoint in endpoints.items():
        source = re.sub(
            r"#define " + name + r" .*", "#define " + name + " " + endpoint, source
        )
    source = re.sub(
        r"#define NATIVE_TRACE_UART_REGISTER\(offset\) \\\n[^\n]*",
        "#define NATIVE_TRACE_UART_REGISTER(offset) fake_uart[(offset)/4]",
        source,
    )
    source = source.replace(
        "NATIVE_RESET_REASON_COMMIT = NATIVE_REASON_COMMIT_KEY_1;",
        "fake_persistent_key(NATIVE_REASON_COMMIT_KEY_1);",
    )
    source = source.replace(
        "NATIVE_RESET_REASON_COMMIT = NATIVE_REASON_COMMIT_KEY_2;",
        "fake_persistent_key(NATIVE_REASON_COMMIT_KEY_2);",
    )
    return source.replace(
        "NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_FIFO_OFFSET) = bytes[i];",
        "fake_trace_out(bytes[i]);",
    )


class NativeAlarmTest(unittest.TestCase):
    def test_mask_only_hal_balances_irq_and_tick_without_clock_calls(self):
        overlay = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        hal = next(item for item in overlay if item["path"].endswith("/sys_pm_hal.c"))
        replacement = next(
            item["new"]
            for item in hal["replacements"]
            if "if (unhygienix_native_sleep_core_only()) {" in item["new"]
        )
        branch = replacement.split("if (unhygienix_native_sleep_core_only())", 1)[0]
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
static bool selected;
static unsigned order, waits, restored;
static uint32_t portNVIC_SYSTICK_CTRL_REG, systick_ctrl_value=7;
static uint32_t int_state1=0x123, int_state2=0x456;
static bool unhygienix_native_sleep_mask_only(void){return selected;}
static void unhygienix_native_sleep_wait(void){assert(order++==0);++waits;}
static void sys_hal_restore_int(uint32_t low,uint32_t high){
 assert(order++==1 && low==int_state1 && high==int_state2);++restored;
}
static void unhygienix_native_sleep_mask_exit(void){assert(order++==2);}
"""
        main = r"""
int main(void){
 selected=false;control();assert(!order && !waits && !restored && !portNVIC_SYSTICK_CTRL_REG);
 selected=true;control();assert(order==3 && waits==1 && restored==1);
 assert(portNVIC_SYSTICK_CTRL_REG==systick_ctrl_value);
 return 0;
}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            main,
            transform=lambda _: "static void control(void){" + branch + "}\n",
        )

    def test_cleanup_irq_observer_preserves_vendor_isr_ownership(self):
        overlay = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        recipe = next(
            item
            for item in overlay
            if item["path"] == "t5_os/cp/middleware/driver/mailbox/mbox0_drv.c"
        )
        source = (
            ROOT / "third_party/hal_tuya/TuyaOpen-T5AI" / recipe["path"]
        ).read_text()
        for replacement in recipe["replacements"]:
            self.assertEqual(source.count(replacement["old"]), replacement["count"])
            source = source.replace(replacement["old"], replacement["new"])
        helper = body(source, "mbox0_drv_isr_handler")
        prefix = (
            PROTOCOL
            + r"""
#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#define SELF_CHNL 0
#define RX_FIFO_STAT_NOT_EMPTY 1u
#define CONFIG_SOC_SMP 0
typedef struct {uint32_t data[2];} mbox0_message_t;
static unsigned status, queued, drained, callbacks, observed, observed_status, progress;
void unhygienix_native_mailbox_receive_observe(uint32_t stage,uint32_t header,uint32_t payload){
 assert(stage==T5AIReceive_DriverReturned && !header && !payload && callbacks);
}
void unhygienix_native_sleep_mailbox_progress(uint32_t value){assert(value<=T5AIMailboxProgress_ACKFailed);progress=value;}
static unsigned get_status(void *p){assert(p);return status;}
static unsigned get_fifo(void *p){assert(p);return queued?RX_FIFO_STAT_NOT_EMPTY:0;}
static struct {unsigned (*chn_get_int_status)(void *);unsigned (*chn_get_rx_fifo_stat)(void *);} channel={get_status,get_fifo};
static struct {unsigned hal;typeof(channel) *chn_drv[1];void (*rx_callback)(mbox0_message_t *);} mbox0_dev;
static void callback(mbox0_message_t *p){assert(p->data[0]>=1 && p->data[0]<=2 && p->data[1]==1);++callbacks;}
static int mbox0_drv_recieve_message(mbox0_message_t *p){assert(queued);p->data[0]=queued--;p->data[1]=1;++drained;return 0;}
void unhygienix_native_sleep_mailbox_irq_observed(uint32_t value){++observed;observed_status=value;}
"""
        )
        main = r"""
int main(void){
 mbox0_dev.chn_drv[0]=&channel;mbox0_dev.rx_callback=callback;
 // A re-pended IRQ must service real FIFO data even with a cleared status latch.
 queued=2;status=0;mbox0_drv_isr_handler();
 assert(observed==1 && !observed_status && !queued && drained==2 && callbacks==2);
 mbox0_drv_isr_handler();assert(observed==2 && drained==2 && callbacks==2);
 status=1;queued=1;mbox0_dev.rx_callback=NULL;mbox0_drv_isr_handler();
 assert(observed==3 && observed_status==1 && !queued && drained==3 && callbacks==2);
 queued=2;mbox0_dev.rx_callback=callback;mbox0_drv_isr_handler();
 assert(drained==5 && callbacks==4 && !queued && observed==4);
 queued=0;mbox0_drv_isr_handler();
 assert(observed==5 && drained==5 && callbacks==4 && progress==T5AIMailboxProgress_ISRReturn);
 return 0;
}
"""
        run(
            recipe["path"].replace(
                "t5_os/", "third_party/hal_tuya/TuyaOpen-T5AI/t5_os/"
            ),
            prefix,
            main,
            transform=lambda _: helper,
        )

    def test_ap_release_follows_cp_outer_critical_exit(self):
        overlay = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        recipe = next(
            item for item in overlay if item["path"] == "t5_os/cp/components/bk_pm/pm.c"
        )
        source = (
            ROOT / "third_party/hal_tuya/TuyaOpen-T5AI" / recipe["path"]
        ).read_text()
        for replacement in recipe["replacements"]:
            self.assertEqual(source.count(replacement["old"]), replacement["count"])
            source = source.replace(replacement["old"], replacement["new"])
        helper = body(source, "pm_low_voltage_process")
        prefix = (
            PROTOCOL
            + r"""
#include <assert.h>
#define CONFIG_AON_RTC 0
#define CONFIG_PM_LV_TIME_COST_DEBUG 0
#define PM_DEBUG_SYS_REG_BASE 0u
#define PM_DEBUG_PMU_REG_BASE 0u
#define REG_READ(address) (address)
#define BK_LOGD(...) ((void)0)
static unsigned nesting, restored, released, resource_sets, resource_restores;
static bool mask_only;
static bool unhygienix_native_sleep_mask_only(void){return mask_only;}
static void unhygienix_native_flash_prepare_mask_only(void){assert(mask_only && nesting==1);}
static unsigned s_debug_en;
static unsigned s_before_low_vol_pd,s_before_low_vol_lpo,s_before_low_vol_psram;
static unsigned s_after_low_vol_pd,s_after_low_vol_lpo,s_after_low_vol_psram;
static unsigned rtos_enter_critical(void){assert(!nesting);return nesting++;}
static void rtos_exit_critical(unsigned level){assert(!level && nesting==1 && restored);--nesting;}
static void pm_low_voltage_resource_set(void){assert(nesting==1);++resource_sets;}
static void pm_enter_low_voltage(void){assert(nesting==1);if(mask_only)restored=1;}
static void pm_low_voltage_resource_restore(void){assert(nesting==1);restored=1;++resource_restores;}
static void unhygienix_native_sleep_stage(unsigned stage){(void)stage;assert(nesting==1);}
void unhygienix_native_sleep_resumed(void){assert(!nesting && restored);++released;}
"""
        )
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            "int main(void){assert(pm_low_voltage_process()==0);assert(released==1 && resource_sets==1 && resource_restores==1);mask_only=true;restored=0;assert(pm_low_voltage_process()==0);assert(released==2 && resource_sets==1 && resource_restores==1);return 0;}",
            transform=lambda source: helper,
        )

    def test_tickless_wrapper_releases_ap_after_tick_and_interrupt_restore(self):
        overlay = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        recipe = next(
            item for item in overlay if item["path"].endswith("/port_tick_impl.h")
        )
        source = (
            ROOT / "third_party/hal_tuya/TuyaOpen-T5AI" / recipe["path"]
        ).read_text()
        for replacement in recipe["replacements"]:
            self.assertEqual(source.count(replacement["old"]), replacement["count"])
            source = source.replace(replacement["old"], replacement["new"])
        helper = body(source, "vPortSuppressTicksAndSleep")
        helper = re.sub(r"__asm volatile\s*\([^;]*\);", "", helper)
        prefix = (
            PROTOCOL
            + r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t TickType_t;
#define CONFIG_PM 0
#define CONFIG_UPDATE_TICK_THEN_ENABLE_INT 1
#define portNVIC_SYSTICK_ENABLE_BIT 1u
#define eAbortSleep 1
#define configPRE_SLEEP_PROCESSING(ticks) ((void)(ticks))
#define configPOST_SLEEP_PROCESSING(ticks) assert(!released)
#define taskENTER_CRITICAL() (++nesting)
#define taskEXIT_CRITICAL() (--nesting)
static uint32_t portNVIC_SYSTICK_CTRL_REG,portNVIC_SYSTICK_LOAD_REG,portNVIC_SYSTICK_CURRENT_VALUE_REG;
static uint32_t ulTimerCountsForOneTick=1000,ulStoppedTimerCompensation=1;
static TickType_t xMaximumPossibleSuppressedTicks=100;
static unsigned nesting,tick_restores,released;
static bool abort_sleep,pending;
static bool allow_tickless=true;
static unsigned progress[32],progress_count;
void unhygienix_native_sleep_port_progress(uint32_t phase){assert(progress_count<32);progress[progress_count++]=phase;}
static void verify_progress(const unsigned *expected,unsigned count){
 assert(progress_count==count);
 for(unsigned i=0;i<count;++i)assert(progress[i]==expected[i]);
 progress_count=0;
}

bool unhygienix_native_sleep_tickless_allowed(void){return allow_tickless;}
static int eTaskConfirmSleepModeStatus(void){return abort_sleep?eAbortSleep:0;}
static void bk_pm_suppress_ticks_and_sleep(TickType_t ticks){assert(ticks && nesting==1);pending=true;}
static void systick_gated_update(TickType_t ticks,uint32_t reload){assert(ticks && reload && nesting==1 && !released);++tick_restores;}
void unhygienix_native_sleep_interrupts_restored(void){
 assert(!nesting);
 if(pending){assert(tick_restores==2 && (portNVIC_SYSTICK_CTRL_REG&1u));++released;pending=false;}
}
"""
        )
        main = r"""
int main(void){
 portNVIC_SYSTICK_CURRENT_VALUE_REG=100;
 portNVIC_SYSTICK_LOAD_REG=123;portNVIC_SYSTICK_CTRL_REG=7;
 allow_tickless=false;vPortSuppressTicksAndSleep(10);
 assert(portNVIC_SYSTICK_CURRENT_VALUE_REG==100 && portNVIC_SYSTICK_LOAD_REG==123 && portNVIC_SYSTICK_CTRL_REG==7);
 assert(!nesting && !tick_restores && !released && !pending);
 const unsigned refused[]={T5AIPort_Enter,T5AIPort_GuardRefused};
 verify_progress(refused,sizeof(refused)/sizeof(*refused));

 allow_tickless=true;
 vPortSuppressTicksAndSleep(10);assert(released==1 && !nesting);
 const unsigned normal[]={T5AIPort_Enter,T5AIPort_TickStopped,T5AIPort_CriticalEntered,
 T5AIPort_PMEnter,T5AIPort_PMReturned,T5AIPort_FirstTickEnter,T5AIPort_FirstTickReturned,
 T5AIPort_InterruptWindowReturned,T5AIPort_SecondTickEnter,T5AIPort_SecondTickReturned,
 T5AIPort_CriticalExited,T5AIPort_APReleaseEnter,T5AIPort_Returned};
 verify_progress(normal,sizeof(normal)/sizeof(*normal));

 released=tick_restores=0;abort_sleep=true;
 vPortSuppressTicksAndSleep(10);assert(!released && !tick_restores && !nesting);
 const unsigned aborted[]={T5AIPort_Enter,T5AIPort_TickStopped,T5AIPort_CriticalEntered,
 T5AIPort_AbortRestored,T5AIPort_APReleaseEnter,T5AIPort_Returned};
 verify_progress(aborted,sizeof(aborted)/sizeof(*aborted));

 return 0;
}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            main,
            transform=lambda source: helper,
        )

    def test_actual_resume_sender_never_queues_or_retries(self):
        overlay = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        replacement = next(
            r["new"]
            for file in overlay
            for r in file["replacements"]
            if "bk_err_t unhygienix_native_sleep_notify_ap(" in r["new"]
        )
        helper = body(replacement, "unhygienix_native_sleep_notify_ap")
        prefix = (
            PROTOCOL
            + r"""
#include <assert.h>
typedef int bk_err_t;
typedef struct {struct {uint32_t cmd;} hdr;uint32_t param1,param2,param3;} mb_chnl_cmd_t;
#define MB_CHNL_PWC 18u
static int result;static unsigned calls;
static bk_err_t mb_chnl_write(unsigned channel,mb_chnl_cmd_t *command){
 assert(channel==MB_CHNL_PWC && command->hdr.cmd==T5AI_PM_NATIVE_SLEEP_RESUMED);
 assert(command->param1==37 && command->param2==0xfffffffeu);
 assert(command->param3==T5AI_PM_NATIVE_ALARM_ABI);++calls;return result;
}
"""
        )
        main = r"""
int main(void){
 for(result=0;result>=-3;--result){
  unsigned before=calls;
  assert(unhygienix_native_sleep_notify_ap(37,0xfffffffeu)==result);
  assert(calls==before+1);
 }
 return 0;
}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            main,
            transform=lambda source: helper,
        )

    def test_explicit_persistent_and_analog_operation_selection(self):
        prefix = (
            PROTOCOL
            + r"""
#include <assert.h>
typedef int status_t;
static status_t prv_arm(T5AIAlarmOperation op,uint32_t ticks,uint32_t *generation){
 assert(op==EXPECTED_OP && ticks==2500);*generation=9;return 73;
}
"""
        )
        for flag, operation in [
            (
                "CONFIG_T5AI_RETAINED_SLEEP_PERSISTENT_TRACE",
                "T5AIAlarm_ArmSleepPersistentTrace",
            ),
            (
                "CONFIG_T5AI_RETAINED_SLEEP_ANALOG_WAKE_TEST",
                "T5AIAlarm_ArmSleepAnalogWake",
            ),
            (
                "CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL",
                "T5AIAlarm_ArmSleepMaskBarrier",
            ),
            (
                "CONFIG_T5AI_RETAINED_SLEEP_MASK_ONLY_CONTROL",
                "T5AIAlarm_ArmSleepMaskOnly",
            ),
            (
                "CONFIG_T5AI_RETAINED_SLEEP_SHALLOW_WFI_CONTROL",
                "T5AIAlarm_ArmSleepAnalogShallowWFI",
            ),
            (
                "CONFIG_T5AI_RETAINED_SLEEP_ANALOG_DOMAINS_RETAINED",
                "T5AIAlarm_ArmSleepAnalogRetainDomains",
            ),
        ]:
            with self.subTest(flag=flag):
                run(
                    "soc/t5ai/native_alarm.c",
                    prefix,
                    "int main(void){uint32_t gen=0;assert(t5ai_native_alarm_prepare_sleep(2500,&gen)==73 && gen==9);return 0;}",
                    defines=[flag + "=1", "EXPECTED_OP=" + operation],
                    transform=lambda source: body(
                        source, "t5ai_native_alarm_prepare_sleep"
                    ),
                )

    def test_ap_only_overrides_all_cp_transition_diagnostics(self):
        prefix = (
            PROTOCOL
            + r"""
#include <assert.h>
typedef int status_t;
static status_t prv_arm(T5AIAlarmOperation op,uint32_t ticks,uint32_t *generation){
 assert(op==T5AIAlarm_ArmSleepWakeOnly && ticks==2500);*generation=9;return 73;
}
"""
        )
        source = (ROOT / "soc/t5ai/native_alarm.c").read_text()
        flags = sorted(
            set(
                re.findall(
                    r"CONFIG_T5AI_RETAINED_SLEEP_\w+",
                    body(source, "t5ai_native_alarm_prepare_sleep"),
                )
            )
        )
        run(
            "soc/t5ai/native_alarm.c",
            prefix,
            "int main(void){uint32_t gen=0;assert(t5ai_native_alarm_prepare_sleep(2500,&gen)==73 && gen==9);return 0;}",
            defines=[flag + "=1" for flag in flags],
            transform=lambda source: body(source, "t5ai_native_alarm_prepare_sleep"),
        )

    def test_real_ap_query_whitelist_covers_every_read_only_operation(self):
        header = (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h").read_text()
        operations = re.findall(r"(T5AIAlarm_\w+)\s*=\s*\d+", header)
        reads = [
            op
            for op in operations
            if not op.startswith("T5AIAlarm_Arm")
            and op
            not in ("T5AIAlarm_Query", "T5AIAlarm_Cancel", "T5AIAlarm_FinishCleanup")
        ]
        mutations = [op for op in operations if op not in reads]
        main = "int main(void){assert(!t5ai_native_alarm_init());uint32_t value=0;\n"
        main += "const T5AIAlarmOperation reads[]={" + ",".join(reads) + "};\n"
        main += "const T5AIAlarmOperation mutations[]={" + ",".join(mutations) + "};\n"
        main += r"""
 for(unsigned i=0;i<sizeof(reads)/sizeof(reads[0]);++i){
  const unsigned before=requests;
  assert(!t5ai_native_alarm_cp_query(reads[i],&value));
  assert(last_op==(unsigned)reads[i] && requests==before+1);
  if(reads[i]==T5AIAlarm_MaskBarrierCount)assert(value==9);
 }
 const unsigned before=requests;
 for(unsigned i=0;i<sizeof(mutations)/sizeof(mutations[0]);++i)
  assert(t5ai_native_alarm_cp_query(mutations[i],&value)==E_INVALID_ARGUMENT);
 assert(t5ai_native_alarm_cp_query(T5AIAlarm_MaskBarrierCount,NULL)==E_INVALID_ARGUMENT);
 assert(t5ai_native_alarm_cp_query(T5AIAlarm_CleanupMailboxProgress+1,&value)==E_INVALID_ARGUMENT);
 assert(requests==before && !locks && !s_alarm_mutex && t5ai_native_alarm_healthy());
 return 0;
}
"""
        prefix = AP_PREFIX.replace(
            "*applied=bad_gen?gen+1:gen;",
            "*applied=bad_gen?gen+1:op==T5AIAlarm_MaskBarrierCount?9:gen;",
        )
        run("soc/t5ai/native_alarm.c", PROTOCOL + prefix, main)

    def test_every_arm_operation_rejects_a_mismatched_generation_echo(self):
        header = (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h").read_text()
        operations = re.findall(r"(T5AIAlarm_Arm\w*)\s*=\s*\d+", header)
        main = (
            "int main(void){const T5AIAlarmOperation arms[]={"
            + ",".join(operations)
            + "};\n"
        )
        main += r"""
 for(unsigned i=0;i<sizeof(arms)/sizeof(arms[0]);++i){
  s_initialized=s_active=s_fired=s_resumed=s_faulted=false;
  s_generation=0;bad_gen=false;
  assert(!t5ai_native_alarm_init());
  uint32_t generation;bad_gen=true;
  assert(prv_arm(arms[i],100,&generation)==E_ERROR);
  assert(s_faulted && !s_active && !t5ai_native_alarm_healthy());
  assert(!locks && !s_alarm_mutex);
 }
 return 0;
}
"""
        run("soc/t5ai/native_alarm.c", PROTOCOL + AP_PREFIX, main)

    def test_actual_cp_mailbox_drain(self):
        overlay = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        mailbox = next(
            item
            for item in overlay
            if item["path"].endswith("/mailbox/mailbox_channel.c")
        )
        function = body(
            mailbox["replacements"][0]["new"], "unhygienix_native_mailbox_sleep_ready"
        )
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define MB_CHNL_PWC 0x42u
#define GET_DST_CPU_ID(channel) 1u
#define CHNL_STATE_IDLE 0u
typedef struct {volatile uint8_t tx_state;} mb_log_chnl_cb_t;
static struct {volatile uint8_t tx_state;} phy_chnl_x_cb[2];
static mb_log_chnl_cb_t channels[3];
static const mb_log_chnl_cb_t *phy_chnl_log_chnl_list[2]={0,channels};
static const unsigned phy_chnl_log_chnl_num[2]={0,3};
static bool mb_chnnl_init_ok;
"""
        main = r"""
int main(void){
 assert(!unhygienix_native_mailbox_sleep_ready());mb_chnnl_init_ok=true;
 assert(unhygienix_native_mailbox_sleep_ready());
 phy_chnl_x_cb[1].tx_state=1;assert(!unhygienix_native_mailbox_sleep_ready());
 phy_chnl_x_cb[1].tx_state=0;
 for(unsigned i=0;i<3;++i){channels[i].tx_state=1;
  assert(!unhygienix_native_mailbox_sleep_ready() && channels[i].tx_state==1);
  channels[i].tx_state=0;assert(unhygienix_native_mailbox_sleep_ready());}
 return 0;
}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            main,
            transform=lambda _: function,
        )

    def test_actual_cp_peripheral_ownership(self):
        overlay = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        pm = next(item for item in overlay if item["path"].endswith("/bk_pm/pm.c"))
        code = next(
            item["new"]
            for item in pm["replacements"]
            if "bool unhygienix_native_peripherals_retained(void)" in item["new"]
        )
        function = body(code, "unhygienix_native_peripherals_retained")
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define PM_POWER_MODULE_NAME_BAKP 5u
#define PM_MODULE_SUB_POWER_DOMAIN_MAX 16u
#define PM_POWER_SUB_MODULE_NAME_BAKP_SDIO 91u
#define PM_POWER_SUB_MODULE_NAME_BAKP_DMA0 93u
static uint32_t s_pm_bakp_pm_state, domain_off;
static uint32_t sys_ll_get_cpu_power_sleep_wakeup_pwd_bakp(void){return domain_off;}
"""
        main = r"""
int main(void){
 assert(!unhygienix_native_peripherals_retained());
 s_pm_bakp_pm_state=1u<<11;assert(!unhygienix_native_peripherals_retained());
 s_pm_bakp_pm_state=1u<<13;assert(!unhygienix_native_peripherals_retained());
 s_pm_bakp_pm_state=(1u<<11)|(1u<<13);assert(unhygienix_native_peripherals_retained());
 domain_off=1;assert(!unhygienix_native_peripherals_retained());domain_off=0;
 s_pm_bakp_pm_state|=1u<<2;assert(unhygienix_native_peripherals_retained());
 return 0;
}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            main,
            transform=lambda _: function,
        )

    def test_actual_hal_preserves_native_ap_gpio_route(self):
        overlay = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        hal = next(
            item
            for item in overlay
            if item["path"].endswith("/bk7258/hal/sys_pm_hal.c")
        )
        code = next(
            item["new"]
            for item in hal["replacements"]
            if "if (unhygienix_native_sleep_cp_gpio_owned())" in item["new"]
        )
        native = (ROOT / "boards/unhygienix/tuya/cp/native_alarm.c").read_text()
        predicate = body(
            native.replace('__attribute__((section(".itcm_sec_code"))) ', ""),
            "unhygienix_native_sleep_cp_gpio_owned",
        )
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
static bool unhygienix_native_sleep_prepared;
static unsigned gpio_routes, ns_routes, wifi_routes;
static void sys_ll_set_cpu0_int_32_63_en_cpu0_wifi_mac_int_gen_en(unsigned v){wifi_routes+=v;}
static void sys_ll_set_cpu0_int_32_63_en_cpu0_gpio_int_en(unsigned v){gpio_routes+=v;}
static void sys_ll_set_cpu0_int_32_63_en_cpu0_wifi_hsu_irq_en(unsigned v){ns_routes+=v;}
"""
        main = r"""
int main(void){
 route();assert(wifi_routes==1 && gpio_routes+ns_routes==1);
 unhygienix_native_sleep_prepared=true;route();
 assert(wifi_routes==2 && gpio_routes+ns_routes==1);
 unhygienix_native_sleep_prepared=false;route();
 assert(wifi_routes==3 && gpio_routes+ns_routes==2);
 return 0;
}
"""
        for secure in (0, 1):
            run(
                "boards/unhygienix/tuya/cp/native_alarm.c",
                prefix,
                main,
                defines=[f"CONFIG_SPE={secure}"],
                transform=lambda _: (
                    predicate + "\nstatic void route(void){\n" + code + "\n}\n"
                ),
            )

    def test_actual_normal_hal_skips_only_post_native_system_mask_shallow_wfi(self):
        overlay = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        recipe = next(
            item for item in overlay if item["path"].endswith("/hal/sys_pm_hal.c")
        )
        text = (
            ROOT / "third_party/hal_tuya/TuyaOpen-T5AI" / recipe["path"]
        ).read_text()
        for change in recipe["replacements"]:
            self.assertEqual(text.count(change["old"]), change["count"])
            text = text.replace(change["old"], change["new"])
        native = (ROOT / "boards/unhygienix/tuya/cp/native_alarm.c").read_text()
        prefix = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
static bool s_native_system_mask_used;
static volatile uint32_t s_native_normal_sleep_skips;
static unsigned shallow_wfis;
static void arch_sleep(void){++shallow_wfis;}
"""
        main = r"""
int main(void){
 sys_hal_enter_normal_sleep(0);assert(shallow_wfis==1 && !s_native_normal_sleep_skips);
 s_native_system_mask_used=true;
 for(unsigned i=0;i<4;++i)sys_hal_enter_normal_sleep(0);
 assert(shallow_wfis==1 && s_native_normal_sleep_skips==4);
 return 0;
}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            main,
            transform=lambda _: (
                body(native, "unhygienix_native_normal_sleep_allowed")
                + body(text, "sys_hal_enter_normal_sleep")
            ),
        )

    def test_idle_port_progress_word_is_passive_and_native_owner_scoped(self):
        native = (ROOT / "boards/unhygienix/tuya/cp/native_alarm.c").read_text()
        prefix = (
            PROTOCOL
            + r"""
#include <assert.h>
static volatile uint32_t s_native_generation,s_native_port_progress;
static unsigned barriers;
#define __DMB() (++barriers)
"""
        )
        main = r"""
int main(void){
 assert(T5AIPort_Returned==T5AI_SLEEP_PORT_PROGRESS_MASK);
 unhygienix_native_sleep_port_progress(T5AIPort_PMEnter);
 assert(!s_native_port_progress && !barriers);
 s_native_generation=7;
 for(unsigned phase=0;phase<=T5AIPort_Returned;++phase){
  unhygienix_native_sleep_port_progress(phase);
  assert(s_native_port_progress==phase && barriers==phase+1);
 }
 const unsigned before=barriers;
 unhygienix_native_sleep_port_progress(T5AIPort_Returned+1);
 assert(barriers==before && s_native_port_progress==T5AIPort_Returned);
 return 0;
}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            main,
            transform=lambda _: body(native, "unhygienix_native_sleep_port_progress"),
        )

    def test_receive_record_survives_cleanup_and_commits_complete_versions(self):
        native = (ROOT / "boards/unhygienix/tuya/cp/native_alarm.c").read_text()
        declaration = native[
            native.index(
                "static volatile T5AIReceiveTrace s_native_receive_trace"
            ) : native.index("// Mailbox ISR is the sole writer")
        ]
        prefix = (
            PROTOCOL
            + r"""
#include <assert.h>
#include <stdint.h>
static volatile uint32_t s_native_generation;
"""
            + declaration
            + r"""
static unsigned barriers;
static void barrier(void) {
 assert(s_native_receive_trace.sequence & T5AI_RECEIVE_TRACE_IN_PROGRESS);
 ++barriers;
}
#define __DMB() barrier()
"""
        )
        main = r"""
int main(void) {
 assert(sizeof(T5AIReceiveTrace)==T5AI_SLEEP_TRACE_ALIGNMENT);
 unhygienix_native_mailbox_receive_observe(T5AIReceive_AdapterEnter,0,123);
 assert(!s_native_receive_trace.magic && !barriers);
 s_native_generation=7;
 unhygienix_native_mailbox_receive_observe(T5AIReceive_AdapterEnter,0,123);
 assert(barriers==2 && s_native_receive_trace.sequence==2);
 assert(s_native_receive_trace.magic==T5AI_RECEIVE_TRACE_MAGIC);
 assert(s_native_receive_trace.version==T5AI_RECEIVE_TRACE_VERSION);
 assert(s_native_receive_trace.generation==7 && s_native_receive_trace.payload==123);
 unhygienix_native_mailbox_receive_observe(T5AIReceive_CopyReturned,0xabcdef12,0);
 assert(s_native_receive_trace.header==0xabcdef12 && s_native_receive_trace.payload==123);
 // Observation remains valid after the cleanup lease closes.
 unhygienix_native_mailbox_receive_observe(T5AIReceive_CompletionReturned,0,0);
 assert(s_native_receive_trace.stage==T5AIReceive_CompletionReturned);
 assert(s_native_receive_trace.header==0xabcdef12 && s_native_receive_trace.sequence==6);
 assert(s_native_receive_trace.inverse==~s_native_receive_trace.sequence);
 const unsigned before=barriers;
 unhygienix_native_mailbox_receive_observe(T5AIReceive_Rejected+1,0,0);
 assert(barriers==before);
 s_native_receive_trace.sequence=UINT32_MAX-1;
 s_native_generation=8;
 unhygienix_native_mailbox_receive_observe(T5AIReceive_AdapterEnter,0,456);
 assert(s_native_receive_trace.sequence==0 && s_native_receive_trace.inverse==UINT32_MAX);
 assert(s_native_receive_trace.generation==8 && !s_native_receive_trace.header);
 assert(s_native_receive_trace.payload==456);
 return 0;
}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            main,
            transform=lambda _: body(
                native, "unhygienix_native_mailbox_receive_observe"
            ),
        )

    def test_cp_applied_status_cancel_fire_and_generation(self):
        for case in range(98):
            with self.subTest(case=case):
                run(
                    "boards/unhygienix/tuya/cp/native_alarm.c",
                    PROTOCOL + CP_PREFIX,
                    CP_MAIN,
                    defines=[f"CASE={case}"],
                    transform=cp_hardware_endpoints,
                )

    def test_ap_early_wake_stale_cancel_timeout_and_abi(self):
        for case in range(39):
            with self.subTest(case=case):
                run(
                    "soc/t5ai/native_alarm.c",
                    PROTOCOL + AP_PREFIX,
                    AP_MAIN,
                    defines=[f"CASE={case}"]
                    + (
                        [
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_MASKED=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_SUBCORES_ON=1",
                        ]
                        if case == 30
                        else ["CONFIG_T5AI_RETAINED_SLEEP_VENDOR_LPO_HALT=1"]
                        if case == 36
                        else ["CONFIG_T5AI_RETAINED_SLEEP_MASKED_CP_WFI=1"]
                        if case == 34
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_SUBCORES_ON=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_KEEP_HF=1",
                        ]
                        if case == 32
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_MASKED=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_SUBCORES_ON=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_KEEP_HF=1",
                        ]
                        if case == 33
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_SUBCORES_ON=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_CPU_IDLE=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_FLASH_IDLE=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_NO_HW_MASK=1",
                        ]
                        if case == 29
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_CPU_IDLE=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_FLASH_IDLE=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_NO_HW_MASK=1",
                        ]
                        if case == 28
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_VENDOR_FLASH_IDLE=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_NO_HW_MASK=1",
                        ]
                        if case == 27
                        else ["CONFIG_T5AI_RETAINED_SLEEP_NO_HW_MASK=1"]
                        if case == 26
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_WFI_KEEP_HF=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_NO_HW_MASK=1",
                        ]
                        if case == 25
                        else ["CONFIG_T5AI_RETAINED_SLEEP_WFI_KEEP_HF=1"]
                        if case == 24
                        else ["CONFIG_T5AI_RETAINED_SLEEP_RETAIN_MODULE_STATE=1"]
                        if case == 23
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_KEEP_HF=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_DOMAIN_BYPASS=1",
                        ]
                        if case in (19, 20)
                        else ["CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY=1"]
                        if case in (5, 6)
                        else ["CONFIG_T5AI_RETAINED_SLEEP_RESOURCES_ONLY=1"]
                        if case in (7, 8)
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_KEEP_HF=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_CORE_ONLY=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_BSP_RESTORE_ONLY=1",
                        ]
                        if case in (17, 18)
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_KEEP_HF=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_CORE_ONLY=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_FLASH_RESTORE_ONLY=1",
                        ]
                        if case in (15, 16)
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_KEEP_HF=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_CORE_ONLY=1",
                        ]
                        if case in (13, 14)
                        else [
                            "CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_KEEP_HF=1",
                        ]
                        if case in (11, 12)
                        else ["CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY=1"]
                        if case in (9, 10)
                        else []
                    ),
                )
