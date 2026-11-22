# SPDX-License-Identifier: Apache-2.0
"""Battery event deadlines, missed edges and asynchronous delivery races."""

import unittest

from tools.tests.t5ai_battery_harness import run_battery

IDLE = r"""
 battery_init();
 for (now=1000;now<=60000;now+=1000) { tick(); assert(!work && !requests); }
 assert(high_callbacks==2 && reads==3 && updates==0);
"""


class BatteryEventsTest(unittest.TestCase):
    def test_idle_reconciliation_reduces_driver_wakeups_without_adc_demand(self):
        run_battery(IDLE)

    def test_startup_demand_and_completion_use_their_actual_deadlines(self):
        run_battery(r"""
 battery_init();unavailable();
 now=29999;tick();assert(!work && !requests);
 now=30000;tick();assert(work);drain();unavailable();assert(requests==1);
 now=30003;done(0,2200);assert(!work && !sampled_logs);
 tick();assert(work);tick();assert(queued==2);drain();
 assert(sampled_logs==1 && updates==2 && battery_get_millivolts()>2000);
 assert(!s_retry && !s_pending && !s_notify);
""")

    def test_late_first_demand_starts_asynchronously_without_waiting_for_a_poll(self):
        run_battery(r"""
 battery_init();now=41001;tick();assert(!requests && !work);
 unavailable();assert(requests==1);unavailable();assert(requests==1);
 done(0,2200);tick();drain();assert(battery_get_millivolts()>2000);
""")

    def test_latest_edge_wins_even_while_normal_service_is_queued(self):
        run_battery(r"""
 battery_init();charge=true;context=4;irq_cb(NULL);context=1;
 now=399;tick();assert(!work);now=400;tick();assert(work);
 now=410;charge=false;context=4;irq_cb(NULL);context=1;
 now=500;drain();assert(!events);tick();assert(!work);
 now=809;tick();assert(!work);now=810;tick();drain();assert(!events);
 charge=true;context=4;irq_cb(NULL);context=1;
 now=1209;tick();assert(!work);now=1210;tick();drain();assert(events==1);
""")

    def test_full_delivery_queue_preserves_notification_and_bounded_retry(self):
        run_battery(r"""
 begin();done(E_ERROR,0);drop=true;tick();assert(!work && s_notify);
 unsigned n=high_callbacks;
 now+=49;tick();assert(high_callbacks==n && !work);
 ++now;tick();assert(!work && high_callbacks==n+1);
 drop=false;now+=49;tick();assert(!work);
 ++now;tick();drain();assert(failed_logs==1 && updates==2);
 now=34999;tick();assert(!work);now=35000;tick();drain();
 assert(updates==3);tick();assert(!work); // no immediate retry storm before the service reads
 unavailable();assert(requests==2);
""")

    def test_charge_read_error_cannot_publish_a_false_transition(self):
        run_battery(r"""
 battery_init();charge=true;context=4;irq_cb(NULL);context=1;
 now=400;gpio_read_error=-5;tick();drain();assert(!events && s_charge_dirty);
 now=799;gpio_read_error=0;tick();assert(!work);
 now=800;tick();drain();assert(events==1 && s_charge_known);
""")

    def test_edge_during_service_is_not_lost(self):
        run_battery(
            r"""
 begin();on_update=inject_edge;done(0,2200);tick();drain();assert(!events);
 now+=399;tick();assert(!work);++now;tick();drain();assert(events==1);
""",
            hooks=r"""
 static void inject_edge(void) { on_update=NULL;charge=true;context=4;irq_cb(NULL);context=1; }
""",
        )

    def test_completion_during_service_gets_a_second_delivery(self):
        run_battery(
            r"""
 begin();on_update=inject_completion;done(0,2200);tick();drain();
 assert(s_notify && requests==2);tick();drain();
 assert(!s_notify && sampled_logs==2 && updates==3);
""",
            hooks=r"""
 static void inject_completion(void) {
   on_update=NULL;now+=60000;unavailable();done(0,2300);
 }
""",
        )

    def test_retry_and_debounce_deadlines_keep_full_uptime_width(self):
        run_battery(r"""
 now=(uint64_t)UINT32_MAX-10;begin();done(E_ERROR,0);tick();drain();
 uint64_t due=now+5000;now=due-1;tick();assert(!work);
 ++now;tick();drain();unavailable();assert(requests==2);
 done(0,2200);tick();drain();
 charge=true;context=4;irq_cb(NULL);context=1;now+=400;tick();drain();assert(events==1);
""")

    def test_irq_setup_failure_keeps_the_existing_fast_safety_poll(self):
        for failure in ("irq_error", "irq_enable_error"):
            with self.subTest(failure=failure):
                run_battery(
                    failure
                    + r""" = -1;
 battery_init();charge=true;now=999;tick();assert(!work);
 now=1000;tick();assert(!work);now=1399;tick();assert(!work);
 now=1400;tick();drain();assert(events==1 && !requests);
"""
                )

    def test_edge_during_irq_enable_is_scheduled_after_initialization(self):
        run_battery(r"""
 edge_on_enable=true;battery_init();assert(s_initialized && s_charge_dirty);
 now=399;tick();assert(!work);now=400;tick();drain();assert(events==1);
 assert(!requests);
""")

    def test_failed_irq_keeps_fast_reconciliation_and_does_not_accept_bad_gpio(self):
        run_battery(r"""
 irq_enable_error=-5;battery_init();charge=true;gpio_read_error=-5;
 now=1000;tick();assert(!work && !s_charge_dirty);
 gpio_read_error=0;now=2000;tick();assert(!work && s_charge_dirty);
 now=2399;tick();assert(!work);now=2400;tick();drain();assert(events==1);
 assert(!requests);
""")
