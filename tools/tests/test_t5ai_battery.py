# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Full T5AI driver: asynchronous service demand, validity, retries and charge events."""

import re
import unittest
from pathlib import Path

from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "drivers/battery/t5ai.c"
HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "system/status_codes.h"
#include <pbl/drivers/battery.h>
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_ERR(...) check_log(__VA_ARGS__)
#define PBL_LOG_WRN(...) check_log(__VA_ARGS__)
#define PBL_LOG_DBG(...) check_log(__VA_ARGS__)
#define PBL_GPIO_INPUT 4
#define PEBBLE_BATTERY_CONNECTION_EVENT 99
struct pbl_gpio {unsigned pin;};
struct pbl_irq_spec {unsigned pin;};
typedef struct {struct pbl_gpio charge;struct pbl_irq_spec interrupt;
 uint8_t channel,samples;uint16_t cal_low,cal_span,divider_numerator,divider_denominator;
} T5AIBatteryConfig;
const T5AIBatteryConfig T5AI_BATTERY_CONFIG={.charge={30},.interrupt={30},
 .channel=15,.samples=16,.cal_low=2716,.cal_span=2854,
 .divider_numerator=2510,.divider_denominator=510};
#define nullptr NULL
#define PBL_MSEC(ms) (ms)
#define pbl_ms_to_ticks(ms) (ms)
struct pbl_work {void (*fn)(struct pbl_work *);};
struct pbl_delayed_work {struct pbl_work work;};
static int pbl_sys_workq_high;

typedef struct {unsigned type;struct {bool is_connected;} battery_connection;} PebbleEvent;
static uint64_t now;
static unsigned depth,context,requests,updates,events,configures,registrations,queued;
static int gpio_error,irq_error;
static bool charge,drop;
static unsigned sampled_logs,failed_logs;
static void check_log(const char *fmt,...){assert(context==1);
 if(strstr(fmt,"Battery ADC sample:"))++sampled_logs;
 if(strstr(fmt,"Battery ADC failed:"))++failed_logs;
}
static status_t request_error;
static void (*regular_cb)(void *),(*irq_cb)(void *),(*work)(void *);
static void (*completion)(status_t,uint16_t,void *);
void pbl_irq_lock(void){++depth;}
void pbl_irq_unlock(void){assert(depth);--depth;}
uint64_t pbl_uptime_ms(void){return now;}
int pbl_gpio_configure(const struct pbl_gpio *g,unsigned f){
 assert(!depth && g->pin==30 && f==4);++configures;return gpio_error;
}
int pbl_gpio_get(const struct pbl_gpio *g){assert(g->pin==30);return charge;}
int pbl_intc_request(const struct pbl_irq_spec *g,void (*cb)(void *),void *ctx){
 assert(g->pin==30 && !ctx);irq_cb=cb;return irq_error;
}
int pbl_intc_enable(const struct pbl_irq_spec *g){assert(g->pin==30);return 0;}
static struct pbl_delayed_work *periodic;
static void run_periodic(void *ctx){(void)ctx;periodic->work.fn(&periodic->work);}
void pbl_delayed_work_init(struct pbl_delayed_work *t,void (*fn)(struct pbl_work *)){t->work.fn=fn;}
bool pbl_delayed_work_schedule(int *q,struct pbl_delayed_work *t,unsigned ms){
 assert(q==&pbl_sys_workq_high && ms==1000);++registrations;periodic=t;regular_cb=run_periodic;return true;
}
bool pbl_delayed_work_schedule_next(struct pbl_delayed_work *t,unsigned ticks){
 assert(t==periodic && ticks==1000);return true;
}
bool pbl_sys_work_try_call(void (*cb)(void *),void *ctx){
 assert(context==2 && !ctx && depth);
 if(drop)return false;
 assert(!work);work=cb;++queued;return true;
}
void battery_state_force_update(void){assert(context==1 && !depth);++updates;}
void event_put(PebbleEvent *e){
 assert(context==1 && !depth && e->type==99 && e->battery_connection.is_connected==charge);++events;
}
typedef struct {status_t status;uint32_t elapsed_ms,invalid_count;int16_t cp_status;
 uint8_t failed_command;bool available;} T5AISaradcDiagnostics;
void t5ai_saradc_get_diagnostics(T5AISaradcDiagnostics *d){
 *d=(T5AISaradcDiagnostics){.failed_command=255};
}
typedef void (*T5AISaradcCompletion)(status_t,uint16_t,void *);
status_t t5ai_saradc_request(uint8_t ch,uint8_t n,T5AISaradcCompletion cb,void *ctx){
 assert(context==1 && !depth && ch==15 && n==16 && !ctx && !completion);++requests;
 if(FAILED(request_error))return request_error;
 completion=cb;return 0;
}
#include DRIVER_SOURCE
static void tick(void){context=2;assert(regular_cb);regular_cb(NULL);context=1;}
static void drain(void){assert(work);void (*cb)(void *)=work;work=NULL;context=1;cb(NULL);}
static __attribute__((unused)) void done(status_t status,uint16_t peak){
 assert(completion);void (*cb)(status_t,uint16_t,void *)=completion;completion=NULL;
 context=3;cb(status,peak,NULL);context=1;assert(!depth);
}
static void unavailable(void){
 BatteryConstants out={.v_mv=123};assert(!battery_get_millivolts());
 assert(battery_get_constants(&out)<0 && out.v_mv==123 && !depth);
}
static __attribute__((unused)) void begin(void){context=1;battery_init();unavailable();now+=30000;tick();drain();
 assert(!battery_get_millivolts() && requests==1);}
