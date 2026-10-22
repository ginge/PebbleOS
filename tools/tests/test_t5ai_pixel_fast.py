# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Compare the T5 pixel path against unchanged upstream C implementations."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STUB = r"""
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define PBL_COLOR 1
#define MAX_PLOT_BRIGHTNESS 3
#define RGB_LOOKUP_TABLE_SIZE 64
#define WITHIN(v,lo,hi) ((v)>=(lo) && (v)<=(hi))
#define GPoint(x,y) ((GPoint){(x),(y)})
#define pbl_panic(...) __builtin_abort()
typedef struct {int16_t x,y;} GPoint;
typedef struct {int16_t w,h;} GSize;
typedef struct {GPoint origin; GSize size;} GRect;
typedef union {uint8_t argb; struct {uint8_t b:2,g:2,r:2,a:2;};} GColor8;
typedef GColor8 GColor;
typedef enum {GBitmapFormat1Bit,GBitmapFormat8Bit,GBitmapFormat1BitPalette,
 GBitmapFormat2BitPalette,GBitmapFormat4BitPalette,GBitmapFormat8BitCircular} GBitmapFormat;
typedef struct {uint32_t offset;uint16_t min_x,max_x;} GBitmapDataRowInfoInternal;
typedef struct {uint8_t *data;int16_t min_x,max_x;} GBitmapDataRowInfo;
typedef struct {void *addr;uint16_t row_size_bytes;
 struct {bool is_bitmap_heap_allocated:1; GBitmapFormat format:3;
 bool is_palette_heap_allocated:1; uint16_t reserved:7; uint8_t version:4;} info;GRect bounds;
 const GBitmapDataRowInfoInternal *data_row_infos;} GBitmap;
