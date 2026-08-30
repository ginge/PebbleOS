# SPDX-License-Identifier: Apache-2.0

from tools.t5ai_image import encode_protected_blocks, verify_protected_blocks


def wafrule(task):
    encoded = encode_protected_blocks(task.inputs[0].read("rb"))
    verify_protected_blocks(encoded)
    task.outputs[0].write(encoded, "wb")