"""


class T5AIBatteryTest(unittest.TestCase):
    def run_driver(self, checks, profile=False, quiet=False, battery_profile=False):
        headers = {
            n: ""
            for n in re.findall(r'#include [<"]([^>"\n]+)[>"]', DRIVER.read_text())
            if "/" in n and n != "pbl/drivers/battery.h"
        }
        headers["system/status_codes.h"] = (
            ROOT / "include/pbl/system/status_codes.h"
        ).read_text()
        run_c(
            ("#define CONFIG_T5AI_POWER_PROFILE 1\n" if profile else "")
            + ("#define CONFIG_T5AI_BATTERY_PROFILE 1\n" if battery_profile else "")
            + ("#define CONFIG_T5AI_BATTERY_QUIET_SUCCESS 1\n" if quiet else "")
            + HARNESS.replace("DRIVER_SOURCE", '"' + str(DRIVER) + '"')
            + "\nint main(void){\n"
            + checks
            + "\nreturn 0;}\n",
            headers,
        )

    def test_sample_logging_opt_in_preserves_failure_logs(self):
        for profile in [False, True]:
            for quiet in [False, True]:
                self.run_driver(
                    f"""
 begin();done(0,2200);tick();drain();
 assert(sampled_logs=={int(not quiet or profile)});
 now+=60000;unavailable();done(E_ERROR,0);tick();drain();
 assert(failed_logs==1);
""",
                    profile=profile,
                    quiet=quiet,
                )

    def test_battery_profile_preserves_quiet_configuration_and_sampling(self):
        self.run_driver(
            r"""
 begin();done(0,2200);tick();drain();
 assert(sampled_logs==1 && requests==1 && updates==2);
 tick();assert(!work && requests==1);
 now+=60000;unavailable();done(E_ERROR,0);tick();drain();
 assert(failed_logs==1 && sampled_logs==1);
""",
            quiet=True,
            battery_profile=True,
        )

    def test_startup_demand_coalescing_and_freshness(self):
        self.run_driver(r"""
 context=1;gpio_error=-1;battery_init();unavailable();assert(!registrations);
 gpio_error=0;irq_error=-1;battery_init();battery_init();assert(registrations==1 && configures==2);
 tick();assert(!work && !requests);
 unavailable();now=30000;tick();drain();unavailable();assert(requests==1);
 unavailable();assert(requests==1);done(0,2200);
 tick();tick();assert(queued==2);drain();
 int mv=battery_get_millivolts();assert(mv>2000 && mv<5000);
 now+=59999;assert(battery_get_millivolts()==mv && requests==1);
 ++now;unavailable();assert(requests==2);done(0,2200);
""")

    def test_failures_recovery_and_terminal_quarantine(self):
        self.run_driver(r"""
 begin();done(0,2200);int mv=battery_get_millivolts();assert(mv>0);
 now+=60000;unavailable();done(E_ERROR,0);unavailable();assert(requests==2);
 now+=4999;tick();drain();unavailable();assert(requests==2);
 ++now;tick();drain();unavailable();assert(requests==3);
 done(0,2300);assert(battery_get_millivolts()>mv);
 now+=60000;unavailable();done(E_AGAIN,0);now+=600000;unavailable();assert(requests==4);
""")

    def test_range_rejection_queue_retry_and_immediate_rejection(self):
        self.run_driver(r"""
 begin();done(0,65535);unavailable();drop=true;tick();assert(!work);
 now+=5000;tick();assert(!work);drop=false;tick();drain();
 request_error=E_INVALID_OPERATION;unavailable();assert(requests==2);
 now+=100000;unavailable();assert(requests==2);
""")

    def test_charge_debounce_both_edges_and_missed_edge_reconciliation(self):
        self.run_driver(r"""
 context=1;battery_init();charge=true;context=4;irq_cb(NULL);context=1;
 now=399;tick();assert(!work);now=400;tick();drain();assert(events==1);
 charge=false;context=4;irq_cb(NULL);context=1;
 now=600;charge=true;context=4;irq_cb(NULL);context=1;
 now=1000;tick();drain();assert(events==1);
 charge=false;now=2000;tick();assert(!work);now=2400;tick();drain();assert(events==2);
 assert(!requests);
""")
