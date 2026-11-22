/* SPDX-License-Identifier: Apache-2.0 */
static void tick(void) {
  unsigned passes = 0;
  for (;;) {
    unsigned found = 4;
    for (unsigned i = 0; i < 4; ++i)
      if (timers[i].armed && timers[i].due <= now) {
        found = i;
        break;
      }
    if (found == 4)
      break;
    assert(++passes < 16); // Catch a self-rearming immediate deadline loop.
    timers[found].armed = false;
    context = 2;
    ++high_callbacks;
    struct pbl_delayed_work *item = timers[found].item;
    item->work.fn(&item->work);
    assert(!depth);
  }
  context = 1;
}
static void drain(void) {
  assert(work);
  void (*fn)(void *) = work;
  work = NULL;
  context = 1;
  fn(NULL);
  assert(!depth);
}
static void done(status_t status, uint16_t peak) {
  assert(completion);
  T5AISaradcCompletion fn = completion;
  completion = NULL;
  context = 3;
  fn(status, peak, NULL);
  context = 1;
  assert(!depth);
}
static void edge(bool level) {
  charge = level;
  assert(irq_cb);
  context = 4;
  irq_cb(NULL);
  context = 1;
  assert(!depth);
}
static void unavailable(void) {
  BatteryConstants out = {.v_mv = 123};
  assert(!battery_get_millivolts());
  assert(battery_get_constants(&out) < 0 && out.v_mv == 123 && !depth);
}
static void begin(void) {
  battery_init();
  unavailable();
  now += 30000;
  tick();
  drain();
  assert(!battery_get_millivolts() && requests == 1);
}
