# `pbl build`

Build the configured firmware, or the named CMake targets.

```shell
pbl build
pbl build sdk
```

The build directory must have been set up with {doc}`configure` first. The
outputs land in it, named after the variant (`normal` or `prf`); see
{doc}`../development/build_system/index` for how the build is put together.

## Options

```{pbl-command} build
```

## Unhygienix

Unhygienix ("The village fishmonger") is the independent Tuya T5AI board:

```shell
./pbl configure --board unhygienix
./pbl build
```

It retains the Gabbro app platform and Round 2 companion identity. Use its
{doc}`board tooling <../boards/unhygienix/tooling>` for exact-range `tyutool`
flashing and CP packaging. The generic recovery imaging and Bluetooth/PBZ
firmware update instructions do not apply to Unhygienix.
