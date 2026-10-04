# Pinned Tuya dependencies

`TuyaOpen-T5AI` is the validated vendor platform reference. `TuyaOpen` supplies
the optional peripheral reference build. Both are Git submodules pinned to the
revisions in `boards/unhygienix/tuya/sdk.lock.json`. AP firmware uses native PebbleOS
SoC and driver implementations. CP packaging uses a separately supplied,
hash-checked stock artifact; do not copy generated lab binaries into submodules.

See `docs/boards/unhygienix/tooling.md` for initialization and native build/flash
commands. Keep vendor trees pristine; local adapters belong outside submodules.
