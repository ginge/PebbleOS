/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "protobuf_log_test_helpers.h"

#include "pbl/services/protobuf_log/protobuf_log_private.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TMP_FILE "tmp_protoc_bytes"
#ifndef PEBBLE_SOURCE_ROOT
#error "Define PEBBLE_SOURCE_ROOT to the repository root"
#endif
#define PROTO_PATH "/src/idl/nanopb"
#define PROTOC_PATH "protoc"

void protobuf_log_test_parse_protoc(uint8_t *msg) {
  PLogMessageHdr *hdr = (PLogMessageHdr *)msg;
  FILE *file = fopen(TMP_FILE, "wb");
  fwrite(msg + sizeof(*hdr), 1, hdr->msg_size, file);
  fclose(file);

  system(PROTOC_PATH " --proto_path=\"" PEBBLE_SOURCE_ROOT PROTO_PATH
         "\" --decode=pebble.pipeline.Payload \"" PEBBLE_SOURCE_ROOT PROTO_PATH
         "/payload.proto\" < " TMP_FILE " 2>&1");
}
