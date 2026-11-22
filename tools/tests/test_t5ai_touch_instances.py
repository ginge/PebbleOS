# SPDX-License-Identifier: Apache-2.0
"""Real touch class and driver: isolate callbacks and retain exact board coordinates."""

import unittest

from tools.tests.t5ai_touch_harness import run_touch


class TouchInstancesTest(unittest.TestCase):
    def test_two_native_devices_keep_irqs_queues_retries_and_users_separate(self):
        run_touch(r"""
 report(true,100,200);
 report_index(1,false,0,0);
 assert(pbl_touch_dev_init(&devices[1].touch.dev)==0);
 second_device_state.status=PBL_DEVICE_READY;
 assert(timer_creates==4 && hw[1].read_count==1 && !hw[1].reset_on);
 assert(pbl_touch_enable(&devices[1].touch)==0);
 assert(pbl_touch_enable(&devices[1].touch)==0);
 assert(second_touch_state.users==2 && qcount==2 && refs==2);
 assert(pbl_touch_disable(&devices[1].touch)==0 && second_touch_state.users==1);
 report_index(1,true,200,150);
 run_one();run_one();
 assert(samples==1 && output[0].down && output[0].x==204 && output[0].y==148);
 assert(hw[1].sample_count==1 && hw[1].records[0].down);
 // Second configuration scales 400x300 to 100x80, inverts Y, then swaps axes.
 assert(hw[1].records[0].x==39 && hw[1].records[0].y==50);
 assert(second_touch_state.last.x==39 && second_touch_state.last.y==50);
 assert(first_touch_state.last.x==204 && first_touch_state.last.y==148);
 assert(hw[0].irq_arg==&devices[0] && hw[1].irq_arg==&devices[1]);
 hw[1].read_error=true;fire_timer_index(1);run_one();
 assert(devices[1].state->transfer_retry && !hw[1].records[1].down);
 assert(timer_model(&devices[1].state->retry_timer)->milliseconds==80);
 assert(primary->down && !primary->transfer_retry && timer_ms==20);
 unsigned second_reads=hw[1].read_count;
 report(true,200,300);fire_irq();run_one();
 assert(hw[1].read_count==second_reads && output[samples-1].down);
 assert(primary->enabled && !primary->transfer_retry);
 assert(pbl_touch_disable(&devices[1].touch)==0);
 assert(!devices[1].state->enabled && !hw[1].irq_on && !hw[1].reset_on);
 assert(primary->enabled && irq_enabled && reset_pin && timer_pending);
 fire_irq_index(1);assert(!qcount);
 assert(pbl_touch_enable(&devices[1].touch)==0 && qcount==1);
 // A canceled old retry cannot clear the new generation's coalescing state.
 unsigned new_generation=devices[1].state->generation;
 assert(devices[1].state->retry_generation!=new_generation);
 context=TIMER;
 devices[1].state->retry_timer.work.fn(&devices[1].state->retry_timer.work);
 context=USER;
 assert(qcount==1 && devices[1].state->callback_scheduled);
 assert(devices[1].state->work_generation==new_generation);
 assert(!primary->callback_scheduled && primary->enabled);
 hw[1].read_error=false;report_index(1,false,0,0);run_one();
 assert(!hw[1].records[2].down && !devices[1].state->transfer_retry);
 assert(!timer_model(&devices[1].state->retry_timer)->scheduled);
 assert(!first_touch_state.lock.count && !second_touch_state.lock.count);
""")

    def test_every_native_axis_value_retains_prior_rounding_and_initial_idle(self):
        run_touch(r"""
 run_one();
 assert(samples==1 && !output[0].down && output[0].x==0 && output[0].y==0);
 for(unsigned x=0;x<466;++x) {
   unsigned y=465-x;report(true,x,y);
   if(x)fire_timer();else fire_irq();
   run_one();struct Sample s=output[samples-1];
   assert(s.down && s.x==259-(int)(x*260/466) && s.y==259-(int)(y*260/466));
   assert(first_touch_state.last.x==s.x && first_touch_state.last.y==s.y);
 }
 report(true,466,0);fire_timer();run_one();
 assert(!output[samples-1].down && output[samples-1].x==0 && output[samples-1].y==259);
 assert(!timer_pending);
""")

    def test_palm_listener_can_drop_real_class_reference_without_following_sample(self):
        run_touch(
            r"""
 assert(pbl_touch_enable(TOUCH)==0 && first_touch_state.users==1);
 report(true,100,200);run_one();assert(samples==1 && output[0].down);
 wire[4]=0x80;fire_timer();run_one();
 assert(gestures==1 && samples==1 && !first_touch_state.users && !primary->enabled);
 assert(!first_touch_state.last.down && !reset_pin && !timer_pending);
 report(false,0,0);assert(pbl_touch_enable(TOUCH)==0);run_one();
 assert(samples==2 && !output[1].down && first_touch_state.users==1);
 assert(!first_touch_state.lock.count && !primary->i2c_lock.count);
""",
            gesture_hook="assert(pbl_touch_disable(TOUCH)==0);",
        )

    def test_controller_init_errors_and_zero_geometry_fail_before_io_or_work(self):
        run_touch(
            r"""
 hw[0].device_error=EIO;
 assert(pbl_touch_dev_init(&devices[0].touch.dev)==-EIO);
 assert(!timer_creates && !reads && !resets && !primary->initialized);
 hw[0].device_error=0;
 for(unsigned field=0;field<4;++field) {
   struct pbl_cst92xx_t5ai_state state={.i2c_lock=PBL_MUTEX_INITIALIZER};
   struct pbl_touch_state touch_state={.lock=PBL_MUTEX_INITIALIZER};
   struct pbl_cst92xx_t5ai invalid=devices[0];
   invalid.state=&state;invalid.touch.state=&touch_state;
   if(field==0)invalid.native_width=0;
   if(field==1)invalid.native_height=0;
   if(field==2)invalid.touch.props.size_x=0;
   if(field==3)invalid.touch.props.size_y=0;
   assert(pbl_touch_dev_init(&invalid.touch.dev)==-EINVAL);
   assert(!timer_creates && !reads && !resets && !state.initialized);
   assert(!state.i2c_lock.count && !touch_state.lock.count);
 }
""",
            setup=False,
        )
