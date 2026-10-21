# SPDX-License-Identifier: Apache-2.0
"""Test actual board queue helpers, timestamps and transition barriers."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define static_assert _Static_assert
enum {PEBBLE_TOUCH_EVENT=55,OTHER=12};
enum {TouchEvent_Touchdown,TouchEvent_Liftoff,TouchEvent_PositionUpdate};
typedef struct {uint8_t type;bool non_navigational;int16_t x,y;} TouchEvent;
typedef struct {TouchEvent event;} PebbleTouchEvent;
typedef struct {union {PebbleTouchEvent touch;uint8_t room[9];};uint8_t task_mask,type;} PebbleEvent;
struct pbl_msgq {void *buf;size_t msg_size;unsigned max_msgs;struct {unsigned count,tail;} backend;};
#include HEADER
static PebbleEvent make(unsigned type,unsigned x,uint64_t time) {
 PebbleEvent e={.type=PEBBLE_TOUCH_EVENT,.task_mask=3,.touch={.event={.type=type,.x=x,.y=4}}};
 t5_touch_stamp(&e,time);assert(e.type==PEBBLE_TOUCH_EVENT && e.task_mask==3 && e.touch.event.x==(int16_t)x);
 return e;
}
int main(void) {
 for(uint64_t now=1;now<30000000;now+=131) {
  uint64_t at=now>100?now-100:0;PebbleEvent e=make(2,7,at);
  assert(t5_touch_has_stamp(&e));assert(t5_touch_time(&e,now)==at);
 }
 PebbleEvent buf[8];struct pbl_msgq q={buf,sizeof(PebbleEvent),8,{0,0}};
 // Exercise tails straddling the ring wrap.
 for(unsigned tail=0;tail<8;++tail) {
  q.backend.count=2;q.backend.tail=tail;
  unsigned a=(tail+6)%8,b=(tail+7)%8;
  buf[a]=make(2,10,100);buf[b]=make(2,20,120);
  for(unsigned x=30;x<20000;++x) {
   PebbleEvent e=make(2,x,100+x);
   assert(t5_touch_combine(&q,&e));
   assert(buf[b].touch.event.x==(int16_t)x);
   assert(t5_touch_time(&buf[b],30000)==100+x);
  }
  assert(q.backend.count==2 && q.backend.tail==tail);
  assert(buf[a].touch.event.x==19998 && buf[b].touch.event.x==19999);
  // A control/transition, non-navigation boundary or unrelated event is a barrier.
  for(unsigned type=0;type<2;++type) {
   PebbleEvent e=make(type,3,20);assert(!t5_touch_combine(&q,&e));
   buf[a]=e;e=make(2,4,21);assert(!t5_touch_combine(&q,&e));buf[a]=make(2,0,1);
  }
  PebbleEvent e=make(2,7,200);buf[b].type=OTHER;assert(!t5_touch_combine(&q,&e));
  buf[b]=make(2,0,1);buf[a].touch.event.non_navigational=true;assert(!t5_touch_combine(&q,&e));
 }
 return 0;
}
"""


