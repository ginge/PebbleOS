# SPDX-License-Identifier: Apache-2.0
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
typedef struct {int16_t x,y;} GPoint;
typedef struct {int16_t w,h;} GSize;
typedef struct {GPoint origin;GSize size;} GRect;
typedef int GCompOp;typedef int GColor;
enum {GCompOpAssign=0,GBitmapFormat8Bit=1,GBitmapFormat8BitCircular=2};
typedef struct {uint8_t *data;int min_x,max_x;} GBitmapDataRowInfo;
typedef struct {uint8_t *addr;struct {int format;} info;GRect bounds;unsigned pad;bool legacy;} GBitmap;
static unsigned stock;
static int gbitmap_get_format(const GBitmap *b) {return b->legacy?9:b->info.format;}
static GBitmapDataRowInfo gbitmap_get_data_row_info(const GBitmap *b,unsigned y) {
 unsigned first=b->info.format==2?(y%81):0,last=259-first;
 return (GBitmapDataRowInfo){b->addr+y*(260+b->pad),first,last};
}
void t5ai_stock_bitblt_bitmap_into_bitmap_tiled(GBitmap *d,const GBitmap *s,GRect r,GPoint p,GCompOp m,GColor c) {
 (void)d;(void)s;(void)r;(void)p;(void)m;(void)c;++stock;
}
"""
MAIN = r"""
int main(void) {
 static uint8_t src[70000],dst[70000],expected[70000];
 for(unsigned i=0;i<sizeof src;++i)src[i]=(i*137u)^31u;
 GRect rect={{0,0},{260,260}};GPoint zero={0,0};
 for(unsigned sf=1;sf<=2;++sf)for(unsigned df=1;df<=2;++df)for(unsigned pad=0;pad<4;++pad) {
  GBitmap a={src,{sf},rect,pad,0},b={dst,{df},rect,pad,0};
  memset(dst,0xa5,sizeof dst);memcpy(expected,dst,sizeof dst);
  for(unsigned y=0;y<260;++y) {
   GBitmapDataRowInfo s=gbitmap_get_data_row_info(&a,y),d=gbitmap_get_data_row_info(&b,y);
   for(int x=d.min_x;x<=d.max_x;++x)if(x>=s.min_x && x<=s.max_x)
    expected[(d.data-dst)+x]=s.data[x];
  }
  stock=0;bitblt_bitmap_into_bitmap_tiled(&b,&a,rect,zero,0,0);
  assert(stock==0 && !memcmp(dst,expected,sizeof dst));
  a.legacy=true;bitblt_bitmap_into_bitmap_tiled(&b,&a,rect,zero,0,0);assert(stock==1);a.legacy=false;
  bitblt_bitmap_into_bitmap_tiled(&b,&a,rect,(GPoint){1,0},0,0);assert(stock==2);
  bitblt_bitmap_into_bitmap_tiled(&b,&a,rect,zero,1,0);assert(stock==3);
  bitblt_bitmap_into_bitmap_tiled(&a,&a,rect,zero,0,0);assert(stock==4);
  GRect partial=rect;partial.size.h=259;bitblt_bitmap_into_bitmap_tiled(&b,&a,partial,zero,0,0);assert(stock==5);
 }
 return 0;
}
"""


class NativeCopyTest(unittest.TestCase):
    def test_edges_padding_overlap_and_fallback(self):
        source = (ROOT / "soc/t5ai/compositor_fast.c").read_text()
        source = re.sub(r"^#include[^\n]*\n", "", source, flags=re.MULTILINE)
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "test.c").write_text(PREFIX + source + MAIN)
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
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
