# SPDX-License-Identifier: Apache-2.0
import unittest


class DisplayWorkerTest(unittest.TestCase):
    def test_ownership_context_and_control_order(self):
        import re
        import subprocess
        import tempfile
        from pathlib import Path

        s = (
            Path(__file__).resolve().parents[2]
            / "drivers/display/t5ai/display_co5300.c"
        ).read_text()

        def fn(name):
            m = re.search(
                r"^(?:static )?(?:bool|void) " + name + r"\([^;{]*\)\s*\{",
                s,
                re.MULTILINE,
            )
            a = s.index("{", m.start())
            b = a + 1
            d = 1
            while d:
                d += (s[b] == "{") - (s[b] == "}")
                b += 1
            return s[m.start() : b]

        prefix = r"""
        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include <stddef.h>
        #include <string.h>
        #include <setjmp.h>
        #define CONFIG_T5AI_DISPLAY_WORKER 1
#define T5_IO_BEGIN(name)
#define T5_IO_END(kind, name, amount) do {} while (0)
#define T5_IO_ADD(kind, elapsed, amount) do {} while (0)
        #define PBL_FOREVER -1
        #define PBL_ASSERTN assert
        #define PBL_DISPLAY_HEIGHT 260
        #define PANEL_DCS_BRIGHTNESS 0x51
        static bool s_updating,s_worker_ready=true,s_panel_mutex_ready=true,s_initialized=true;
        static bool s_brightness_dirty,s_enabled=true,s_dma_display_enabled=true,s_dma_display_failed;
        static uint8_t s_brightness;
        typedef struct {uint16_t address;uint8_t *data;} DisplayRow;
        typedef bool (*NextRowCallback)(DisplayRow*);typedef void (*UpdateCompleteCallback)(void);
        static DisplayRow s_worker_rows[260];
        static unsigned s_worker_count,s_worker_next;
        static UpdateCompleteCallback s_worker_complete;
        static int s_panel_mutex,s_display_gate=1,s_display_work,task,physical,complete_count,applied;
        static jmp_buf suspend;static int why;
        static void (*main_callback)(void*);static void *main_arg;
        static void pbl_mutex_lock(int *m,int timeout){(void)timeout;assert(!*m);*m=1;}
        static void pbl_mutex_unlock(int *m){assert(*m);*m=0;}
        static int pbl_sem_take(int *sem,int timeout) {
         (void)timeout;if(!*sem){why=sem==&s_display_gate?1:2;longjmp(suspend,1);}--*sem;return 0;
        }
        static void pbl_sem_give(int *sem){assert(!*sem);++*sem;}
        static void launcher_task_add_callback(void (*cb)(void*),void *arg){assert(task==1 && !main_callback);main_callback=cb;main_arg=arg;}
        static bool prv_register(unsigned reg,const uint8_t *data,unsigned size){assert(reg==0x51 && size==1 && s_panel_mutex);applied=*data;return true;}
        static void prv_display_set_enabled(bool enabled){assert(s_panel_mutex && !s_display_gate);s_enabled=enabled;}
        static unsigned first,last,next_row,read_count;static uint8_t pixels[260][2];
        static bool source(DisplayRow *row){assert(task==0);if(next_row==last)return false;*row=(DisplayRow){next_row,pixels[next_row]};++next_row;return true;}
        static void done(void){assert(task==0 && !s_updating && !s_panel_mutex && s_display_gate);++complete_count;}
        static void prv_display_update(NextRowCallback next,UpdateCompleteCallback cb,bool dma,bool invert,bool queued) {
         assert(task==(queued?1:0));assert(dma==s_dma_display_enabled && !invert);++physical;
         if(queued)assert(s_updating && !s_display_gate);else s_updating=true;
         pbl_mutex_lock(&s_panel_mutex,-1);DisplayRow r;unsigned y=first;
         while(next(&r)){assert(r.address==y && r.data==pixels[y] && r.data[0]==(uint8_t)y);++y;++read_count;}
         assert(y==last);pbl_mutex_unlock(&s_panel_mutex);
         if(!queued)s_updating=false;
         cb();
        }
        """
        functions = "\n".join(
            fn(n)
            for n in [
                "prv_dma_requested",
                "prv_panel_lock",
                "prv_panel_unlock",
                "prv_apply_brightness",
                "display_co5300_set_brightness",
                "display_set_enabled",
                "prv_worker_next_row",
                "prv_worker_complete_on_main",
                "prv_worker_finished",
                "prv_display_worker",
                "display_update",
            ]
        )
        main = r"""
        static void run(unsigned a,unsigned b,bool dma) {
         first=next_row=a;last=b;s_dma_display_enabled=dma;s_dma_display_failed=!dma;
         int before=physical,cb=complete_count;
         display_update(source,done);
         assert(physical==before && complete_count==cb && s_updating && !s_display_gate && s_display_work);
         // KernelMain trying sleep waits before taking the panel mutex; no command overtakes DMA.
         if(setjmp(suspend)==0){display_set_enabled(false);assert(0);}assert(why==1 && !s_panel_mutex && s_enabled);
         task=1;if(setjmp(suspend)==0)prv_display_worker(NULL);assert(why==2);task=0;
         assert(physical==before+1 && complete_count==cb && s_updating && s_display_gate && main_callback);
         // Physical controls can finish before the queued Main completion, avoiding deadlock.
         display_set_enabled(false);assert(!s_enabled);display_set_enabled(true);
         display_co5300_set_brightness(37);assert(s_brightness_dirty);
         void (*cbfn)(void*)=main_callback;main_callback=NULL;cbfn(main_arg);
         assert(complete_count==cb+1 && !s_updating && applied==37 && !s_brightness_dirty);
        }
        int main(void) {
         memset(s_worker_rows,0xaa,sizeof s_worker_rows);
         for(unsigned y=0;y<260;++y)pixels[y][0]=y;
         run(0,260,true);run(17,33,true);run(260,260,true);run(40,44,false);
         assert(read_count==280);
         return 0;
        }
        """
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "test.c").write_text(prefix + functions + main)
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
