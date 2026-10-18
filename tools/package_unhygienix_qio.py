#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

"""Create a boot/CP-preserving Unhygienix QIO image."""

import argparse
from pathlib import Path
import sys

if __package__:
    from tools.unhygienix_image import package_ap_qio
else:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from tools.unhygienix_image import package_ap_qio


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-qio", required=True, type=Path)
    parser.add_argument("--base-sha256", required=True)
    parser.add_argument("--protected-ap", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        image = package_ap_qio(
            args.base_qio.read_bytes(),
            args.protected_ap.read_bytes(),
            args.base_sha256,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(image)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
