# SPDX-License-Identifier: Apache-2.0
"""Compare the actual rotated transport against the existing pixel/scale contract."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pbl/soc/t5ai/row_memory.h>
#define PBL_DISPLAY_WIDTH 260u
#define PBL_DISPLAY_HEIGHT 260u
#define PANEL_WIDTH 466u
#define PANEL_HEIGHT 466u
#define PANEL_ROW_BYTES 932u
#define PANEL_X_OFFSET 0u
#define PANEL_DMA_BUFFER_BYTES (1864u*CONFIG_T5AI_DISPLAY_BATCH_ROWS)
#define DISPLAY_COLOR_COUNT 64u
#define DISPLAY_COLOR_MASK 63u
#define DISPLAY_COLOR_RED_SHIFT 4u
#define DISPLAY_COLOR_GREEN_SHIFT 2u
#define DISPLAY_COLOR_COMPONENT_MASK 3u
#define DISPLAY_RGB565_GREEN_STEP 21u
#define DISPLAY_RGB565_RED_SHIFT 11u
#define DISPLAY_RGB565_GREEN_SHIFT 5u
#define TRANSFER_BEGIN(name)
#define TRANSFER_END(field,name)
#define PBL_FOREVER -1
#define PBL_ASSERTN assert
#define PBL_LOG_WRN(...) do { assert(!s_panel_mutex); } while (0)
typedef struct {uint16_t address;uint8_t *data;} DisplayRow;
typedef bool (*NextRowCallback)(DisplayRow*);
typedef void (*UpdateCompleteCallback)(void);
static bool s_initialized=true,s_updating,s_rotated,s_dma_display_failed;
static bool s_dma_display_enabled=true;
static int s_panel_mutex;
static uint16_t s_rgb565[64],s_panel_source_x[466];
static uint8_t s_panel_row[932] __attribute__((aligned(32)));
static uint8_t s_dma_rows[2][PANEL_DMA_BUFFER_BYTES] __attribute__((aligned(32)));
static unsigned s_dma_buffer,s_batch_rows;
static size_t s_batch_bytes,s_dma_pending_bytes;
#ifdef CONFIG_T5AI_DISPLAY_ROTATED_DMA
static DisplayRow s_rotated_rows[260];
static unsigned s_rotated_count,s_rotated_next;
static bool s_rotated_reverse;
#endif
static uint8_t panel[466*932],pixels[260][260],alternate[260];
static const uint8_t *pending;
static size_t pending_length,cursor,window_start;
static uint8_t pending_guard[6*932];
static unsigned submits,commands,callbacks;
static bool queued;
static void pbl_mutex_lock(int *m,int timeout){(void)timeout;assert(!*m);*m=1;}
static void pbl_mutex_unlock(int *m){assert(*m);*m=0;}
static void prv_apply_brightness(void){assert(s_panel_mutex);}
static uint16_t prv_panel_row_start(uint16_t y){return (y*466u+259u)/260u;}
static bool prv_set_window(unsigned x0,unsigned y0,unsigned x1,unsigned y1){
 assert(!pending && x0==0 && x1==465 && y0<=y1 && y1<466);window_start=cursor=y0*932;++commands;return true;
}
static bool prv_pixel_stream_begin(void){cursor=window_start;return true;}
static void prv_pixel_stream_end(void){assert(!pending);}
static size_t prv_pixel_stream_write(const uint8_t *data,size_t length,size_t aperture){
 assert(cursor+length<=sizeof panel);memcpy(panel+cursor,data,length);cursor+=length;return aperture+length/4;
}
static void prv_dcache_clean(void *data,size_t length){(void)data;(void)length;}
static void prv_pixel_stream_start_dma(const uint8_t *data,size_t length){
 assert(!pending && length<=sizeof pending_guard);pending=data;pending_length=length;
 memcpy(pending_guard,data,length);++submits;
}
static bool prv_pixel_stream_finish_dma(size_t *aperture,size_t length){
 if(!pending)return true;
 assert(length==pending_length && !memcmp(pending,pending_guard,length));
 *aperture=prv_pixel_stream_write(pending,length,*aperture);pending=NULL;return true;
}
"""
MAIN = r"""
static DisplayRow rows[260];static unsigned count,next;
static bool source(DisplayRow *row){assert(s_updating && s_panel_mutex);if(next==count)return false;*row=rows[next++];return true;}
static void done(void){assert(s_updating==queued && !s_panel_mutex);++callbacks;}
static void run(bool rotated,bool dma,bool invert,bool asynchronous,unsigned which){
 s_rotated=rotated;s_updating=queued=asynchronous;s_dma_display_failed=false;
 s_dma_buffer=s_batch_rows=0;s_batch_bytes=s_dma_pending_bytes=0;
 pending=NULL;cursor=submits=commands=callbacks=next=count=0;
 memset(panel,0x5a,sizeof panel);const uint8_t *expected[260]={0};
 if(which==0){for(unsigned y=0;y<260;++y)rows[count++]=(DisplayRow){y,pixels[y]};}
 if(which==1){for(unsigned y=17;y<33;++y)rows[count++]=(DisplayRow){y,pixels[y]};}
 if(which==2){const unsigned ys[]={0,1,3,7,9,11,259};for(unsigned i=0;i<7;++i)rows[count++]=(DisplayRow){ys[i],pixels[ys[i]]};}
 if(which==3){rows[count++]=(DisplayRow){3,pixels[3]};rows[count++]=(DisplayRow){1,pixels[1]};rows[count++]=(DisplayRow){3,alternate};rows[count++]=(DisplayRow){0,pixels[0]};}
 for(unsigned i=0;i<count;++i)expected[rows[i].address]=rows[i].data;
 prv_display_update(source,done,dma,invert,asynchronous);
 assert(callbacks==1 && !pending && !s_dma_display_failed && !s_panel_mutex);
 if(!dma)assert(!submits);
#ifndef CONFIG_T5AI_DISPLAY_ROTATED_DMA
 if(rotated)assert(!submits);
#endif
 for(unsigned py=0;py<466;++py){
  unsigned sy=py*260/466;if(rotated)sy=259-sy;
  for(unsigned px=0;px<466;++px){
   unsigned offset=py*932+2*px;
   if(!expected[sy]){assert(panel[offset]==0x5a && panel[offset+1]==0x5a);continue;}
   unsigned sx=px*260/466;if(rotated)sx=259-sx;
   unsigned c=expected[sy][sx]&63;if(invert)c^=63;
   static const uint8_t rb[4]={0,10,21,31};
   unsigned rgb=(rb[c>>4]<<11)|(((c>>2)&3)*21<<5)|rb[c&3];
   assert(panel[offset]==(rgb>>8) && panel[offset+1]==(rgb&255));
  }
 }
 for(unsigned y=0;y<260;++y)for(unsigned x=0;x<260;++x)assert(pixels[y][x]==(uint8_t)(x*3+y*5));
 s_updating=false;
}
int main(void){
 for(unsigned y=0;y<260;++y)for(unsigned x=0;x<260;++x)pixels[y][x]=x*3+y*5;
 for(unsigned x=0;x<260;++x)alternate[x]=pixels[3][x]^63;
 prv_init_conversion_tables();
 for(unsigned r=0;r<2;++r)for(unsigned d=0;d<2;++d)for(unsigned i=0;i<2;++i)
  for(unsigned q=0;q<2;++q)for(unsigned c=0;c<5;++c)run(r,d,i,q,c);
 return 0;
}
"""


