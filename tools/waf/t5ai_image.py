# SPDX-License-Identifier: Apache-2.0

from tools.t5ai_image import (
    encode_pebble_application,
    validate_pinned_ap_layout,
    verify_protected_blocks,
)


def wafrule(task):
    validate_pinned_ap_layout()
    encoded = encode_pebble_application(task.inputs[0].read("rb"))
    verify_protected_blocks(encoded)
    task.outputs[0].write(encoded, "wb")
