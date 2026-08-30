# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

from tools.check_t5ai_image_map import check_map


def wafrule(task):
    check_map(Path(task.generator.bld.bldnode.make_node("pebbleos.map").abspath()))
    task.outputs[0].write("T5AI image layout validated\n")
