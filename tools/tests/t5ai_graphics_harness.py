# SPDX-License-Identifier: Apache-2.0
"""Compile complete graphics units against production headers and board config."""

import subprocess
import sys
import tempfile
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]


def run_graphics(
    fixture,
    local_source,
    benchmark=False,
    defines=(),
    stock_pixel=False,
    text_cache=False,
    core_stub=False,
    config_values=None,
):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        config = load_config("unhygienix")
        config.syms["LOG_HASHED"].set_value("n")
        config.syms["T5AI_DRAW_PROFILE"].set_value("n")
        for name, value in (config_values or {}).items():
            config.syms[name].set_value(value)
            if config.syms[name].str_value != value:
                raise ValueError(
                    f"Graphics test configuration did not apply {name}={value}"
                )
        config.write_autoconf(str(path / "config.h"))
        (path / "applib").mkdir()
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools/cmake/generate.py"),
                "applib-malloc",
                "--input",
                str(ROOT / "pebble/applib/applib_malloc.json"),
                "--header",
                str(path / "applib/applib_malloc.auto.h"),
                "--impl",
                str(path / "malloc.c"),
                "--min-sdk",
                "0",
                "--no-size-checks",
            ],
            check=True,
            cwd=ROOT,
        )

        # These generated font resource IDs are not used by bitmap operations.
        (path / "font_resource_keys.auto.h").write_text(
            "/* No font resources used. */\n"
        )
        includes = [
            path,
            ROOT,
            ROOT / "include",
            ROOT / "include/pbl",
            ROOT / "pebble",
            ROOT / "subsys",
            ROOT / "kernel",
            ROOT / "kernel/arch",
            ROOT / "kernel/arch/posix/include",
            ROOT / "third_party/upng/upng",
        ]
        flags = [
            "clang-21",
            "-std=gnu23",
            "-O2",
            "-g",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Wno-unknown-attributes",
            "-ffunction-sections",
            "-fdata-sections",
            "-fno-pie",
            "-include",
            str(path / "config.h"),
            *[arg for inc in includes for arg in ("-I", str(inc))],
        ]
        if not benchmark:
            flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
        units = [
            ("tools/tests/fixtures/t5ai_graphics/support.c", []),
            ("pebble/applib/graphics/8_bit/bitblt_private.c", []),
            ("pebble/applib/graphics/bitblt.c", []),
            ("pebble/applib/graphics/gbitmap.c", []),
            ("pebble/applib/graphics/gtypes.c", []),
            ("pebble/applib/graphics/gcolor_definitions.c", []),
            ("kernel/tick.c", []),
            (
                fixture,
                [
                    *(["-DBENCHMARK"] if benchmark else []),
                    *["-D" + value for value in defines],
                ],
            ),
        ]
        if local_source:
            units.insert(0, (local_source, []))
        if stock_pixel:
            units.append(
                (
                    "pebble/applib/graphics/graphics_private.c",
                    [
                        "-Dgraphics_private_plot_pixel=t5ai_stock_graphics_private_plot_pixel"
                    ],
                )
            )
        if text_cache or core_stub:
            core = path / "pbl/soc/t5ai/core.h"
            core.parent.mkdir(parents=True)
            core.write_text(
                "unsigned test_control(void);\n#define __get_CONTROL() test_control()\n"
                "unsigned test_ipsr(void);\n#define __get_IPSR() test_ipsr()\n"
            )
        if text_cache:
            flags += ["-I", str(ROOT / "tests/stubs")]
            units += [
                (source, [])
                for source in (
                    "pebble/applib/graphics/text_layout.c",
                    "pebble/applib/graphics/utf8.c",
                    "pebble/applib/graphics/bidi.c",
                    "pebble/applib/graphics/arabic_shaping.c",
                    "pebble/applib/fonts/codepoint.c",
                    "tests/stubs/default/text_resources.c",
                    "lib/util/hash.c",
                    "lib/util/iterator.c",
                    "lib/util/utf8.c",
                )
            ]
        objects = []
        for index, (source, extra) in enumerate(units):
            obj = path / f"{index}.o"
            subprocess.run(
                [*flags, *extra, "-c", str(ROOT / source), "-o", str(obj)], check=True
            )
            objects.append(str(obj))
        exe = path / "test"
        subprocess.run(
            [*flags, "-no-pie", "-Wl,--gc-sections", *objects, "-o", str(exe)],
            check=True,
        )
        result = subprocess.run(
            [str(exe)], check=False, capture_output=True, text=True, timeout=30
        )
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        return result.stdout


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", choices=("compositor", "pixel", "text_cache"))
    parser.add_argument(
        "--benchmark",
        action="store_true",
        help="measure host CPU time without sanitizers; not device qualification",
    )
    args = parser.parse_args()
    sources = {
        "compositor": "compositor_fast.c",
        "pixel": "pixel_fast.c",
        "text_cache": "text_node_cache.c",
    }
    print(
        run_graphics(
            f"tools/tests/fixtures/t5ai_graphics/{args.path}.c",
            "soc/t5ai/" + sources[args.path],
            benchmark=args.benchmark,
            stock_pixel=args.path == "pixel",
            text_cache=args.path == "text_cache",
        ),
        end="",
    )