class TouchQueueTest(unittest.TestCase):
    def test_wrap_velocity_samples_and_transitions(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            for header in ["kernel/events.h", "pbl/kernel/msgq.h"]:
                path = d / header
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("#pragma once\n")
            (d / "test.c").write_text(HARNESS)
            subprocess.run(
                [
                    "cc",
                    "-I",
                    str(d),
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined,bounds",
                    "-fno-sanitize-recover=all",
                    '-DHEADER="' + str(ROOT / "soc/t5ai/touch_queue.h") + '"',
                    str(d / "test.c"),
                    "-o",
                    str(d / "test"),
                ],
                check=True,
            )
            subprocess.run([str(d / "test")], check=True)


WRAPPERS = r"""
#define T5_IO_ADD(kind, elapsed, amount) do {} while (0)
static uint64_t now;
static unsigned control, irq_depth;
#define __get_CONTROL() control
static uint64_t rtc_get_ticks(void) {return now;}
static void pbl_irq_lock(void) {++irq_depth;}
static void pbl_irq_unlock(void) {assert(irq_depth);--irq_depth;}
typedef unsigned pbl_timeout_t;
static bool pbl_timeout_is_no_wait(unsigned t) {return !t;}
typedef struct {bool is_unprivileged;} PebbleProcessMd;
static PebbleProcessMd md;
static const PebbleProcessMd *app_manager_get_current_app_md(void) {return &md;}
static PebbleEvent ring[8], main_event;
static struct pbl_msgq appq={ring,sizeof(PebbleEvent),8,{0,0}};
enum {PebbleTask_App=3};
static struct pbl_msgq *pebble_task_get_to_queue(unsigned task) {assert(task==3);return &appq;}
static void __real_event_put(PebbleEvent *e) {main_event=*e;}
static int __real_pbl_msgq_put(struct pbl_msgq *q,const void *e,unsigned timeout) {
 if(timeout)assert(!irq_depth);
 assert(q->backend.count<8);
 memcpy((uint8_t*)q->buf+q->backend.tail*q->msg_size,e,q->msg_size);
 q->backend.tail=(q->backend.tail+1)%8;++q->backend.count;return 0;
}
"""
FORWARD_MAIN = r"""
int main(void) {
 PebbleEvent e={.type=PEBBLE_TOUCH_EVENT,.task_mask=5,.touch={.event={.type=2,.x=1,.y=4}}};
 // Sample production time survives a long Main backlog, including ring-tail coalescing.
 for(unsigned sample=100;sample<=140;sample+=20) {
  now=sample;e.touch.event.x=sample;__wrap_event_put(&e);
  assert(t5_touch_time(&main_event,now)==sample);
  now=1000;assert(!__wrap_pbl_msgq_put(&appq,&main_event,0));
 }
 assert(appq.backend.count==2 && ring[0].task_mask==5 && ring[1].task_mask==5);
 assert(t5_touch_time(&ring[0],now)==120 && t5_touch_time(&ring[1],now)==140);
 assert(ring[0].touch.event.x==120 && ring[1].touch.event.x==140);
 e.touch.event.type=TouchEvent_Liftoff;now=160;__wrap_event_put(&e);now=1100;
 assert(!__wrap_pbl_msgq_put(&appq,&main_event,0));assert(appq.backend.count==3);
 assert(ring[2].touch.event.type==TouchEvent_Liftoff && t5_touch_time(&ring[2],now)==160);
 // Other queues and blocking calls keep their locking/timeout contract.
 PebbleEvent other[8];struct pbl_msgq q={other,sizeof(PebbleEvent),8,{0,0}};
 assert(!__wrap_pbl_msgq_put(&q,&e,3000) && !irq_depth);
 memset(&e,0,sizeof e);e.type=PEBBLE_TOUCH_EVENT;md.is_unprivileged=true;
 __wrap_event_put(&e);assert(!t5_touch_has_stamp(&main_event));
 md.is_unprivileged=false;control=1;__wrap_event_put(&e);assert(!t5_touch_has_stamp(&main_event));
 return 0;
}
"""


class TouchForwardingTest(unittest.TestCase):
    def test_sample_time_survives_main_backlog(self):
        import re

        source = (ROOT / "soc/t5ai/touch_queue.c").read_text()

        def function(name):
            m = re.search(
                r"^(?:static )?(?:bool|void|int) " + name + r"\([^;{]*\)\s*\{",
                source,
                re.MULTILINE,
            )
            a = source.index("{", m.start())
            depth = 1
            end = a + 1
            while depth:
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
            return source[m.start() : end]

        base = HARNESS[: HARNESS.index("static PebbleEvent make(")]
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            for header in ["kernel/events.h", "pbl/kernel/msgq.h"]:
                path = d / header
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("#pragma once\n")
            (d / "test.c").write_text(
                base
                + WRAPPERS
                + "\n".join(
                    function(n)
                    for n in ["prv_target", "__wrap_event_put", "__wrap_pbl_msgq_put"]
                )
                + FORWARD_MAIN
            )
            subprocess.run(
                [
                    "cc",
                    "-I",
                    str(d),
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined,bounds",
                    "-fno-sanitize-recover=all",
                    '-DHEADER="' + str(ROOT / "soc/t5ai/touch_queue.h") + '"',
                    str(d / "test.c"),
                    "-o",
                    str(d / "test"),
                ],
                check=True,
            )
            subprocess.run([str(d / "test")], check=True)
