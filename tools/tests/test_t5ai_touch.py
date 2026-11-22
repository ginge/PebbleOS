# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise CST92xx's full driver with modeled IRQs, timers and KernelBG work."""

import unittest

from tools.tests.t5ai_touch_harness import run_touch


class T5AITouchTest(unittest.TestCase):
    def run_driver(self, checks, gesture_hook=""):
        run_touch(checks, gesture_hook)

    def test_stuck_idle_int_backs_off_but_real_edges_and_contacts_stay_fast(self):
        self.run_driver(r"""
pad=0;run_one();assert(timer_ms==20 && !output[0].down);
const unsigned delays[]={40,80,160,320,640,1000,1000};
for(unsigned i=0;i<sizeof(delays)/sizeof(delays[0]);++i){
 unsigned before=reads;fire_watchdog();assert(!qcount && reads==before);
 fire_timer();run_one();assert(timer_ms==delays[i] && !output[samples-1].down);
}
// A genuine falling edge bypasses idle backoff without waiting for the timer.
report(true,100,200);fire_irq();assert(qcount==1);run_one();
assert(output[samples-1].down && timer_ms==20 && !primary->idle_poll_ms);
// Held and suppressed contacts never use idle backoff.
for(unsigned i=0;i<3;++i){fire_timer();run_one();assert(timer_ms==20);}
wire[4]=0x80;fire_timer();run_one();assert(primary->palm_down && timer_ms==20);
report(false,0,0);fire_timer();run_one();assert(!primary->palm_down && timer_ms==20);
pad=1;fire_timer();run_one();assert(!timer_pending && !primary->idle_poll_ms);
// Long idle does no bus I/O; first touch is dispatched immediately.
unsigned before=reads;for(unsigned i=0;i<20;++i)fire_watchdog();
assert(reads==before && !qcount);
report(true,200,300);fire_irq();run_one();assert(output[samples-1].down && timer_ms==20);
""")

    def test_idle_ack_echo_backoff_resets_across_failure_and_enable(self):
        self.run_driver(r"""
pad=0;wire[0]=0xab;wire[5]=0;run_one();
fire_timer();run_one();assert(timer_ms==40 && !output[samples-1].down);
fire_timer();run_one();assert(timer_ms==80);
fail_ack=true;fire_timer();run_one();assert(timer_ms==80 && !primary->idle_poll_ms);
fail_ack=false;fire_timer();run_one();assert(timer_ms==20 && !primary->transfer_retry);
fire_timer();run_one();assert(timer_ms==40);
test_set_enabled(false);test_set_enabled(false);
assert(!timer_pending && !primary->idle_poll_ms && !primary->transfer_retry && !reset_pin);
test_set_enabled(true);test_set_enabled(true);run_one();assert(timer_ms==20);
""")

    def test_failed_transfer_irq_storm_cannot_bypass_retry_or_reset_delay(self):
        self.run_driver(r"""
report(true,100,200);run_one();fail_read=true;interrupt_during_read=true;
fire_timer();run_one();assert(qcount==1 && timer_ms==80 && primary->transfer_retry);
unsigned before=reads,baseline=resets;
// Work already queued by an IRQ during I2C also respects the retry delay.
run_one();assert(reads==before && timer_pending && !primary->callback_scheduled);
for(unsigned i=0;i<100;++i)fire_irq();
fire_watchdog();assert(!qcount && reads==before && resets==baseline);
fire_timer();run_one();assert(reads==before+1 && timer_ms==80);
fire_timer();run_one();assert(timer_ms==1000 && resets==baseline+1);
before=reads;for(unsigned i=0;i<100;++i)fire_irq();
fire_watchdog();assert(!qcount && reads==before && timer_pending);
// Retry enqueue rejection remains recoverable through the regular watchdog.
fire_timer();assert(qcount==1 && !primary->transfer_retry);
fail_read=false;report(false,0,0);fire_watchdog();run_one();
assert(!timer_pending && !primary->transfer_retry && !output[samples-1].down);
report(true,20,30);fire_irq();run_one();assert(output[samples-1].down);
""")

    def test_disable_during_failure_rejects_stale_retry_and_repeated_cycles(self):
        self.run_driver(r"""
fail_write=true;run_one();assert(primary->transfer_retry && timer_ms==80);

for(unsigned i=0;i<10;++i){
 test_set_enabled(false);unsigned before=reads;
 fire_irq();assert(!qcount && reads==before && !timer_pending && !reset_pin);
 test_set_enabled(true);assert(qcount==1 && !primary->transfer_retry);

 assert(qcount==1 && primary->callback_scheduled);
 fail_write=false;report(false,0,0);run_one();assert(!timer_pending);
}
""")

    def test_palm_listener_disable_cancels_following_raw_sample(self):
        self.run_driver(
            r"""
report(true,100,200);run_one();assert(samples==1 && output[0].down);
wire[4]=0x80;fire_timer();run_one();
assert(gestures==1 && samples==1 && !primary->enabled && !reset_pin && !timer_pending);
report(false,0,0);test_set_enabled(true);run_one();
assert(samples==2 && !output[1].down && !primary->palm_down && !timer_pending);
""",
            gesture_hook="test_set_enabled(false);",
        )

    def test_native_reports_coalescing_and_liftoff_without_edge(self):
        self.run_driver(r"""
report(true,100,200);fire_irq();fire_irq();assert(qcount==1);
run_one();assert(samples==1 && output[0].down && output[0].x==204 && output[0].y==148);
assert(timer_pending && timer_ms==20 && !qcount);
report(true,200,300);fire_timer();run_one();
assert(samples==2 && output[1].x==148 && output[1].y==92);
report(false,0,0);fire_timer();run_one();
assert(samples==3 && !output[2].down && output[2].x==148 && output[2].y==92);
assert(!timer_pending && !refs && !primary->callback_scheduled);
unsigned before=reads;fire_watchdog();assert(reads==before && !qcount);
""")

    def test_embedded_work_coalesces_and_recovers_lost_edges_without_timer_io(self):
        self.run_driver(r"""
run_one();unsigned before=reads;
report(true,10,20);fire_irq();fire_irq();fire_watchdog();
assert(qcount==1 && refs==1 && reads==before);run_one();
assert(output[samples-1].down);
fire_timer();assert(qcount==1);fire_irq();assert(qcount==1);run_one();
report(false,0,0);fire_timer();run_one();assert(!timer_pending);
// INT fell without a delivered ISR: watchdog catches the asserted line.
pad=0;before=reads;fire_watchdog();assert(qcount==1 && reads==before);
run_one();assert(timer_pending && timer_ms==20);
pad=1;fire_timer();run_one();assert(!timer_pending);
""")

    def test_i2c_failure_releases_contact_and_retries_after_reset(self):
        self.run_driver(r"""
report(true,100,200);run_one();fail_read=true;
unsigned baseline_resets=resets;
fire_timer();run_one();assert(!output[samples-1].down && timer_ms==80);
fire_timer();run_one();assert(timer_ms==80);
fire_timer();run_one();assert(timer_ms==1000 && resets==baseline_resets+1);
assert(!primary->consecutive_read_failures && irq_enabled);
fail_read=false;report(false,0,0);fire_timer();run_one();
assert(!timer_pending && !output[samples-1].down && !primary->i2c_lock.count);
fail_write=true;unsigned before=reads;fire_irq();run_one();
assert(reads==before && timer_pending && timer_ms==80 && !primary->i2c_lock.count);
""")

    def test_disable_reenable_rejects_queued_callbacks_and_old_timer(self):
        self.run_driver(r"""
report(true,100,200);run_one();

fire_irq();assert(qcount==1);
test_set_enabled(false);
assert(!irq_enabled && !reset_pin && !timer_pending && !timer_model(&primary->watchdog_timer)->scheduled);
unsigned before=reads,prior=samples;run_one();assert(reads==before && samples==prior && !refs);
// Re-enable queues new work; stale timer must not clear its coalescing state.
test_set_enabled(true);assert(qcount==1 && primary->callback_scheduled);

assert(qcount==1 && primary->callback_scheduled);
run_one();assert(samples==prior+1 && output[samples-1].down);
fire_irq();test_set_enabled(false);test_set_enabled(true);
assert(qcount==1);before=reads;run_one();
assert(reads==before+1 && !primary->callback_scheduled && !refs);
""")

    def test_production_ack_and_ack_failure_recovery(self):
        self.run_driver(r"""
assert(acks==1);report(true,100,200);run_one();assert(acks==2 && output[0].down);
fail_ack=true;fire_timer();run_one();
assert(!output[1].down && timer_ms==80 && primary->consecutive_read_failures==1);
fail_ack=false;fire_timer();run_one();
assert(output[2].down && !primary->consecutive_read_failures && !primary->i2c_lock.count);
""")

    def test_palm_once_suppresses_coordinates_until_all_contacts_lift(self):
        self.run_driver(r"""
report(true,100,200);wire[4]=0x80;run_one();
assert(gestures==1 && !output[0].down && palm_sequence<last_sample_sequence);
assert(timer_pending && timer_ms==20 && primary->palm_down);
fire_timer();run_one();assert(gestures==1 && !output[1].down);
// A remaining finger cannot start a new raw contact during the cover episode.
report(true,200,300);fire_timer();run_one();assert(!output[2].down && primary->palm_down);
// First slot lifted, but a second contact is still active.
wire[0]=0;wire[5]=2;wire[7]=0x16;fire_timer();run_one();
assert(primary->palm_down && timer_pending && gestures==1 && !output[3].down);
wire[5]=1;wire[7]=0;fire_timer();run_one();
assert(!primary->palm_down && !timer_pending && !output[4].down);
report(true,200,300);fire_irq();run_one();assert(output[5].down && gestures==1);
""")

    def test_palm_after_contact_and_invalid_cover_flags(self):
        self.run_driver(r"""
report(true,100,200);run_one();assert(output[0].down);
wire[4]=0x80;fire_timer();run_one();
assert(gestures==1 && !output[1].down && palm_sequence<last_sample_sequence);
fail_read=true;fire_timer();run_one();assert(primary->palm_down && gestures==1);
fail_read=false;fire_timer();run_one();assert(gestures==1 && !output[3].down);
report(false,0,0);fire_timer();run_one();assert(!primary->palm_down);
report(true,100,200);wire[4]=0x80;wire[6]=0;
fire_irq();run_one();assert(gestures==1 && !primary->palm_down);
wire[6]=0xab;wire[0]=0xab;fire_irq();run_one();assert(gestures==1);
report(true,100,200);wire[4]=0x80;fire_irq();run_one();assert(gestures==2);
test_set_enabled(false);assert(!primary->palm_down && !primary->down);
test_set_enabled(true);run_one();assert(gestures==3);
""")

    def test_ack_echo_does_not_synthesize_liftoff(self):
        self.run_driver(r"""
report(true,100,200);run_one();
wire[0]=0xab;wire[5]=0;fire_timer();run_one();
assert(output[1].down && output[1].x==output[0].x && timer_pending);
report(false,0,0);fire_timer();run_one();assert(!output[2].down && !timer_pending);
""")

    def test_interrupt_during_i2c_and_invalid_reports(self):
        self.run_driver(r"""
interrupt_during_read=true;report(true,0,465);run_one();
assert(qcount==1 && refs==1 && output[0].x==259 && output[0].y==0);
run_one();assert(!qcount && !refs);
report(true,466,200);fire_timer();run_one();assert(!output[samples-1].down && !timer_pending);
report(true,100,200);wire[6]=0;fire_irq();run_one();assert(!output[samples-1].down);
report(true,100,200);wire[0]=5;fire_irq();run_one();assert(!output[samples-1].down);
assert(!primary->i2c_lock.count && !primary->i2c_lock.count && !depth && !input_phase);
""")


if __name__ == "__main__":
    unittest.main()
