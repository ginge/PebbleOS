#!/bin/sh
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
os_dir=/home/baz/coredevices/pebbleos
sdk_dir=$(mktemp -d /tmp/live-fps-sdk.XXXXXX)
trap 'rm -rf "$sdk_dir"' EXIT HUP INT TERM
export PATH="/home/baz/pebbleos-sdk-0.1.8/arm-none-eabi/bin:$PATH"
export PYTHONPATH="$os_dir/.venv/lib/python3.12/site-packages${PYTHONPATH:+:$PYTHONPATH}"
mkdir -p "$sdk_dir/sdk/gabbro"
cp -a "$os_dir/build-pixel-fast/sdk/gabbro/include" "$sdk_dir/sdk/gabbro/"
cp -a "$os_dir/build-pixel-fast/sdk/gabbro/lib" "$sdk_dir/sdk/gabbro/"
/usr/bin/python3.12 "$os_dir/tools/cmake/sdk.py" \
  --output "$sdk_dir/sdk" --platform gabbro --work-dir "$sdk_dir/waf-build"
cd "$project_dir"
/usr/bin/python3.12 "$sdk_dir/sdk/waf" configure clean build
