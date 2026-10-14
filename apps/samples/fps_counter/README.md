# Live FPS

Minimal Gabbro watchapp for the Unhygienix watch. Displays app draw throughput
to one decimal place, sampled over approximately one second. The first sample
shows `--.-`. Back exits normally; measurement pauses when another window
takes focus and restarts on return.

Version 1.1 adds a CPU-intensive burst every five seconds while focused. Each
burst runs integer mixing and multiplication for approximately 750 ms on the
app thread, intentionally stalling its draws. A volatile checksum preserves
the computation. App logs report duration, batch count and checksum for each
burst. The workload timer is cancelled on focus loss and exit.

Each completed draw schedules the next dirty request with a 1 ms app timer.
Frames are counted in the draw callback, not the timer. A single pixel changes
each frame to prevent unchanged screen content from suppressing display work.
This measures this app's rendering throughput, not global OS FPS or the LCD's
physical scan frequency. It continuously drives rendering while open.

Build on this machine with the local PebbleOS SDK headers and ARM toolchain:

```sh
sh build.sh
```

The script assembles SDK tools in a temporary directory and uses the Gabbro
headers and library from `build-pixel-fast`. It does not alter firmware sources.

The resulting `build/fps_counter.pbw` can be sideloaded using the Pebble phone
app or an existing Pebble app-upload connection. No JavaScript, resources,
phone permissions, or companion app are required.
