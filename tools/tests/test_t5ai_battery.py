# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Full T5AI driver: asynchronous service demand, validity, retries and charge events."""

import unittest

from tools.tests.t5ai_battery_harness import run_battery


class T5AIBatteryTest(unittest.TestCase):
    def run_driver(self, checks, profile=False, quiet=False, battery_profile=False):
        run_battery(
            checks, profile=profile, quiet=quiet, battery_profile=battery_profile
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
 gpio_error=0;irq_error=-1;battery_init();battery_init();assert(configures==2 && timers[0].item && timers[1].item && !timers[2].item);
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
 now+=5000;tick();assert(!work);drop=false;now+=50;tick();drain();
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
 charge=false;now=30000;tick();assert(!work);now=30400;tick();drain();assert(events==2);
 assert(!requests);
""")