class DisplayRotationTest(unittest.TestCase):
    def test_pixels_partial_gaps_duplicates_and_dma_ownership(self):
        source = (ROOT / "drivers/display/t5ai/display_co5300.c").read_text()

        def function(name):
            m = re.search(r"\b" + name + r"\(", source)
            start = source.rfind("\nstatic ", 0, m.start()) + 1
            begin = source.index("{", m.end())
            depth = 1
            end = begin + 1
            while depth:
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
            return source[start:end]

        names = [
            "prv_dma_failed",
            "prv_init_conversion_tables",
            "prv_convert_row",
            "prv_flush_dma_batch",
            "prv_finish_row_dma",
            "prv_send_logical_row",
            "prv_stream_logical_row",
            "prv_profile_next_row",
            "prv_display_update",
        ]
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            for rotated in [False, True]:
                extra = function("prv_rotated_next_row") if rotated else ""
                (p / "test.c").write_text(
                    PREFIX
                    + extra
                    + "\n".join(
                        (
                            "#ifdef CONFIG_T5AI_DISPLAY_ROTATED_DMA\n"
                            + function("prv_replay_dma_row")
                            + "\n#endif\n"
                            if n == "prv_stream_logical_row"
                            else ""
                        )
                        + function(n)
                        for n in names
                    )
                    + MAIN
                )
                for batch in [1, 2, 3]:
                    flags = ["-DCONFIG_T5AI_DISPLAY_ROTATED_DMA"] if rotated else []
                    subprocess.run(
                        [
                            "cc",
                            "-O2",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            "-I",
                            str(ROOT / "include"),
                            "-fsanitize=undefined,bounds",
                            "-fno-sanitize-recover=all",
                            f"-DCONFIG_T5AI_DISPLAY_BATCH_ROWS={batch}",
                            *flags,
                            str(p / "test.c"),
                            "-o",
                            str(p / "test"),
                        ],
                        check=True,
                    )
                    subprocess.run([str(p / "test")], check=True)
