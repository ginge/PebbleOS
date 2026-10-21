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
typedef struct {int x,y;} GPoint;typedef struct {int w,h;} GSize;
typedef struct {GPoint origin;GSize size;} GRect;
typedef unsigned GFont;typedef int GTextOverflowMode;typedef int GTextAlignment;
typedef struct {uint32_t hash;GRect box;GFont font;int overflow_mode,alignment;GSize max_used_size;} TextLayout;
typedef struct {uintptr_t impl;int inset,origin,page;} TextLayoutFlowData;
typedef struct {TextLayout base;int16_t line_spacing_delta;TextLayoutFlowData flow_data;} TextLayoutExtended;
typedef TextLayout *GTextLayoutCacheRef;typedef struct {int x;} Window;typedef Window GContext;
struct pbl_thread {int id;};static struct pbl_thread app,other,*pbl_cur=&app;
static int control,task,calls;enum {PebbleTask_App=3};
static int __get_CONTROL(void) {return control;}static int pebble_task_get_current(void) {return task;}
#define PBL_LOG_MODULE_DEFINE(...)
#define LOG_LEVEL_DEBUG 0
static GSize graphics_text_layout_get_max_used_size(GContext *ctx,const char *text,GFont font,GRect box,int overflow,int align,TextLayout *l) {
 (void)ctx;unsigned hash=0;for(const char *s=text;*s;++s)hash=hash*31+(unsigned char)*s;
 if(l->hash!=hash || l->font!=font || memcmp(&l->box,&box,sizeof box) || l->overflow_mode!=overflow || l->alignment!=align) {
  ++calls;l->hash=hash;l->font=font;l->box=box;l->overflow_mode=overflow;l->alignment=align;
  l->max_used_size=(GSize){(int)strlen(text),box.size.h};
 }
 return l->max_used_size;
}
void t5ai_cache_stock_window_render(Window *w,GContext *ctx) {(void)w;(void)ctx;}
"""
MAIN = r"""
static void query(const char *text,unsigned font,GRect box,int overflow,int align,int spacing,int flow) {
 TextLayoutExtended e={.line_spacing_delta=spacing,.flow_data={.inset=flow}};
 GSize a=t5ai_text_node_cached_size(NULL,text,font,box,overflow,align,&e.base);
 assert(a.w==(int)strlen(text) && e.line_spacing_delta==spacing && e.flow_data.inset==flow);
}
int main(void) {
 GRect b={{0,0},{100,40}};task=3;
 memset(s_cache,0xaa,sizeof s_cache);window_render(NULL,NULL);s_active=true;
 query("hello",1,b,0,0,0,0);assert(calls==1);
 query("hello",1,b,0,0,0,0);assert(calls==1 && s_hits==1);
 query("world",1,b,0,0,0,0);assert(calls==2);
 query("hello",2,b,0,0,0,0);assert(calls==3);
 query("hello",1,b,1,0,0,0);assert(calls==4);
 query("hello",1,b,0,1,0,0);assert(calls==5);
 query("hello",1,b,0,0,1,0);assert(calls==6);
 query("hello",1,b,0,0,0,1);assert(calls==7);
 GRect c=b;c.origin.y=5;query("hello",1,c,0,0,0,0);assert(calls==8);
 window_render(NULL,NULL);s_active=true;query("hello",1,b,0,0,0,0);assert(calls==9);
 control=1;query("hello",1,b,0,0,0,0);assert(calls==10);control=0;
 pbl_cur=&other;query("hello",1,b,0,0,0,0);assert(calls==11);pbl_cur=&app;
 char longtext[100];memset(longtext,'x',99);longtext[99]=0;
 query(longtext,1,b,0,0,0,0);query(longtext,1,b,0,0,0,0);assert(calls==13 && s_bypass==2);
 return 0;
}
"""


class TextNodeCacheTest(unittest.TestCase):
    def test_keys_invalidation_ownership_and_extended_fields(self):
        source = (ROOT / "soc/t5ai/text_node_cache.c").read_text()
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
                    "-fsanitize=undefined,bounds",
                    "-fno-sanitize-recover=all",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
