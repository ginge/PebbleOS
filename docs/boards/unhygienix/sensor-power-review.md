# Unhygienix sensor power review — 31 October 2026

Source review only. No sensor register, sampling, shake, display or installed
firmware change was made during the Wi-Fi qualification. No sensor energy saving
or fitted-silicon WOM qualification is claimed.

## Reference WOM helper is incomplete for the documented C procedure

Pinned TuyaOpen board `qmi8658.c` has `qmi8658_enable_wake_on_motion()` and a
matching disable helper. Enable disables sensors, selects 2 g / low-power 21 Hz,
writes threshold to `0x0b`, writes zero to `0x0c`, then enables acceleration.
Neither helper issues a CTRL9 command or performs its completion handshake.
The helper name alone does not establish working WOM on this watch.

[QST QMI8658C Rev A](https://www.qstcorp.com/upload/pdf/202210/13-52-27%20QMI8658C%20Datasheet%20Rev%20A%20%281%29.pdf),
sections 12.2–12.6 (pp. 67–68), requires applying CAL1 settings through CTRL9
command `0x08` for entry and exit. CAL1_H selects the interrupt, initial polarity
and blanking interval; zero selects INT1. Reading STATUS1 acknowledges a WOM
event. This is a C-documentation comparison, not identification of fitted silicon.

Native `drivers/imu/qmi8658/qmi8658_t5ai.c` uses INT2 and an existing bounded
CTRL9 handshake for FIFO operations. Initialization reads WHO_AM_I and revision,
but a successful revision is logged only at DEBUG. The frozen power AP has no
new revision/interrupt evidence. Its current software shake needs every sample:
watermark one and at most 32 ms sampling, with existing sensitivity/holdoff.
Do not silently replace that gesture contract with a low-power WOM threshold.

## Required next work

1. Record fitted WHO_AM_I/revision and verify the corresponding command/status
   behavior in a separate qualification image or read-only diagnostic window.
   The older accel-reset artifact explicitly lacked independent live-sensor
   confirmation; a reset without an init error does not fill this gap.
2. Measure motion-backlight on/off IRQ, watchdog, I2C time and completed gestures
   with identical display settings. Existing opt-in QMI/I2C profiling can count
   these, but its logging/build changes need identical control/candidate setup.
3. Implement WOM entry/exit locally in the native driver, using the qualified
   command handshake, INT2 route and real acknowledgment behavior. Balance
   subscriber/rate changes, cancellation, failure recovery and return to normal
   FIFO acquisition. Preserve requested high-rate clients and gyro-off policy.
4. Qualify thresholds against shake sensitivity/latency and false positives,
   static/no-motion, repeated wake/rearm, concurrent touch and I2C errors. Compare
   energy per completed gesture as well as idle. Keep 100 kHz and current FIFO
   bounds until shared-bus timing and overflow behavior pass separately.

WOM interrupt behavior must then join the complete suspend/wake coordinator;
working sensor interrupts alone do not permit removing the application sleep veto.
