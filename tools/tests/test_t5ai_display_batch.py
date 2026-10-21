# SPDX-License-Identifier: Apache-2.0
"""Exercise actual batch packing functions with delayed DMA ownership."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def extract(source, name):
    match = re.search(r"static (?:bool|size_t) " + name + r"\(", source)
    start = match.start()
    begin = source.index("{", match.end())
    depth, end = 1, begin + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include HEADER
#define PBL_DISPLAY_HEIGHT 260u
#define PANEL_HEIGHT 466u
#define PANEL_ROW_BYTES 932u
#define PANEL_DMA_BUFFER_BYTES (1864u * CONFIG_T5AI_DISPLAY_BATCH_ROWS)
#define PBL_ASSERTN assert
#define TRANSFER_BEGIN(x)
#define TRANSFER_END(x,y)
static uint8_t s_dma_rows[2][PANEL_DMA_BUFFER_BYTES], s_panel_row[PANEL_ROW_BYTES];
static unsigned s_dma_buffer, s_batch_rows, submits;
static size_t s_batch_bytes,s_dma_pending_bytes;
static const uint8_t *pending;
static size_t pending_length, output_length;
static uint8_t output[466*932];
static bool fail;
static void prv_convert_row(uint8_t *d, const uint8_t *s, bool rev, bool inv) {
  (void)rev;(void)inv;memset(d,*s,PANEL_ROW_BYTES);
}
static size_t prv_pixel_stream_write(const uint8_t *d,size_t n,size_t a) {
  memcpy(output+output_length,d,n);output_length+=n;return a+n/4;
}
static void prv_dcache_clean(void *d,size_t n) {(void)d;(void)n;}
static bool prv_pixel_stream_finish_dma(size_t *a,size_t n) {
  if(!pending) return true;
  assert(n==pending_length);
  if(fail) {pending=NULL;return false;}
  *a=prv_pixel_stream_write(pending,n,*a);pending=NULL;return true;
}
static void prv_pixel_stream_start_dma(const uint8_t *d,size_t n) {
  assert(!pending);pending=d;pending_length=n;++submits;
}
"""
MAIN = r"""
static void run(unsigned first,unsigned last,bool dma) {
 s_dma_buffer=s_batch_rows=submits=0;s_batch_bytes=s_dma_pending_bytes=0;
 pending=NULL;output_length=0;size_t aperture=0;
 for(unsigned y=first;y<last;++y) {uint8_t src=y;assert(prv_stream_logical_row(y,&src,&aperture,dma,false,false));}
 assert(prv_finish_row_dma(&aperture));assert(!pending && !s_batch_bytes);
 size_t pos=0;
 for(unsigned y=first;y<last;++y) {
  unsigned a=(y*466+259)/260,b=((y+1)*466+259)/260;
  for(unsigned i=0;i<(b-a)*932;++i)assert(output[pos++]==(uint8_t)y);
 }
 assert(pos==output_length);assert(aperture*4==pos);
 assert(submits==(dma?(last-first+CONFIG_T5AI_DISPLAY_BATCH_ROWS-1)/CONFIG_T5AI_DISPLAY_BATCH_ROWS:0));
}
int main(void) {
 run(0,260,true);run(17,32,true);run(259,260,true);run(111,113,true);run(0,260,false);
 // Fail the previous DMA after preparing the next batch; no owned buffer can be reused.
 size_t a=0;uint8_t src=42;pending=s_dma_rows[1];pending_length=s_dma_pending_bytes=932;fail=true;
 for(unsigned i=0;i+1<CONFIG_T5AI_DISPLAY_BATCH_ROWS;++i)assert(prv_stream_logical_row(i,&src,&a,true,false,false));
 assert(!prv_stream_logical_row(CONFIG_T5AI_DISPLAY_BATCH_ROWS-1,&src,&a,true,false,false));
 assert(!pending && !s_batch_bytes && !s_batch_rows);
 return 0;
}
"""


class BatchTest(unittest.TestCase):
    def test_packing_ownership_partial_and_failure(self):
        source = (ROOT / "drivers/display/t5ai/display_co5300.c").read_text()
        body = "\n".join(
            extract(source, n)
            for n in [
                "prv_flush_dma_batch",
                "prv_finish_row_dma",
                "prv_stream_logical_row",
            ]
        )
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "test.c").write_text(PREFIX + body + MAIN)
            for rows in [1, 2, 3]:
                subprocess.run(
                    [
                        "cc",
                        "-O2",
                        "-I",
                        str(ROOT / "include"),
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-fsanitize=undefined,bounds",
                        "-fno-sanitize-recover=all",
                        f"-DCONFIG_T5AI_DISPLAY_BATCH_ROWS={rows}",
                        '-DHEADER="'
                        + str(ROOT / "drivers/display/t5ai/row_memory.h")
                        + '"',
                        str(d / "test.c"),
                        "-o",
                        str(d / "test"),
                    ],
                    check=True,
                )
                subprocess.run([str(d / "test")], check=True)