static inline int16_t grect_get_max_x(const GRect *r) {return r->origin.x+r->size.w;}
static inline int16_t grect_get_max_y(const GRect *r) {return r->origin.y+r->size.h;}
GColor8 gcolor_alpha_blend(GColor8,GColor8);
"""
HARNESS = r"""
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "stock.c"
#include SOURCE
static uint8_t expected[128],actual[128];
static GBitmapDataRowInfoInternal rows[8];
static GBitmap bitmap={.row_size_bytes=16,.bounds={{0,0},{12,8}},.data_row_infos=rows};
static void check(GRect clip,int x,int y,uint16_t opacity,uint8_t color,uint8_t dest) {
 memset(expected,dest,sizeof(expected));memcpy(actual,expected,sizeof(actual));
 bitmap.addr=expected;
 t5ai_stock_graphics_private_plot_pixel(&bitmap,&clip,x,y,opacity,(GColor){.argb=color});
 bitmap.addr=actual;
 graphics_private_plot_pixel(&bitmap,&clip,x,y,opacity,(GColor){.argb=color});
 if(memcmp(actual,expected,sizeof(actual))) {
  fprintf(stderr,"mismatch format=%u x=%d y=%d opacity=%u color=%u dest=%u\n",
          bitmap.info.format,x,y,opacity,color,dest);__builtin_abort();
 }
}
static uint32_t state=42;
static uint32_t random_value(void) {state=state*1664525u+1013904223u;return state;}
int main(void) {
 GRect full={{0,0},{16,8}};
 for(unsigned i=0;i<8;++i)rows[i]=(GBitmapDataRowInfoInternal){i*16,i%4,15-i%4};
 for(unsigned fmt=0;fmt<6;++fmt) {
  bitmap.info.format=fmt;
  for(unsigned c=0;c<256;++c)for(unsigned d=0;d<256;++d)for(unsigned a=0;a<4;++a)
   check(full,6,3,a,c,d);
  for(unsigned a=0;a<65536;++a)check(full,6,3,a,0x91,0x67);
  for(int x=-3;x<20;++x)for(int y=-3;y<11;++y)for(unsigned a=0;a<4;++a) {
   check(full,x,y,a,0x56,0x89);
   check((GRect){{12,7},{-10,-6}},x,y,a,0x56,0x89);
   check((GRect){{2,1},{0,6}},x,y,a,0x56,0x89);
  }
  for(unsigned i=0;i<100000;++i) {
   // Rows remain valid whenever the clip admits a point.
   int x=(int)(random_value()%25)-4,y=random_value()%8;
   GRect clip={{(int16_t)(random_value()%20-3),0},
               {(int16_t)(random_value()%31-15),8}};
   bitmap.bounds.origin.x=random_value()%4;
   bitmap.bounds.size.w=random_value()%12;
   check(clip,x,y,random_value(),random_value(),random_value());
  }
  bitmap.bounds=(GRect){{0,0},{12,8}};
  // Clip endpoint overflow and int-to-GPoint narrowing use the stock fallback.
  check((GRect){{32760,0},{20,8}},0,3,0,0x23,0x47);
  check(full,65542,3,0,0x23,0x47);
  check(full,6,65539,0,0x23,0x47);
  if(fmt==GBitmapFormat8BitCircular) {
   rows[3].min_x=32768;check(full,6,3,1,0x23,0x47);
   rows[3].max_x=65535;check(full,6,3,1,0x23,0x47);
   rows[3]=(GBitmapDataRowInfoInternal){48,3,12};
  }
 }
 // Exercise real display-sized storage, including offsets beyond 64 KiB.
 static uint8_t large_expected[260*260],large_actual[260*260];
 static GBitmapDataRowInfoInternal large_rows[260];
 GRect screen={{0,0},{260,260}};
 bitmap.bounds=screen;bitmap.row_size_bytes=260;bitmap.data_row_infos=large_rows;
 for(unsigned y=0;y<260;++y)large_rows[y]=(GBitmapDataRowInfoInternal){y*260,y%40,259-y%40};
 for(unsigned format=0;format<2;++format) {
  bitmap.info.format=format?GBitmapFormat8BitCircular:GBitmapFormat8Bit;
  for(unsigned i=0;i<sizeof(large_expected);++i)large_expected[i]=i;
  memcpy(large_actual,large_expected,sizeof(large_actual));
  for(unsigned i=0;i<100000;++i) {
   int x=(int)(random_value()%270)-5,y=random_value()%260;
   uint16_t opacity=random_value();GColor color={.argb=random_value()};
   bitmap.addr=large_expected;
   t5ai_stock_graphics_private_plot_pixel(&bitmap,&screen,x,y,opacity,color);
   bitmap.addr=large_actual;
   graphics_private_plot_pixel(&bitmap,&screen,x,y,opacity,color);
  }
  assert(!memcmp(large_expected,large_actual,sizeof(large_actual)));
 }
 puts("pixel equivalence: all formats, colour pairs, alpha values and geometry passed");
 return 0;
}
"""


def function(source, name):
    match = re.search(
        r"(?:bool|GColor8|GBitmapDataRowInfo|void)\s+" + name + r"\s*\(", source
    )
    start = match.start()
    begin = source.index("{", match.end())
    depth = 1
    end = begin + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class PixelFastTest(unittest.TestCase):
    def test_stock_equivalence(self):
        types = (ROOT / "fw/applib/graphics/gtypes.c").read_text()
        bitmap = (ROOT / "fw/applib/graphics/gbitmap.c").read_text()
        graphics = (ROOT / "fw/applib/graphics/graphics_private.c").read_text()
        table = re.search(
            r"static const uint8_t s_blending_lookup_33_percent\[.*?\n};",
            types,
            re.DOTALL,
        ).group()
        stock = "\n".join(
            [
                '#include "applib/graphics/graphics_private.h"',
                table,
                function(types, "grect_contains_point"),
                function(types, "gcolor_blend"),
                function(types, "gcolor_alpha_blend"),
                function(bitmap, "prv_gbitmap_get_data_row_info").replace(
                    "prv_gbitmap_get_data_row_info", "gbitmap_get_data_row_info"
                ),
                function(graphics, "graphics_private_plot_pixel").replace(
                    "graphics_private_plot_pixel",
                    "t5ai_stock_graphics_private_plot_pixel",
                ),
            ]
        )
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            header = d / "applib/graphics/graphics_private.h"
            header.parent.mkdir(parents=True)
            header.write_text(STUB)
            (d / "stock.c").write_text(stock)
            (d / "test.c").write_text(HARNESS)
            executable = d / "test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined,bounds",
                    "-fno-sanitize-recover=all",
                    '-DSOURCE="' + str(ROOT / "soc/t5ai/pixel_fast.c") + '"',
                    "-I",
                    str(d),
                    str(d / "test.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
