# SPDX-License-Identifier: Apache-2.0
"""Compile production native driver code with modeled hardware endpoints."""

import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run(source, prefix, main, defines=(), transform=None):
    text = (ROOT / source).read_text()
    text = re.sub(r"^#include .*$", "", text, flags=re.MULTILINE)
    if transform is not None:
        text = transform(text)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "test.c").write_text(prefix + "\n" + text + "\n" + main)
        subprocess.run(
            [
                "cc",
                "-std=gnu2x",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-parameter",
                "-Wno-unused-function",
                "-Wno-unused-variable",
                "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all",
                "-fno-pie",
                "-no-pie",
                *["-D" + d for d in defines],
                str(path / "test.c"),
                "-o",
                str(path / "test"),
            ],
            check=True,
        )
        subprocess.run([str(path / "test")], check=True, timeout=30)


DISPLAY_PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define nullptr NULL
#define PBL_DISPLAY_WIDTH 260u
#define PBL_DISPLAY_HEIGHT 260u
#define T5AI_SHARED_ALIGNMENT 32u
#define T5AI_AON_RTC_HZ 32768u
#define PBL_FOREVER 0
#define PBL_LOG_MODULE_DEFINE(...)
struct pbl_mutex { bool held; };
struct pbl_mutex co5300_lock;
struct pbl_panel {int unused;};
struct pbl_spi_dev {int unused;};
static const struct {struct pbl_panel panel;struct pbl_spi_dev spi;} CO5300;
struct pbl_spi_xfer {const void *tx_buf;void *rx_buf;uint32_t len;uint8_t tx_nbits;};
struct pbl_display_state {bool rotated;};
struct pbl_display {struct pbl_display_state *state;uint16_t width,height;};
struct pbl_display_buffer {uint8_t *data;uint32_t pitch;};
struct pbl_display_ops {
 int (*init)(const struct pbl_display *);
 int (*enable)(const struct pbl_display *);
 int (*disable)(const struct pbl_display *);
 int (*set_rotated)(const struct pbl_display *,bool);
 int (*update)(const struct pbl_display *,const struct pbl_display_buffer *,uint16_t,uint16_t);
 int (*clear)(const struct pbl_display *);
};
static unsigned callbacks, transfers,fail_at,sleeps;
static bool fail_always;
static int completed_status;
static uint16_t y_first,y_last;
static uint8_t panel[466][932],pixels[260][264];
static void pbl_mutex_lock(struct pbl_mutex *m,int timeout){assert(!m->held);m->held=true;}
static void pbl_mutex_unlock(struct pbl_mutex *m){assert(m->held);m->held=false;}
static int pbl_panel_prepare(const struct pbl_panel *p){return 0;}
static int pbl_panel_enable(const struct pbl_panel *p){return 0;}
static int pbl_panel_disable(const struct pbl_panel *p){return 0;}
static void pbl_display_complete(const struct pbl_display *d,int status){assert(!co5300_lock.held);++callbacks;completed_status=status;}
static void pbl_sleep_ms(unsigned ms){assert(ms==1000);++sleeps;}
static uint64_t t5ai_rtc_get_raw_ticks(void){return 0;}
static int pbl_spi_transfer(const struct pbl_spi_dev *dev,const struct pbl_spi_xfer *xfers,size_t count){
 assert(co5300_lock.held && dev==&CO5300.spi);
 if(++transfers==fail_at || fail_always)return -EIO;
 const uint8_t *command=xfers[0].tx_buf;
 if(count==1){
  assert(xfers[0].len==8 && command[0]==2 && !command[1] && !command[3]);
  if(command[2]==0x2a)assert(command[4]==0 && command[5]==6 && command[6]==1 && command[7]==0xd7);
  else {assert(command[2]==0x2b);y_first=command[4]*256+command[5];y_last=command[6]*256+command[7];}
 }else{
  assert(count==2 && command[0]==0x32 && command[2]==0x2c && xfers[0].len==4);
  assert(xfers[1].tx_nbits==4 && y_first<=y_last && y_last<466);
  assert(!(y_first&1) && !((y_last-y_first+1)&1));
  assert(xfers[1].len==(unsigned)(y_last-y_first+1)*932);
  memcpy(panel[y_first],xfers[1].tx_buf,xfers[1].len);
 }
 return 0;
}
"""
DISPLAY_MAIN = r"""
static void verify(unsigned first,unsigned last,bool rotated) {
 memset(panel,0x5a,sizeof panel);callbacks=transfers=0;
 struct pbl_display_state state={.rotated=rotated};
 struct pbl_display disp={.state=&state,.width=260,.height=260};
 struct pbl_display_buffer buf={.data=&pixels[0][0],.pitch=264};
 assert(prv_update(&disp,&buf,first,last)==0 && callbacks==1 && completed_status==0);
 unsigned logical_first=rotated?259-last:first,logical_end=rotated?260-first:last+1;
 unsigned physical_first=((logical_first*466+259)/260)&~1u;
 unsigned physical_end=(((logical_end*466+259)/260)+1)&~1u;
 for(unsigned y=0;y<466;++y)for(unsigned x=0;x<466;++x){
  unsigned sy=y*260/466,sx=x*260/466;
  if(rotated){sy=259-sy;sx=259-sx;}
  if(y<physical_first || y>=physical_end){assert(panel[y][2*x]==0x5a && panel[y][2*x+1]==0x5a);continue;}
  unsigned c=pixels[sy][sx]&63;
  static const unsigned rb[]={0,10,21,31};
  unsigned rgb=(rb[c>>4]<<11)|(((c>>2)&3)*21<<5)|rb[c&3];
  assert(panel[y][2*x]==rgb>>8 && panel[y][2*x+1]==(rgb&255));
 }
 for(unsigned y=0;y<260;++y)for(unsigned x=0;x<264;++x)assert(pixels[y][x]==(uint8_t)(3*x+5*y));
}
int main(void){
 for(unsigned y=0;y<260;++y)for(unsigned x=0;x<264;++x)pixels[y][x]=3*x+5*y;
 prv_init_conversion_tables();
#if CASE == 0
 for(unsigned rot=0;rot<2;++rot){verify(0,259,rot);verify(17,32,rot);verify(259,259,rot);verify(0,0,rot);}
#elif CASE == 1
 struct pbl_display_state state={0};struct pbl_display disp={&state,260,260};
 struct pbl_display_buffer buf={.data=NULL,.pitch=264};
 assert(prv_update(&disp,&buf,0,1)==-EINVAL && !callbacks);
 buf.data=&pixels[0][0];buf.pitch=259;
 assert(prv_update(&disp,&buf,0,1)==-EINVAL && !callbacks);
 assert(prv_init(&disp)==0 && prv_enable(&disp)==0 && prv_disable(&disp)==0);
 assert(prv_clear(&disp)==0);for(unsigned i=0;i<sizeof panel;++i)assert(((uint8_t *)panel)[i]==0);
#elif CASE == 2
 fail_at=17;verify(0,259,true);assert(s_retries==1 && sleeps==1);
 fail_always=true;struct pbl_display_state state={0};struct pbl_display disp={&state,260,260};
 struct pbl_display_buffer buf={&pixels[0][0],264};callbacks=0;
 assert(prv_update(&disp,&buf,1,2)==0 && completed_status==-EIO && callbacks==1);
 assert(s_retries==3 && sleeps==3);
 callbacks=0;assert(prv_update(&disp,&buf,1,2)==0 && callbacks==1 && sleeps==3);
#elif CASE == 3
 verify(0,259,false);assert(transfers<=36*3);verify(30,35,true);assert(transfers==3);
#endif
 return 0;
}
"""
